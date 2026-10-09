// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "iputils.h"
#include "networkclient.h"
#include "vpnutils.h"

#include <QHostAddress>
#include <QObject>
#include <QRegularExpression>
#include <QtAlgorithms>
#include <QtEndian>

#include <cstring>

using namespace NetworkDBus;

static const QString ipv4Setting = QStringLiteral("ipv4");
static const QString ipv6Setting = QStringLiteral("ipv6");


static QString text(const QVariantMap &form, const QString &key)
{
    return form.value(key).toString().trimmed();
}

static QString methodOf(const NMVariantMapMap &settings, const QString &family)
{
    // A profile without the setting gets "auto" from NetworkManager's normalization.
    const QString method = settings.value(family).value(QStringLiteral("method")).toString();
    return method.isEmpty() ? QStringLiteral("auto") : method;
}

static QString methodLabel(const QString &method, bool v4)
{
    if (method == QLatin1String("auto"))
        return v4 ? QObject::tr("Automatic (DHCP)") : QObject::tr("Automatic");
    if (method == QLatin1String("manual"))
        return QObject::tr("Manual");
    if (method == QLatin1String("disabled"))
        return QObject::tr("Off");
    if (method == QLatin1String("dhcp"))
        return QObject::tr("DHCP Only");
    if (method == QLatin1String("link-local"))
        return QObject::tr("Link-Local Only");
    if (method == QLatin1String("shared"))
        return QObject::tr("Shared to Other Computers");
    if (method == QLatin1String("ignore"))
        return QObject::tr("Left to the System");
    return method;
}

static QVariantList methods(const QString &current, bool v4)
{
    QStringList values {QStringLiteral("auto"), QStringLiteral("manual"), QStringLiteral("disabled")};
    if (!values.contains(current))
        values << current;
    QVariantList list;
    for (const QString &value : values)
        list << QVariantMap {{QStringLiteral("value"), value}, {QStringLiteral("label"), methodLabel(value, v4)}};
    return list;
}

// Whether the family takes addresses, gateways and name servers in NetworkManager's
// verify(); "shared" takes addresses but its name servers are the shared ones.
static bool configurable(const QString &method)
{
    return method == QLatin1String("auto") || method == QLatin1String("manual") || method == QLatin1String("dhcp");
}

// Four decimal parts only: QHostAddress also takes "10.1" and other inet_aton() forms.
static bool parseIpv4(const QString &value, quint32 *address = nullptr)
{
    static const QRegularExpression dotted(QStringLiteral("^(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})\\.(\\d{1,3})$"));
    const QRegularExpressionMatch match = dotted.match(value);
    if (!match.hasMatch())
        return false;
    quint32 result = 0;
    for (int part = 1; part <= 4; ++part) {
        const uint byte = match.captured(part).toUInt();
        if (byte > 255)
            return false;
        result = (result << 8) | byte;
    }
    if (address)
        *address = result;
    return true;
}

static bool parseIpv6(const QString &value)
{
    // A scope such as "%eth0" would be a link-local address on one interface.
    if (value.contains(QLatin1Char('%')))
        return false;
    const QHostAddress address(value);
    return address.protocol() == QAbstractSocket::IPv6Protocol && !address.isNull();
}

static bool hostAddress(const QString &value, bool v4)
{
    if (v4) {
        quint32 address = 0;
        return parseIpv4(value, &address) && address != 0 && address != 0xffffffff;
    }
    return parseIpv6(value) && QHostAddress(value) != QHostAddress(QHostAddress::AnyIPv6);
}

// "255.255.255.0", or a prefix length, with or without its slash.
static int parsePrefix(const QString &value, bool v4)
{
    QString prefix = value;
    if (prefix.startsWith(QLatin1Char('/')))
        prefix.remove(0, 1);
    bool number = false;
    const int length = prefix.toInt(&number);
    if (number)
        return length >= 1 && length <= (v4 ? 32 : 128) ? length : -1;

    quint32 mask = 0;
    if (!v4 || !parseIpv4(value, &mask) || mask == 0)
        return -1;
    // The ones must come first: 255.0.255.0 is no netmask.
    const quint32 hostBits = ~mask;
    if (hostBits & (hostBits + 1))
        return -1;
    return qPopulationCount(mask);
}

static QString netmask(uint prefix)
{
    if (prefix == 0 || prefix > 32)
        return {};
    return QHostAddress(quint32(0xffffffffu << (32 - prefix))).toString();
}

