// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "vpnschema.h"

#include <QObject>

using namespace NetworkDBus;

namespace {

// Builds one kind's table; the helpers keep each field to a line.
struct Table {
    QList<VpnSchema::Field> fields;
    QString section;
    bool advanced = false;

    VpnSchema::Field &add(const QString &key, const QString &label, VpnSchema::Type type)
    {
        VpnSchema::Field field;
        field.key = key;
        field.label = label;
        field.type = type;
        field.advanced = advanced;
        field.section = section;
        fields << field;
        return fields.last();
    }

    VpnSchema::Field &text(const QString &key, const QString &label) { return add(key, label, VpnSchema::Type::Text); }
    VpnSchema::Field &file(const QString &key, const QString &label) { return add(key, label, VpnSchema::Type::File); }
    VpnSchema::Field &flag(const QString &key, const QString &label) { return add(key, label, VpnSchema::Type::Bool); }

    VpnSchema::Field &password(const QString &key, const QString &label, bool allowNotRequired = false)
    {
        VpnSchema::Field &field = add(key, label, VpnSchema::Type::Password);
        field.allowNotRequired = allowNotRequired;
        return field;
    }

    VpnSchema::Field &number(const QString &key, const QString &label, int minimum, int maximum,
                             const QString &defaultValue = QString())
    {
        VpnSchema::Field &field = add(key, label, VpnSchema::Type::Number);
        field.minimum = minimum;
        field.maximum = maximum;
        field.defaultValue = defaultValue;
        field.placeholder = defaultValue;
        return field;
    }

    VpnSchema::Field &choice(const QString &key, const QString &label, const QList<VpnSchema::Choice> &choices,
                             const QString &defaultValue = QString())
    {
        VpnSchema::Field &field = add(key, label, VpnSchema::Type::Choice);
        field.choices = choices;
        field.defaultValue = defaultValue;
        return field;
    }

    // The PPP options the pppd-based plugins share.
    void ppp(bool authentication = true)
    {
        section = QObject::tr("Authentication methods");
        if (authentication) {
            flag(QStringLiteral("refuse-pap"), QStringLiteral("PAP")).inverted = true;
            flag(QStringLiteral("refuse-chap"), QStringLiteral("CHAP")).inverted = true;
            flag(QStringLiteral("refuse-mschap"), QStringLiteral("MSCHAP")).inverted = true;
            flag(QStringLiteral("refuse-mschapv2"), QStringLiteral("MSCHAPv2")).inverted = true;
            flag(QStringLiteral("refuse-eap"), QStringLiteral("EAP")).inverted = true;
        }
        section = QObject::tr("Encryption and compression");
        choice(QStringLiteral("require-mppe"), QObject::tr("Point-to-point encryption"),
               {{QString(), QObject::tr("Not required")},
                {QStringLiteral("require-mppe"), QObject::tr("Any")},
                {QStringLiteral("require-mppe-128"), QObject::tr("128-bit")},
                {QStringLiteral("require-mppe-40"), QObject::tr("40-bit")}})
                .exclusiveKeys = true;
        flag(QStringLiteral("mppe-stateful"), QObject::tr("Stateful encryption"));
        flag(QStringLiteral("nobsdcomp"), QObject::tr("BSD compression")).inverted = true;
        flag(QStringLiteral("nodeflate"), QObject::tr("Deflate compression")).inverted = true;
        flag(QStringLiteral("no-vj-comp"), QObject::tr("TCP header compression")).inverted = true;
        section = QObject::tr("Link");
        VpnSchema::Field &echo = flag(QStringLiteral("lcp-echo-interval"), QObject::tr("Send PPP echo packets"));
        echo.trueValue = QStringLiteral("30");
        echo.alsoSet.insert(QStringLiteral("lcp-echo-failure"), QStringLiteral("5"));
    }
};

} // namespace

static QList<VpnSchema::Choice> tlsVersions()
{
    return {{QString(), QObject::tr("Default")},
            {QStringLiteral("1.0"), QStringLiteral("1.0")},
            {QStringLiteral("1.1"), QStringLiteral("1.1")},
            {QStringLiteral("1.2"), QStringLiteral("1.2")},
            {QStringLiteral("1.3"), QStringLiteral("1.3")}};
}

static QList<VpnSchema::Choice> dhGroups(const QString &defaultLabel, const QStringList &extra = {})
{
    QList<VpnSchema::Choice> choices {{QString(), defaultLabel}};
    for (const QString &value : extra)
        choices << VpnSchema::Choice {value, value};
    for (const char *group : {"dh1", "dh2", "dh5", "dh14", "dh15", "dh16", "dh17", "dh18"})
        choices << VpnSchema::Choice {QLatin1String(group), QObject::tr("DH group %1").arg(QLatin1String(group + 2))};
    return choices;
}

