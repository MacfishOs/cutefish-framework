#include "bluetoothmanager.h"
#include "bluetoothagent.h"
#include "bluetoothrfkill.h"

#include <QDBusArgument>
#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QLoggingCategory>
#include <QPointer>
#include <QRegularExpression>
#include <utility>

using namespace BluetoothDBus;

Q_LOGGING_CATEGORY(lcBluetooth, "cutefish.bluetooth")

BluetoothManager::BluetoothManager(QObject *parent)
    : BluetoothManager(QDBusConnection::systemBus(), true, parent)
{
    connect(this, &BluetoothManager::deviceFailed, this, &BluetoothManager::notify);
}

BluetoothManager::BluetoothManager(const QDBusConnection &bus, bool monitorRfkill, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
    registerBluetoothDBusTypes();
    connect(this, &BluetoothManager::changed, this, [this] {
        const bool present = available();
        if (m_lastAvailable != present) {
            m_lastAvailable = present;
            emit availableChanged();
        }
        const bool powered = enabled();
        if (m_lastEnabled != powered) {
            m_lastEnabled = powered;
            emit enabledChanged();
        }
    });
    m_agent = new BluetoothAgent(this);
    if (monitorRfkill)
        m_rfkill = new BluetoothRfkill(this);
    auto *watcher = new QDBusServiceWatcher(QStringLiteral("org.bluez"), m_bus,
                                            QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            &BluetoothManager::ownerChanged);
    m_promptTimer.setSingleShot(true);
    m_promptTimer.setInterval(120000);
    connect(&m_promptTimer, &QTimer::timeout, this, [this] {
        const QString device = m_pairingDevice;
        cancelPairing();
        deviceFailure(QStringLiteral("Pair"), device,
                      tr("The device didn't finish pairing in time. Try again."));
    });
    m_retryTimer.setSingleShot(true);
    m_retryTimer.setInterval(2000);
    connect(&m_retryTimer, &QTimer::timeout, this, &BluetoothManager::refresh);
    refresh();
}

BluetoothManager::~BluetoothManager()
{
    finishPrompt(QStringLiteral("org.bluez.Error.Canceled"));
    for (const auto &path : std::as_const(m_pairingOperations))
        m_bus.asyncCall(QDBusMessage::createMethodCall(m_owner, path, deviceInterface,
                                                       QStringLiteral("CancelPairing")));
    if (m_agentReady) {
        auto message = QDBusMessage::createMethodCall(m_owner, QStringLiteral("/org/bluez"),
                                                      agentManagerInterface,
                                                      QStringLiteral("UnregisterAgent"));
        message << QVariant::fromValue(QDBusObjectPath(agentPath));
        m_bus.asyncCall(message);
    }
    if (m_agentEnabled)
        m_bus.unregisterObject(agentPath);
    for (const auto &path : std::as_const(m_discoveryOwned)) {
        m_bus.asyncCall(QDBusMessage::createMethodCall(m_owner, path, adapterInterface,
                                                       QStringLiteral("StopDiscovery")));
    }
}

void BluetoothManager::refresh()
{
    if (m_loading)
        return;
    if (!m_bus.isConnected()) {
        fail(tr("The system D-Bus connection is unavailable."));
        return;
    }
    auto message = QDBusMessage::createMethodCall(
            QStringLiteral("org.freedesktop.DBus"), QStringLiteral("/org/freedesktop/DBus"),
            QStringLiteral("org.freedesktop.DBus"), QStringLiteral("GetNameOwner"));
    message << QStringLiteral("org.bluez");
    const auto generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation] {
        const QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (generation != m_generation)
            return;
        if (reply.isError()) {
            if (m_operational)
                reset();
            return;
        }
        if (!m_operational)
            load(reply.value());
    });
}

void BluetoothManager::ownerChanged(const QString &, const QString &, const QString &newOwner)
{
    reset();
    if (!newOwner.isEmpty())
        load(newOwner);
}

