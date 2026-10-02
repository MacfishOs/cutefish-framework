#include "vpnutils.h"
#include "networkclient.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QObject>
#include <QRegularExpression>
#include <QUuid>
#include <QtEndian>

using namespace NetworkDBus;

namespace {

const QString servicePrefix = QStringLiteral("org.freedesktop.NetworkManager.");

struct Plugin {
    QString name;
    QStringList services;
};

struct PluginCache {
    QStringList directories {QStringLiteral("/etc/NetworkManager/VPN"),
                             QStringLiteral("/usr/lib/NetworkManager/VPN")};
    QList<QDateTime> stamps;
    QList<Plugin> plugins;
};

PluginCache &cache()
{
    static PluginCache cache;
    return cache;
}

// The [VPN Connection] group of a plugin's .name file.
Plugin readNameFile(const QString &path)
{
    Plugin plugin;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return plugin;

    bool inGroup = false;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.startsWith(QLatin1Char('['))) {
            inGroup = line == QLatin1String("[VPN Connection]");
            continue;
        }
        const int equals = line.indexOf(QLatin1Char('='));
        if (!inGroup || equals < 0)
            continue;

        const QString key = line.left(equals).trimmed();
        const QString value = line.mid(equals + 1).trimmed();
        if (key == QLatin1String("name"))
            plugin.name = value;
        else if (key == QLatin1String("service"))
            plugin.services.prepend(value);
        else if (key == QLatin1String("aliases"))
            plugin.services += value.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    }
    return plugin;
}

// Read again only when a directory changed: plugins come and go with packages.
const QList<Plugin> &plugins()
{
    PluginCache &cache = ::cache();
    QList<QDateTime> stamps;
    for (const QString &directory : std::as_const(cache.directories))
        stamps << QFileInfo(directory).lastModified();
    if (stamps == cache.stamps)
        return cache.plugins;

    cache.stamps = stamps;
    cache.plugins.clear();
    for (const QString &directory : std::as_const(cache.directories)) {
        const QStringList files = QDir(directory).entryList({QStringLiteral("*.name")}, QDir::Files, QDir::Name);
        for (const QString &file : files) {
            const Plugin plugin = readNameFile(QDir(directory).filePath(file));
            if (!plugin.name.isEmpty() && !plugin.services.isEmpty())
                cache.plugins << plugin;
        }
    }
    return cache.plugins;
}

QString shortName(const QString &serviceType)
{
    for (const Plugin &plugin : plugins()) {
        if (plugin.services.contains(serviceType))
            return plugin.name;
    }
    return serviceType.startsWith(servicePrefix) ? serviceType.mid(servicePrefix.size())
                                                 : serviceType.section(QLatin1Char('.'), -1);
}

} // namespace

