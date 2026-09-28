#pragma once

#include "../bluetoothmanager.h"
#include <QDBusPendingCall>
#include <QDBusVariant>
#include <QDBusVirtualObject>

namespace {
const QString adapter = QStringLiteral("/org/bluez/hci0");
const QString device = adapter + QStringLiteral("/dev_AA_BB_CC_DD_EE_FF");
const QString adapterInterface = QStringLiteral("org.bluez.Adapter1");
const QString deviceInterface = QStringLiteral("org.bluez.Device1");
} // namespace

class FakeBluez : public QDBusVirtualObject
{
public:
    explicit FakeBluez(const QDBusConnection &bus)
        : bus(bus)
    {
        objects[QDBusObjectPath(adapter)][adapterInterface] = {{"Address", "11:22:33:44:55:66"},
                                                               {"Alias", "Test adapter"},
                                                               {"Powered", true},
                                                               {"Pairable", true}};
        objects[QDBusObjectPath(device)][deviceInterface] = {
                {"Address", "AA:BB:CC:DD:EE:FF"},
                {"Alias", "Headphones"},
                {"Adapter", QVariant::fromValue(QDBusObjectPath(adapter))},
                {"Paired", false},
                {"Connected", false}};
    }

    QString introspect(const QString &) const override { return {}; }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        calls.append(message.member());
        lastPath = message.path();
        if (message.member() == failMethod) {
            connection.send(message.createErrorReply(failName, failMessage));
            return true;
        }
        const auto args = message.arguments();
        if (message.member() == QStringLiteral("GetManagedObjects")) {
            if (holdSnapshot) {
                message.setDelayedReply(true);
                pendingSnapshot = message;
                snapshot = objects;
            } else {
                connection.send(message.createReply({QVariant::fromValue(objects)}));
            }
        } else if (message.member() == QStringLiteral("Set")) {
            set(message.path(), args[0].toString(), args[1].toString(),
                qvariant_cast<QDBusVariant>(args[2]).variant());
            connection.send(message.createReply());
        } else if (message.member() == QStringLiteral("GetAll")) {
            if (holdGetAll) {
                message.setDelayedReply(true);
                pendingGetAll = message;
                oldProperties =
                        objects.value(QDBusObjectPath(message.path())).value(args[0].toString());
            } else {
                connection.send(message.createReply(QVariantList {
                        objects.value(QDBusObjectPath(message.path())).value(args[0].toString())}));
            }
        } else if (message.member() == QStringLiteral("RegisterAgent")) {
            agentOwner = message.service();
            agentPath = qvariant_cast<QDBusObjectPath>(args[0]).path();
            connection.send(message.createReply());
        } else if (message.member() == QStringLiteral("Pair")) {
            if (holdPair) {
                message.setDelayedReply(true);
                pendingPair = message;
            } else {
                set(message.path(), deviceInterface, QStringLiteral("Paired"), true);
                connection.send(message.createReply());
            }
        } else if (message.member() == QStringLiteral("CancelPairing")) {
            if (pendingPair.type() == QDBusMessage::MethodCallMessage) {
                connection.send(pendingPair.createErrorReply(
                        QStringLiteral("org.bluez.Error.AuthenticationCanceled"),
                        QStringLiteral("Canceled")));
                pendingPair = {};
            }
            connection.send(message.createReply());
        } else if (message.member() == QStringLiteral("Connect")
                   || message.member() == QStringLiteral("Disconnect")) {
            set(message.path(), deviceInterface, QStringLiteral("Connected"),
                message.member() == QStringLiteral("Connect"));
            connection.send(message.createReply());
        } else if (message.member() == QStringLiteral("RemoveDevice")) {
            remove(qvariant_cast<QDBusObjectPath>(args[0]).path(), {deviceInterface});
            connection.send(message.createReply());
        } else if (message.member().endsWith(QStringLiteral("Discovery"))) {
            set(message.path(), adapterInterface, QStringLiteral("Discovering"),
                message.member() == QStringLiteral("StartDiscovery"));
            connection.send(message.createReply());
        } else {
            connection.send(message.createReply());
        }
        return true;
    }

    void set(const QString &path, const QString &interface, const QString &key,
             const QVariant &value)
    {
        objects[QDBusObjectPath(path)][interface][key] = value;
        auto signal =
                QDBusMessage::createSignal(path, QStringLiteral("org.freedesktop.DBus.Properties"),
                                           QStringLiteral("PropertiesChanged"));
        signal << interface << QVariantMap {{key, value}} << QStringList {};
        bus.send(signal);
    }

    void add(const QString &path, const BluetoothInterfaces &interfaces)
    {
        for (auto it = interfaces.cbegin(); it != interfaces.cend(); ++it)
            objects[QDBusObjectPath(path)][it.key()] = it.value();
        auto signal = QDBusMessage::createSignal(
                QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                QStringLiteral("InterfacesAdded"));
        signal << QVariant::fromValue(QDBusObjectPath(path)) << QVariant::fromValue(interfaces);
        bus.send(signal);
    }

    void remove(const QString &path, const QStringList &interfaces)
    {
        for (const auto &interface : interfaces)
            objects[QDBusObjectPath(path)].remove(interface);
        if (objects[QDBusObjectPath(path)].isEmpty())
            objects.remove(QDBusObjectPath(path));
        auto signal = QDBusMessage::createSignal(
                QStringLiteral("/"), QStringLiteral("org.freedesktop.DBus.ObjectManager"),
                QStringLiteral("InterfacesRemoved"));
        signal << QVariant::fromValue(QDBusObjectPath(path)) << interfaces;
        bus.send(signal);
    }

    QDBusPendingCall ask(const QString &method, const QVariantList &arguments)
    {
        auto message = QDBusMessage::createMethodCall(agentOwner, agentPath,
                                                      QStringLiteral("org.bluez.Agent1"), method);
        message.setArguments(arguments);
        return bus.asyncCall(message);
    }

    QDBusConnection bus;
    BluetoothObjects objects;
    QStringList calls;
    QString lastPath;
    QString failMethod;
    QString failName = QStringLiteral("org.bluez.Error.Failed");
    QString failMessage = QStringLiteral("Injected failure");
    QString agentOwner;
    QString agentPath;
    bool holdPair = false;
    bool holdSnapshot = false;
    bool holdGetAll = false;
    QDBusMessage pendingPair;
    QDBusMessage pendingSnapshot;
    QDBusMessage pendingGetAll;
    BluetoothObjects snapshot;
    QVariantMap oldProperties;
};