void BluetoothManager::reset()
{
    ++m_generation;
    m_retryTimer.stop();
    if (!m_owner.isEmpty()) {
        m_bus.disconnect(m_owner, QStringLiteral("/"),
                         QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                         QStringLiteral("InterfacesAdded"), this,
                         SLOT(interfacesAdded(QDBusObjectPath, BluetoothInterfaces)));
        m_bus.disconnect(m_owner, QStringLiteral("/"),
                         QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                         QStringLiteral("InterfacesRemoved"), this,
                         SLOT(interfacesRemoved(QDBusObjectPath, QStringList)));
        m_bus.disconnect(m_owner, QString(), propertiesInterface,
                         QStringLiteral("PropertiesChanged"), this,
                         SLOT(propertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
    }
    finishPrompt(QStringLiteral("org.bluez.Error.Canceled"));
    m_owner.clear();
    m_operational = false;
    m_loading = false;
    m_agentReady = false;
    m_registeringAgent = false;
    m_events.clear();
    m_discoveryOwned.clear();
    m_discoveryPending.clear();
    m_discoveryFailed.clear();
    m_stopRetries.clear();
    m_pairingOperations.clear();
    const auto objects = m_objects;
    m_objects.clear();
    emit adaptersChanged();
    emit devicesChanged();
    emit changed();
    for (auto *object : objects)
        object->deleteLater();
}

void BluetoothManager::load(const QString &owner)
{
    if (m_loading)
        return;
    m_owner = owner;
    m_loading = true;
    m_bus.connect(owner, QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                  QStringLiteral("InterfacesAdded"), this,
                  SLOT(interfacesAdded(QDBusObjectPath, BluetoothInterfaces)));
    m_bus.connect(owner, QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                  QStringLiteral("InterfacesRemoved"), this,
                  SLOT(interfacesRemoved(QDBusObjectPath, QStringList)));
    m_bus.connect(owner, QString(), propertiesInterface, QStringLiteral("PropertiesChanged"), this,
                  SLOT(propertiesChanged(QString, QVariantMap, QStringList, QDBusMessage)));
    call(QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
         QStringLiteral("GetManagedObjects"), {}, [this](const QDBusMessage &reply) {
             if (reply.type() == QDBusMessage::ErrorMessage) {
                 reset();
                 m_retryTimer.start();
                 return;
             }
             const auto objects = qdbus_cast<BluetoothObjects>(reply.arguments().value(0));
             m_loading = false;
             for (auto it = objects.cbegin(); it != objects.cend(); ++it)
                 interfacesAdded(it.key(), it.value());
             const auto events = std::exchange(m_events, {});
             for (const auto &event : events)
                 event();
             m_operational = true;
             emit changed();
             registerAgent();
             syncDiscovery();
         });
}

void BluetoothManager::interfacesAdded(const QDBusObjectPath &path,
                                       const BluetoothInterfaces &interfaces)
{
    if (m_loading) {
        m_events.append([this, path, interfaces] {
            interfacesAdded(path, interfaces);
        });
        return;
    }
    auto *entry = m_objects.value(path.path());
    if (!entry) {
        entry = new BluetoothObject(path.path(), this);
        m_objects.insert(path.path(), entry);
        connect(entry, &BluetoothObject::changed, this, [this] {
            emit changed();
            emit devicesChanged();
            QTimer::singleShot(0, this, &BluetoothManager::syncDiscovery);
        });
    }
    const bool wasAdapter = entry->isAdapter();
    for (auto it = interfaces.cbegin(); it != interfaces.cend(); ++it)
        entry->update(it.key(), it.value());
    if (!wasAdapter && entry->isAdapter())
        emit adaptersChanged();
    emit devicesChanged();
    emit changed();
}

void BluetoothManager::interfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces)
{
    if (m_loading) {
        m_events.append([this, path, interfaces] {
            interfacesRemoved(path, interfaces);
        });
        return;
    }
    auto *entry = object(path.path());
    if (!entry)
        return;
    if (interfaces.contains(deviceInterface) || interfaces.contains(adapterInterface)) {
        if (m_pairingPath == path.path())
            finishPrompt(QStringLiteral("org.bluez.Error.Canceled"));
        const bool adapter = entry->isAdapter();
        m_objects.remove(path.path());
        m_discoveryOwned.remove(path.path());
        m_discoveryPending.remove(path.path());
        m_discoveryFailed.remove(path.path());
        m_stopRetries.remove(path.path());
        m_pairingOperations.remove(path.path());
        // bluetoothd removes an adapter's devices itself, one InterfacesRemoved
        // each, before the adapter.
        if (adapter)
            emit adaptersChanged();
        entry->deleteLater();
    } else {
        for (const auto &interface : interfaces)
            entry->removeInterface(interface);
        // bluetoothd names media transports from a global counter, so their paths never
        // return; the standard freedesktop interfaces alone do not keep an object.
        if (!entry->hasBluezInterface()) {
            m_objects.remove(path.path());
            entry->deleteLater();
        }
    }
    emit devicesChanged();
    emit changed();
}

void BluetoothManager::propertiesChanged(const QString &interface, const QVariantMap &properties,
                                         const QStringList &invalidated,
                                         const QDBusMessage &message)
{
    if (m_loading) {
        m_events.append([this, interface, properties, invalidated, message] {
            propertiesChanged(interface, properties, invalidated, message);
        });
        return;
    }
    auto *entry = object(message.path());
    if (!entry)
        return;
    entry->update(interface, properties, invalidated);
    // A code shown for a pairing another process started has nothing left to
    // wait for once the device reports it paired.
    if (interface == deviceInterface && entry->path() == m_pairingPath && !m_hasRequest
        && properties.value(QStringLiteral("Paired")).toBool())
        finishPrompt();
    if (interface == adapterInterface && !entry->powered()) {
        m_discoveryOwned.remove(entry->path());
        m_discoveryFailed.remove(entry->path());
        m_stopRetries.remove(entry->path());
    }
    // Invalidated values must be fetched again; a missing optional value stays absent.
    if (!invalidated.isEmpty()) {
        QPointer<BluetoothObject> guard(entry);
        QMap<QString, quint64> revisions;
        for (const auto &key : invalidated)
            revisions.insert(key, entry->revision(interface, key));
        call(entry->path(), propertiesInterface, QStringLiteral("GetAll"), {interface},
             [guard, interface, revisions](const QDBusMessage &reply) {
                 if (!guard || !guard->hasInterface(interface)
                     || reply.type() == QDBusMessage::ErrorMessage)
                     return;
                 const auto values = qdbus_cast<QVariantMap>(reply.arguments().value(0));
                 QVariantMap refreshed;
                 for (auto it = revisions.cbegin(); it != revisions.cend(); ++it) {
                     if (guard->revision(interface, it.key()) == it.value()
                         && values.contains(it.key()))
                         refreshed.insert(it.key(), values.value(it.key()));
                 }
                 if (!refreshed.isEmpty())
                     guard->update(interface, refreshed);
             },
             Errors::Ignore);
    }
}

QList<BluetoothObject *> BluetoothManager::adapters() const
{
    QList<BluetoothObject *> result;
    for (auto *object : m_objects) {
        if (object->isAdapter())
            result.append(object);
    }
    return result;
}

QList<BluetoothObject *> BluetoothManager::devices() const
{
    QList<BluetoothObject *> result;
    for (auto *entry : m_objects) {
        if (entry->isDevice() && object(entry->adapterPath()))
            result.append(entry);
    }
    return result;
}

BluetoothObject *BluetoothManager::object(const QString &path) const
{
    return m_objects.value(path);
}

bool BluetoothManager::enabled() const
{
    for (auto *adapter : adapters()) {
        if (adapter->powered())
            return true;
    }
    return false;
}

bool BluetoothManager::blocked() const
{
    return !enabled();
}

bool BluetoothManager::hardwareBlocked() const
{
    return m_rfkill && m_rfkill->hardwareBlocked();
}

bool BluetoothManager::scanning() const
{
    for (auto *adapter : adapters()) {
        if (adapter->discovering())
            return true;
    }
    return false;
}

void BluetoothManager::setEnabled(bool enabled)
{
    clearError();
    if (enabled && m_rfkill && !m_rfkill->unblock())
        return;
    if (enabled && hardwareBlocked())
        fail(tr("Bluetooth is blocked by a hardware switch."));
    if (!enabled)
        setDiscoveryRequested(false);
    if (!m_operational) {
        fail(tr("The Bluetooth service is unavailable."));
        return;
    }
    for (auto *adapter : adapters())
        adapter->setPowered(enabled);
}

void BluetoothManager::call(const QString &path, const QString &interface, const QString &method,
                            const QVariantList &arguments, Completion completion, Errors errors,
                            int timeout)
{
    if (m_owner.isEmpty()) {
        const auto error = QDBusMessage::createError(QStringLiteral("org.bluez.Error.NotReady"),
                                                     tr("The Bluetooth service is unavailable."));
        if (errors == Errors::Report)
            fail(error.errorMessage());
        if (completion)
            completion(error);
        return;
    }
    auto message = QDBusMessage::createMethodCall(m_owner, path, interface, method);
    message.setArguments(arguments);
    const auto generation = m_generation;
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, timeout), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, generation, method, completion, errors] {
                const QDBusMessage reply = watcher->reply();
                watcher->deleteLater();
                if (generation != m_generation)
                    return;
                if (reply.type() == QDBusMessage::ErrorMessage && errors == Errors::Report) {
                    qCWarning(lcBluetooth) << method << "failed:" << reply.errorName()
                                           << reply.errorMessage();
                    const QString reason = describe(reply);
                    fail(reason.isEmpty() ? tr("Bluetooth couldn't complete the request.")
                                          : reason);
                }
                if (completion)
                    completion(reply);
            });
}

