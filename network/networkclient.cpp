#include "networkclient.h"

#include <QCoreApplication>
#include <QDBusArgument>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QPointer>

#include <pwd.h>
#include <unistd.h>

using namespace NetworkDBus;

NetworkClient::NetworkClient(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
    registerNetworkDBusTypes();

    m_changedTimer.setSingleShot(true);
    m_changedTimer.setInterval(0);
    connect(&m_changedTimer, &QTimer::timeout, this, &NetworkClient::changed);

    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(2000);
    connect(&m_retryTimer, &QTimer::timeout, this, &NetworkClient::refresh);

    auto *watcher = new QDBusServiceWatcher(service, m_bus, QDBusServiceWatcher::WatchForOwnerChange,
                                            this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &owner) {
                reset();
                if (!owner.isEmpty())
                    load(owner);
            });

    refresh();
}

NetworkClient::~NetworkClient() = default;

NetworkClient *NetworkClient::instance()
{
    static QPointer<NetworkClient> client;
    if (!client)
        client = new NetworkClient(QDBusConnection::systemBus(), QCoreApplication::instance());
    return client;
}

bool NetworkClient::has(const QString &path, const QString &interface) const
{
    return m_objects.value(path).contains(interface);
}

QVariant NetworkClient::property(const QString &path, const QString &interface,
                                 const QString &name) const
{
    return m_objects.value(path).value(interface).value(name);
}

QVariant NetworkClient::manager(const QString &name) const
{
    return property(managerPath, managerInterface, name);
}

QStringList NetworkClient::devices() const
{
    return manager(QStringLiteral("Devices")).toStringList();
}

uint NetworkClient::deviceType(const QString &device) const
{
    return property(device, deviceInterface, QStringLiteral("DeviceType")).toUInt();
}

uint NetworkClient::deviceState(const QString &device) const
{
    // StateReason carries the state and its reason together.
    const QVariantList stateReason = property(device, deviceInterface, QStringLiteral("StateReason")).toList();
    if (!stateReason.isEmpty())
        return stateReason.first().toUInt();
    return property(device, deviceInterface, QStringLiteral("State")).toUInt();
}

QString NetworkClient::activeConnection(const QString &device) const
{
    return path(property(device, deviceInterface, QStringLiteral("ActiveConnection")));
}

QStringList NetworkClient::activeConnections() const
{
    return manager(QStringLiteral("ActiveConnections")).toStringList();
}

QStringList NetworkClient::accessPoints(const QString &device) const
{
    return property(device, wirelessInterface, QStringLiteral("AccessPoints")).toStringList();
}

QStringList NetworkClient::connections() const
{
    return m_settings.keys();
}

bool NetworkClient::readable(const QString &connection) const
{
    return m_settings.contains(connection);
}

NMVariantMapMap NetworkClient::settings(const QString &connection) const
{
    return m_settings.value(connection);
}

QVariant NetworkClient::setting(const QString &connection, const QString &setting,
                                const QString &key) const
{
    return plain(m_settings.value(connection).value(setting).value(key));
}

QString NetworkClient::hotspotConnection() const
{
    for (const QString &connection : connections()) {
        const QString mode = setting(connection, QStringLiteral("802-11-wireless"), QStringLiteral("mode")).toString();
        if ((mode == QLatin1String("ap") || mode == QLatin1String("adhoc"))
            && setting(connection, QStringLiteral("ipv4"), QStringLiteral("method")).toString()
                    == QLatin1String("shared"))
            return connection;
    }
    return {};
}

QString NetworkClient::permission(const QString &name) const
{
    return m_permissions.value(name);
}

bool NetworkClient::sharesNewConnections() const
{
    return permission(QStringLiteral("org.freedesktop.NetworkManager.settings.modify.system"))
            == QLatin1String("yes");
}

QString NetworkClient::userPermission()
{
    const passwd *pw = ::getpwuid(::getuid());
    return QStringLiteral("user:%1:").arg(pw ? QString::fromLocal8Bit(pw->pw_name) : QString());
}

void NetworkClient::call(const QString &path, const QString &interface, const QString &method,
                         const QVariantList &arguments, QObject *context, Completion completion,
                         int timeout)
{
    if (m_owner.isEmpty()) {
        if (completion) {
            completion(QDBusMessage::createError(QDBusError::ServiceUnknown,
                                                 QStringLiteral("NetworkManager is not running")));
        }
        return;
    }

    auto message = QDBusMessage::createMethodCall(m_owner, path, interface, method);
    message.setArguments(arguments);
    // Parented to the context, so a reply arriving after it is gone goes nowhere.
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, timeout),
                                                context ? context : this);
    connect(watcher, &QDBusPendingCallWatcher::finished, watcher,
            [watcher, completion = std::move(completion)] {
                const QDBusMessage reply = watcher->reply();
                watcher->deleteLater();
                if (completion)
                    completion(reply);
            });
}