static Table openvpn()
{
    Table t;
    t.text(QStringLiteral("remote"), QObject::tr("Server")).required = true;
    t.fields.last().placeholder = QObject::tr("host[:port[:proto]], several separated by commas");
    t.choice(QStringLiteral("connection-type"), QObject::tr("Sign in with"),
             {{QStringLiteral("password"), QObject::tr("User name and password")},
              {QStringLiteral("tls"), QObject::tr("Certificates")},
              {QStringLiteral("password-tls"), QObject::tr("User name, password and certificates")},
              {QStringLiteral("static-key"), QObject::tr("Static key")}},
             QStringLiteral("tls"));
    const QString withPassword = QStringLiteral("connection-type=password|password-tls");
    const QString withCertificates = QStringLiteral("connection-type=tls|password-tls");
    t.text(QStringLiteral("username"), QObject::tr("User name")).showWhen = withPassword;
    t.password(QStringLiteral("password"), QObject::tr("Password"), true).showWhen = withPassword;
    t.file(QStringLiteral("ca"), QObject::tr("CA certificate")).showWhen = QStringLiteral("connection-type=tls|password|password-tls");
    t.file(QStringLiteral("cert"), QObject::tr("User certificate")).showWhen = withCertificates;
    t.file(QStringLiteral("key"), QObject::tr("Private key")).showWhen = withCertificates;
    t.password(QStringLiteral("cert-pass"), QObject::tr("Private key password"), true).showWhen = withCertificates;
    t.file(QStringLiteral("static-key"), QObject::tr("Static key")).showWhen = QStringLiteral("connection-type=static-key");
    t.choice(QStringLiteral("static-key-direction"), QObject::tr("Key direction"),
             {{QString(), QObject::tr("None")}, {QStringLiteral("0"), QStringLiteral("0")}, {QStringLiteral("1"), QStringLiteral("1")}})
            .showWhen = QStringLiteral("connection-type=static-key");
    t.text(QStringLiteral("remote-ip"), QObject::tr("Remote IP address")).showWhen = QStringLiteral("connection-type=static-key");
    t.text(QStringLiteral("local-ip"), QObject::tr("Local IP address")).showWhen = QStringLiteral("connection-type=static-key");

    t.advanced = true;
    t.section = QObject::tr("General");
    t.number(QStringLiteral("port"), QObject::tr("Port"), 1, 65535, QStringLiteral("1194"));
    t.flag(QStringLiteral("proto-tcp"), QObject::tr("Use TCP"));
    t.choice(QStringLiteral("dev-type"), QObject::tr("Device type"),
             {{QString(), QObject::tr("Automatic")}, {QStringLiteral("tun"), QStringLiteral("TUN")}, {QStringLiteral("tap"), QStringLiteral("TAP")}});
    t.text(QStringLiteral("dev"), QObject::tr("Device name"));
    t.number(QStringLiteral("tunnel-mtu"), QObject::tr("Tunnel MTU"), 1, 65535, QStringLiteral("1500"));
    t.number(QStringLiteral("fragment-size"), QObject::tr("UDP fragment size"), 1, 65535, QStringLiteral("1300"));
    t.flag(QStringLiteral("mssfix"), QObject::tr("Restrict TCP maximum segment size"));
    t.choice(QStringLiteral("mtu-disc"), QObject::tr("Path MTU discovery"),
             {{QString(), QObject::tr("Default")}, {QStringLiteral("no"), QObject::tr("Never")},
              {QStringLiteral("maybe"), QObject::tr("When possible")}, {QStringLiteral("yes"), QObject::tr("Always")}});
    t.number(QStringLiteral("reneg-seconds"), QObject::tr("Renegotiate every (seconds)"), 0, 604800, QStringLiteral("0"));
    t.choice(QStringLiteral("compress"), QObject::tr("Compression"),
             {{QString(), QObject::tr("None")}, {QStringLiteral("lzo"), QStringLiteral("LZO")},
              {QStringLiteral("lz4"), QStringLiteral("LZ4")}, {QStringLiteral("lz4-v2"), QStringLiteral("LZ4 v2")},
              {QStringLiteral("yes"), QObject::tr("Automatic")}});
    t.choice(QStringLiteral("allow-compression"), QObject::tr("Allow compression"),
             {{QString(), QObject::tr("Default")}, {QStringLiteral("asym"), QObject::tr("Incoming only")},
              {QStringLiteral("yes"), QObject::tr("Both ways")}, {QStringLiteral("no"), QObject::tr("No")}});
    t.flag(QStringLiteral("remote-random"), QObject::tr("Pick servers at random"));
    t.flag(QStringLiteral("remote-random-hostname"), QObject::tr("Randomise server host names"));
    t.flag(QStringLiteral("allow-pull-fqdn"), QObject::tr("Allow pulled host names"));
    t.flag(QStringLiteral("tun-ipv6"), QObject::tr("IPv6 inside the tunnel"));
    t.flag(QStringLiteral("float"), QObject::tr("Accept the server from any address"));
    t.flag(QStringLiteral("push-peer-info"), QObject::tr("Send client details to the server"));
    t.number(QStringLiteral("ping"), QObject::tr("Ping every (seconds)"), 1, 65535, QStringLiteral("30"));
    t.number(QStringLiteral("ping-exit"), QObject::tr("Exit without a ping after (seconds)"), 1, 65535, QStringLiteral("30"));
    t.number(QStringLiteral("ping-restart"), QObject::tr("Restart without a ping after (seconds)"), 1, 65535, QStringLiteral("30"));
    t.number(QStringLiteral("connect-timeout"), QObject::tr("Connection timeout (seconds)"), 1, 65535, QStringLiteral("120"));
    t.number(QStringLiteral("max-routes"), QObject::tr("Maximum routes"), 1, 65535, QStringLiteral("100"));

    t.section = QObject::tr("Security");
    t.text(QStringLiteral("cipher"), QObject::tr("Cipher")).placeholder = QObject::tr("Default");
    t.text(QStringLiteral("data-ciphers"), QObject::tr("Data ciphers")).placeholder = QObject::tr("Default");
    t.text(QStringLiteral("data-ciphers-fallback"), QObject::tr("Data cipher fallback")).placeholder = QObject::tr("Default");
    t.flag(QStringLiteral("ncp-disable"), QObject::tr("Disable cipher negotiation"));
    QList<VpnSchema::Choice> hmac {{QString(), QObject::tr("Default")}, {QStringLiteral("none"), QObject::tr("None")}};
    for (const char *name : {"MD5", "SHA1", "SHA224", "SHA256", "SHA384", "SHA512", "RIPEMD160", "RSA-MD4"})
        hmac << VpnSchema::Choice {QLatin1String(name), QLatin1String(name)};
    t.choice(QStringLiteral("auth"), QObject::tr("HMAC authentication"), hmac);
    t.number(QStringLiteral("keysize"), QObject::tr("Key size (bits)"), 1, 65535, QStringLiteral("128"));
    t.file(QStringLiteral("crl-verify-file"), QObject::tr("Revocation list file"));
    t.file(QStringLiteral("crl-verify-dir"), QObject::tr("Revocation list directory"));

    t.section = QObject::tr("TLS");
    t.text(QStringLiteral("verify-x509-name"), QObject::tr("Server certificate name")).placeholder
            = QObject::tr("name:vpn.example.com, subject:… or name-prefix:…");
    t.choice(QStringLiteral("remote-cert-tls"), QObject::tr("Server certificate usage"),
             {{QString(), QObject::tr("Not checked")}, {QStringLiteral("server"), QObject::tr("Server")}, {QStringLiteral("client"), QObject::tr("Client")}});
    t.choice(QStringLiteral("ns-cert-type"), QObject::tr("Netscape certificate type"),
             {{QString(), QObject::tr("Not checked")}, {QStringLiteral("server"), QObject::tr("Server")}, {QStringLiteral("client"), QObject::tr("Client")}});
    t.file(QStringLiteral("ta"), QObject::tr("TLS authentication key"));
    t.choice(QStringLiteral("ta-dir"), QObject::tr("TLS key direction"),
             {{QString(), QObject::tr("None")}, {QStringLiteral("0"), QStringLiteral("0")}, {QStringLiteral("1"), QStringLiteral("1")}});
    t.file(QStringLiteral("tls-crypt"), QObject::tr("TLS encryption key"));
    t.file(QStringLiteral("tls-crypt-v2"), QObject::tr("TLS encryption key (v2)"));
    t.file(QStringLiteral("extra-certs"), QObject::tr("Extra certificates"));
    t.text(QStringLiteral("tls-cipher"), QObject::tr("TLS cipher")).placeholder = QObject::tr("Default");
    t.choice(QStringLiteral("tls-version-min"), QObject::tr("Minimum TLS version"), tlsVersions());
    t.flag(QStringLiteral("tls-version-min-or-highest"), QObject::tr("Or the highest the server supports"));
    t.choice(QStringLiteral("tls-version-max"), QObject::tr("Maximum TLS version"), tlsVersions());

    t.section = QObject::tr("Proxy");
    t.choice(QStringLiteral("proxy-type"), QObject::tr("Proxy"),
             {{QString(), QObject::tr("None")}, {QStringLiteral("http"), QStringLiteral("HTTP")}, {QStringLiteral("socks"), QStringLiteral("SOCKS")}});
    const QString withProxy = QStringLiteral("proxy-type=http|socks");
    t.text(QStringLiteral("proxy-server"), QObject::tr("Proxy server")).showWhen = withProxy;
    t.number(QStringLiteral("proxy-port"), QObject::tr("Proxy port"), 1, 65535).showWhen = withProxy;
    t.flag(QStringLiteral("proxy-retry"), QObject::tr("Retry indefinitely")).showWhen = withProxy;
    t.text(QStringLiteral("http-proxy-username"), QObject::tr("Proxy user name")).showWhen = QStringLiteral("proxy-type=http");
    t.password(QStringLiteral("http-proxy-password"), QObject::tr("Proxy password"), true).showWhen = QStringLiteral("proxy-type=http");
    return t;
}

