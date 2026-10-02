#include "secretagent.h"
#include "handler.h"
#include "networkclient.h"
#include "secretstore.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusError>
#include <QDebug>
#include <QTimer>

using namespace NetworkDBus;

namespace {

const QString connectionSetting = QStringLiteral("connection");
const QString wirelessSecurity = QStringLiteral("802-11-wireless-security");
const QString ieee8021x = QStringLiteral("802-1x");
const QString agentIdentifier = QStringLiteral("com.cutefish.network");

const QString noSecrets = QStringLiteral("org.freedesktop.NetworkManager.SecretAgent.NoSecrets");
const QString userCanceled = QStringLiteral("org.freedesktop.NetworkManager.SecretAgent.UserCanceled");
const QString agentCanceled = QStringLiteral("org.freedesktop.NetworkManager.SecretAgent.AgentCanceled");
const QString permissionDenied =
        QStringLiteral("org.freedesktop.NetworkManager.SecretAgent.PermissionDenied");

// NetworkManager keeps one flags property for all four WEP keys.
QString flagsKey(const QString &key)
{
    if (key.startsWith(QLatin1String("wep-key")) && key.size() == 8)
        return QStringLiteral("wep-key-flags");
    return key + QLatin1String("-flags");
}

bool savedByAgent(uint flags)
{
    return (flags & SecretAgentOwned) && !(flags & SecretNotSaved);
}

// A VPN's data and secrets: string to string, nested in the vpn setting.
QMap<QString, QString> stringMap(const QVariant &value)
{
    QMap<QString, QString> map;
    const QVariantMap entries = NetworkDBus::plain(value).toMap();
    for (auto it = entries.cbegin(); it != entries.cend(); ++it)
        map.insert(it.key(), it.value().toString());
    return map;
}

} // namespace

// The object NetworkManager calls. It stays out of the QML type, which also serves the
// session's own com.cutefish.SecretAgent interface.
class SecretAgentService : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.NetworkManager.SecretAgent")

public:
    explicit SecretAgentService(NetworkSecretAgent *owner)
        : QObject(owner)
        , m_owner(owner)
    {
    }

public Q_SLOTS:
    NMVariantMapMap GetSecrets(const NMVariantMapMap &connection,
                               const QDBusObjectPath &connectionPath, const QString &settingName,
                               const QStringList &hints, uint flags)
    {
        if (!accepted())
            return {};

        setDelayedReply(true);
        m_owner->getSecrets(connection, connectionPath.path(), settingName, hints, flags, message());
        return {};
    }

    void CancelGetSecrets(const QDBusObjectPath &connectionPath, const QString &settingName)
    {
        if (accepted())
            m_owner->cancelGetSecrets(connectionPath.path(), settingName);
    }

    void SaveSecrets(const NMVariantMapMap &connection, const QDBusObjectPath &connectionPath)
    {
        Q_UNUSED(connectionPath)
        if (!accepted())
            return;

        setDelayedReply(true);
        m_owner->saveSecrets(connection, message());
    }

    void DeleteSecrets(const NMVariantMapMap &connection, const QDBusObjectPath &connectionPath)
    {
        Q_UNUSED(connectionPath)
        if (!accepted())
            return;

        setDelayedReply(true);
        m_owner->deleteSecrets(connection, message());
    }

private:
    bool accepted()
    {
        if (m_owner->fromNetworkManager(message()))
            return true;
        sendErrorReply(permissionDenied, QStringLiteral("Only NetworkManager may ask for secrets"));
        return false;
    }

    NetworkSecretAgent *m_owner;
};

NetworkSecretAgent::NetworkSecretAgent(QObject *parent)
    : QObject(parent)
    , m_client(NetworkClient::instance())
    , m_service(new SecretAgentService(this))
    , m_store(new SecretStore(this))
    , m_handler(new Handler(this))
{
    QDBusConnection session = QDBusConnection::sessionBus();
    if (!session.registerService(QStringLiteral("com.cutefish.SecretAgent"))
        || !session.registerObject(QStringLiteral("/SecretAgent"), this,
                                   QDBusConnection::ExportScriptableSlots))
        qWarning() << "Cannot offer com.cutefish.SecretAgent:" << session.lastError().message();

    QDBusConnection bus = m_client->bus();
    if (!bus.registerObject(secretAgentPath, m_service, QDBusConnection::ExportAllSlots))
        qWarning() << "Cannot export the NetworkManager secret agent:" << bus.lastError().message();

    // Registered with every NetworkManager that starts; one that quit took its requests along.
    connect(m_client, &NetworkClient::readyChanged, this, [this] {
        m_registered = false;
        ++m_registration;
        if (m_client->ready()) {
            registerAgent();
            return;
        }

        const bool prompting = !m_requests.isEmpty();
        m_lookups.clear();
        m_requests.clear();
        if (prompting)
            Q_EMIT requestChanged();
    });
    connect(m_client, &NetworkClient::changed, this, &NetworkSecretAgent::dismissStalePrompts);

    if (m_client->ready())
        registerAgent();
}