static QStringList dnsWords(const QString &value)
{
    static const QRegularExpression separators(QStringLiteral("[,\\s]+"));
    return value.split(separators, Qt::SkipEmptyParts);
}

// A name server with the server name DNS over TLS checks, as dns-data writes it.
static QString dnsAddress(const QString &word)
{
    return word.section(QLatin1Char('#'), 0, 0);
}

static QStringList dnsServers(const NMVariantMapMap &settings)
{
    QStringList servers;
    for (const QString &family : {ipv4Setting, ipv6Setting}) {
        const QVariantMap ip = settings.value(family);
        // NetworkManager 1.56 sends both; its daemon takes "dns" when a client sends both.
        if (ip.contains(QStringLiteral("dns-data"))) {
            servers += ip.value(QStringLiteral("dns-data")).toStringList();
        } else if (family == ipv4Setting) {
            // Network byte order.
            for (uint server : listOf<uint>(ip.value(QStringLiteral("dns"))))
                servers << QHostAddress(qFromBigEndian(server)).toString();
        } else {
            for (const QByteArray &server : listOf<QByteArray>(ip.value(QStringLiteral("dns")))) {
                if (server.size() != 16)
                    continue;
                Q_IPV6ADDR raw;
                std::memcpy(raw.c, server.constData(), 16);
                servers << QHostAddress(raw).toString();
            }
        }
    }
    return servers;
}

