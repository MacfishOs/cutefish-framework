#pragma once

#include "../networkdbustypes.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QHash>
#include <QSet>

namespace FakeNM {

const QString managerPath = QStringLiteral("/org/freedesktop/NetworkManager");
const QString manager = QStringLiteral("org.freedesktop.NetworkManager");
const QString device = QStringLiteral("org.freedesktop.NetworkManager.Device");
const QString wireless = QStringLiteral("org.freedesktop.NetworkManager.Device.Wireless");
const QString accessPoint = QStringLiteral("org.freedesktop.NetworkManager.AccessPoint");
const QString active = QStringLiteral("org.freedesktop.NetworkManager.Connection.Active");
const QString connection = QStringLiteral("org.freedesktop.NetworkManager.Settings.Connection");
const QString settings = QStringLiteral("org.freedesktop.NetworkManager.Settings");
const QString agentManager = QStringLiteral("org.freedesktop.NetworkManager.AgentManager");
const QString properties = QStringLiteral("org.freedesktop.DBus.Properties");
const QString objectManager = QStringLiteral("org.freedesktop.DBus.ObjectManager");

inline QVariant path(const QString &path)
{
    return QVariant::fromValue(QDBusObjectPath(path.isEmpty() ? QStringLiteral("/") : path));
}

inline QVariant paths(const QStringList &paths)
{
    QList<QDBusObjectPath> list;
    for (const QString &path : paths)
        list << QDBusObjectPath(path);
    return QVariant::fromValue(list);
}

} // namespace FakeNM

// NetworkManager on a private bus connection: its objects, profiles and the calls made to
// it, with the signals NetworkManager sends when they change.
class FakeNetworkManager : public QDBusVirtualObject
{
public:
    explicit FakeNetworkManager(const QDBusConnection &bus)
        : m_bus(bus)
    {
        registerNetworkDBusTypes();
        qDBusRegisterMetaType<QList<QVariantMap>>();
        objects[FakeNM::managerPath][FakeNM::manager] = {
            {QStringLiteral("Version"), QStringLiteral("1.52.1")},
            {QStringLiteral("State"), 70u},
            {QStringLiteral("NetworkingEnabled"), true},
            {QStringLiteral("WirelessEnabled"), true},
            {QStringLiteral("WirelessHardwareEnabled"), true},
            {QStringLiteral("WwanEnabled"), false},
            {QStringLiteral("WwanHardwareEnabled"), false},
            {QStringLiteral("Devices"), FakeNM::paths({})},
            {QStringLiteral("ActiveConnections"), FakeNM::paths({})},
            {QStringLiteral("PrimaryConnection"), FakeNM::path({})},
            {QStringLiteral("PrimaryConnectionType"), QString()},
        };
        objects[FakeNM::managerPath + QStringLiteral("/Settings")][FakeNM::settings] = {
            {QStringLiteral("Connections"), FakeNM::paths({})},
        };
        objects[FakeNM::managerPath + QStringLiteral("/AgentManager")][FakeNM::agentManager] = {};
    }

    QString introspect(const QString &) const override { return {}; }

    // A new object, announced the way NetworkManager announces one.
    void add(const QString &path, const NMVariantMapMap &interfaces, bool announce = true)
    {
        objects[path] = interfaces;
        if (interfaces.contains(FakeNM::connection))
            appendPath(FakeNM::managerPath + QStringLiteral("/Settings"), FakeNM::settings,
                       QStringLiteral("Connections"), path, announce);
        if (interfaces.contains(FakeNM::active))
            appendPath(FakeNM::managerPath, FakeNM::manager, QStringLiteral("ActiveConnections"), path,
                       announce);
        if (interfaces.contains(FakeNM::device))
            appendPath(FakeNM::managerPath, FakeNM::manager, QStringLiteral("Devices"), path, announce);
        if (announce) {
            emitSignal(QStringLiteral("/org/freedesktop"), FakeNM::objectManager, QStringLiteral("InterfacesAdded"),
                       {FakeNM::path(path), QVariant::fromValue(interfaces)});
        }
    }

    void remove(const QString &path)
    {
        const QStringList interfaces = objects.value(path).keys();
        objects.remove(path);
        settings.remove(path);
        removePath(FakeNM::managerPath + QStringLiteral("/Settings"), FakeNM::settings,
                   QStringLiteral("Connections"), path);
        removePath(FakeNM::managerPath, FakeNM::manager, QStringLiteral("ActiveConnections"), path);
        removePath(FakeNM::managerPath, FakeNM::manager, QStringLiteral("Devices"), path);
        emitSignal(QStringLiteral("/org/freedesktop"), FakeNM::objectManager, QStringLiteral("InterfacesRemoved"),
                   {FakeNM::path(path), interfaces});
    }

    void set(const QString &path, const QString &interface, const QString &name, const QVariant &value)
    {
        objects[path][interface][name] = value;
        emitSignal(path, FakeNM::properties, QStringLiteral("PropertiesChanged"),
                   {interface, QVariantMap {{name, value}}, QStringList()});
    }

    void emitSignal(const QString &path, const QString &interface, const QString &name,
                    const QVariantList &arguments)
    {
        auto signal = QDBusMessage::createSignal(path, interface, name);
        signal.setArguments(arguments);
        m_bus.send(signal);
    }

    int count(const QString &member) const
    {
        int count = 0;
        for (const QDBusMessage &call : calls)
            count += call.member() == member;
        return count;
    }