NetworkSecretAgent::~NetworkSecretAgent()
{
    if (!m_client)
        return;

    for (const Request &request : std::as_const(m_lookups) + std::as_const(m_requests)) {
        if (request.accessPointPath.isEmpty())
            fail(request, agentCanceled, QStringLiteral("The agent is going away"));
    }

    if (m_registered)
        m_client->call(agentManagerPath, agentManagerInterface, QStringLiteral("Unregister"));
    m_client->bus().unregisterObject(secretAgentPath);
}

QString NetworkSecretAgent::connectionName() const
{
    return m_requests.isEmpty() ? QString() : m_requests.first().connectionName;
}

NetworkSecretAgent::Kind NetworkSecretAgent::kind() const
{
    return m_requests.isEmpty() ? WpaPassword : m_requests.first().kind;
}

QString NetworkSecretAgent::identity() const
{
    return m_requests.isEmpty() ? QString() : m_requests.first().identity;
}

bool NetworkSecretAgent::retry() const
{
    return !m_requests.isEmpty() && m_requests.first().retry;
}

QVariantList NetworkSecretAgent::fields() const
{
    QVariantList fields;
    if (m_requests.isEmpty())
        return fields;

    const Request &request = m_requests.first();
    for (const VpnUtils::SecretField &field : request.fields) {
        fields << QVariantMap {{QStringLiteral("key"), field.key},
                               {QStringLiteral("label"), field.label},
                               {QStringLiteral("secret"), field.secret},
                               {QStringLiteral("value"), request.values.value(field.key)}};
    }
    return fields;
}

QString NetworkSecretAgent::message() const
{
    return m_requests.isEmpty() ? QString() : m_requests.first().serverMessage;
}

bool NetworkSecretAgent::fromNetworkManager(const QDBusMessage &message) const
{
    // The bus policy already lets only root talk to agents; this keeps it to NetworkManager.
    return m_client->owner().isEmpty() || message.service() == m_client->owner();
}

void NetworkSecretAgent::registerAgent(int attempt)
{
    const quint64 registration = m_registration;
    m_client->call(agentManagerPath, agentManagerInterface, QStringLiteral("RegisterWithCapabilities"),
                   {agentIdentifier, 1u}, this, [this, registration, attempt](const QDBusMessage &reply) {
                       if (registration != m_registration)
                           return;
                       if (reply.type() == QDBusMessage::ReplyMessage) {
                           m_registered = true;
                           return;
                       }

                       // Right after it appears, NetworkManager may not export its agent
                       // manager yet.
                       if (attempt < 6) {
                           QTimer::singleShot(100 << attempt, this, [this, registration, attempt] {
                               if (registration == m_registration)
                                   registerAgent(attempt + 1);
                           });
                           return;
                       }
                       qWarning() << "Cannot register the NetworkManager secret agent:"
                                  << reply.errorMessage();
                   });
}

