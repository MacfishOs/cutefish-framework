#include "../vpnutils.h"

#include <QTemporaryDir>
#include <QTest>

static QStringList keys(const QList<VpnUtils::SecretField> &fields)
{
    QStringList keys;
    for (const auto &field : fields)
        keys << field.key;
    return keys;
}

static const QString openvpn = QStringLiteral("org.freedesktop.NetworkManager.openvpn");

class VpnUtilsTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        QVERIFY(plugins.isValid());
        QFile name(plugins.filePath(QStringLiteral("nm-openvpn-service.name")));
        QVERIFY(name.open(QIODevice::WriteOnly));
        name.write("[VPN Connection]\nname=openvpn\nservice=org.freedesktop.NetworkManager.openvpn\n"
                   "program=/usr/libexec/nm-openvpn-service\n\n[libnm]\nplugin=libnm-vpn-plugin-openvpn.so\n");
        name.close();
        VpnUtils::setPluginDirectories({plugins.path()});
    }

    void readsInstalledPlugins()
    {
        QCOMPARE(VpnUtils::installedPlugins(), QStringList {QStringLiteral("openvpn")});
        QVERIFY(VpnUtils::pluginInstalled(openvpn));
        QVERIFY(!VpnUtils::pluginInstalled(QStringLiteral("org.freedesktop.NetworkManager.l2tp")));
        QCOMPARE(VpnUtils::package(QStringLiteral("l2tp")), QStringLiteral("network-manager-l2tp"));
    }

    void openVpnAsksForWhatItsTypeNeeds()
    {
        QCOMPARE(keys(VpnUtils::secretFields(openvpn, {{QStringLiteral("connection-type"), QStringLiteral("password")}}, {})),
                 QStringList {QStringLiteral("password")});
        QCOMPARE(keys(VpnUtils::secretFields(openvpn, {{QStringLiteral("connection-type"), QStringLiteral("password-tls")},
                                                       {QStringLiteral("key"), QStringLiteral("/k.pem")}},
                                             {})),
                 (QStringList {QStringLiteral("password"), QStringLiteral("cert-pass")}));
        // A key file whose password is not required, and no key at all, ask nothing.
        QVERIFY(VpnUtils::secretFields(openvpn, {{QStringLiteral("connection-type"), QStringLiteral("tls")},
                                                 {QStringLiteral("key"), QStringLiteral("/k.pem")},
                                                 {QStringLiteral("cert-pass-flags"), QStringLiteral("4")}},
                                       {})
                        .isEmpty());
        QVERIFY(VpnUtils::secretFields(openvpn, {{QStringLiteral("connection-type"), QStringLiteral("static-key")}}, {})
                        .isEmpty());
    }

    void hintsNameTheSecret()
    {
        const auto fields = VpnUtils::secretFields(openvpn, {{QStringLiteral("connection-type"), QStringLiteral("password")}},
                                                   {QStringLiteral("x-vpn-message:Enter your token"),
                                                    QStringLiteral("x-dynamic-challenge-echo:Token")});
        QCOMPARE(fields.size(), 1);
        QCOMPARE(fields.first().key, QStringLiteral("x-dynamic-challenge-echo:Token"));
        QCOMPARE(fields.first().label, QStringLiteral("Token"));
        QVERIFY(!fields.first().secret);
        QCOMPARE(VpnUtils::hintMessage({QStringLiteral("x-vpn-message:Enter your token")}),
                 QStringLiteral("Enter your token"));
    }

    void otherPlugins()
    {
        QCOMPARE(keys(VpnUtils::secretFields(QStringLiteral("org.freedesktop.NetworkManager.l2tp"),
                                             {{QStringLiteral("machine-auth-type"), QStringLiteral("psk")}}, {})),
                 (QStringList {QStringLiteral("password"), QStringLiteral("ipsec-psk")}));
        QCOMPARE(keys(VpnUtils::secretFields(QStringLiteral("org.freedesktop.NetworkManager.vpnc"), {}, {})),
                 (QStringList {QStringLiteral("Xauth password"), QStringLiteral("IPSec secret")}));
        QVERIFY(VpnUtils::secretFields(QStringLiteral("org.freedesktop.NetworkManager.strongswan"),
                                       {{QStringLiteral("method"), QStringLiteral("agent")}}, {})
                        .isEmpty());
        QCOMPARE(keys(VpnUtils::secretFields(QStringLiteral("org.freedesktop.NetworkManager.pptp"), {}, {})),
                 QStringList {QStringLiteral("password")});
    }

    void userNames()
    {
        QCOMPARE(VpnUtils::userNameKey(openvpn, {{QStringLiteral("connection-type"), QStringLiteral("password")}}),
                 QStringLiteral("username"));
        QVERIFY(VpnUtils::userNameKey(openvpn, {{QStringLiteral("connection-type"), QStringLiteral("tls")}}).isEmpty());
        QCOMPARE(VpnUtils::userNameKey(QStringLiteral("org.freedesktop.NetworkManager.l2tp"), {}), QStringLiteral("user"));
        QCOMPARE(VpnUtils::userNameKey(QStringLiteral("org.freedesktop.NetworkManager.vpnc"), {}),
                 QStringLiteral("Xauth username"));
    }

    void profilesByHand()
    {
        QString error;
        NMVariantMapMap openvpnProfile = VpnUtils::manualProfile({{QStringLiteral("kind"), QStringLiteral("openvpn")},
                                                                  {QStringLiteral("server"), QStringLiteral("vpn.example.com")},
                                                                  {QStringLiteral("authentication"), QStringLiteral("certificates")},
                                                                  {QStringLiteral("caFile"), QStringLiteral("/ca.pem")},
                                                                  {QStringLiteral("certFile"), QStringLiteral("/me.pem")},
                                                                  {QStringLiteral("keyFile"), QStringLiteral("/me.key")}},
                                                                 &error);
        QVERIFY2(!openvpnProfile.isEmpty(), qPrintable(error));
        QCOMPARE(openvpnProfile.value(QStringLiteral("connection")).value(QStringLiteral("id")).toString(),
                 QStringLiteral("vpn.example.com"));
        const auto data = openvpnProfile.value(QStringLiteral("vpn")).value(QStringLiteral("data")).value<QMap<QString, QString>>();
        QCOMPARE(data.value(QStringLiteral("connection-type")), QStringLiteral("tls"));
        QCOMPARE(data.value(QStringLiteral("key")), QStringLiteral("/me.key"));
        QCOMPARE(data.value(QStringLiteral("cert-pass-flags")), QStringLiteral("1"));

        const NMVariantMapMap wireguard = VpnUtils::manualProfile({{QStringLiteral("kind"), QStringLiteral("wireguard")},
                                                                   {QStringLiteral("name"), QStringLiteral("Home")},
                                                                   {QStringLiteral("server"), QStringLiteral("vpn.example.com")},
                                                                   {QStringLiteral("privateKey"), QStringLiteral("yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk=")},
                                                                   {QStringLiteral("publicKey"), QStringLiteral("xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg=")},
                                                                   {QStringLiteral("address"), QStringLiteral("10.0.0.2/24")}},
                                                                  &error);
        QVERIFY2(!wireguard.isEmpty(), qPrintable(error));
        const auto peers = wireguard.value(QStringLiteral("wireguard")).value(QStringLiteral("peers")).value<QList<QVariantMap>>();
        QCOMPARE(peers.first().value(QStringLiteral("endpoint")).toString(), QStringLiteral("vpn.example.com:51820"));
        QCOMPARE(peers.first().value(QStringLiteral("allowed-ips")).toStringList(),
                 (QStringList {QStringLiteral("0.0.0.0/0"), QStringLiteral("::/0")}));

        const NMVariantMapMap ipv6Server = VpnUtils::manualProfile({{QStringLiteral("kind"), QStringLiteral("wireguard")},
                                                                    {QStringLiteral("server"), QStringLiteral("2001:db8::1")},
                                                                    {QStringLiteral("privateKey"), QStringLiteral("yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk=")},
                                                                    {QStringLiteral("publicKey"), QStringLiteral("xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg=")},
                                                                    {QStringLiteral("address"), QStringLiteral("10.0.0.2/24")}},
                                                                   &error);
        QVERIFY2(!ipv6Server.isEmpty(), qPrintable(error));
        QCOMPARE(ipv6Server.value(QStringLiteral("wireguard")).value(QStringLiteral("peers")).value<QList<QVariantMap>>()
                         .first().value(QStringLiteral("endpoint")).toString(),
                 QStringLiteral("[2001:db8::1]:51820"));

        QVERIFY(VpnUtils::manualProfile({{QStringLiteral("kind"), QStringLiteral("pptp")},
                                         {QStringLiteral("server"), QStringLiteral("x")}},
                                        &error)
                        .isEmpty());
        QVERIFY(!error.isEmpty());
    }

    void failures()
    {
        // NM_ACTIVE_CONNECTION_STATE_REASON_USER_DISCONNECTED and _NO_SECRETS say nothing.
        QVERIFY(VpnUtils::failureMessage(2).isEmpty());
        QVERIFY(VpnUtils::failureMessage(9).isEmpty());
        QVERIFY(!VpnUtils::failureMessage(10).isEmpty());
    }

    void readsWireGuard()
    {
        const QByteArray config = "[Interface]\n"
                                  "PrivateKey = yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk=  # mine\n"
                                  "Address = 10.99.0.2/24, fd00::2\n"
                                  "DNS = 10.99.0.1, corp.example\n"
                                  "\n"
                                  "[Peer]\n"
                                  "PublicKey = xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg=\n"
                                  "AllowedIPs = 10.99.0.0/24, 10.98.0.0/24\n"
                                  "Endpoint = vpn.example.com:51820\n"
                                  "PersistentKeepalive = 25\n";
        QVERIFY(VpnUtils::isWireGuardConfig(config));

        QString error;
        const NMVariantMapMap profile = VpnUtils::wireGuardProfile(config, QStringLiteral("Cafe (Tokyo)"), &error);
        QVERIFY2(!profile.isEmpty(), qPrintable(error));
        QCOMPARE(profile.value(QStringLiteral("connection")).value(QStringLiteral("id")).toString(), QStringLiteral("Cafe (Tokyo)"));
        QCOMPARE(profile.value(QStringLiteral("connection")).value(QStringLiteral("interface-name")).toString(),
                 QStringLiteral("CafeTokyo"));

        const auto peers = profile.value(QStringLiteral("wireguard")).value(QStringLiteral("peers")).value<QList<QVariantMap>>();
        QCOMPARE(peers.size(), 1);
        QCOMPARE(peers.first().value(QStringLiteral("allowed-ips")).toStringList(),
                 (QStringList {QStringLiteral("10.99.0.0/24"), QStringLiteral("10.98.0.0/24")}));
        QCOMPARE(peers.first().value(QStringLiteral("persistent-keepalive")).metaType(), QMetaType::fromType<uint>());

        const QVariantMap ipv4 = profile.value(QStringLiteral("ipv4"));
        QCOMPARE(ipv4.value(QStringLiteral("method")).toString(), QStringLiteral("manual"));
        const auto addresses = ipv4.value(QStringLiteral("address-data")).value<QList<QVariantMap>>();
        QCOMPARE(addresses.first().value(QStringLiteral("prefix")).toUInt(), 24u);
        QCOMPARE(ipv4.value(QStringLiteral("dns-search")).toStringList(), QStringList {QStringLiteral("corp.example")});
        QCOMPARE(profile.value(QStringLiteral("ipv6")).value(QStringLiteral("address-data")).value<QList<QVariantMap>>()
                         .first().value(QStringLiteral("prefix")).toUInt(),
                 128u);
    }

    void rejectsWhatIsNotWireGuard()
    {
        QString error;
        QVERIFY(VpnUtils::wireGuardProfile("[Interface]\nPrivateKey = short\n", QStringLiteral("x"), &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(VpnUtils::wireGuardProfile("[Interface]\nPrivateKey = yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk=\nFoo = 1\n",
                                           QStringLiteral("x"), &error)
                        .isEmpty());
        QVERIFY(!VpnUtils::isWireGuardConfig("client\nremote vpn.example.com 1194\n"));
    }

private:
    QTemporaryDir plugins;
};

QTEST_GUILESS_MAIN(VpnUtilsTest)

#include "tst_vpnutils.moc"