void BluetoothManager::setProperty(BluetoothObject *entry, const QString &property,
                                   const QVariant &value)
{
    if (!entry || object(entry->path()) != entry)
        return;
    const QString interface = entry->isAdapter() ? adapterInterface : deviceInterface;
    call(entry->path(), propertiesInterface, QStringLiteral("Set"),
         {interface, property, QVariant::fromValue(QDBusVariant(value))});
}

void BluetoothManager::operate(BluetoothObject *entry, const QString &method,
                               const QVariantList &arguments)
{
    if (!entry || object(entry->path()) != entry || !entry->isDevice()) {
        deviceFailure(method, entry ? entry->name() : QString(),
                      tr("This device is no longer available."));
        return;
    }
    const bool cancel = method == QStringLiteral("CancelPairing");
    if (entry->busy() && !cancel) {
        deviceFailure(method, entry->name(), tr("The device is busy. Try again in a moment."));
        return;
    }
    // Without an agent of its own the caller pairs through BlueZ's default one,
    // which the shell keeps registered.
    if (method == QStringLiteral("Pair") && m_agentEnabled && !m_agentReady) {
        deviceFailure(method, entry->name(),
                      tr("Bluetooth isn't ready yet. Try again in a moment."));
        return;
    }
    clearError();
    if (!cancel)
        entry->setBusy(true);
    const bool remove = method == QStringLiteral("RemoveDevice");
    QPointer<BluetoothObject> guard(entry);
    const QString path = entry->path();
    const QString name = entry->name();
    if (method == QStringLiteral("Pair"))
        m_pairingOperations.insert(path);
    call(
            remove ? entry->adapterPath() : path, remove ? adapterInterface : deviceInterface,
            method, remove ? QVariantList {QVariant::fromValue(QDBusObjectPath(path))} : arguments,
            [this, guard, path, name, method, cancel](const QDBusMessage &reply) {
                if (method == QStringLiteral("Pair"))
                    m_pairingOperations.remove(path);
                if (guard && !cancel)
                    guard->setBusy(false);
                if (method == QStringLiteral("Pair") || cancel) {
                    if (m_pairingPath == path)
                        finishPrompt(QStringLiteral("org.bluez.Error.Canceled"));
                }
                if (reply.type() == QDBusMessage::ErrorMessage && !cancel) {
                    qCWarning(lcBluetooth) << method << "failed:" << reply.errorName()
                                           << reply.errorMessage();
                    // Dismissing the pairing prompt ends Pair with Canceled or Rejected,
                    // and a device that is already in the requested state needs no report.
                    static const QStringList quiet {
                            QStringLiteral("org.bluez.Error.AuthenticationCanceled"),
                            QStringLiteral("org.bluez.Error.AuthenticationRejected"),
                            QStringLiteral("org.bluez.Error.AlreadyConnected"),
                            QStringLiteral("org.bluez.Error.AlreadyExists"),
                            QStringLiteral("org.bluez.Error.NotConnected")};
                    const bool alreadyConnected =
                            reply.errorMessage().endsWith(QLatin1String("-already-connected"));
                    if (!quiet.contains(reply.errorName()) && !alreadyConnected)
                        deviceFailure(method, guard ? guard->name() : name, describe(reply));
                }
                if (!guard || reply.type() == QDBusMessage::ErrorMessage
                    || method != QStringLiteral("Pair"))
                    return;
                // Trust is granted only after a successful user-initiated pairing.
                guard->setBusy(true);
                call(path, propertiesInterface, QStringLiteral("Set"),
                     {deviceInterface, QStringLiteral("Trusted"),
                      QVariant::fromValue(QDBusVariant(true))},
                     [this, guard](const QDBusMessage &trustedReply) {
                         if (!guard)
                             return;
                         guard->setBusy(false);
                         if (trustedReply.type() != QDBusMessage::ErrorMessage)
                             operate(guard, QStringLiteral("Connect"));
                     });
            },
            Errors::Ignore, method == QStringLiteral("Pair") ? 180000 : 60000);
}

