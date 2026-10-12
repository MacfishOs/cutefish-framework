#include "bluetoothobject.h"
#include "bluetoothmanager.h"

using namespace BluetoothDBus;

BluetoothObject::BluetoothObject(const QString &path, BluetoothManager *manager)
    : QObject(manager)
    , m_path(path)
    , m_manager(manager)
{
}

QVariant BluetoothObject::value(const QString &property) const
{
    return m_interfaces.value(isAdapter() ? adapterInterface : deviceInterface).value(property);
}

QVariant BluetoothObject::value(const QString &interface, const QString &property) const
{
    return m_interfaces.value(interface).value(property);
}

QString BluetoothObject::address() const
{
    return value(QStringLiteral("Address")).toString();
}

QString BluetoothObject::name() const
{
    QString name = value(QStringLiteral("Alias")).toString();
    if (name.isEmpty())
        name = value(QStringLiteral("Name")).toString();
    return name.isEmpty() ? address() : name;
}

QString BluetoothObject::icon() const
{
    return value(QStringLiteral("Icon")).toString();
}

bool BluetoothObject::powered() const
{
    return value(QStringLiteral("Powered")).toBool();
}

bool BluetoothObject::discovering() const
{
    return value(QStringLiteral("Discovering")).toBool();
}

bool BluetoothObject::discoverable() const
{
    return value(QStringLiteral("Discoverable")).toBool();
}

bool BluetoothObject::pairable() const
{
    return value(QStringLiteral("Pairable")).toBool();
}

bool BluetoothObject::paired() const
{
    return value(QStringLiteral("Paired")).toBool();
}

bool BluetoothObject::connected() const
{
    return value(QStringLiteral("Connected")).toBool();
}

bool BluetoothObject::trusted() const
{
    return value(QStringLiteral("Trusted")).toBool();
}

bool BluetoothObject::blocked() const
{
    return value(QStringLiteral("Blocked")).toBool();
}

bool BluetoothObject::servicesResolved() const
{
    return value(QStringLiteral("ServicesResolved")).toBool();
}

int BluetoothObject::rssi() const
{
    const auto signal = value(QStringLiteral("RSSI"));
    return signal.isValid() ? signal.toInt() : -32768;
}

int BluetoothObject::batteryPercentage() const
{
    return m_interfaces.value(QStringLiteral("org.bluez.Battery1"))
            .value(QStringLiteral("Percentage"), -1)
            .toInt();
}

QStringList BluetoothObject::uuids() const
{
    return value(QStringLiteral("UUIDs")).toStringList();
}

QString BluetoothObject::adapterPath() const
{
    return qvariant_cast<QDBusObjectPath>(value(QStringLiteral("Adapter"))).path();
}

bool BluetoothObject::isAdapter() const
{
    return m_interfaces.contains(adapterInterface);
}

bool BluetoothObject::isDevice() const
{
    return m_interfaces.contains(deviceInterface);
}

bool BluetoothObject::hasBluezInterface() const
{
    for (auto it = m_interfaces.cbegin(); it != m_interfaces.cend(); ++it) {
        if (it.key().startsWith(QLatin1String("org.bluez.")))
            return true;
    }
    return false;
}

void BluetoothObject::setValue(const QString &property, const QVariant &value)
{
    m_manager->setObjectProperty(this, property, value);
}

void BluetoothObject::setName(const QString &name)
{
    setValue(QStringLiteral("Alias"), name);
}

void BluetoothObject::setPowered(bool enabled)
{
    setValue(QStringLiteral("Powered"), enabled);
}

void BluetoothObject::setDiscoverable(bool enabled)
{
    setValue(QStringLiteral("Discoverable"), enabled);
}

void BluetoothObject::setPairable(bool enabled)
{
    setValue(QStringLiteral("Pairable"), enabled);
}

void BluetoothObject::setTrusted(bool enabled)
{
    setValue(QStringLiteral("Trusted"), enabled);
}

void BluetoothObject::setBlocked(bool enabled)
{
    setValue(QStringLiteral("Blocked"), enabled);
}

void BluetoothObject::connectToDevice()
{
    m_manager->operate(this, QStringLiteral("Connect"));
}

void BluetoothObject::disconnectFromDevice()
{
    m_manager->operate(this, QStringLiteral("Disconnect"));
}

void BluetoothObject::pair()
{
    m_manager->operate(this, QStringLiteral("Pair"));
}

void BluetoothObject::cancelPairing()
{
    m_manager->operate(this, QStringLiteral("CancelPairing"));
}

void BluetoothObject::forget()
{
    m_manager->operate(this, QStringLiteral("RemoveDevice"));
}

void BluetoothObject::connectProfile(const QString &uuid)
{
    m_manager->operate(this, QStringLiteral("ConnectProfile"), {uuid});
}

void BluetoothObject::disconnectProfile(const QString &uuid)
{
    m_manager->operate(this, QStringLiteral("DisconnectProfile"), {uuid});
}

void BluetoothObject::startDiscovery()
{
    m_manager->discovery(this, true);
}

void BluetoothObject::stopDiscovery()
{
    m_manager->discovery(this, false);
}

void BluetoothObject::mediaCommand(const QString &command)
{
    m_manager->mediaCommand(this, command);
}

void BluetoothObject::readDeviceInformation()
{
    m_manager->readDeviceInformation(this);
}

void BluetoothObject::update(const QString &interface, const QVariantMap &properties,
                             const QStringList &invalidated)
{
    auto &values = m_interfaces[interface];
    for (const auto &key : invalidated) {
        values.remove(key);
        m_revisions[interface][key] = ++m_revision;
    }
    for (auto it = properties.cbegin(); it != properties.cend(); ++it) {
        values.insert(it.key(), it.value());
        m_revisions[interface][it.key()] = ++m_revision;
    }
    emit changed();
}

quint64 BluetoothObject::revision(const QString &interface, const QString &property) const
{
    return m_revisions.value(interface).value(property);
}

void BluetoothObject::removeInterface(const QString &interface)
{
    m_interfaces.remove(interface);
    m_revisions.remove(interface);
    emit changed();
}

void BluetoothObject::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit changed();
}

void BluetoothObject::setModelNumber(const QString &modelNumber)
{
    if (m_modelNumber == modelNumber)
        return;
    m_modelNumber = modelNumber;
    emit changed();
}

void BluetoothObject::setFirmwareRevision(const QString &firmwareRevision)
{
    if (m_firmwareRevision == firmwareRevision)
        return;
    m_firmwareRevision = firmwareRevision;
    emit changed();
}