static Table vpnc()
{
    Table t;
    t.text(QStringLiteral("IPSec gateway"), QObject::tr("Server")).required = true;
    t.text(QStringLiteral("IPSec ID"), QObject::tr("Group name")).required = true;
    t.text(QStringLiteral("Xauth username"), QObject::tr("User name"));
    VpnSchema::Field &user = t.password(QStringLiteral("Xauth password"), QObject::tr("Password"), true);
    user.legacyTypeKey = QStringLiteral("xauth-password-type");
    VpnSchema::Field &group = t.password(QStringLiteral("IPSec secret"), QObject::tr("Group password"), true);
    group.legacyTypeKey = QStringLiteral("ipsec-secret-type");
    t.flag(QStringLiteral("IKE Authmode"), QObject::tr("Hybrid authentication")).trueValue = QStringLiteral("hybrid");
    t.file(QStringLiteral("CA-File"), QObject::tr("CA certificate")).showWhen = QStringLiteral("IKE Authmode=hybrid");

    t.advanced = true;
    t.section = QObject::tr("General");
    t.text(QStringLiteral("Domain"), QObject::tr("Domain"));
    t.choice(QStringLiteral("Vendor"), QObject::tr("Vendor"),
             {{QString(), QStringLiteral("Cisco")}, {QStringLiteral("netscreen"), QStringLiteral("Netscreen")},
              {QStringLiteral("fortigate"), QStringLiteral("Fortigate")}});
    t.text(QStringLiteral("Application Version"), QObject::tr("Application version"));
    t.number(QStringLiteral("Interface MTU"), QObject::tr("Interface MTU"), 1200, 12000);
    t.section = QObject::tr("Security");
    t.flag(QStringLiteral("Enable Single DES"), QObject::tr("Weak encryption (single DES)"));
    t.flag(QStringLiteral("Enable no encryption"), QObject::tr("No encryption"));
    t.flag(QStringLiteral("Enable weak authentication"), QObject::tr("Weak authentication"));
    t.flag(QStringLiteral("Enable weak encryption"), QObject::tr("Weak encryption"));
    t.choice(QStringLiteral("NAT Traversal Mode"), QObject::tr("NAT traversal"),
             {{QString(), QObject::tr("NAT-T when detected")}, {QStringLiteral("force-natt"), QObject::tr("NAT-T always")},
              {QStringLiteral("cisco-udp"), QObject::tr("Cisco UDP")}, {QStringLiteral("none"), QObject::tr("Off")}});
    t.choice(QStringLiteral("IKE DH Group"), QObject::tr("IKE DH group"), dhGroups(QObject::tr("DH group 2")));
    t.choice(QStringLiteral("Perfect Forward Secrecy"), QObject::tr("Perfect forward secrecy"),
             dhGroups(QObject::tr("As the server says"), {QStringLiteral("nopfs")}));
    t.number(QStringLiteral("Local Port"), QObject::tr("Local port"), 0, 65535, QStringLiteral("0"));
    t.number(QStringLiteral("DPD idle timeout (our side)"), QObject::tr("Dead peer detection (seconds, 0 is off)"), 0, 86400, QStringLiteral("90"));
    return t;
}