void NetworkSecretAgent::getSecrets(const NMVariantMapMap &connection,
                                    const QString &connectionPath, const QString &settingName,
                                    const QStringList &hints, uint flags, const QDBusMessage &message)
{
    Request request;
    request.id = ++m_nextId;
    request.message = message;
    request.connectionPath = connectionPath;
    request.settingName = settingName;
    request.connectionName = connection.value(connectionSetting).value(QStringLiteral("id")).toString();
    request.uuid = connection.value(connectionSetting).value(QStringLiteral("uuid")).toString();
    request.interactive = flags & (AllowInteraction | RequestNew);
    request.retry = flags & RequestNew;
    request.noErrors = flags & NoErrors;

    const QVariantMap setting = connection.value(settingName);

    if (settingName == QLatin1String("vpn")) {
        request.kind = VpnSecrets;
        request.vpnData = stringMap(setting.value(QStringLiteral("data")));
        request.fields = VpnUtils::secretFields(setting.value(QStringLiteral("service-type")).toString(),
                                                request.vpnData, hints);
        request.serverMessage = VpnUtils::hintMessage(hints);
        const QMap<QString, QString> existing = stringMap(setting.value(QStringLiteral("secrets")));
        for (auto it = existing.cbegin(); it != existing.cend(); ++it)
            request.values.insert(it.key(), it.value());
    } else if (settingName == wirelessSecurity) {
        const QString keyMgmt = setting.value(QStringLiteral("key-mgmt")).toString();

        if (keyMgmt == QLatin1String("wpa-psk") || keyMgmt == QLatin1String("sae")
            || keyMgmt == QLatin1String("wpa-none")) {
            request.kind = WpaPassword;
            request.key = QStringLiteral("psk");
        } else if (keyMgmt == QLatin1String("none")) {
            request.kind = WepKey;
            request.key = QStringLiteral("wep-key%1")
                                  .arg(setting.value(QStringLiteral("wep-tx-keyidx")).toUInt());
        } else if (keyMgmt == QLatin1String("ieee8021x")
                   && setting.value(QStringLiteral("auth-alg")).toString()
                           == QLatin1String("leap")) {
            request.kind = LeapPassword;
            request.key = QStringLiteral("leap-password");
            request.identity = setting.value(QStringLiteral("leap-username")).toString();
        }
    } else if (settingName == ieee8021x) {
        const QStringList eap = setting.value(QStringLiteral("eap")).toStringList();
        request.identity = setting.value(QStringLiteral("identity")).toString();

        if (eap == QStringList {QStringLiteral("tls")}) {
            request.kind = PrivateKeyPassword;
            request.key = QStringLiteral("private-key-password");
        } else if (!request.identity.isEmpty()) {
            // The identity is not a secret, so a reply cannot supply a missing one.
            request.kind = EapPassword;
            request.key = QStringLiteral("password");
        }
    }
    request.secretFlags = setting.value(flagsKey(request.key)).toUInt();

    // A second request for the same secret replaces the first, and its prompt.
    const auto same = [&](const Request &other) {
        return other.connectionPath == connectionPath && other.settingName == settingName;
    };
    for (int i = m_lookups.size() - 1; i >= 0; --i) {
        if (same(m_lookups.at(i)))
            fail(m_lookups.takeAt(i), agentCanceled, QStringLiteral("A newer request replaced it"));
    }
    for (int i = m_requests.size() - 1; i >= 0; --i) {
        if (!same(m_requests.at(i)))
            continue;
        fail(m_requests.at(i), agentCanceled, QStringLiteral("A newer request replaced it"));
        if (request.interactive && (!request.key.isEmpty() || !request.fields.isEmpty())) {
            m_requests[i] = request;
            if (i == 0)
                Q_EMIT requestChanged();
            return;
        }
        m_requests.removeAt(i);
        if (i == 0)
            Q_EMIT requestChanged();
    }

    if (request.kind == VpnSecrets) {
        getVpnSecrets(request);
        return;
    }

    // RequestNew means what is stored was just rejected, and a secret that is never saved
    // has nothing stored.
    if (request.retry || (request.secretFlags & SecretNotSaved) || request.uuid.isEmpty()) {
        ask(request);
        return;
    }

    m_lookups.append(request);
    m_store->load(request.uuid, settingName, [this, id = request.id](const QVariantMap &secrets) {
        for (int i = 0; i < m_lookups.size(); ++i) {
            if (m_lookups.at(i).id != id)
                continue;

            const Request request = m_lookups.takeAt(i);
            const bool complete = !secrets.isEmpty()
                    && (request.key.isEmpty() || secrets.contains(request.key));
            if (complete || (!secrets.isEmpty() && !request.interactive)) {
                NMVariantMapMap reply;
                reply.insert(request.settingName, secrets);
                this->reply(request, reply);
            } else {
                ask(request);
            }
            return;
        }
    });
}

void NetworkSecretAgent::getVpnSecrets(Request request)
{
    // Nothing to ask for, a plugin that signs in by itself for instance: what NetworkManager
    // has is the answer.
    if (request.fields.isEmpty()) {
        replyVpn(request, request.values);
        return;
    }
    if (request.retry || request.uuid.isEmpty()) {
        ask(request);
        return;
    }

    m_lookups.append(request);
    m_store->load(request.uuid, QStringLiteral("vpn"), [this, id = request.id](const QVariantMap &stored) {
        for (int i = 0; i < m_lookups.size(); ++i) {
            if (m_lookups.at(i).id != id)
                continue;

            Request request = m_lookups.takeAt(i);
            for (auto it = stored.cbegin(); it != stored.cend(); ++it) {
                if (request.values.value(it.key()).toString().isEmpty())
                    request.values.insert(it.key(), it.value());
            }

            // A one-time code and other secrets that are never saved are asked for each time.
            bool complete = true;
            for (const VpnUtils::SecretField &field : std::as_const(request.fields)) {
                const uint flags = request.vpnData.value(field.key + QLatin1String("-flags")).toUInt();
                complete &= !(flags & SecretNotSaved) && !request.values.value(field.key).toString().isEmpty();
            }

            // Without interaction, an answer with no secrets has NetworkManager ask again with it.
            if (complete || !request.interactive)
                replyVpn(request, request.values);
            else
                ask(request);
            return;
        }
    });
}