void NetworkClient::setProperty(const QString &path, const QString &interface, const QString &name,
                                const QVariant &value, QObject *context, Completion completion)
{
    call(path, propertiesInterface, QStringLiteral("Set"),
         {interface, name, QVariant::fromValue(QDBusVariant(value))}, context, std::move(completion));
}

QString NetworkClient::path(const QVariant &value)
{
    const QString path = plain(value).toString();
    return path == QLatin1String("/") ? QString() : path;
}

void NetworkClient::refresh()
{
    if (m_loading || m_loaded)
        return;

    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.DBus"),
                                                  QStringLiteral("/org/freedesktop/DBus"),
                                                  QStringLiteral("org.freedesktop.DBus"),
                                                  QStringLiteral("GetNameOwner"));
    message << service;
    const auto generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation] {
        const QDBusMessage reply = watcher->reply();
        watcher->deleteLater();
        if (generation != m_generation || m_loading || m_loaded)
            return;
        if (reply.type() == QDBusMessage::ReplyMessage)
            load(reply.arguments().value(0).toString());
    });
}

void NetworkClient::reset()
{
    ++m_generation;
    m_retryTimer.stop();
    if (!m_owner.isEmpty())
        subscribe(m_owner, false);
    m_owner.clear();
    m_loading = false;
    m_loaded = false;

    const QStringList paths = m_objects.keys();
    m_objects.clear();
    m_settings.clear();
    m_settingsRequests.clear();
    m_permissions.clear();

    for (const QString &path : paths)
        Q_EMIT objectRemoved(path);
    if (m_ready) {
        m_ready = false;
        Q_EMIT readyChanged();
    }
    scheduleChanged();
}

void NetworkClient::load(const QString &owner)
{
    m_owner = owner;
    m_loading = true;

    // Subscribed before the fetch: a signal that arrives before its reply describes a state
    // the reply already holds, so the handlers ignore everything while loading.
    subscribe(owner, true);

    const auto generation = m_generation;
    call(rootPath, objectManagerInterface, QStringLiteral("GetManagedObjects"), {}, this,
         [this, generation](const QDBusMessage &reply) {
             if (generation != m_generation)
                 return;
             if (reply.type() != QDBusMessage::ReplyMessage) {
                 // NetworkManager most likely quit while answering.
                 reset();
                 m_retryTimer.start();
                 return;
             }

             const QVariantMap objects = plain(reply.arguments().value(0)).toMap();
             for (auto it = objects.cbegin(); it != objects.cend(); ++it)
                 addInterfaces(it.key(), it->toMap());

             m_loading = false;
             m_loaded = true;
             fetchPermissions();
             updateReady();
             scheduleChanged();
         });
}

