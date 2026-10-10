#pragma once

#include "../networkdbustypes.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
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

// NetworkManager's completion of a profile for the access point it is added and activated
// with (nm_wifi_utils_complete_connection() for an infrastructure AP), then its 802-1x
// verify(): the completed profile, or the error NetworkManager would refuse it with.
inline QString completeForAccessPoint(NMVariantMapMap &profile, const QVariantMap &ap)
{
    constexpr uint privacy = 0x1;
    constexpr uint keyPsk = 0x100, key8021x = 0x200, keySae = 0x400, keyOwe = 0x800 | 0x1000,
                   keySuiteB = 0x2000;
    const QString wsecName = QStringLiteral("802-11-wireless-security");
    const QString eapName = QStringLiteral("802-1x");

    QVariantMap &wifi = profile[QStringLiteral("802-11-wireless")];
    const QByteArray ssid = ap.value(QStringLiteral("Ssid")).toByteArray();
    if (!wifi.contains(QStringLiteral("ssid")))
        wifi.insert(QStringLiteral("ssid"), ssid);
    else if (wifi.value(QStringLiteral("ssid")).toByteArray() != ssid)
        return QStringLiteral("802-11-wireless.ssid: connection does not match access point");
    const QString mode = wifi.value(QStringLiteral("mode"), QStringLiteral("infrastructure")).toString();
    if (mode != QLatin1String("infrastructure") && mode != QLatin1String("ap"))
        return QStringLiteral("802-11-wireless.mode: connection does not match access point");
    wifi.insert(QStringLiteral("mode"), mode);

    const uint flags = ap.value(QStringLiteral("Flags")).toUInt();
    const uint wpa = ap.value(QStringLiteral("WpaFlags")).toUInt();
    const uint rsn = ap.value(QStringLiteral("RsnFlags")).toUInt();
    const bool has8021x = profile.contains(eapName);

    if (!(flags & privacy) && !wpa && !rsn) {
        if (profile.contains(wsecName) || has8021x)
            return QStringLiteral("Access point is unencrypted but setting specifies security");
        return {};
    }

    QVariantMap &wsec = profile[wsecName];
    const QString keyMgmt = wsec.value(QStringLiteral("key-mgmt")).toString();
    const QString authAlg = wsec.value(QStringLiteral("auth-alg")).toString();
    const bool leap = wsec.contains(QStringLiteral("leap-username"));

    if (!wpa && !rsn) {
        if (keyMgmt.startsWith(QLatin1String("wpa")) || keyMgmt == QLatin1String("sae"))
            return QStringLiteral("WEP access point but setting requires WPA");
        if (leap) {
            wsec.insert(QStringLiteral("key-mgmt"), QStringLiteral("ieee8021x"));
            wsec.insert(QStringLiteral("auth-alg"), QStringLiteral("leap"));
        } else if (has8021x || keyMgmt == QLatin1String("ieee8021x")) {
            wsec.insert(QStringLiteral("key-mgmt"), QStringLiteral("ieee8021x"));
            wsec.insert(QStringLiteral("auth-alg"), QStringLiteral("open"));
        } else {
            wsec.insert(QStringLiteral("key-mgmt"), QStringLiteral("none"));
        }
    } else {
        if (keyMgmt == QLatin1String("ieee8021x") || leap)
            return QStringLiteral("WPA authentication is incompatible with non-EAP (original) LEAP or Dynamic WEP");
        if (!authAlg.isEmpty() && authAlg != QLatin1String("open"))
            return QStringLiteral("WPA authentication is incompatible with Shared Key authentication");
        if (wsec.contains(QStringLiteral("wep-key0")))
            return QStringLiteral("WPA access point but setting requires WEP");
        if (keyMgmt == QLatin1String("wpa-psk")) {
            if (has8021x)
                return QStringLiteral("WPA-PSK authentication is incompatible with 802.1x");
            if (!((wpa | rsn) & keyPsk))
                return QStringLiteral("Access point does not support PSK but setting requires it");
        }
        const bool wpaEap = keyMgmt == QLatin1String("wpa-eap") || keyMgmt == QLatin1String("wpa-eap-suite-b-192");
        if (wpaEap && !has8021x)
            return QStringLiteral("WPA-EAP authentication requires an 802.1x setting");
        if (!keyMgmt.isEmpty() && !wpaEap && has8021x)
            return QStringLiteral("802.1x setting requires 'wpa-eap' key management");
        if ((wpaEap || has8021x) && !((wpa | rsn) & key8021x) && !(rsn & keySuiteB))
            return QStringLiteral("Access point does not support 802.1x but setting requires it");

        QString completed;
        if (has8021x)
            completed = QStringLiteral("wpa-eap");
        else if ((rsn & keySae) && ((wpa | rsn) & keyPsk))
            completed = keyMgmt == QLatin1String("sae") ? keyMgmt : QStringLiteral("wpa-psk");
        else if (keyMgmt == QLatin1String("sae") || (rsn & keySae))
            completed = QStringLiteral("sae");
        else if (keyMgmt == QLatin1String("owe") || (rsn & keyOwe))
            completed = QStringLiteral("owe");
        else if (keyMgmt == QLatin1String("wpa-psk") || ((wpa | rsn) & keyPsk))
            completed = QStringLiteral("wpa-psk");
        else if (keyMgmt == QLatin1String("wpa-eap-suite-b-192") || (rsn & keySuiteB))
            completed = QStringLiteral("wpa-eap-suite-b-192");
        else
            return QStringLiteral("Failed to determine AP security information");
        wsec.insert(QStringLiteral("key-mgmt"), completed);
    }

    if (!has8021x)
        return {};

    // The methods' checks in libnm-core-impl/nm-setting-8021x.c.
    const QVariantMap eap = profile.value(eapName);
    const QStringList methods = eap.value(QStringLiteral("eap")).toStringList();
    if (methods.isEmpty())
        return QStringLiteral("802-1x.eap: property is missing");
    for (const QString &method : methods) {
        if (method == QLatin1String("tls")) {
            if (!eap.contains(QStringLiteral("client-cert")) || !eap.contains(QStringLiteral("private-key")))
                return QStringLiteral("802-1x.client-cert: property is missing");
            continue;
        }
        if (method == QLatin1String("sim") || method == QLatin1String("otp") || method == QLatin1String("external"))
            continue;
        if (eap.value(QStringLiteral("identity")).toString().isEmpty())
            return QStringLiteral("802-1x.identity: property is missing");
        if ((method == QLatin1String("peap") || method == QLatin1String("ttls"))
            && eap.contains(QStringLiteral("phase2-auth")) == eap.contains(QStringLiteral("phase2-autheap")))
            return QStringLiteral("802-1x.phase2-auth, 802-1x.phase2-autheap: exactly one property must be set");
    }
    return {};
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
            NMVariantMapMap partial = qdbus_cast<NMVariantMapMap>(args.value(0));
            const QString specificObject = args.value(2).value<QDBusObjectPath>().path();
            if (objects.value(specificObject).contains(FakeNM::accessPoint)) {
                const QString error = FakeNM::completeForAccessPoint(
                        partial, objects.value(specificObject).value(FakeNM::accessPoint));
                if (!error.isEmpty()) {
                    connection.send(message.createErrorReply(
                            QStringLiteral("org.freedesktop.NetworkManager.Settings.Connection.InvalidProperty"), error));
                    return true;
                }
            }
            const QString profile = addConnection(partial);
            const QString activeConnection =
                    activate(profile, args.value(1).value<QDBusObjectPath>().path(),
                             args.value(2).value<QDBusObjectPath>().path());
            if (dropActivations)
                remove(activeConnection);
            reply << FakeNM::path(profile) << FakeNM::path(activeConnection);
        } else if (member == QStringLiteral("ActivateConnection")) {
            reply << FakeNM::path(activate(args.value(0).value<QDBusObjectPath>().path(),
                                           args.value(1).value<QDBusObjectPath>().path(),
                                           args.value(2).value<QDBusObjectPath>().path()));
        } else if (member == QStringLiteral("DeactivateConnection") || member == QStringLiteral("Disconnect")
                   || member == QStringLiteral("RequestScan") || member == QStringLiteral("Enable")
                   || member == QStringLiteral("RegisterWithCapabilities") || member == QStringLiteral("Reapply")
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
    // Activations fail and vanish before their reply is sent.
    bool dropActivations = false;
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