static Table l2tp()
{
    Table t;
    t.text(QStringLiteral("gateway"), QObject::tr("Server")).required = true;
    t.choice(QStringLiteral("user-auth-type"), QObject::tr("Sign in with"),
             {{QString(), QObject::tr("User name and password")}, {QStringLiteral("tls"), QObject::tr("Certificates")}});
    const QString withPassword = QStringLiteral("user-auth-type=");
    const QString withCertificates = QStringLiteral("user-auth-type=tls");
    t.text(QStringLiteral("user"), QObject::tr("User name")).showWhen = withPassword;
    t.password(QStringLiteral("password"), QObject::tr("Password"), true).showWhen = withPassword;
    t.text(QStringLiteral("domain"), QObject::tr("NT domain")).showWhen = withPassword;
    t.file(QStringLiteral("user-ca"), QObject::tr("CA certificate")).showWhen = withCertificates;
    t.file(QStringLiteral("user-cert"), QObject::tr("User certificate")).showWhen = withCertificates;
    t.file(QStringLiteral("user-key"), QObject::tr("Private key")).showWhen = withCertificates;
    t.password(QStringLiteral("user-certpass"), QObject::tr("Private key password"), true).showWhen = withCertificates;

    t.advanced = true;
    t.section = QObject::tr("IPsec");
    t.flag(QStringLiteral("ipsec-enabled"), QObject::tr("IPsec tunnel to the server"));
    const QString withIpsec = QStringLiteral("ipsec-enabled=yes");
    t.text(QStringLiteral("ipsec-remote-id"), QObject::tr("Server identity")).showWhen = withIpsec;
    t.choice(QStringLiteral("machine-auth-type"), QObject::tr("Machine authentication"),
             {{QString(), QObject::tr("Pre-shared key")}, {QStringLiteral("tls"), QObject::tr("Certificates")}})
            .showWhen = withIpsec;
    t.password(QStringLiteral("ipsec-psk"), QObject::tr("Pre-shared key"), true).showWhen = QStringLiteral("machine-auth-type=");
    t.file(QStringLiteral("machine-ca"), QObject::tr("Machine CA certificate")).showWhen = QStringLiteral("machine-auth-type=tls");
    t.file(QStringLiteral("machine-cert"), QObject::tr("Machine certificate")).showWhen = QStringLiteral("machine-auth-type=tls");
    t.file(QStringLiteral("machine-key"), QObject::tr("Machine private key")).showWhen = QStringLiteral("machine-auth-type=tls");
    t.password(QStringLiteral("machine-certpass"), QObject::tr("Machine key password"), true).showWhen = QStringLiteral("machine-auth-type=tls");
    t.text(QStringLiteral("ipsec-ike"), QObject::tr("Phase 1 algorithms")).showWhen = withIpsec;
    t.text(QStringLiteral("ipsec-esp"), QObject::tr("Phase 2 algorithms")).showWhen = withIpsec;
    t.number(QStringLiteral("ipsec-ikelifetime"), QObject::tr("Phase 1 lifetime (seconds)"), 0, 1000000).showWhen = withIpsec;
    t.number(QStringLiteral("ipsec-salifetime"), QObject::tr("Phase 2 lifetime (seconds)"), 0, 1000000).showWhen = withIpsec;
    t.flag(QStringLiteral("ipsec-forceencaps"), QObject::tr("Force UDP encapsulation")).showWhen = withIpsec;
    t.flag(QStringLiteral("ipsec-ipcomp"), QObject::tr("IP compression")).showWhen = withIpsec;
    t.flag(QStringLiteral("ipsec-ikev2"), QObject::tr("Use IKEv2")).showWhen = withIpsec;
    t.flag(QStringLiteral("ipsec-pfs"), QObject::tr("Disable perfect forward secrecy")).showWhen = withIpsec;
    t.fields.last().trueValue = QStringLiteral("no");
    t.ppp();
    t.section = QObject::tr("Link");
    t.number(QStringLiteral("mtu"), QObject::tr("MTU"), 575, 1500, QStringLiteral("1400"));
    t.number(QStringLiteral("mru"), QObject::tr("MRU"), 575, 1500, QStringLiteral("1400"));
    t.number(QStringLiteral("mrru"), QObject::tr("Multilink MRRU"), 1500, 4500);
    t.flag(QStringLiteral("ephemeral-port"), QObject::tr("Use a random source port"));
    return t;
}

static Table pptp()
{
    Table t;
    t.text(QStringLiteral("gateway"), QObject::tr("Server")).required = true;
    t.text(QStringLiteral("user"), QObject::tr("User name"));
    t.password(QStringLiteral("password"), QObject::tr("Password"), true);
    t.text(QStringLiteral("domain"), QObject::tr("NT domain"));
    t.advanced = true;
    t.ppp();
    t.number(QStringLiteral("unit"), QObject::tr("PPP unit number"), 0, 65535);
    return t;
}

static Table sstp()
{
    Table t;
    t.text(QStringLiteral("gateway"), QObject::tr("Server")).required = true;
    t.fields.last().placeholder = QObject::tr("host[:port]");
    t.choice(QStringLiteral("connection-type"), QObject::tr("Sign in with"),
             {{QString(), QObject::tr("User name and password")}, {QStringLiteral("tls"), QObject::tr("Certificates")}});
    const QString withPassword = QStringLiteral("connection-type=");
    const QString withCertificates = QStringLiteral("connection-type=tls");
    t.text(QStringLiteral("user"), QObject::tr("User name")).showWhen = withPassword;
    t.password(QStringLiteral("password"), QObject::tr("Password"), true).showWhen = withPassword;
    t.text(QStringLiteral("domain"), QObject::tr("NT domain")).showWhen = withPassword;
    t.file(QStringLiteral("tls-ca-cert"), QObject::tr("CA certificate")).showWhen = withCertificates;
    t.file(QStringLiteral("tls-user-cert"), QObject::tr("User certificate")).showWhen = withCertificates;
    t.file(QStringLiteral("tls-user-key"), QObject::tr("Private key")).showWhen = withCertificates;
    t.password(QStringLiteral("tls-user-key-secret"), QObject::tr("Private key password"), true).showWhen = withCertificates;

    t.advanced = true;
    t.section = QObject::tr("Certificates");
    t.file(QStringLiteral("ca-cert"), QObject::tr("Server CA certificate"));
    t.file(QStringLiteral("crl-file"), QObject::tr("Revocation list"));
    t.flag(QStringLiteral("ignore-cert-warn"), QObject::tr("Ignore certificate warnings"));
    t.flag(QStringLiteral("tls-ext"), QObject::tr("TLS hostname extension")).falseValue = QStringLiteral("no");
    t.fields.last().defaultValue = QStringLiteral("yes");
    t.section = QObject::tr("TLS");
    t.text(QStringLiteral("tls-identity"), QObject::tr("Identity"));
    t.choice(QStringLiteral("tls-verify-method"), QObject::tr("Verify the server name by"),
             {{QString(), QObject::tr("Default")}, {QStringLiteral("none"), QObject::tr("Nothing")},
              {QStringLiteral("subject"), QObject::tr("Subject")}, {QStringLiteral("name"), QObject::tr("Name")},
              {QStringLiteral("suffix"), QObject::tr("Name suffix")}});
    t.text(QStringLiteral("tls-remotename"), QObject::tr("Expected server name"));
    t.flag(QStringLiteral("tls-verify-key-usage"), QObject::tr("Check key usage"));
    t.choice(QStringLiteral("tls-max-version"), QObject::tr("Maximum TLS version"), tlsVersions());
    t.ppp();
    for (VpnSchema::Field &field : t.fields) {
        if (field.key == QLatin1String("refuse-pap") || field.key == QLatin1String("refuse-chap"))
            field.defaultValue = QStringLiteral("yes");
    }
    t.section = QObject::tr("Link");
    t.number(QStringLiteral("unit"), QObject::tr("PPP unit number"), 0, 65535);
    t.number(QStringLiteral("mtu"), QObject::tr("MTU"), 1, 65535);
    t.section = QObject::tr("Proxy");
    t.text(QStringLiteral("proxy-server"), QObject::tr("Proxy server"));
    t.number(QStringLiteral("proxy-port"), QObject::tr("Proxy port"), 1, 65535);
    t.text(QStringLiteral("proxy-user"), QObject::tr("Proxy user name"));
    t.password(QStringLiteral("proxy-password"), QObject::tr("Proxy password"), true);
    return t;
}