void BluetoothManager::setDiscoveryRequested(bool requested)
{
    m_discoveryFailed.clear();
    m_stopRetries.clear();
    if (m_discoveryRequested == requested) {
        syncDiscovery();
        return;
    }
    m_discoveryRequested = requested;
    emit changed();
    syncDiscovery();
}

void BluetoothManager::syncDiscovery()
{
    if (!m_operational)
        return;
    for (auto *adapter : adapters())
        discovery(adapter, m_discoveryRequested && adapter->powered());
}

void BluetoothManager::discovery(BluetoothObject *adapter, bool start)
{
    if (!adapter || !adapter->isAdapter() || !m_operational)
        return;
    const QString path = adapter->path();
    if (m_discoveryPending.contains(path) || m_discoveryOwned.contains(path) == start
        || (start && m_discoveryFailed.contains(path))
        || (!start && m_stopRetries.value(path) >= 3))
        return;
    m_discoveryPending.insert(path);
    QPointer<BluetoothObject> guard(adapter);
    call(path, adapterInterface,
         start ? QStringLiteral("StartDiscovery") : QStringLiteral("StopDiscovery"), {},
         [this, path, guard, start](const QDBusMessage &reply) {
             m_discoveryPending.remove(path);
             if (!guard || object(path) != guard)
                 return;
             if (reply.type() == QDBusMessage::ErrorMessage) {
                 if (start) {
                     m_discoveryFailed.insert(path);
                     if (m_discoveryOwned.isEmpty() && m_discoveryPending.isEmpty())
                         m_discoveryRequested = false;
                     emit changed();
                 } else {
                     ++m_stopRetries[path];
                     const auto generation = m_generation;
                     QTimer::singleShot(1000, this, [this, generation] {
                         if (generation == m_generation)
                             syncDiscovery();
                     });
                 }
                 return;
             }
             if (start)
                 m_discoveryOwned.insert(path);
             else
                 m_discoveryOwned.remove(path);
             m_stopRetries.remove(path);
             if (start != (m_discoveryRequested && guard->powered()))
                 syncDiscovery();
         });
}