    QDBusMessage last(const QString &member) const
    {
        for (auto it = calls.crbegin(); it != calls.crend(); ++it) {
            if (it->member() == member)
                return *it;
        }
        return {};
    }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        const QVariantList args = message.arguments();
        const QString member = message.member();
        const QString path = message.path();
        QDBusMessage reply = message.createReply();

        if (message.interface() != FakeNM::properties)
            calls << message;

        if (member == QStringLiteral("GetManagedObjects")) {
            NMObjects managed;
            for (auto it = objects.cbegin(); it != objects.cend(); ++it)
                managed.insert(QDBusObjectPath(it.key()), it.value());
            reply << QVariant::fromValue(managed);
        } else if (member == QStringLiteral("GetAll")) {
            reply << objects.value(path).value(args.value(0).toString());
        } else if (member == QStringLiteral("Get")) {
            reply << QVariant::fromValue(QDBusVariant(
                    objects.value(path).value(args.value(0).toString()).value(args.value(1).toString())));
        } else if (member == QStringLiteral("Set")) {
            set(path, args.value(0).toString(), args.value(1).toString(),
                args.value(2).value<QDBusVariant>().variant());
        } else if (member == QStringLiteral("GetPermissions")) {
            reply << QVariant::fromValue(permissions);
        } else if (member == QStringLiteral("GetSettings")) {
            if (privateConnections.contains(path)) {
                reply = message.createErrorReply(QStringLiteral("org.freedesktop.NetworkManager.Settings.PermissionDenied"),
                                                 QStringLiteral("Not yours"));
            } else {
                reply << QVariant::fromValue(settings.value(path));
            }
        } else if (member == QStringLiteral("GetSecrets")) {
            reply << QVariant::fromValue(secrets.value(path));
        } else if (member == QStringLiteral("Update")) {
            settings[path] = qdbus_cast<NMVariantMapMap>(args.value(0));
            emitSignal(path, FakeNM::connection, QStringLiteral("Updated"), {});
        } else if (member == QStringLiteral("Delete")) {
            connection.send(reply);
            remove(path);
            return true;
        } else if (member == QStringLiteral("AddConnection")) {
            reply << FakeNM::path(addConnection(qdbus_cast<NMVariantMapMap>(args.value(0))));
        } else if (member == QStringLiteral("AddAndActivateConnection")) {
            const QString profile = addConnection(qdbus_cast<NMVariantMapMap>(args.value(0)));
            const QString activeConnection =
                    activate(profile, args.value(1).value<QDBusObjectPath>().path(),
                             args.value(2).value<QDBusObjectPath>().path());
            reply << FakeNM::path(profile) << FakeNM::path(activeConnection);
        } else if (member == QStringLiteral("ActivateConnection")) {
            reply << FakeNM::path(activate(args.value(0).value<QDBusObjectPath>().path(),
                                           args.value(1).value<QDBusObjectPath>().path(),
                                           args.value(2).value<QDBusObjectPath>().path()));
        } else if (member == QStringLiteral("DeactivateConnection") || member == QStringLiteral("Disconnect")
                   || member == QStringLiteral("RequestScan") || member == QStringLiteral("Enable")
                   || member == QStringLiteral("RegisterWithCapabilities")
                   || member == QStringLiteral("Unregister")) {
            // Recorded above; the tests drive the outcome.
        } else {
            return false;
        }

        connection.send(reply);
        return true;
    }

    QString addConnection(const NMVariantMapMap &profile)
    {
        const QString path = FakeNM::managerPath + QStringLiteral("/Settings/%1").arg(++m_next);
        settings[path] = profile;
        add(path, {{FakeNM::connection, {{QStringLiteral("Unsaved"), false}, {QStringLiteral("Flags"), 0u}}}});
        return path;
    }

    QString activate(const QString &profile, const QString &device, const QString &specificObject)
    {
        const QString path = FakeNM::managerPath + QStringLiteral("/ActiveConnection/%1").arg(++m_next);
        add(path, {{FakeNM::active,
                    {{QStringLiteral("Connection"), FakeNM::path(profile)},
                     {QStringLiteral("SpecificObject"), FakeNM::path(specificObject)},
                     {QStringLiteral("Devices"), FakeNM::paths({device})},
                     {QStringLiteral("State"), 1u},
                     {QStringLiteral("Type"), QStringLiteral("802-11-wireless")},
                     {QStringLiteral("Vpn"), false}}}});
        return path;
    }

    QHash<QString, NMVariantMapMap> objects;
    QHash<QString, NMVariantMapMap> settings;
    QHash<QString, NMVariantMapMap> secrets;
    QSet<QString> privateConnections;
    QMap<QString, QString> permissions;
    QList<QDBusMessage> calls;

private:
    void appendPath(const QString &owner, const QString &interface, const QString &name,
                    const QString &path, bool announce)
    {
        QList<QDBusObjectPath> list = qvariant_cast<QList<QDBusObjectPath>>(objects[owner][interface][name]);
        list << QDBusObjectPath(path);
        if (announce)
            set(owner, interface, name, QVariant::fromValue(list));
        else
            objects[owner][interface][name] = QVariant::fromValue(list);
    }

    void removePath(const QString &owner, const QString &interface, const QString &name, const QString &path)
    {
        QList<QDBusObjectPath> list = qvariant_cast<QList<QDBusObjectPath>>(objects[owner][interface][name]);
        if (list.removeAll(QDBusObjectPath(path)))
            set(owner, interface, name, QVariant::fromValue(list));
    }

    QDBusConnection m_bus;
    int m_next = 100;
};