namespace VpnUtils {

bool isVpn(const NetworkClient *client, const QString &connection)
{
    if (!client->readable(connection))
        return false;
    const QString type = client->setting(connection, QStringLiteral("connection"), QStringLiteral("type")).toString();
    if (type != QLatin1String("vpn") && type != QLatin1String("wireguard"))
        return false;
    // A port belongs to the profile it is a port of.
    return client->setting(connection, QStringLiteral("connection"), QStringLiteral("controller")).toString().isEmpty()
            && client->setting(connection, QStringLiteral("connection"), QStringLiteral("master")).toString().isEmpty();
}

QString kind(const NetworkClient *client, const QString &connection)
{
    if (client->setting(connection, QStringLiteral("connection"), QStringLiteral("type")).toString()
        == QLatin1String("wireguard"))
        return QStringLiteral("wireguard");
    return shortName(client->setting(connection, QStringLiteral("vpn"), QStringLiteral("service-type")).toString());
}

QString kindLabel(const QString &kind)
{
    if (kind == QLatin1String("wireguard"))
        return QStringLiteral("WireGuard");
    if (kind == QLatin1String("openvpn"))
        return QStringLiteral("OpenVPN");
    if (kind == QLatin1String("l2tp"))
        return QStringLiteral("L2TP");
    if (kind == QLatin1String("pptp"))
        return QStringLiteral("PPTP");
    if (kind == QLatin1String("sstp"))
        return QStringLiteral("SSTP");
    if (kind == QLatin1String("vpnc"))
        return QObject::tr("Cisco VPN");
    if (kind == QLatin1String("openconnect"))
        return QStringLiteral("OpenConnect");
    if (kind == QLatin1String("strongswan") || kind == QLatin1String("libreswan"))
        return QStringLiteral("IPsec");
    if (kind == QLatin1String("fortisslvpn"))
        return QStringLiteral("Fortinet SSL VPN");
    return kind.isEmpty() ? QStringLiteral("VPN") : kind;
}

QString package(const QString &kind)
{
    return QStringLiteral("network-manager-") + kind;
}

bool pluginInstalled(const QString &serviceType)
{
    for (const Plugin &plugin : plugins()) {
        if (plugin.services.contains(serviceType))
            return true;
    }
    return false;
}

QStringList installedPlugins()
{
    QStringList names;
    for (const Plugin &plugin : plugins()) {
        if (!names.contains(plugin.name))
            names << plugin.name;
    }
    return names;
}

void setPluginDirectories(const QStringList &directories)
{
    cache() = PluginCache();
    cache().directories = directories;
}

QString activeConnection(const NetworkClient *client, const QString &connection)
{
    for (const QString &active : client->activeConnections()) {
        if (NetworkClient::path(client->property(active, activeInterface, QStringLiteral("Connection"))) == connection)
            return active;
    }
    return {};
}

int state(const NetworkClient *client, const QString &active)
{
    if (active.isEmpty() || !client->has(active, activeInterface))
        return ActiveDeactivated;

    // NM_VPN_CONNECTION_STATE_*: the tunnel is up only once the plugin says so.
    const QString vpnInterface = QStringLiteral("org.freedesktop.NetworkManager.VPN.Connection");
    if (client->has(active, vpnInterface)) {
        const uint vpnState = client->property(active, vpnInterface, QStringLiteral("VpnState")).toUInt();
        if (vpnState >= 1 && vpnState <= 4)
            return ActiveActivating;
        if (vpnState == 5)
            return ActiveActivated;
        if (vpnState >= 6)
            return ActiveDeactivated;
    }
    return client->property(active, activeInterface, QStringLiteral("State")).toInt();
}

QString failureMessage(uint reason)
{
    // NM_ACTIVE_CONNECTION_STATE_REASON_*.
    switch (reason) {
    case 1:
    case 2:
    case 9:
    case 11:
        return {};
    case 3:
        return QObject::tr("The network connection was interrupted.");
    case 4:
        return QObject::tr("The VPN service stopped unexpectedly.");
    case 5:
        return QObject::tr("The VPN service returned invalid configuration.");
    case 6:
        return QObject::tr("The connection attempt timed out.");
    case 7:
        return QObject::tr("The VPN service did not start in time.");
    case 8:
        return QObject::tr("The VPN service failed to start.");
    case 10:
        return QObject::tr("Sign-in failed. Check the user name and password.");
    default:
        return QObject::tr("Couldn't connect.");
    }
}

QList<SecretField> secretFields(const QString &serviceType, const QMap<QString, QString> &data,
                                const QStringList &hints)
{
    const auto flags = [&data](const QString &key) {
        return data.value(key + QLatin1String("-flags")).toUInt();
    };
    const auto required = [&flags](const QString &key) {
        return !(flags(key) & SecretNotRequired);
    };

    const QString password = QObject::tr("Password");
    QList<SecretField> fields;

    // Hints name exactly what NetworkManager is missing, a one-time code for instance.
    const QString challenge = QStringLiteral("x-dynamic-challenge:");
    const QString echoChallenge = QStringLiteral("x-dynamic-challenge-echo:");
    const QString message = hintMessage(hints);
    for (const QString &hint : hints) {
        if (hint.startsWith(QLatin1String("x-vpn-message:")))
            continue;
        SecretField field {hint, message.isEmpty() ? password : message};
        if (hint.startsWith(echoChallenge)) {
            field.secret = false;
            field.label = hint.mid(echoChallenge.size());
        } else if (hint.startsWith(challenge)) {
            field.label = hint.mid(challenge.size());
        } else if (hint == QLatin1String("cert-pass")) {
            field.label = QObject::tr("Private key password");
        } else if (hint == QLatin1String("http-proxy-password")) {
            field.label = QObject::tr("Proxy password");
        }
        fields << field;
    }
    if (!fields.isEmpty())
        return fields;

    const QString kind = shortName(serviceType);
    if (kind == QLatin1String("openvpn")) {
        const QString type = data.value(QStringLiteral("connection-type"));
        if ((type == QLatin1String("password") || type == QLatin1String("password-tls"))
            && required(QStringLiteral("password")))
            fields << SecretField {QStringLiteral("password"), password};
        if ((type == QLatin1String("tls") || type == QLatin1String("password-tls"))
            && data.contains(QStringLiteral("key")) && required(QStringLiteral("cert-pass")))
            fields << SecretField {QStringLiteral("cert-pass"), QObject::tr("Private key password")};
        if (data.contains(QStringLiteral("proxy-server")) && required(QStringLiteral("http-proxy-password")))
            fields << SecretField {QStringLiteral("http-proxy-password"), QObject::tr("Proxy password")};
    } else if (kind == QLatin1String("l2tp")) {
        const QString user = data.value(QStringLiteral("user-auth-type"));
        const QString machine = data.value(QStringLiteral("machine-auth-type"));
        if ((user.isEmpty() || user == QLatin1String("password")) && required(QStringLiteral("password")))
            fields << SecretField {QStringLiteral("password"), password};
        else if (user == QLatin1String("tls") && required(QStringLiteral("user-certpass")))
            fields << SecretField {QStringLiteral("user-certpass"), QObject::tr("Certificate password")};
        if (machine == QLatin1String("psk") && required(QStringLiteral("ipsec-psk")))
            fields << SecretField {QStringLiteral("ipsec-psk"), QObject::tr("Pre-shared key")};
        else if (machine == QLatin1String("tls") && required(QStringLiteral("machine-certpass")))
            fields << SecretField {QStringLiteral("machine-certpass"), QObject::tr("Machine certificate password")};
    } else if (kind == QLatin1String("vpnc")) {
        if (required(QStringLiteral("Xauth password")))
            fields << SecretField {QStringLiteral("Xauth password"), password};
        if (required(QStringLiteral("IPSec secret")))
            fields << SecretField {QStringLiteral("IPSec secret"), QObject::tr("Group password")};
    } else if (kind == QLatin1String("strongswan")) {
        const QString method = data.value(QStringLiteral("method"));
        if (method == QLatin1String("agent") || data.value(QStringLiteral("secret_type")) == QLatin1String("unused"))
            return fields;
        if (method == QLatin1String("key"))
            fields << SecretField {QStringLiteral("password"), QObject::tr("Private key password")};
        else if (method == QLatin1String("smartcard"))
            fields << SecretField {QStringLiteral("password"), QObject::tr("PIN")};
        else
            fields << SecretField {QStringLiteral("password"), password};
    } else if (kind == QLatin1String("libreswan")) {
        if (data.value(QStringLiteral("xauthpasswordinputmodes")) != QLatin1String("unused"))
            fields << SecretField {QStringLiteral("xauthpassword"), password};
        if (data.value(QStringLiteral("pskinputmodes")) != QLatin1String("unused"))
            fields << SecretField {QStringLiteral("pskvalue"), QObject::tr("Group password")};
    } else if (kind == QLatin1String("fortisslvpn")) {
        if (required(QStringLiteral("password")))
            fields << SecretField {QStringLiteral("password"), password};
        if (flags(QStringLiteral("otp")) == SecretNotSaved)
            fields << SecretField {QStringLiteral("otp"), QObject::tr("One-time password"), false};
    } else if (kind == QLatin1String("openconnect")) {
        // Its sign-in is a conversation with the server, which no password prompt covers.
    } else if (required(QStringLiteral("password"))) {
        fields << SecretField {QStringLiteral("password"), password};
    }
    return fields;
}

QString userNameKey(const QString &serviceType, const QMap<QString, QString> &data)
{
    const QString kind = shortName(serviceType);
    if (kind == QLatin1String("openvpn")) {
        const QString type = data.value(QStringLiteral("connection-type"));
        return type == QLatin1String("password") || type == QLatin1String("password-tls") ? QStringLiteral("username")
                                                                                          : QString();
    }
    if (kind == QLatin1String("l2tp")) {
        const QString type = data.value(QStringLiteral("user-auth-type"));
        return type.isEmpty() || type == QLatin1String("password") ? QStringLiteral("user") : QString();
    }
    if (kind == QLatin1String("pptp") || kind == QLatin1String("sstp") || kind == QLatin1String("fortisslvpn"))
        return QStringLiteral("user");
    if (kind == QLatin1String("vpnc"))
        return QStringLiteral("Xauth username");
    if (kind == QLatin1String("strongswan") && data.value(QStringLiteral("method")) == QLatin1String("eap"))
        return QStringLiteral("user");
    return {};
}

QString hintMessage(const QStringList &hints)
{
    const QString tag = QStringLiteral("x-vpn-message:");
    for (const QString &hint : hints) {
        if (hint.startsWith(tag))
            return hint.mid(tag.size()).trimmed();
    }
    return {};
}

bool isWireGuardConfig(const QByteArray &content)
{
    return content.contains("[Interface]") && content.contains("PrivateKey");
}

NMVariantMapMap wireGuardProfile(const QByteArray &content, const QString &name, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return NMVariantMapMap();
    };
    const auto key = [](const QString &value) {
        return QByteArray::fromBase64(value.toLatin1(), QByteArray::AbortOnBase64DecodingErrors).size() == 32;
    };