void BluetoothManager::mediaCommand(BluetoothObject *device, const QString &command)
{
    const QStringList commands {"Play",     "Pause",       "Stop",  "Next",
                                "Previous", "FastForward", "Rewind"};
    if (!device || !commands.contains(command))
        return;
    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
        if (it.key().startsWith(device->path() + QLatin1Char('/'))
            && it.value()->hasInterface(QStringLiteral("org.bluez.MediaPlayer1"))) {
            call(it.key(), QStringLiteral("org.bluez.MediaPlayer1"), command);
            return;
        }
    }
    fail(tr("This device has no media player."));
}

void BluetoothManager::setAgentEnabled(bool enabled)
{
    if (m_agentEnabled == enabled)
        return;
    if (enabled && !m_bus.registerVirtualObject(agentPath, m_agent)) {
        fail(tr("Cannot export the Bluetooth pairing agent: %1").arg(m_bus.lastError().message()));
        return;
    }
    m_agentEnabled = enabled;
    if (enabled) {
        registerAgent();
    } else {
        const QString promptPath = m_pairingPath;
        cancelPairing();
        const auto pending = m_pairingOperations;
        for (const auto &path : pending) {
            if (path == promptPath)
                continue;
            if (auto *device = object(path))
                operate(device, QStringLiteral("CancelPairing"));
        }
        if (m_agentReady)
            call(QStringLiteral("/org/bluez"), agentManagerInterface,
                 QStringLiteral("UnregisterAgent"),
                 {QVariant::fromValue(QDBusObjectPath(agentPath))});
        m_agentReady = false;
        m_bus.unregisterObject(agentPath);
    }
    emit changed();
}