void NetworkSecretAgent::replyVpn(const Request &request, const QVariantMap &values)
{
    QMap<QString, QString> secrets;
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        if (!it.value().toString().isEmpty())
            secrets.insert(it.key(), it.value().toString());
    }

    NMVariantMapMap answer;
    answer[QStringLiteral("vpn")][QStringLiteral("secrets")] = QVariant::fromValue(secrets);
    reply(request, answer);
}

void NetworkSecretAgent::cancelGetSecrets(const QString &connectionPath,
                                          const QString &settingName)
{
    // NetworkManager may cancel a request it already has the answer to.
    auto matches = [&](const Request &request) {
        return request.connectionPath == connectionPath && request.settingName == settingName;
    };

    for (int i = 0; i < m_lookups.size(); ++i) {
        if (!matches(m_lookups.at(i)))
            continue;
        fail(m_lookups.takeAt(i), agentCanceled, QStringLiteral("Canceled by NetworkManager"));
        return;
    }

    for (int i = 0; i < m_requests.size(); ++i) {
        if (!matches(m_requests.at(i)))
            continue;
        fail(m_requests.takeAt(i), agentCanceled, QStringLiteral("Canceled by NetworkManager"));
        if (i == 0)
            Q_EMIT requestChanged();
        return;
    }
}

void NetworkSecretAgent::saveSecrets(const NMVariantMapMap &connection, const QDBusMessage &message)
{
    const QVariantMap connectionMap = connection.value(connectionSetting);
    const QString uuid = connectionMap.value(QStringLiteral("uuid")).toString();
    const QString name = connectionMap.value(QStringLiteral("id")).toString();
    if (uuid.isEmpty()) {
        m_client->bus().send(message.createReply());
        return;
    }

    // System-owned secrets are NetworkManager's to store; only agent-owned ones come here.
    QList<SecretStore::Entry> entries;
    for (auto setting = connection.cbegin(); setting != connection.cend(); ++setting) {
        if (setting.key() == QLatin1String("vpn")) {
            const QMap<QString, QString> data = stringMap(setting->value(QStringLiteral("data")));
            const QMap<QString, QString> secrets = stringMap(setting->value(QStringLiteral("secrets")));
            for (auto it = secrets.cbegin(); it != secrets.cend(); ++it) {
                if (savedByAgent(data.value(it.key() + QLatin1String("-flags")).toUInt()))
                    entries.append({setting.key(), it.key(), it.value()});
            }
            continue;
        }
        for (auto key = setting->cbegin(); key != setting->cend(); ++key) {
            const QVariant flags = setting->value(flagsKey(key.key()));
            if (!flags.isValid() || key->typeId() != QMetaType::QString || !savedByAgent(flags.toUInt()))
                continue;
            entries.append({setting.key(), key.key(), key->toString()});
        }
    }

    QDBusConnection bus = m_client->bus();
    m_store->replace(uuid, name, entries, [bus, message]() mutable {
        bus.send(message.createReply());
    });
}

void NetworkSecretAgent::deleteSecrets(const NMVariantMapMap &connection, const QDBusMessage &message)
{
    const QString uuid = connection.value(connectionSetting).value(QStringLiteral("uuid")).toString();
    QDBusConnection bus = m_client->bus();
    if (uuid.isEmpty()) {
        bus.send(message.createReply());
        return;
    }
    m_store->remove(uuid, [bus, message]() mutable {
        bus.send(message.createReply());
    });
}

