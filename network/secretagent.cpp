#include "secretagent.h"
#include "handler.h"
#include "secretstore.h"

#include <QDBusConnection>
#include <QDBusError>
#include <QDebug>

#include <NetworkManagerQt/Manager>
#include <NetworkManagerQt/SecretAgent>
#include <NetworkManagerQt/Setting>
#include <NetworkManagerQt/WirelessDevice>

namespace {

const QString connectionSetting = QStringLiteral("connection");
const QString wirelessSecurity = QStringLiteral("802-11-wireless-security");
const QString ieee8021x = QStringLiteral("802-1x");

// NetworkManager keeps one flags property for all four WEP keys.
QString flagsKey(const QString &key)
{
    if (key.startsWith(QLatin1String("wep-key")) && key.size() == 8)
        return QStringLiteral("wep-key-flags");
    return key + QLatin1String("-flags");
}

} // namespace

// The object NetworkManager calls. It stays out of the QML type, whose type
// registrar cannot describe a NetworkManagerQt base class.
class SecretAgentService : public NetworkManager::SecretAgent
{
    Q_OBJECT

public:
    explicit SecretAgentService(NetworkSecretAgent *owner)
        : NetworkManager::SecretAgent(QStringLiteral("com.cutefish.network"), owner)
        , m_owner(owner)
    {
    }

public Q_SLOTS:
    NMVariantMapMap GetSecrets(const NMVariantMapMap &connection,
                               const QDBusObjectPath &connectionPath, const QString &settingName,
                               const QStringList &hints, uint flags) override
    {
        Q_UNUSED(hints)

        setDelayedReply(true);
        m_owner->getSecrets(connection, connectionPath.path(), settingName, flags, message());
        return {};
    }

    void CancelGetSecrets(const QDBusObjectPath &connectionPath,
                          const QString &settingName) override
    {
        m_owner->cancelGetSecrets(connectionPath.path(), settingName);
    }

    void SaveSecrets(const NMVariantMapMap &connection,
                     const QDBusObjectPath &connectionPath) override
    {
        Q_UNUSED(connectionPath)
        m_owner->saveSecrets(connection);
    }

    void DeleteSecrets(const NMVariantMapMap &connection,
                       const QDBusObjectPath &connectionPath) override
    {
        Q_UNUSED(connectionPath)
        m_owner->deleteSecrets(connection);
    }

private:
    NetworkSecretAgent *m_owner;
};

NetworkSecretAgent::NetworkSecretAgent(QObject *parent)
    : QObject(parent)
    , m_service(new SecretAgentService(this))
    , m_store(new SecretStore(this))
    , m_handler(new Handler(this))
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(QStringLiteral("com.cutefish.SecretAgent"))
        || !bus.registerObject(QStringLiteral("/SecretAgent"), this,
                               QDBusConnection::ExportScriptableSlots))
        qWarning() << "Cannot offer com.cutefish.SecretAgent:" << bus.lastError().message();

    connect(NetworkManager::notifier(), &NetworkManager::Notifier::deviceRemoved, this,
            [this](const QString &devicePath) {
                m_watchedDevices.remove(devicePath);
                dismiss([&](const Request &request) { return request.devicePath == devicePath; });
            });
}