void BluetoothManager::registerAgent()
{
    if (!m_agentEnabled || !m_operational || m_agentReady || m_registeringAgent)
        return;
    m_registeringAgent = true;
    call(QStringLiteral("/org/bluez"), agentManagerInterface, QStringLiteral("RegisterAgent"),
         {QVariant::fromValue(QDBusObjectPath(agentPath)), QStringLiteral("KeyboardDisplay")},
         [this](const QDBusMessage &reply) {
             m_registeringAgent = false;
             if (reply.type() == QDBusMessage::ErrorMessage)
                 return;
             if (!m_agentEnabled) {
                 call(QStringLiteral("/org/bluez"), agentManagerInterface,
                      QStringLiteral("UnregisterAgent"),
                      {QVariant::fromValue(QDBusObjectPath(agentPath))});
                 return;
             }
             m_agentReady = true;
             emit changed();
             call(QStringLiteral("/org/bluez"), agentManagerInterface,
                  QStringLiteral("RequestDefaultAgent"),
                  {QVariant::fromValue(QDBusObjectPath(agentPath))});
         });
}

void BluetoothManager::prompt(const QDBusMessage &message)
{
    const auto args = message.arguments();
    const QString path = qvariant_cast<QDBusObjectPath>(args.value(0)).path();
    const QString kind = message.member();
    const bool display = kind.startsWith(QStringLiteral("Display"));
    if (m_hasRequest || (!m_pairingPath.isEmpty() && m_pairingPath != path)) {
        m_bus.send(message.createErrorReply(QStringLiteral("org.bluez.Error.Rejected"),
                                            tr("Another pairing request is active.")));
        return;
    }
    m_pairingPath = path;
    auto *device = object(path);
    m_pairingDevice = device ? device->name() : path;
    m_pairingKind = kind;
    m_pairingCode.clear();
    m_pairingEntered = 0;
    if (kind == QStringLiteral("RequestConfirmation") || kind == QStringLiteral("DisplayPasskey"))
        m_pairingCode = QStringLiteral("%1").arg(args.value(1).toUInt(), 6, 10, QLatin1Char('0'));
    else if (kind == QStringLiteral("DisplayPinCode") || kind == QStringLiteral("AuthorizeService"))
        m_pairingCode = args.value(1).toString();
    if (kind == QStringLiteral("DisplayPasskey"))
        m_pairingEntered = args.value(2).toUInt();
    if (display) {
        m_bus.send(message.createReply());
    } else {
        message.setDelayedReply(true);
        m_request = message;
        m_hasRequest = true;
    }
    m_promptTimer.start();
    emit pairingChanged();
}

void BluetoothManager::respondPairing(bool accept, const QString &code)
{
    if (!m_hasRequest)
        return;
    if (!accept) {
        finishPrompt(QStringLiteral("org.bluez.Error.Rejected"));
        return;
    }
    QVariantList arguments;
    if (m_pairingKind == QStringLiteral("RequestPinCode")) {
        if (code.isEmpty() || code.toUtf8().size() > 16) {
            fail(tr("Enter a PIN containing 1–16 bytes."));
            return;
        }
        arguments.append(code);
    } else if (m_pairingKind == QStringLiteral("RequestPasskey")) {
        static const QRegularExpression digits(QStringLiteral("^[0-9]{1,6}$"));
        if (!digits.match(code).hasMatch()) {
            fail(tr("Enter a passkey containing 1–6 digits."));
            return;
        }
        arguments.append(QVariant::fromValue(code.toUInt()));
    }
    m_bus.send(m_request.createReply(arguments));
    m_hasRequest = false;
    finishPrompt();
}

void BluetoothManager::finishPrompt(const QString &error)
{
    if (m_hasRequest)
        m_bus.send(m_request.createErrorReply(
                error.isEmpty() ? QStringLiteral("org.bluez.Error.Canceled") : error,
                tr("Pairing request dismissed.")));
    m_hasRequest = false;
    m_request = QDBusMessage();
    m_promptTimer.stop();
    m_pairingKind.clear();
    m_pairingPath.clear();
    m_pairingDevice.clear();
    m_pairingCode.clear();
    m_pairingEntered = 0;
    emit pairingChanged();
}

void BluetoothManager::cancelPairing()
{
    const QString path = m_pairingPath;
    finishPrompt(QStringLiteral("org.bluez.Error.Canceled"));
    if (auto *device = object(path))
        operate(device, QStringLiteral("CancelPairing"));
}

void BluetoothManager::fail(const QString &message)
{
    m_error = message;
    emit errorChanged();
}