    enum { None, Interface, Peer } section = None;
    QString privateKey;
    uint listenPort = 0;
    uint fwmark = 0;
    uint mtu = 0;
    bool peerRoutes = true;
    QList<QVariantMap> addresses[2];
    QList<uint> dns4;
    QList<QByteArray> dns6;
    QStringList search;
    QList<QVariantMap> peers;

    // Like `wg setconf`: whitespace anywhere is dropped and '#' starts a comment.
    const QStringList lines = QString::fromUtf8(content).split(QLatin1Char('\n'));
    for (int number = 1; number <= lines.size(); ++number) {
        QString line = lines.at(number - 1).section(QLatin1Char('#'), 0, 0);
        line.remove(QRegularExpression(QStringLiteral("\\s")));
        if (line.isEmpty())
            continue;

        if (line.compare(QLatin1String("[Interface]"), Qt::CaseInsensitive) == 0) {
            section = Interface;
            continue;
        }
        if (line.compare(QLatin1String("[Peer]"), Qt::CaseInsensitive) == 0) {
            section = Peer;
            peers.append(QVariantMap());
            continue;
        }

        const int equals = line.indexOf(QLatin1Char('='));
        const QString field = line.left(equals);
        const QString value = equals < 0 ? QString() : line.mid(equals + 1);
        const QStringList words = value.split(QLatin1Char(','), Qt::SkipEmptyParts);
        const QString invalid = QObject::tr("Line %1 has an invalid %2.").arg(number).arg(field);
        bool ok = true;

        if (section == Interface && field.compare(QLatin1String("PrivateKey"), Qt::CaseInsensitive) == 0) {
            if (!key(value))
                return fail(invalid);
            privateKey = value;
        } else if (section == Interface && field.compare(QLatin1String("Address"), Qt::CaseInsensitive) == 0) {
            for (const QString &word : words) {
                const QHostAddress address(word.section(QLatin1Char('/'), 0, 0));
                if (address.isNull())
                    return fail(invalid);
                const bool v4 = address.protocol() == QAbstractSocket::IPv4Protocol;
                int prefix = v4 ? 32 : 128;
                if (word.contains(QLatin1Char('/'))) {
                    bool valid = false;
                    prefix = word.section(QLatin1Char('/'), 1).toInt(&valid);
                    if (!valid || prefix < 0 || prefix > (v4 ? 32 : 128))
                        return fail(invalid);
                }
                addresses[v4 ? 0 : 1] << QVariantMap {{QStringLiteral("address"), address.toString()},
                                                       {QStringLiteral("prefix"), uint(prefix)}};
            }
        } else if (section == Interface && field.compare(QLatin1String("DNS"), Qt::CaseInsensitive) == 0) {
            for (const QString &word : words) {
                const QHostAddress address(word);
                if (address.isNull())
                    search << word;
                else if (address.protocol() == QAbstractSocket::IPv4Protocol)
                    dns4 << qToBigEndian(address.toIPv4Address());
                else
                    dns6 << QByteArray(reinterpret_cast<const char *>(address.toIPv6Address().c), 16);
            }
        } else if (section == Interface && field.compare(QLatin1String("MTU"), Qt::CaseInsensitive) == 0) {
            mtu = value.toUInt(&ok);
        } else if (section == Interface && field.compare(QLatin1String("ListenPort"), Qt::CaseInsensitive) == 0) {
            listenPort = value.toUInt(&ok);
            ok &= listenPort <= 0xffff;
        } else if (section == Interface && field.compare(QLatin1String("FwMark"), Qt::CaseInsensitive) == 0) {
            fwmark = value == QLatin1String("off") ? 0 : value.toUInt(&ok, 0);
        } else if (section == Interface && field.compare(QLatin1String("Table"), Qt::CaseInsensitive) == 0) {
            // Route tables by number or name aren't carried over; "off" means no routes.
            peerRoutes = value != QLatin1String("off");
        } else if (section == Interface
                   && QStringList {QStringLiteral("PreUp"), QStringLiteral("PreDown"), QStringLiteral("PostUp"),
                                   QStringLiteral("PostDown"), QStringLiteral("SaveConfig")}
                              .contains(field, Qt::CaseInsensitive)) {
            // No scripts are run.
        } else if (section == Peer && field.compare(QLatin1String("PublicKey"), Qt::CaseInsensitive) == 0) {
            ok = key(value);
            peers.last().insert(QStringLiteral("public-key"), value);
        } else if (section == Peer && field.compare(QLatin1String("PresharedKey"), Qt::CaseInsensitive) == 0) {
            ok = key(value);
            peers.last().insert(QStringLiteral("preshared-key"), value);
            peers.last().insert(QStringLiteral("preshared-key-flags"), uint(NetworkDBus::SecretSystemOwned));
        } else if (section == Peer && field.compare(QLatin1String("Endpoint"), Qt::CaseInsensitive) == 0) {
            ok = value.contains(QLatin1Char(':'));
            peers.last().insert(QStringLiteral("endpoint"), value);
        } else if (section == Peer && field.compare(QLatin1String("AllowedIPs"), Qt::CaseInsensitive) == 0) {
            QStringList allowed = peers.last().value(QStringLiteral("allowed-ips")).toStringList();
            allowed += words;
            peers.last().insert(QStringLiteral("allowed-ips"), allowed);
        } else if (section == Peer && field.compare(QLatin1String("PersistentKeepalive"), Qt::CaseInsensitive) == 0) {
            const uint seconds = value == QLatin1String("off") ? 0 : value.toUInt(&ok);
            ok &= seconds <= 0xffff;
            peers.last().insert(QStringLiteral("persistent-keepalive"), seconds);
        } else {
            return fail(QObject::tr("Line %1 isn't part of a WireGuard configuration.").arg(number));
        }
        if (!ok)
            return fail(invalid);
    }