void NetworkSecretAgent::ConnectWireless(const QString &devicePath,
                                         const QString &accessPointPath, const QString &name,
                                         bool wep)
{
    if (!m_requests.isEmpty() && m_requests.first().accessPointPath == accessPointPath)
        return;

    // Another network was picked: the prompt switches to it, and whatever Wi-Fi
    // password was being asked for is given up, as connecting to this one would.
    for (int i = m_requests.size() - 1; i >= 0; --i) {
        const Request &request = m_requests.at(i);
        if (request.accessPointPath.isEmpty()) {
            if (request.settingName != wirelessSecurity && request.settingName != ieee8021x)
                continue;
            fail(request, userCanceled, QStringLiteral("Another network was chosen"));
        }
        m_requests.removeAt(i);
    }

    Request request;
    request.id = ++m_nextId;
    request.connectionName = name;
    request.kind = wep ? WepKey : WpaPassword;
    request.interactive = true;
    request.devicePath = devicePath;
    request.accessPointPath = accessPointPath;
    request.deviceActive = m_client->activeConnection(devicePath);
    m_requests.prepend(request);
    Q_EMIT requestChanged();
}

void NetworkSecretAgent::respond(const QString &secret)
{
    if (m_requests.isEmpty())
        return;

    const Request request = m_requests.first();

    if (!request.accessPointPath.isEmpty()) {
        m_handler->addAndActivateConnection(request.devicePath, request.accessPointPath, secret);
        finish();
        return;
    }

    NMVariantMapMap secrets;
    secrets[request.settingName][request.key] = secret;
    reply(request, secrets);

    // NetworkManager hands typed secrets back to no agent: storing them is the agent's job.
    if (!request.uuid.isEmpty() && savedByAgent(request.secretFlags)) {
        m_store->save(request.uuid, request.connectionName, request.settingName, request.key,
                      secret);
    }

    finish();
}

void NetworkSecretAgent::respondFields(const QVariantMap &values)
{
    if (m_requests.isEmpty() || m_requests.first().kind != VpnSecrets)
        return;

    Request request = m_requests.first();
    for (auto it = values.cbegin(); it != values.cend(); ++it)
        request.values.insert(it.key(), it.value());
    replyVpn(request, request.values);

    // As for Wi-Fi, typed secrets the agent keeps are the agent's to store.
    for (const VpnUtils::SecretField &field : std::as_const(request.fields)) {
        const QString value = values.value(field.key).toString();
        const uint flags = request.vpnData.value(field.key + QLatin1String("-flags")).toUInt();
        if (!request.uuid.isEmpty() && !value.isEmpty() && savedByAgent(flags))
            m_store->save(request.uuid, request.connectionName, QStringLiteral("vpn"), field.key, value);
    }

    finish();
}

void NetworkSecretAgent::cancel()
{
    if (m_requests.isEmpty())
        return;

    if (m_requests.first().accessPointPath.isEmpty())
        fail(m_requests.first(), userCanceled, QStringLiteral("Canceled by the user"));
    finish();
}

void NetworkSecretAgent::reply(const Request &request, const NMVariantMapMap &secrets)
{
    m_client->bus().send(request.message.createReply(QVariant::fromValue(secrets)));
}

void NetworkSecretAgent::fail(const Request &request, const QString &error, const QString &text)
{
    m_client->bus().send(request.message.createErrorReply(error, text));
}

void NetworkSecretAgent::ask(const Request &request)
{
    if (!request.interactive
        || (request.key.isEmpty() && request.fields.isEmpty() && request.accessPointPath.isEmpty())) {
        // Secrets read through Settings.Connection.GetSecrets expect an answer, not an error.
        if (request.noErrors) {
            NMVariantMapMap empty;
            empty.insert(request.settingName, {});
            reply(request, empty);
        } else {
            fail(request, noSecrets, QStringLiteral("No %1 secrets to offer").arg(request.settingName));
        }
        return;
    }

    m_requests.append(request);
    if (m_requests.size() == 1)
        Q_EMIT requestChanged();
}

void NetworkSecretAgent::finish()
{
    m_requests.removeFirst();
    Q_EMIT requestChanged();
}

void NetworkSecretAgent::dismissStalePrompts()
{
    if (!m_client->ready())
        return;

    // A prompt for a network that went out of range, a device that went away, or a device
    // that started connecting elsewhere is out of date.
    bool headChanged = false;
    for (int i = m_requests.size() - 1; i >= 0; --i) {
        const Request &request = m_requests.at(i);
        if (request.accessPointPath.isEmpty())
            continue;
        if (m_client->has(request.accessPointPath, accessPointInterface)
            && m_client->has(request.devicePath, deviceInterface)) {
            const QString active = m_client->activeConnection(request.devicePath);
            if (active.isEmpty() || active == request.deviceActive)
                continue;
        }
        m_requests.removeAt(i);
        headChanged |= i == 0;
    }

    if (headChanged)
        Q_EMIT requestChanged();
}

#include "secretagent.moc"