void BluetoothManager::deviceFailure(const QString &method, const QString &device,
                                     const QString &body)
{
    QString summary;
    if (method == QStringLiteral("Connect") || method == QStringLiteral("ConnectProfile"))
        summary = tr("Couldn't connect to %1");
    else if (method == QStringLiteral("Disconnect")
             || method == QStringLiteral("DisconnectProfile"))
        summary = tr("Couldn't disconnect %1");
    else if (method == QStringLiteral("Pair"))
        summary = tr("Couldn't pair with %1");
    else if (method == QStringLiteral("RemoveDevice"))
        summary = tr("Couldn't forget %1");
    else
        return;
    emit deviceFailed(summary.arg(device.isEmpty() ? tr("the device") : device),
                      body.isEmpty() ? tr("Try again. If it keeps failing, turn the device off "
                                          "and on again.")
                                     : body);
}

// BlueZ reports most connection failures as org.bluez.Error.Failed with a
// "br-connection-*" or "le-connection-*" reason, or an errno string on older versions.
QString BluetoothManager::describe(const QDBusMessage &reply) const
{
    const QString name = reply.errorName().section(QLatin1Char('.'), -1);
    QString reason = reply.errorMessage();
    for (const auto prefix : {QLatin1String("br-connection-"), QLatin1String("le-connection-")}) {
        if (reason.startsWith(prefix))
            reason = reason.mid(prefix.size());
    }

    if (name == QLatin1String("NoReply") || name == QLatin1String("AuthenticationTimeout")
        || reason == QLatin1String("timeout") || reason == QLatin1String("Connection timed out"))
        return tr("The device didn't respond in time. Make sure it is turned on and nearby.");
    if (name == QLatin1String("ConnectionAttemptFailed") || reason == QLatin1String("page-timeout")
        || reason == QLatin1String("Host is down"))
        return tr("The device didn't respond. Make sure it is turned on, nearby, and not "
                  "connected to another device.");
    if (reason == QLatin1String("key-missing"))
        return tr("The device no longer recognizes this computer. Forget the device and pair "
                  "it again.");
    if (name == QLatin1String("AuthenticationFailed"))
        return tr("Pairing failed. Make sure the code matches on both devices and try again.");
    if (name == QLatin1String("AuthenticationRejected"))
        return tr("The device declined the request.");
    if (reason == QLatin1String("refused") || reason == QLatin1String("Connection refused"))
        return tr("The device refused the connection.");
    if (name == QLatin1String("NotSupported") || reason == QLatin1String("profile-unavailable")
        || reason == QLatin1String("not-supported") || reason == QLatin1String("sdp-search")
        || reason == QLatin1String("Protocol not available"))
        return tr("The device has no features this computer can use.");
    if (reason == QLatin1String("adapter-not-powered"))
        return tr("Bluetooth is turned off.");
    if (name == QLatin1String("NotReady"))
        return tr("Bluetooth isn't ready yet. Try again in a moment.");
    if (name == QLatin1String("InProgress") || reason == QLatin1String("busy")
        || reason == QLatin1String("Operation already in progress"))
        return tr("The device is busy. Try again in a moment.");
    if (reason == QLatin1String("concurrent-connection-limit"))
        return tr("Too many devices are connected. Disconnect one and try again.");
    if (reason == QLatin1String("aborted-by-remote"))
        return tr("The device ended the connection.");
    if (reason == QLatin1String("aborted-by-local") || reason == QLatin1String("canceled"))
        return tr("The connection was interrupted. Try again.");
    if (name == QLatin1String("DoesNotExist") || name == QLatin1String("UnknownObject"))
        return tr("This device is no longer available.");
    return QString();
}

void BluetoothManager::notify(const QString &summary, const QString &body)
{
    const QString service = QStringLiteral("org.freedesktop.Notifications");
    const QString path = QStringLiteral("/org/freedesktop/Notifications");
    QDBusMessage message =
            QDBusMessage::createMethodCall(service, path, service, QStringLiteral("Notify"));
    message << tr("Bluetooth") << 0u << QStringLiteral("bluetooth") << summary << body
            << QStringList() << QVariantMap() << -1;
    QDBusConnection::sessionBus().call(message, QDBus::NoBlock);
}

void BluetoothManager::clearError()
{
    if (m_error.isEmpty())
        return;
    m_error.clear();
    emit errorChanged();
}