static Table strongswan()
{
    Table t;
    t.text(QStringLiteral("address"), QObject::tr("Server")).required = true;
    t.file(QStringLiteral("certificate"), QObject::tr("Server CA certificate"));
    t.text(QStringLiteral("remote-identity"), QObject::tr("Server identity"));
    t.choice(QStringLiteral("method"), QObject::tr("Sign in with"),
             {{QStringLiteral("eap"), QObject::tr("User name and password (EAP)")},
              {QStringLiteral("psk"), QObject::tr("Pre-shared key")},
              {QStringLiteral("cert"), QObject::tr("Certificate")},
              {QStringLiteral("eap-tls"), QObject::tr("Certificate (EAP-TLS)")},
              {QStringLiteral("key"), QObject::tr("Certificate and private key")},
              {QStringLiteral("agent"), QObject::tr("Certificate and SSH agent")},
              {QStringLiteral("smartcard"), QObject::tr("Smart card")}},
             QStringLiteral("eap"));
    t.text(QStringLiteral("user"), QObject::tr("User name")).showWhen = QStringLiteral("method=eap|psk");
    t.choice(QStringLiteral("cert-source"), QObject::tr("Certificate from"),
             {{QString(), QObject::tr("A file")}, {QStringLiteral("agent"), QObject::tr("The SSH agent")},
              {QStringLiteral("smartcard"), QObject::tr("A smart card")}})
            .showWhen = QStringLiteral("method=cert|eap-tls");
    t.file(QStringLiteral("usercert"), QObject::tr("User certificate")).showWhen = QStringLiteral("method=cert|eap-tls|key|agent");
    t.file(QStringLiteral("userkey"), QObject::tr("Private key")).showWhen = QStringLiteral("method=cert|eap-tls|key");
    t.password(QStringLiteral("password"), QObject::tr("Password"), true).showWhen = QStringLiteral("method=eap|psk|cert|eap-tls|key|smartcard");
    t.text(QStringLiteral("local-identity"), QObject::tr("Client identity"));
    t.advanced = true;
    t.section = QObject::tr("Options");
    t.number(QStringLiteral("server-port"), QObject::tr("Server port"), 1, 65535, QStringLiteral("500"));
    t.flag(QStringLiteral("virtual"), QObject::tr("Request an inner IP address"));
    t.flag(QStringLiteral("encap"), QObject::tr("Enforce UDP encapsulation"));
    t.flag(QStringLiteral("ipcomp"), QObject::tr("IP compression"));
    t.flag(QStringLiteral("proposal"), QObject::tr("Custom algorithm proposals"));
    t.text(QStringLiteral("ike"), QObject::tr("IKE proposal")).showWhen = QStringLiteral("proposal=yes");
    t.text(QStringLiteral("esp"), QObject::tr("ESP proposal")).showWhen = QStringLiteral("proposal=yes");
    t.text(QStringLiteral("local-ts"), QObject::tr("Local traffic selectors"));
    t.text(QStringLiteral("remote-ts"), QObject::tr("Remote traffic selectors"));
    return t;
}