    if (privateKey.isEmpty())
        return fail(QObject::tr("The configuration has no private key."));
    for (const QVariantMap &peer : std::as_const(peers)) {
        if (!peer.contains(QStringLiteral("public-key")))
            return fail(QObject::tr("A peer has no public key."));
    }

    // Interface names are at most 15 characters of a few kinds.
    QString interface = name;
    interface.remove(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")));
    interface = interface.left(15);
    if (interface.isEmpty())
        interface = QStringLiteral("wg0");

    NMVariantMapMap profile;
    profile[QStringLiteral("connection")] = {
        {QStringLiteral("id"), name},
        {QStringLiteral("uuid"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {QStringLiteral("type"), QStringLiteral("wireguard")},
        {QStringLiteral("interface-name"), interface},
    };

    QVariantMap wireguard {
        {QStringLiteral("private-key"), privateKey},
        {QStringLiteral("private-key-flags"), uint(NetworkDBus::SecretSystemOwned)},
        {QStringLiteral("listen-port"), listenPort},
        {QStringLiteral("fwmark"), fwmark},
        {QStringLiteral("mtu"), mtu},
        {QStringLiteral("peer-routes"), peerRoutes},
        {QStringLiteral("peers"), QVariant::fromValue(peers)},
    };
    profile[QStringLiteral("wireguard")] = wireguard;

    for (int family = 0; family < 2; ++family) {
        QVariantMap ip;
        ip.insert(QStringLiteral("method"), addresses[family].isEmpty() ? QStringLiteral("disabled") : QStringLiteral("manual"));
        if (!addresses[family].isEmpty()) {
            ip.insert(QStringLiteral("address-data"), QVariant::fromValue(addresses[family]));
            // DNS needs an address of its family; without search domains, the server
            // answers for every name, as wg-quick has it.
            const bool hasDns = family == 0 ? !dns4.isEmpty() : !dns6.isEmpty();
            if (hasDns) {
                ip.insert(QStringLiteral("dns"), family == 0 ? QVariant::fromValue(dns4) : QVariant::fromValue(dns6));
                ip.insert(QStringLiteral("dns-search"), search.isEmpty() ? QStringList {QStringLiteral("~")} : search);
            }
        }
        profile[family == 0 ? QStringLiteral("ipv4") : QStringLiteral("ipv6")] = ip;
    }
    return profile;
}

QVariantList manualKinds()
{
    QVariantList kinds;
    const auto add = [&kinds](const QString &kind, const QString &label, bool installed) {
        kinds << QVariantMap {{QStringLiteral("kind"), kind},
                              {QStringLiteral("label"), label},
                              {QStringLiteral("installed"), installed},
                              {QStringLiteral("package"), package(kind)}};
    };
    add(QStringLiteral("wireguard"), QStringLiteral("WireGuard"), true);
    add(QStringLiteral("openvpn"), QStringLiteral("OpenVPN"), pluginInstalled(servicePrefix + QStringLiteral("openvpn")));
    add(QStringLiteral("l2tp"), QStringLiteral("L2TP/IPsec"), pluginInstalled(servicePrefix + QStringLiteral("l2tp")));
    add(QStringLiteral("pptp"), QStringLiteral("PPTP"), pluginInstalled(servicePrefix + QStringLiteral("pptp")));
    return kinds;
}

NMVariantMapMap manualProfile(const QVariantMap &form, QString *error)
{
    const auto fail = [error](const QString &message) {
        if (error)
            *error = message;
        return NMVariantMapMap();
    };
    const auto field = [&form](const char *key) {
        return form.value(QLatin1String(key)).toString().trimmed();
    };

    const QString kind = field("kind");
    const QString server = field("server");
    const QString name = field("name").isEmpty() ? server : field("name");
    if (server.isEmpty())
        return fail(QObject::tr("Enter the server."));

    if (kind == QLatin1String("wireguard")) {
        // Written out as a wg-quick file, so the form is checked the way imports are.
        QString endpoint = server;
        // NetworkManager takes a bare IPv6 address's last group for the port, so it needs brackets.
        if (QHostAddress(server).protocol() == QAbstractSocket::IPv6Protocol
            && QHostAddress(server.section(QLatin1Char(':'), 0, -2)).isNull())
            endpoint = QStringLiteral("[%1]").arg(server);
        if (!endpoint.contains(QLatin1Char(':')) || endpoint.endsWith(QLatin1Char(']')))
            endpoint += QStringLiteral(":51820");
        QString config = QStringLiteral("[Interface]\nPrivateKey = %1\nAddress = %2\n")
                                 .arg(field("privateKey"), field("address"));
        if (!field("dns").isEmpty())
            config += QStringLiteral("DNS = %1\n").arg(field("dns"));
        config += QStringLiteral("[Peer]\nPublicKey = %1\nEndpoint = %2\nAllowedIPs = %3\n")
                          .arg(field("publicKey"), endpoint,
                               field("allowedIps").isEmpty() ? QStringLiteral("0.0.0.0/0, ::/0") : field("allowedIps"));
        if (field("privateKey").isEmpty() || field("publicKey").isEmpty() || field("address").isEmpty())
            return fail(QObject::tr("Enter the private key, the address and the server's public key."));
        return wireGuardProfile(config.toUtf8(), name, error);
    }

    QMap<QString, QString> data;
    QMap<QString, QString> secrets;
    const QString agentOwned = QString::number(NetworkDBus::SecretAgentOwned);
    const auto secret = [&](const QString &key, const QString &value) {
        data.insert(key + QLatin1String("-flags"), agentOwned);
        if (!value.isEmpty())
            secrets.insert(key, value);
    };

    if (kind == QLatin1String("openvpn")) {
        data.insert(QStringLiteral("remote"), server);
        if (!field("caFile").isEmpty())
            data.insert(QStringLiteral("ca"), field("caFile"));
        if (field("authentication") == QLatin1String("certificates")) {
            if (field("certFile").isEmpty() || field("keyFile").isEmpty())
                return fail(QObject::tr("Choose your certificate and private key."));
            data.insert(QStringLiteral("connection-type"), QStringLiteral("tls"));
            data.insert(QStringLiteral("cert"), field("certFile"));
            data.insert(QStringLiteral("key"), field("keyFile"));
            secret(QStringLiteral("cert-pass"), form.value(QStringLiteral("keyPassword")).toString());
        } else {
            if (field("userName").isEmpty())
                return fail(QObject::tr("Enter your user name."));
            data.insert(QStringLiteral("connection-type"), QStringLiteral("password"));
            data.insert(QStringLiteral("username"), field("userName"));
            secret(QStringLiteral("password"), form.value(QStringLiteral("password")).toString());
        }
    } else if (kind == QLatin1String("l2tp") || kind == QLatin1String("pptp")) {
        if (field("userName").isEmpty())
            return fail(QObject::tr("Enter your user name."));
        data.insert(QStringLiteral("gateway"), server);
        data.insert(QStringLiteral("user"), field("userName"));
        secret(QStringLiteral("password"), form.value(QStringLiteral("password")).toString());
        // With a pre-shared key, L2TP runs inside IPsec.
        if (kind == QLatin1String("l2tp") && !field("presharedKey").isEmpty()) {
            data.insert(QStringLiteral("ipsec-enabled"), QStringLiteral("yes"));
            data.insert(QStringLiteral("machine-auth-type"), QStringLiteral("psk"));
            secret(QStringLiteral("ipsec-psk"), form.value(QStringLiteral("presharedKey")).toString());
        }
    } else {
        return fail(QObject::tr("This kind of VPN can't be set up by hand."));
    }

    NMVariantMapMap profile;
    profile[QStringLiteral("connection")] = {
        {QStringLiteral("id"), name},
        {QStringLiteral("uuid"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {QStringLiteral("type"), QStringLiteral("vpn")},
    };
    QVariantMap vpn {
        {QStringLiteral("service-type"), servicePrefix + kind},
        {QStringLiteral("data"), QVariant::fromValue(data)},
    };
    if (!secrets.isEmpty())
        vpn.insert(QStringLiteral("secrets"), QVariant::fromValue(secrets));
    profile[QStringLiteral("vpn")] = vpn;
    profile[QStringLiteral("ipv4")] = {{QStringLiteral("method"), QStringLiteral("auto")}};
    profile[QStringLiteral("ipv6")] = {{QStringLiteral("method"), QStringLiteral("auto")}};
    return profile;
}

} // namespace VpnUtils