namespace IpUtils {

QVariantMap form(const NMVariantMapMap &settings)
{
    QVariantMap form;
    for (const bool v4 : {true, false}) {
        const QString family = v4 ? ipv4Setting : ipv6Setting;
        const QString prefix = v4 ? QStringLiteral("ipv4") : QStringLiteral("ipv6");
        const QVariantMap ip = settings.value(family);
        const QString method = methodOf(settings, family);
        const auto addresses = listOf<QVariantMap>(ip.value(QStringLiteral("address-data")));
        const QVariantMap first = addresses.value(0);
        const uint length = first.value(QStringLiteral("prefix")).toUInt();

        form.insert(prefix + QStringLiteral("Method"), method);
        form.insert(prefix + QStringLiteral("Methods"), methods(method, v4));
        form.insert(prefix + QStringLiteral("Address"), first.value(QStringLiteral("address")).toString());
        if (v4)
            form.insert(QStringLiteral("ipv4Netmask"), netmask(length));
        else
            form.insert(QStringLiteral("ipv6Prefix"), length ? QString::number(length) : QString());
        form.insert(prefix + QStringLiteral("Gateway"), ip.value(QStringLiteral("gateway")).toString());
    }

    // The switch stands for both families; IPv4's decides when they differ.
    form.insert(QStringLiteral("automaticDns"), !settings.value(ipv4Setting).value(QStringLiteral("ignore-auto-dns")).toBool());
    form.insert(QStringLiteral("dns"), dnsServers(settings).join(QStringLiteral(", ")));
    return form;
}

QVariantMap currentAddresses(const NetworkClient *client, const QString &connection)
{
    // Read off the device, as the details rows are.
    const QString active = VpnUtils::activeConnection(client, connection);
    const QString device = client->property(active, activeInterface, QStringLiteral("Devices")).toStringList().value(0);
    if (device.isEmpty())
        return {};

    QVariantMap current;
    const auto first = [](const QVariant &addresses) {
        return listOf<QVariantMap>(addresses).value(0);
    };

    const QString ip4 = NetworkClient::path(client->property(device, deviceInterface, QStringLiteral("Ip4Config")));
    const QVariantMap address4 = first(client->property(ip4, ip4ConfigInterface, QStringLiteral("AddressData")));
    if (!address4.isEmpty()) {
        current.insert(QStringLiteral("ipv4Address"), address4.value(QStringLiteral("address")).toString());
        current.insert(QStringLiteral("ipv4Netmask"), netmask(address4.value(QStringLiteral("prefix")).toUInt()));
        current.insert(QStringLiteral("ipv4Gateway"), client->property(ip4, ip4ConfigInterface, QStringLiteral("Gateway")).toString());
    }

    // The global address, not the link-local one every interface has.
    const QString ip6 = NetworkClient::path(client->property(device, deviceInterface, QStringLiteral("Ip6Config")));
    const auto addresses6 = listOf<QVariantMap>(client->property(ip6, ip6ConfigInterface, QStringLiteral("AddressData")));
    for (const QVariantMap &address : addresses6) {
        const QHostAddress host(address.value(QStringLiteral("address")).toString());
        if (host.isLinkLocal())
            continue;
        current.insert(QStringLiteral("ipv6Address"), host.toString());
        current.insert(QStringLiteral("ipv6Prefix"), QString::number(address.value(QStringLiteral("prefix")).toUInt()));
        current.insert(QStringLiteral("ipv6Gateway"), client->property(ip6, ip6ConfigInterface, QStringLiteral("Gateway")).toString());
        break;
    }
    return current;
}

QVariantMap check(const QVariantMap &form)
{
    bool complete = true;
    QString message;
    const auto fail = [&message](const QString &text) {
        if (message.isEmpty())
            message = text;
    };

    for (const bool v4 : {true, false}) {
        const QString prefix = v4 ? QStringLiteral("ipv4") : QStringLiteral("ipv6");
        if (text(form, prefix + QStringLiteral("Method")) != QLatin1String("manual"))
            continue;

        const QString address = text(form, prefix + QStringLiteral("Address"));
        const QString length = text(form, v4 ? QStringLiteral("ipv4Netmask") : QStringLiteral("ipv6Prefix"));
        const QString gateway = text(form, prefix + QStringLiteral("Gateway"));
        if (address.isEmpty())
            complete = false;
        else if (!hostAddress(address, v4))
            fail(v4 ? QObject::tr("%1 isn't an IPv4 address.").arg(address)
                    : QObject::tr("%1 isn't an IPv6 address.").arg(address));
        // IPv6 has a usual prefix length; an IPv4 network has none to assume.
        if (length.isEmpty() && v4)
            complete = false;
        else if (!length.isEmpty() && parsePrefix(length, v4) < 0)
            fail(v4 ? QObject::tr("%1 isn't a subnet mask.").arg(length)
                    : QObject::tr("The prefix length must be a number from 1 to 128."));
        if (!gateway.isEmpty() && !hostAddress(gateway, v4))
            fail(QObject::tr("%1 isn't a router address.").arg(gateway));
    }

    const bool ipv4Dns = configurable(text(form, QStringLiteral("ipv4Method")));
    const bool ipv6Dns = configurable(text(form, QStringLiteral("ipv6Method")));
    for (const QString &word : dnsWords(form.value(QStringLiteral("dns")).toString())) {
        const QString address = dnsAddress(word);
        if (hostAddress(address, true)) {
            if (!ipv4Dns)
                fail(QObject::tr("IPv4 name servers need IPv4 set to automatic or manual."));
        } else if (hostAddress(address, false)) {
            if (!ipv6Dns)
                fail(QObject::tr("IPv6 name servers need IPv6 set to automatic or manual."));
        } else {
            fail(QObject::tr("%1 isn't a name server address.").arg(word));
        }
    }

    return {{QStringLiteral("complete"), complete && message.isEmpty()}, {QStringLiteral("message"), message}};
}

NMVariantMapMap settings(const QVariantMap &form, const NMVariantMapMap &existing, bool *changed, QString *error)
{
    const QVariantMap result = check(form);
    if (!result.value(QStringLiteral("complete")).toBool()) {
        if (error) {
            const QString message = result.value(QStringLiteral("message")).toString();
            *error = message.isEmpty() ? QObject::tr("Fill in the address and subnet mask.") : message;
        }
        return {};
    }

    // Compared as the form shows them; the address fields count only for a manual setup,
    // as they are hidden otherwise.
    const QVariantMap stored = IpUtils::form(existing);
    const auto differs = [&form, &stored](const QString &prefix, const QString &lengthKey) {
        const QString method = text(form, prefix + QStringLiteral("Method"));
        if (method != text(stored, prefix + QStringLiteral("Method")))
            return true;
        if (method != QLatin1String("manual"))
            return false;
        for (const QString &key : {prefix + QStringLiteral("Address"), lengthKey, prefix + QStringLiteral("Gateway")}) {
            if (text(form, key) != text(stored, key))
                return true;
        }
        return false;
    };
    const bool ipv4Changed = differs(QStringLiteral("ipv4"), QStringLiteral("ipv4Netmask"));
    const bool ipv6Changed = differs(QStringLiteral("ipv6"), QStringLiteral("ipv6Prefix"));
    // The servers as a list, so "1.1.1.1,8.8.8.8" is the same as the stored "1.1.1.1, 8.8.8.8".
    const bool dnsChanged = form.value(QStringLiteral("automaticDns")).toBool() != stored.value(QStringLiteral("automaticDns")).toBool()
            || dnsWords(form.value(QStringLiteral("dns")).toString()) != dnsWords(stored.value(QStringLiteral("dns")).toString());
    if (changed)
        *changed = ipv4Changed || ipv6Changed || dnsChanged;

    NMVariantMapMap profile = existing;
    for (const bool v4 : {true, false}) {
        const QString family = v4 ? ipv4Setting : ipv6Setting;
        const QString prefix = v4 ? QStringLiteral("ipv4") : QStringLiteral("ipv6");
        if (!(v4 ? ipv4Changed : ipv6Changed))
            continue;

        QVariantMap ip = profile.value(family);
        const QString method = text(form, prefix + QStringLiteral("Method"));
        ip.insert(QStringLiteral("method"), method);
        // NetworkManager's daemon prefers the deprecated "addresses" to "address-data" when
        // both are sent, so it goes.
        ip.remove(QStringLiteral("addresses"));
        ip.remove(QStringLiteral("gateway"));

        if (method == QLatin1String("manual")) {
            const QString length = text(form, v4 ? QStringLiteral("ipv4Netmask") : QStringLiteral("ipv6Prefix"));
            QList<QVariantMap> addresses = listOf<QVariantMap>(ip.value(QStringLiteral("address-data")));
            const QVariantMap address {
                {QStringLiteral("address"), QHostAddress(text(form, prefix + QStringLiteral("Address"))).toString()},
                {QStringLiteral("prefix"), uint(length.isEmpty() ? 64 : parsePrefix(length, v4))},
            };
            // Only the first address is in the form; any further ones stay.
            if (addresses.isEmpty())
                addresses << address;
            else
                addresses[0] = address;
            ip.insert(QStringLiteral("address-data"), QVariant::fromValue(addresses));
            const QString gateway = text(form, prefix + QStringLiteral("Gateway"));
            if (!gateway.isEmpty())
                ip.insert(QStringLiteral("gateway"), QHostAddress(gateway).toString());
        } else if (method != QLatin1String("shared")) {
            // As in GNOME's editor: addresses of their own only for a manual setup.
            ip.remove(QStringLiteral("address-data"));
        }

        if (!configurable(method)) {
            for (const QString &key : {QStringLiteral("dns"), QStringLiteral("dns-data"), QStringLiteral("dns-search")})
                ip.remove(key);
        }
        profile[family] = ip;
    }

    if (dnsChanged) {
        QList<uint> dns4;
        QList<QByteArray> dns6;
        QStringList data4;
        QStringList data6;
        for (const QString &word : dnsWords(form.value(QStringLiteral("dns")).toString())) {
            const QHostAddress address(dnsAddress(word));
            if (address.protocol() == QAbstractSocket::IPv4Protocol) {
                dns4 << qToBigEndian(address.toIPv4Address());
                data4 << word;
            } else {
                dns6 << QByteArray(reinterpret_cast<const char *>(address.toIPv6Address().c), 16);
                data6 << word;
            }
        }

        for (const bool v4 : {true, false}) {
            const QString family = v4 ? ipv4Setting : ipv6Setting;
            QVariantMap ip = profile.value(family);
            // dns-data only goes to a NetworkManager that sent it: an older one rejects
            // a key it doesn't know.
            const bool data = ip.contains(QStringLiteral("dns-data"));
            ip.remove(QStringLiteral("dns"));
            ip.remove(QStringLiteral("dns-data"));
            if (data && !(v4 ? data4 : data6).isEmpty())
                ip.insert(QStringLiteral("dns-data"), v4 ? data4 : data6);
            else if (v4 && !dns4.isEmpty())
                ip.insert(QStringLiteral("dns"), QVariant::fromValue(dns4));
            else if (!v4 && !dns6.isEmpty())
                ip.insert(QStringLiteral("dns"), QVariant::fromValue(dns6));
            // Off, only the servers above are used; on, they come before the network's own.
            if (configurable(methodOf(profile, family)))
                ip.insert(QStringLiteral("ignore-auto-dns"), !form.value(QStringLiteral("automaticDns")).toBool());
            profile[family] = ip;
        }
    }

    return profile;
}

} // namespace IpUtils