static Table libreswan()
{
    Table t;
    t.text(QStringLiteral("right"), QObject::tr("Server")).required = true;
    t.choice(QStringLiteral("ikev2"), QObject::tr("Sign in with"),
             {{QString(), QObject::tr("Group password and user name (IKEv1)")},
              {QStringLiteral("insist"), QObject::tr("Certificate (IKEv2)")}});
    const QString xauth = QStringLiteral("ikev2=");
    const QString certificate = QStringLiteral("ikev2=insist");
    t.text(QStringLiteral("leftid"), QObject::tr("Group name")).showWhen = xauth;
    VpnSchema::Field &group = t.password(QStringLiteral("pskvalue"), QObject::tr("Group password"), true);
    group.showWhen = xauth;
    group.legacyTypeKey = QStringLiteral("pskinputmodes");
    t.text(QStringLiteral("leftxauthusername"), QObject::tr("User name")).showWhen = xauth;
    VpnSchema::Field &user = t.password(QStringLiteral("xauthpassword"), QObject::tr("Password"), true);
    user.showWhen = xauth;
    user.legacyTypeKey = QStringLiteral("xauthpasswordinputmodes");
    t.text(QStringLiteral("leftcert"), QObject::tr("Certificate name")).showWhen = certificate;
    t.text(QStringLiteral("rightid"), QObject::tr("Server identity"));
    t.advanced = true;
    t.section = QObject::tr("Security");
    t.text(QStringLiteral("ike"), QObject::tr("Phase 1 algorithms"));
    t.text(QStringLiteral("esp"), QObject::tr("Phase 2 algorithms"));
    t.text(QStringLiteral("ikelifetime"), QObject::tr("Phase 1 lifetime"));
    t.text(QStringLiteral("salifetime"), QObject::tr("Phase 2 lifetime"));
    t.flag(QStringLiteral("rekey"), QObject::tr("Disable rekeying")).trueValue = QStringLiteral("no");
    t.flag(QStringLiteral("pfs"), QObject::tr("Disable perfect forward secrecy")).trueValue = QStringLiteral("no");
    t.choice(QStringLiteral("authby"), QObject::tr("Authenticate by"),
             {{QString(), QObject::tr("Default")}, {QStringLiteral("secret"), QObject::tr("Pre-shared key")},
              {QStringLiteral("rsasig"), QObject::tr("RSA signature")}, {QStringLiteral("null"), QObject::tr("None")}});
    t.section = QObject::tr("Networks");
    t.text(QStringLiteral("Domain"), QObject::tr("Domain"));
    t.text(QStringLiteral("leftsubnet"), QObject::tr("Local subnet"));
    t.text(QStringLiteral("rightsubnet"), QObject::tr("Remote subnet"));
    t.flag(QStringLiteral("narrowing"), QObject::tr("Allow narrowing"));
    t.flag(QStringLiteral("mobike"), QObject::tr("Mobility (MOBIKE)"));
    t.flag(QStringLiteral("leftmodecfgclient"), QObject::tr("Don't ask the server for an address")).trueValue = QStringLiteral("no");
    t.choice(QStringLiteral("fragmentation"), QObject::tr("IKE fragmentation"),
             {{QString(), QObject::tr("Default")}, {QStringLiteral("no"), QObject::tr("Off")}, {QStringLiteral("force"), QObject::tr("Forced")}});
    t.text(QStringLiteral("ipsec-interface"), QObject::tr("IPsec interface"));
    t.section = QObject::tr("Dead peer detection");
    t.number(QStringLiteral("dpddelay"), QObject::tr("Delay (seconds)"), 0, 86400);
    t.number(QStringLiteral("dpdtimeout"), QObject::tr("Timeout (seconds)"), 0, 86400);
    t.choice(QStringLiteral("dpdaction"), QObject::tr("Action"),
             {{QString(), QObject::tr("Default")}, {QStringLiteral("hold"), QObject::tr("Hold")},
              {QStringLiteral("clear"), QObject::tr("Clear")}, {QStringLiteral("restart"), QObject::tr("Restart")}});
    t.section = QObject::tr("Certificates");
    t.text(QStringLiteral("rightcert"), QObject::tr("Server certificate"));
    t.text(QStringLiteral("rightca"), QObject::tr("Server CA"));
    t.choice(QStringLiteral("leftsendcert"), QObject::tr("Send the certificate"),
             {{QString(), QObject::tr("Default")}, {QStringLiteral("always"), QObject::tr("Always")},
              {QStringLiteral("ifasked"), QObject::tr("If asked")}, {QStringLiteral("never"), QObject::tr("Never")}});
    t.flag(QStringLiteral("require-id-on-certificate"), QObject::tr("Accept certificates without the identity")).trueValue = QStringLiteral("no");
    return t;
}

static Table fortisslvpn()
{
    Table t;
    t.text(QStringLiteral("gateway"), QObject::tr("Server")).required = true;
    t.text(QStringLiteral("user"), QObject::tr("User name"));
    t.password(QStringLiteral("password"), QObject::tr("Password"), true);
    t.file(QStringLiteral("ca"), QObject::tr("CA certificate"));
    t.file(QStringLiteral("cert"), QObject::tr("User certificate"));
    t.file(QStringLiteral("key"), QObject::tr("Private key"));
    t.advanced = true;
    t.section = QObject::tr("Options");
    t.text(QStringLiteral("trusted-cert"), QObject::tr("Trusted certificate fingerprint"));
    t.text(QStringLiteral("realm"), QObject::tr("Realm"));
    VpnSchema::Field &otp = t.flag(QStringLiteral("otp-flags"), QObject::tr("Ask for a one-time password"));
    otp.trueValue = QString::number(SecretNotSaved);
    VpnSchema::Field &tfa = t.flag(QStringLiteral("2fa-flags"), QObject::tr("Two-factor authentication"));
    tfa.trueValue = QString::number(SecretAgentOwned);
    return t;
}

static Table ssh()
{
    Table t;
    t.text(QStringLiteral("remote"), QObject::tr("Server")).required = true;
    t.choice(QStringLiteral("auth-type"), QObject::tr("Sign in with"),
             {{QString(), QObject::tr("The SSH agent")}, {QStringLiteral("password"), QObject::tr("Password")},
              {QStringLiteral("key"), QObject::tr("Key file")}});
    t.password(QStringLiteral("password"), QObject::tr("Password"), true).showWhen = QStringLiteral("auth-type=password");
    t.file(QStringLiteral("key-file"), QObject::tr("Key file")).showWhen = QStringLiteral("auth-type=key");
    t.text(QStringLiteral("remote-ip"), QObject::tr("Remote IP address")).required = true;
    t.text(QStringLiteral("local-ip"), QObject::tr("Local IP address")).required = true;
    t.text(QStringLiteral("netmask"), QObject::tr("Netmask")).required = true;
    t.flag(QStringLiteral("ip-6"), QObject::tr("IPv6 addresses too"));
    t.text(QStringLiteral("remote-ip-6"), QObject::tr("Remote IPv6 address")).showWhen = QStringLiteral("ip-6=yes");
    t.text(QStringLiteral("local-ip-6"), QObject::tr("Local IPv6 address")).showWhen = QStringLiteral("ip-6=yes");
    t.text(QStringLiteral("netmask-6"), QObject::tr("IPv6 prefix")).showWhen = QStringLiteral("ip-6=yes");
    t.advanced = true;
    t.section = QObject::tr("Options");
    t.number(QStringLiteral("port"), QObject::tr("Port"), 1, 65535, QStringLiteral("22"));
    t.number(QStringLiteral("tunnel-mtu"), QObject::tr("Tunnel MTU"), 1, 65535, QStringLiteral("1500"));
    t.text(QStringLiteral("remote-username"), QObject::tr("Remote user name")).placeholder = QStringLiteral("root");
    t.number(QStringLiteral("remote-dev"), QObject::tr("Remote device number"), 0, 255, QStringLiteral("100"));
    t.flag(QStringLiteral("tap-dev"), QObject::tr("TAP device"));
    t.flag(QStringLiteral("no-default-route"), QObject::tr("No default route"));
    t.text(QStringLiteral("extra-opts"), QObject::tr("Extra SSH options"));
    return t;
}