void NetworkClient::subscribe(const QString &owner, bool connect)
{
    const auto bind = [this, &owner, connect](const QString &path, const QString &interface,
                                              const QString &name, const char *slot) {
        if (connect)
            m_bus.connect(owner, path, interface, name, this, slot);
        else
            m_bus.disconnect(owner, path, interface, name, this, slot);
    };

    bind(rootPath, objectManagerInterface, QStringLiteral("InterfacesAdded"),
         SLOT(interfacesAdded(QDBusObjectPath, NMVariantMapMap)));
    bind(rootPath, objectManagerInterface, QStringLiteral("InterfacesRemoved"),
         SLOT(interfacesRemoved(QDBusObjectPath, QStringList)));
    bind(QString(), propertiesInterface, QStringLiteral("PropertiesChanged"),
         SLOT(propertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
    bind(QString(), connectionInterface, QStringLiteral("Updated"),
         SLOT(connectionUpdated(QDBusMessage)));
    // The reason of an active connection's state exists only on this signal.
    bind(QString(), activeInterface, QStringLiteral("StateChanged"),
         SLOT(activeStateSignal(uint, uint, QDBusMessage)));
    bind(managerPath, managerInterface, QStringLiteral("CheckPermissions"), SLOT(fetchPermissions()));
}

void NetworkClient::addInterfaces(const QString &path, const QVariantMap &interfaces)
{
    auto &object = m_objects[path];
    for (auto it = interfaces.cbegin(); it != interfaces.cend(); ++it) {
        QVariantMap &properties = object[it.key()];
        const QVariantMap added = it->toMap();
        for (auto property = added.cbegin(); property != added.cend(); ++property)
            properties.insert(property.key(), plain(property.value()));
    }

    if (interfaces.contains(connectionInterface))
        fetchSettings(path);
}

void NetworkClient::interfacesAdded(const QDBusObjectPath &path, const NMVariantMapMap &interfaces)
{
    if (m_loading)
        return;

    QVariantMap added;
    for (auto it = interfaces.cbegin(); it != interfaces.cend(); ++it)
        added.insert(it.key(), it.value());
    addInterfaces(path.path(), added);
    scheduleChanged();
}

void NetworkClient::interfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces)
{
    if (m_loading)
        return;

    const QString objectPath = path.path();
    auto object = m_objects.find(objectPath);
    if (object == m_objects.end())
        return;

    for (const QString &interface : interfaces)
        object->remove(interface);

    if (interfaces.contains(connectionInterface)) {
        m_settings.remove(objectPath);
        m_settingsRequests.remove(objectPath);
    }

    if (object->isEmpty()) {
        m_objects.erase(object);
        Q_EMIT objectRemoved(objectPath);
    }
    updateReady();
    scheduleChanged();
}

void NetworkClient::propertiesChanged(const QString &interface, const QVariantMap &properties,
                                      const QStringList &, const QDBusMessage &message)
{
    // Counters tick for whoever set a refresh rate; nothing here shows them.
    if (m_loading || interface == statisticsInterface)
        return;

    auto object = m_objects.find(message.path());
    if (object == m_objects.end() || !object->contains(interface))
        return;

    QVariantMap &cached = (*object)[interface];
    for (auto it = properties.cbegin(); it != properties.cend(); ++it)
        cached.insert(it.key(), plain(it.value()));
    scheduleChanged();
}

void NetworkClient::connectionUpdated(const QDBusMessage &message)
{
    if (m_loading || !has(message.path(), connectionInterface))
        return;
    fetchSettings(message.path());
}

void NetworkClient::activeStateSignal(uint state, uint reason, const QDBusMessage &message)
{
    if (m_loading)
        return;

    const QString path = message.path();
    if (has(path, activeInterface))
        m_objects[path][activeInterface].insert(QStringLiteral("State"), state);
    Q_EMIT activeStateChanged(path, state, reason);
    scheduleChanged();
}

void NetworkClient::fetchSettings(const QString &path)
{
    // A newer request makes an older reply stale, whichever arrives first.
    const quint64 request = ++m_nextRequest;
    m_settingsRequests.insert(path, request);

    const auto generation = m_generation;
    call(path, connectionInterface, QStringLiteral("GetSettings"), {}, this,
         [this, generation, path, request](const QDBusMessage &reply) {
             if (generation != m_generation || m_settingsRequests.value(path) != request)
                 return;
             m_settingsRequests.remove(path);

             // Another user's private profile: it exists, but its settings are not ours to read.
             if (reply.type() == QDBusMessage::ReplyMessage)
                 m_settings.insert(path, qdbus_cast<NMVariantMapMap>(reply.arguments().value(0)));
             else
                 m_settings.remove(path);
             updateReady();
             scheduleChanged();
         });
}

void NetworkClient::fetchPermissions()
{
    if (m_loading)
        return;

    const quint64 request = ++m_nextRequest;
    m_permissionsRequest = request;
    const auto generation = m_generation;
    call(managerPath, managerInterface, QStringLiteral("GetPermissions"), {}, this,
         [this, generation, request](const QDBusMessage &reply) {
             if (generation != m_generation || request != m_permissionsRequest)
                 return;
             if (reply.type() == QDBusMessage::ReplyMessage)
                 m_permissions = qdbus_cast<QMap<QString, QString>>(reply.arguments().value(0));
             else
                 m_permissions.clear();
             scheduleChanged();
         });
}

void NetworkClient::scheduleChanged()
{
    if (!m_changedTimer.isActive())
        m_changedTimer.start();
}

void NetworkClient::updateReady()
{
    // Once ready, later profile fetches don't take the network lists away again.
    if (m_ready || !m_loaded || !m_settingsRequests.isEmpty())
        return;
    m_ready = true;
    Q_EMIT readyChanged();
}