NetworkSecretAgent::~NetworkSecretAgent()
{
    for (const Request &request : std::as_const(m_lookups) + std::as_const(m_requests)) {
        if (!request.accessPointPath.isEmpty())
            continue;
        m_service->sendError(SecretAgentService::AgentCanceled,
                             QStringLiteral("The agent is going away"), request.message);
    }
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

void NetworkSecretAgent::getSecrets(const NMVariantMapMap &connection,
                                    const QString &connectionPath, const QString &settingName,
                                    uint flags, const QDBusMessage &message)
{
    Request request;
    request.id = ++m_nextId;
    request.message = message;
    request.connectionPath = connectionPath;
    request.settingName = settingName;
    request.connectionName = connection.value(connectionSetting).value(QStringLiteral("id")).toString();
    request.interactive = flags & (SecretAgentService::AllowInteraction | SecretAgentService::RequestNew);
    request.retry = flags & SecretAgentService::RequestNew;

    const QVariantMap setting = connection.value(settingName);

    if (settingName == wirelessSecurity) {
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

    // RequestNew means what is stored was just rejected. VPN secrets nest in a
    // map of their own, which the keyring lookup does not rebuild.
    const QString uuid = connection.value(connectionSetting).value(QStringLiteral("uuid")).toString();
    if (request.retry || uuid.isEmpty() || settingName == QLatin1String("vpn")) {
        ask(request);
        return;
    }

    m_lookups.append(request);
    m_store->load(uuid, settingName, [this, id = request.id](const QVariantMap &secrets) {
        for (int i = 0; i < m_lookups.size(); ++i) {
            if (m_lookups.at(i).id != id)
                continue;

            const Request request = m_lookups.takeAt(i);
            if (secrets.isEmpty()) {
                ask(request);
            } else {
                NMVariantMapMap reply;
                reply.insert(request.settingName, secrets);
                QDBusConnection::systemBus().send(
                        request.message.createReply(QVariant::fromValue(reply)));
            }
            return;
        }
    });
}

void NetworkSecretAgent::cancelGetSecrets(const QString &connectionPath,
                                          const QString &settingName)
{
    auto matches = [&](const Request &request) {
        return request.connectionPath == connectionPath && request.settingName == settingName;
    };

    for (int i = 0; i < m_lookups.size(); ++i) {
        if (!matches(m_lookups.at(i)))
            continue;

        m_service->sendError(SecretAgentService::AgentCanceled,
                             QStringLiteral("Canceled by NetworkManager"),
                  m_lookups.takeAt(i).message);
        return;
    }

    for (int i = 0; i < m_requests.size(); ++i) {
        if (!matches(m_requests.at(i)))
            continue;

        m_service->sendError(SecretAgentService::AgentCanceled,
                             QStringLiteral("Canceled by NetworkManager"),
                  m_requests.takeAt(i).message);
        if (i == 0)
            emit requestChanged();
        return;
    }
}

void NetworkSecretAgent::saveSecrets(const NMVariantMapMap &connection)
{
    const QVariantMap connectionMap = connection.value(connectionSetting);
    const QString uuid = connectionMap.value(QStringLiteral("uuid")).toString();
    const QString name = connectionMap.value(QStringLiteral("id")).toString();
    if (uuid.isEmpty())
        return;

    // System-owned secrets are NetworkManager's to store; only agent-owned
    // ones come here to be kept.
    for (auto setting = connection.cbegin(); setting != connection.cend(); ++setting) {
        for (auto key = setting->cbegin(); key != setting->cend(); ++key) {
            const auto flags = setting->value(flagsKey(key.key()));
            if (!flags.isValid() || key->typeId() != QMetaType::QString
                || !(flags.toUInt() & NetworkManager::Setting::AgentOwned))
                continue;

            m_store->save(uuid, name, setting.key(), key.key(), key->toString());
        }
    }
}

void NetworkSecretAgent::deleteSecrets(const NMVariantMapMap &connection)
{
    const QString uuid = connection.value(connectionSetting).value(QStringLiteral("uuid")).toString();
    if (!uuid.isEmpty())
        m_store->remove(uuid);
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
            m_service->sendError(SecretAgentService::UserCanceled,
                                 QStringLiteral("Another network was chosen"), request.message);
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
    m_requests.prepend(request);
    watchDevice(devicePath);
    emit requestChanged();
}

void NetworkSecretAgent::respond(const QString &secret)
{
    if (m_requests.isEmpty())
        return;

    const Request &request = m_requests.first();

    if (!request.accessPointPath.isEmpty()) {
        m_handler->addAndActivateConnection(request.devicePath, request.accessPointPath, secret);
        finish();
        return;
    }

    NMVariantMapMap secrets;
    secrets[request.settingName][request.key] = secret;
    QDBusConnection::systemBus().send(request.message.createReply(QVariant::fromValue(secrets)));

    finish();
}

void NetworkSecretAgent::cancel()
{
    if (m_requests.isEmpty())
        return;

    if (!m_requests.first().accessPointPath.isEmpty()) {
        finish();
        return;
    }

    m_service->sendError(SecretAgentService::UserCanceled, QStringLiteral("Canceled by the user"),
                         m_requests.first().message);
    finish();
}

void NetworkSecretAgent::ask(const Request &request)
{
    if (!request.interactive || (request.key.isEmpty() && request.accessPointPath.isEmpty())) {
        m_service->sendError(SecretAgentService::NoSecrets,
                             QStringLiteral("No %1 secrets to offer").arg(request.settingName),
                             request.message);
        return;
    }

    m_requests.append(request);
    if (m_requests.size() == 1)
        emit requestChanged();
}

void NetworkSecretAgent::finish()
{
    m_requests.removeFirst();
    emit requestChanged();
}

void NetworkSecretAgent::dismiss(const std::function<bool(const Request &)> &matches)
{
    bool headChanged = false;
    for (int i = m_requests.size() - 1; i >= 0; --i) {
        const Request &request = m_requests.at(i);
        if (request.accessPointPath.isEmpty() || !matches(request))
            continue;
        m_requests.removeAt(i);
        headChanged |= i == 0;
    }

    if (headChanged)
        emit requestChanged();
}

void NetworkSecretAgent::watchDevice(const QString &devicePath)
{
    if (m_watchedDevices.contains(devicePath))
        return;

    auto device = NetworkManager::findNetworkInterface(devicePath)
                          .objectCast<NetworkManager::WirelessDevice>();
    if (!device)
        return;
    m_watchedDevices.insert(devicePath);

    connect(device.data(), &NetworkManager::WirelessDevice::accessPointDisappeared, this,
            [this](const QString &accessPointPath) {
                dismiss([&](const Request &request) {
                    return request.accessPointPath == accessPointPath;
                });
            });
    // Something else started connecting on this device: the prompt is out of date.
    connect(device.data(), &NetworkManager::Device::activeConnectionChanged, this,
            [this, devicePath, weak = device.toWeakRef()] {
                const auto device = weak.toStrongRef();
                if (!device || !device->activeConnection())
                    return;
                dismiss([&](const Request &request) { return request.devicePath == devicePath; });
            });
}

#include "secretagent.moc"