static Table iodine()
{
    Table t;
    t.text(QStringLiteral("topdomain"), QObject::tr("Tunnel domain")).required = true;
    t.text(QStringLiteral("nameserver"), QObject::tr("Name server"));
    t.password(QStringLiteral("password"), QObject::tr("Password"));
    t.advanced = true;
    t.section = QObject::tr("Options");
    t.number(QStringLiteral("fragsize"), QObject::tr("Fragment size"), 0, 65535);
    return t;
}

static Table openconnect()
{
    Table t;
    t.choice(QStringLiteral("protocol"), QObject::tr("Protocol"),
             {{QString(), QObject::tr("Cisco AnyConnect or openconnect")},
              {QStringLiteral("nc"), QObject::tr("Juniper Network Connect")},
              {QStringLiteral("gp"), QObject::tr("Palo Alto GlobalProtect")},
              {QStringLiteral("pulse"), QObject::tr("Pulse Connect Secure")},
              {QStringLiteral("f5"), QObject::tr("F5 BIG-IP")},
              {QStringLiteral("fortinet"), QObject::tr("Fortinet SSL VPN")},
              {QStringLiteral("array"), QObject::tr("Array Networks")}});
    t.text(QStringLiteral("gateway"), QObject::tr("Server")).required = true;
    t.file(QStringLiteral("cacert"), QObject::tr("CA certificate"));
    t.file(QStringLiteral("usercert"), QObject::tr("User certificate"));
    t.file(QStringLiteral("userkey"), QObject::tr("Private key"));
    t.advanced = true;
    t.section = QObject::tr("Options");
    t.text(QStringLiteral("proxy"), QObject::tr("Proxy")).placeholder = QStringLiteral("http://… or socks://…");
    t.text(QStringLiteral("useragent"), QObject::tr("User agent"));
    t.choice(QStringLiteral("reported_os"), QObject::tr("Reported operating system"),
             {{QString(), QObject::tr("Default")}, {QStringLiteral("linux"), QStringLiteral("Linux")},
              {QStringLiteral("linux-64"), QStringLiteral("Linux 64-bit")}, {QStringLiteral("win"), QStringLiteral("Windows")},
              {QStringLiteral("mac-intel"), QStringLiteral("Mac")}, {QStringLiteral("android"), QStringLiteral("Android")},
              {QStringLiteral("apple-ios"), QStringLiteral("iOS")}});
    t.flag(QStringLiteral("disable_udp"), QObject::tr("Disable UDP (DTLS)"));
    t.flag(QStringLiteral("prevent_invalid_cert"), QObject::tr("Refuse invalid server certificates"));
    t.flag(QStringLiteral("pem_passphrase_fsid"), QObject::tr("Private key passphrase from the file system ID"));
    t.section = QObject::tr("Trojan");
    t.flag(QStringLiteral("enable_csd_trojan"), QObject::tr("Run the server's compliance script"));
    t.file(QStringLiteral("csd_wrapper"), QObject::tr("Wrapper script")).showWhen = QStringLiteral("enable_csd_trojan=yes");
    t.section = QObject::tr("Machine certificate");
    t.file(QStringLiteral("mcacert"), QObject::tr("Machine certificate"));
    t.file(QStringLiteral("mcakey"), QObject::tr("Machine private key"));
    t.section = QObject::tr("Software token");
    t.choice(QStringLiteral("stoken_source"), QObject::tr("Token"),
             {{QString(), QObject::tr("None")}, {QStringLiteral("stokenrc"), QObject::tr("RSA SecurID from ~/.stokenrc")},
              {QStringLiteral("manual"), QObject::tr("RSA SecurID")}, {QStringLiteral("totp"), QStringLiteral("TOTP")},
              {QStringLiteral("hotp"), QStringLiteral("HOTP")}, {QStringLiteral("yubioath"), QStringLiteral("Yubikey OATH")}});
    t.password(QStringLiteral("stoken_string"), QObject::tr("Token secret")).showWhen = QStringLiteral("stoken_source=manual|totp|hotp|yubioath");
    return t;
}

static const QMap<QString, Table> &tables()
{
    static const QMap<QString, Table> tables {
        {QStringLiteral("openvpn"), openvpn()},
        {QStringLiteral("openconnect"), openconnect()},
        {QStringLiteral("vpnc"), vpnc()},
        {QStringLiteral("l2tp"), l2tp()},
        {QStringLiteral("pptp"), pptp()},
        {QStringLiteral("sstp"), sstp()},
        {QStringLiteral("strongswan"), strongswan()},
        {QStringLiteral("libreswan"), libreswan()},
        {QStringLiteral("fortisslvpn"), fortisslvpn()},
        {QStringLiteral("ssh"), ssh()},
        {QStringLiteral("iodine"), iodine()},
    };
    return tables;
}

// Whether a field's condition holds for these form values (strings, or bools for Bools).
static bool shown(const VpnSchema::Field &field, const QList<VpnSchema::Field> &fields, const QVariantMap &values)
{
    if (field.showWhen.isEmpty())
        return true;
    const QString key = field.showWhen.section(QLatin1Char('='), 0, 0);
    const QStringList accepted = field.showWhen.section(QLatin1Char('='), 1).split(QLatin1Char('|'));
    QString value;
    for (const VpnSchema::Field &other : fields) {
        if (other.key != key)
            continue;
        const QVariant raw = values.value(key);
        if (other.type == VpnSchema::Type::Bool)
            value = raw.toBool() != other.inverted ? other.trueValue : other.falseValue;
        else if (raw.isNull())
            value = other.defaultValue;
        else
            value = raw.toString();
        break;
    }
    return accepted.contains(value);
}

static QString legacyType(uint flags)
{
    if (flags & SecretNotRequired)
        return QStringLiteral("unused");
    if (flags & SecretNotSaved)
        return QStringLiteral("ask");
    return QStringLiteral("save");
}

