#pragma once

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QMap>

namespace {

const QString managerPath = QStringLiteral("/org/freedesktop/NetworkManager");
const QString managerInterface = QStringLiteral("org.freedesktop.NetworkManager");
const QString wiredPath = managerPath + QStringLiteral("/Devices/1");
const QString wirelessPath = managerPath + QStringLiteral("/Devices/2");

// NM_DEVICE_TYPE_* and NM_DEVICE_STATE_DISCONNECTED
constexpr uint ethernetType = 1;
constexpr uint wifiType = 2;
constexpr uint disconnectedState = 30;

} // namespace

// NetworkManager as NetworkManagerQt reads it, with one Ethernet and one Wi-Fi device;
// served from its own thread because NetworkManagerQt calls it synchronously.
class FakeNetworkManager : public QDBusVirtualObject
{
public:
    explicit FakeNetworkManager(const QDBusConnection &bus)
        : bus(bus)
    {
        const QList<QDBusObjectPath> devices {QDBusObjectPath(wiredPath),
                                              QDBusObjectPath(wirelessPath)};
        properties[managerPath][managerInterface] = {{"Version", "1.52.0"},
                                                     {"State", 70u},
                                                     {"NetworkingEnabled", true},
                                                     {"WirelessEnabled", true},
                                                     {"WirelessHardwareEnabled", true},
                                                     {"Devices", QVariant::fromValue(devices)}};
        properties[managerPath + QStringLiteral("/Settings")]
                  [QStringLiteral("org.freedesktop.NetworkManager.Settings")] = {
                {"Connections", QVariant::fromValue(QList<QDBusObjectPath>())}};
        addDevice(wiredPath, QStringLiteral("eth0"), ethernetType);
        addDevice(wirelessPath, QStringLiteral("wlan0"), wifiType);
        properties[wirelessPath]
                  [QStringLiteral("org.freedesktop.NetworkManager.Device.Wireless")] = {
                {"AccessPoints", QVariant::fromValue(QList<QDBusObjectPath>())}};
    }

    QString introspect(const QString &) const override { return {}; }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        const auto args = message.arguments();
        const QVariantMap values = properties.value(message.path()).value(args.value(0).toString());
        if (message.member() == QStringLiteral("GetAll")) {
            connection.send(message.createReply(QVariant::fromValue(values)));
        } else if (message.member() == QStringLiteral("Get")) {
            const QString name = args.value(1).toString();
            if (values.contains(name))
                connection.send(
                        message.createReply(QVariant::fromValue(QDBusVariant(values.value(name)))));
            else
                connection.send(message.createErrorReply(
                        QStringLiteral("org.freedesktop.DBus.Error.UnknownProperty"), name));
        } else {
            connection.send(message.createReply());
        }
        return true;
    }

    void removeDevice(const QString &path)
    {
        auto signal = QDBusMessage::createSignal(managerPath, managerInterface,
                                                 QStringLiteral("DeviceRemoved"));
        signal << QVariant::fromValue(QDBusObjectPath(path));
        bus.send(signal);
    }

    QDBusConnection bus;

private:
    void addDevice(const QString &path, const QString &interface, uint type)
    {
        properties[path][QStringLiteral("org.freedesktop.NetworkManager.Device")] = {
                {"Interface", interface},
                {"DeviceType", type},
                {"Managed", true},
                {"State", disconnectedState}};
    }

    QMap<QString, QMap<QString, QVariantMap>> properties;
};