namespace VpnSchema {

QStringList kinds()
{
    return {QStringLiteral("openvpn"),   QStringLiteral("openconnect"), QStringLiteral("vpnc"),
            QStringLiteral("l2tp"),      QStringLiteral("pptp"),        QStringLiteral("sstp"),
            QStringLiteral("strongswan"), QStringLiteral("libreswan"),  QStringLiteral("fortisslvpn"),
            QStringLiteral("ssh"),       QStringLiteral("iodine")};
}

QList<Field> fields(const QString &kind)
{
    return tables().value(kind).fields;
}

QStringList importPatterns(const QString &kind)
{
    if (kind == QLatin1String("openvpn"))
        return {QStringLiteral("*.ovpn"), QStringLiteral("*.conf")};
    if (kind == QLatin1String("vpnc"))
        return {QStringLiteral("*.pcf")};
    if (kind == QLatin1String("l2tp") || kind == QLatin1String("openconnect") || kind == QLatin1String("libreswan"))
        return {QStringLiteral("*.conf")};
    return {};
}

bool exportable(const QString &kind)
{
    return !importPatterns(kind).isEmpty();
}

QVariantList formFields(const QString &kind, const QMap<QString, QString> &data)
{
    QVariantList list;
    for (const Field &field : fields(kind)) {
        QVariantMap item {
            {QStringLiteral("key"), field.key},
            {QStringLiteral("label"), field.label},
            {QStringLiteral("type"), int(field.type)},
            {QStringLiteral("advanced"), field.advanced},
            {QStringLiteral("section"), field.section},
            {QStringLiteral("required"), field.required},
            {QStringLiteral("placeholder"), field.placeholder},
            {QStringLiteral("showWhen"), field.showWhen},
            {QStringLiteral("allowNotRequired"), field.allowNotRequired},
            {QStringLiteral("trueValue"), field.trueValue},
            {QStringLiteral("minimum"), field.minimum},
            {QStringLiteral("maximum"), field.maximum},
        };
        const QString stored = data.value(field.key);
        switch (field.type) {
        case Type::Bool: {
            bool on;
            if (field.exclusiveKeys)
                on = false;
            else if (stored.isEmpty())
                on = field.defaultValue == field.trueValue;
            else
                on = stored == field.trueValue;
            item.insert(QStringLiteral("value"), on != field.inverted);
            break;
        }
        case Type::Choice: {
            QVariantList choices;
            QString value = stored.isEmpty() ? field.defaultValue : stored;
            if (field.exclusiveKeys) {
                value.clear();
                for (const Choice &choice : field.choices) {
                    if (!choice.value.isEmpty() && data.value(choice.value) == QLatin1String("yes"))
                        value = choice.value;
                }
            }
            bool known = false;
            for (const Choice &choice : field.choices) {
                choices << QVariantMap {{QStringLiteral("value"), choice.value}, {QStringLiteral("label"), choice.label}};
                known |= choice.value == value;
            }
            // A value from another editor stays as it is, shown as itself.
            if (!known)
                choices << QVariantMap {{QStringLiteral("value"), value}, {QStringLiteral("label"), value}};
            item.insert(QStringLiteral("choices"), choices);
            item.insert(QStringLiteral("value"), value);
            break;
        }
        case Type::Password: {
            const QString flags = data.value(field.key + QLatin1String("-flags"));
            item.insert(QStringLiteral("value"), QString());
            item.insert(QStringLiteral("storage"), flags.isEmpty() ? uint(SecretAgentOwned) : flags.toUInt());
            break;
        }
        default:
            item.insert(QStringLiteral("value"), stored);
            break;
        }
        list << item;
    }
    return list;
}

bool apply(const QString &kind, const QVariantMap &values, const QVariantMap &storage, const QVariantMap &secrets,
           bool shared, QMap<QString, QString> &data, QMap<QString, QString> &vpnSecrets, QString *error)
{
    const QList<Field> fields = VpnSchema::fields(kind);
    if (fields.isEmpty()) {
        if (error)
            *error = QObject::tr("This kind of VPN can't be set up by hand.");
        return false;
    }

    for (const Field &field : fields) {
        // A hidden field keeps what the profile has, as the editors leave it alone too.
        if (!shown(field, fields, values))
            continue;
        const QVariant raw = values.value(field.key);
        const QString text = raw.toString().trimmed();

        switch (field.type) {
        case Type::Bool: {
            const bool on = raw.toBool() != field.inverted;
            const bool written = field.defaultValue.isEmpty() ? on : (on ? field.trueValue : field.falseValue) != field.defaultValue;
            if (on)
                data.insert(field.key, field.trueValue);
            else if (!field.falseValue.isEmpty() && written)
                data.insert(field.key, field.falseValue);
            else
                data.remove(field.key);
            for (auto it = field.alsoSet.cbegin(); it != field.alsoSet.cend(); ++it) {
                if (on)
                    data.insert(it.key(), it.value());
                else
                    data.remove(it.key());
            }
            break;
        }
        case Type::Choice:
            if (field.exclusiveKeys) {
                for (const Choice &choice : field.choices) {
                    if (!choice.value.isEmpty())
                        data.remove(choice.value);
                }
                if (!text.isEmpty())
                    data.insert(text, QStringLiteral("yes"));
            } else if (text.isEmpty() || text == field.defaultValue) {
                data.remove(field.key);
            } else {
                data.insert(field.key, text);
            }
            break;
        case Type::Password: {
            const uint flags = storage.contains(field.key) ? storage.value(field.key).toUInt()
                                                            : uint(shared ? SecretSystemOwned : SecretAgentOwned);
            data.insert(field.key + QLatin1String("-flags"), QString::number(flags));
            if (!field.legacyTypeKey.isEmpty())
                data.insert(field.legacyTypeKey, legacyType(flags));
            const QString typed = secrets.value(field.key).toString();
            if (flags & (SecretNotSaved | SecretNotRequired))
                vpnSecrets.remove(field.key);
            else if (!typed.isEmpty())
                vpnSecrets.insert(field.key, typed);
            break;
        }
        case Type::Number:
            if (!text.isEmpty()) {
                bool ok = false;
                const int number = text.toInt(&ok);
                if (!ok || (field.maximum > field.minimum && (number < field.minimum || number > field.maximum))) {
                    if (error)
                        *error = QObject::tr("%1 must be a number from %2 to %3.").arg(field.label).arg(field.minimum).arg(field.maximum);
                    return false;
                }
            }
            Q_FALLTHROUGH();
        default:
            if (text.isEmpty() && field.required) {
                if (error)
                    *error = QObject::tr("Enter the %1.").arg(field.label.toLower());
                return false;
            }
            if (text.isEmpty())
                data.remove(field.key);
            else
                data.insert(field.key, text);
            break;
        }
    }
    return true;
}

} // namespace VpnSchema
