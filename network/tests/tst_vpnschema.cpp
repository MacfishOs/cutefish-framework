// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../vpnschema.h"

#include <QTest>

using namespace NetworkDBus;

static QVariantMap formItem(const QString &kind, const QMap<QString, QString> &data, const QString &key)
{
    const QVariantList fields = VpnSchema::formFields(kind, data);
    for (const QVariant &item : fields) {
        if (item.toMap().value(QStringLiteral("key")) == key)
            return item.toMap();
    }
    return {};
}

class VpnSchemaTest : public QObject
{
    Q_OBJECT

private slots:
    void everyKindHasAServerField()
    {
        QCOMPARE(VpnSchema::kinds().size(), 11);
        for (const QString &kind : VpnSchema::kinds()) {
            const QList<VpnSchema::Field> fields = VpnSchema::fields(kind);
            QVERIFY2(!fields.isEmpty(), qPrintable(kind));
            bool required = false;
            for (const VpnSchema::Field &field : fields) {
                required |= field.required;
                QVERIFY2(!field.key.isEmpty() && !field.label.isEmpty(), qPrintable(kind));
                if (field.type == VpnSchema::Type::Choice)
                    QVERIFY2(field.choices.size() >= 2, qPrintable(kind + QLatin1Char('/') + field.key));
            }
            QVERIFY2(required, qPrintable(kind));
        }
        QVERIFY(VpnSchema::fields(QStringLiteral("wireguard")).isEmpty());
        QVERIFY(VpnSchema::exportable(QStringLiteral("openvpn")));
        QVERIFY(!VpnSchema::exportable(QStringLiteral("pptp")));
    }

    void requiredFieldsAreChecked()
    {
        QMap<QString, QString> data, secrets;
        QString error;
        QVERIFY(!VpnSchema::apply(QStringLiteral("openvpn"), {}, {}, {}, false, data, secrets, &error));
        QVERIFY(error.contains(QStringLiteral("server")));
        QVERIFY(!VpnSchema::apply(QStringLiteral("wireguard"), {}, {}, {}, false, data, secrets, &error));
        QVERIFY(!VpnSchema::apply(QStringLiteral("openvpn"),
                                  {{QStringLiteral("remote"), QStringLiteral("vpn.example.com")},
                                   {QStringLiteral("port"), QStringLiteral("70000")}},
                                  {}, {}, false, data, secrets, &error));
        QVERIFY(error.contains(QStringLiteral("Port")));
    }

    void openVpnRoundTrip()
    {
        QMap<QString, QString> data, secrets;
        QString error;
        const QVariantMap values {
            {QStringLiteral("remote"), QStringLiteral(" vpn.example.com:1194 ")},
            {QStringLiteral("connection-type"), QStringLiteral("password-tls")},
            {QStringLiteral("username"), QStringLiteral("alice")},
            {QStringLiteral("ca"), QStringLiteral("/etc/ca.pem")},
            {QStringLiteral("cert"), QStringLiteral("/home/a/cert.pem")},
            {QStringLiteral("key"), QStringLiteral("/home/a/key.pem")},
            {QStringLiteral("proto-tcp"), true},
            {QStringLiteral("port"), QStringLiteral("443")},
            {QStringLiteral("compress"), QStringLiteral("lz4")},
            {QStringLiteral("dev-type"), QString()},
            // Hidden while the proxy is off: must not be written.
            {QStringLiteral("proxy-server"), QStringLiteral("proxy.example.com")},
        };
        QVERIFY2(VpnSchema::apply(QStringLiteral("openvpn"), values,
                                  {{QStringLiteral("cert-pass"), uint(SecretNotRequired)}},
                                  {{QStringLiteral("password"), QStringLiteral("s3cret")},
                                   {QStringLiteral("cert-pass"), QStringLiteral("ignored")}},
                                  false, data, secrets, &error),
                 qPrintable(error));
        QCOMPARE(data.value(QStringLiteral("remote")), QStringLiteral("vpn.example.com:1194"));
        QCOMPARE(data.value(QStringLiteral("connection-type")), QStringLiteral("password-tls"));
        QCOMPARE(data.value(QStringLiteral("proto-tcp")), QStringLiteral("yes"));
        QCOMPARE(data.value(QStringLiteral("port")), QStringLiteral("443"));
        QCOMPARE(data.value(QStringLiteral("compress")), QStringLiteral("lz4"));
        QVERIFY(!data.contains(QStringLiteral("dev-type")));
        QVERIFY(!data.contains(QStringLiteral("proxy-server")));
        QCOMPARE(data.value(QStringLiteral("password-flags")), QStringLiteral("1"));
        QCOMPARE(data.value(QStringLiteral("cert-pass-flags")), QStringLiteral("4"));
        QCOMPARE(secrets.value(QStringLiteral("password")), QStringLiteral("s3cret"));
        QVERIFY(!secrets.contains(QStringLiteral("cert-pass")));

        // Reading it back shows the same form.
        QCOMPARE(formItem(QStringLiteral("openvpn"), data, QStringLiteral("proto-tcp")).value(QStringLiteral("value")), true);
        QCOMPARE(formItem(QStringLiteral("openvpn"), data, QStringLiteral("compress")).value(QStringLiteral("value")), QStringLiteral("lz4"));
        QCOMPARE(formItem(QStringLiteral("openvpn"), data, QStringLiteral("cert-pass")).value(QStringLiteral("storage")).toUInt(), uint(SecretNotRequired));
        QCOMPARE(formItem(QStringLiteral("openvpn"), data, QStringLiteral("password")).value(QStringLiteral("storage")).toUInt(), uint(SecretAgentOwned));
        QCOMPARE(formItem(QStringLiteral("openvpn"), data, QStringLiteral("password")).value(QStringLiteral("value")), QString());

        // A shared profile's passwords belong to the system; "ask every time" keeps none.
        QMap<QString, QString> sharedData, sharedSecrets;
        QVERIFY(VpnSchema::apply(QStringLiteral("openvpn"), values, {{QStringLiteral("password"), uint(SecretNotSaved)}},
                                 {{QStringLiteral("password"), QStringLiteral("typed")}}, true, sharedData, sharedSecrets, &error));
        QCOMPARE(sharedData.value(QStringLiteral("password-flags")), QStringLiteral("2"));
        QCOMPARE(sharedData.value(QStringLiteral("cert-pass-flags")), QStringLiteral("0"));
        QVERIFY(sharedSecrets.isEmpty());
    }

    void keysTheFormDoesNotKnowStay()
    {
        QMap<QString, QString> data {{QStringLiteral("remote"), QStringLiteral("old.example.com")},
                                     {QStringLiteral("tls-remote"), QStringLiteral("legacy")},
                                     {QStringLiteral("cipher"), QStringLiteral("AES-256-GCM")}};
        QMap<QString, QString> secrets;
        QVERIFY(VpnSchema::apply(QStringLiteral("openvpn"),
                                 {{QStringLiteral("remote"), QStringLiteral("new.example.com")}, {QStringLiteral("cipher"), QString()}},
                                 {}, {}, false, data, secrets, nullptr));
        QCOMPARE(data.value(QStringLiteral("remote")), QStringLiteral("new.example.com"));
        QCOMPARE(data.value(QStringLiteral("tls-remote")), QStringLiteral("legacy"));
        QVERIFY(!data.contains(QStringLiteral("cipher")));
        // A choice value from another editor is offered as itself.
        const QVariantList choices = formItem(QStringLiteral("openvpn"), {{QStringLiteral("auth"), QStringLiteral("whirlpool")}},
                                              QStringLiteral("auth")).value(QStringLiteral("choices")).toList();
        QCOMPARE(choices.last().toMap().value(QStringLiteral("value")), QStringLiteral("whirlpool"));
    }

    void pppOptionsAreInvertedAndExclusive()
    {
        QMap<QString, QString> data, secrets;
        QVERIFY(VpnSchema::apply(QStringLiteral("pptp"),
                                 {{QStringLiteral("gateway"), QStringLiteral("pptp.example.com")},
                                  {QStringLiteral("refuse-pap"), false},
                                  {QStringLiteral("refuse-chap"), true},
                                  {QStringLiteral("nobsdcomp"), false},
                                  {QStringLiteral("require-mppe"), QStringLiteral("require-mppe-128")},
                                  {QStringLiteral("lcp-echo-interval"), true}},
                                 {}, {}, false, data, secrets, nullptr));
        QCOMPARE(data.value(QStringLiteral("refuse-pap")), QStringLiteral("yes"));
        QVERIFY(!data.contains(QStringLiteral("refuse-chap")));
        QCOMPARE(data.value(QStringLiteral("nobsdcomp")), QStringLiteral("yes"));
        QCOMPARE(data.value(QStringLiteral("require-mppe-128")), QStringLiteral("yes"));
        QVERIFY(!data.contains(QStringLiteral("require-mppe")));
        QCOMPARE(data.value(QStringLiteral("lcp-echo-interval")), QStringLiteral("30"));
        QCOMPARE(data.value(QStringLiteral("lcp-echo-failure")), QStringLiteral("5"));

        QCOMPARE(formItem(QStringLiteral("pptp"), data, QStringLiteral("refuse-pap")).value(QStringLiteral("value")), false);
        QCOMPARE(formItem(QStringLiteral("pptp"), data, QStringLiteral("refuse-chap")).value(QStringLiteral("value")), true);
        QCOMPARE(formItem(QStringLiteral("pptp"), data, QStringLiteral("require-mppe")).value(QStringLiteral("value")),
                 QStringLiteral("require-mppe-128"));
        QCOMPARE(formItem(QStringLiteral("pptp"), data, QStringLiteral("lcp-echo-interval")).value(QStringLiteral("value")), true);

        // Switching encryption to "any" replaces the 128-bit key, and the echo off removes both keys.
        QVERIFY(VpnSchema::apply(QStringLiteral("pptp"),
                                 {{QStringLiteral("gateway"), QStringLiteral("pptp.example.com")},
                                  {QStringLiteral("require-mppe"), QStringLiteral("require-mppe")},
                                  {QStringLiteral("lcp-echo-interval"), false}},
                                 {}, {}, false, data, secrets, nullptr));
        QCOMPARE(data.value(QStringLiteral("require-mppe")), QStringLiteral("yes"));
        QVERIFY(!data.contains(QStringLiteral("require-mppe-128")));
        QVERIFY(!data.contains(QStringLiteral("lcp-echo-interval")));
        QVERIFY(!data.contains(QStringLiteral("lcp-echo-failure")));
    }

    void sstpDefaultsAreYes()
    {
        // tls-ext and refuse-pap default to yes: only turning them off writes a key.
        QCOMPARE(formItem(QStringLiteral("sstp"), {}, QStringLiteral("tls-ext")).value(QStringLiteral("value")), true);
        QCOMPARE(formItem(QStringLiteral("sstp"), {}, QStringLiteral("refuse-pap")).value(QStringLiteral("value")), false);
        QMap<QString, QString> data, secrets;
        QVERIFY(VpnSchema::apply(QStringLiteral("sstp"),
                                 {{QStringLiteral("gateway"), QStringLiteral("sstp.example.com")},
                                  {QStringLiteral("tls-ext"), false},
                                  {QStringLiteral("refuse-pap"), true},
                                  {QStringLiteral("refuse-chap"), false}},
                                 {}, {}, false, data, secrets, nullptr));
        QCOMPARE(data.value(QStringLiteral("tls-ext")), QStringLiteral("no"));
        QVERIFY(!data.contains(QStringLiteral("refuse-pap")));
        QCOMPARE(data.value(QStringLiteral("refuse-chap")), QStringLiteral("yes"));
        QCOMPARE(formItem(QStringLiteral("sstp"), data, QStringLiteral("tls-ext")).value(QStringLiteral("value")), false);
    }

    void legacyTypeKeysFollowTheFlags()
    {
        QMap<QString, QString> data, secrets;
        QVERIFY(VpnSchema::apply(QStringLiteral("vpnc"),
                                 {{QStringLiteral("IPSec gateway"), QStringLiteral("vpn.example.com")},
                                  {QStringLiteral("IPSec ID"), QStringLiteral("group")},
                                  {QStringLiteral("IKE Authmode"), true}},
                                 {{QStringLiteral("Xauth password"), uint(SecretNotSaved)},
                                  {QStringLiteral("IPSec secret"), uint(SecretNotRequired)}},
                                 {{QStringLiteral("Xauth password"), QStringLiteral("x")}}, false, data, secrets, nullptr));
        QCOMPARE(data.value(QStringLiteral("xauth-password-type")), QStringLiteral("ask"));
        QCOMPARE(data.value(QStringLiteral("ipsec-secret-type")), QStringLiteral("unused"));
        QCOMPARE(data.value(QStringLiteral("IKE Authmode")), QStringLiteral("hybrid"));
        QVERIFY(secrets.isEmpty());
        QVERIFY(VpnSchema::apply(QStringLiteral("libreswan"),
                                 {{QStringLiteral("right"), QStringLiteral("vpn.example.com")}, {QStringLiteral("rekey"), true}},
                                 {{QStringLiteral("pskvalue"), uint(SecretAgentOwned)}},
                                 {{QStringLiteral("pskvalue"), QStringLiteral("group")}}, false, data, secrets, nullptr));
        QCOMPARE(data.value(QStringLiteral("pskinputmodes")), QStringLiteral("save"));
        QCOMPARE(data.value(QStringLiteral("rekey")), QStringLiteral("no"));
        QCOMPARE(secrets.value(QStringLiteral("pskvalue")), QStringLiteral("group"));
    }

    void fortiOtpFlagsAreNumbers()
    {
        QMap<QString, QString> data, secrets;
        QVERIFY(VpnSchema::apply(QStringLiteral("fortisslvpn"),
                                 {{QStringLiteral("gateway"), QStringLiteral("f.example.com")}, {QStringLiteral("otp-flags"), true},
                                  {QStringLiteral("2fa-flags"), true}},
                                 {}, {}, false, data, secrets, nullptr));
        QCOMPARE(data.value(QStringLiteral("otp-flags")), QStringLiteral("2"));
        QCOMPARE(data.value(QStringLiteral("2fa-flags")), QStringLiteral("1"));
        QCOMPARE(formItem(QStringLiteral("fortisslvpn"), data, QStringLiteral("otp-flags")).value(QStringLiteral("value")), true);
        QVERIFY(VpnSchema::apply(QStringLiteral("fortisslvpn"),
                                 {{QStringLiteral("gateway"), QStringLiteral("f.example.com")}, {QStringLiteral("otp-flags"), false}},
                                 {}, {}, false, data, secrets, nullptr));
        // Off is the absent key, which the plugin reads as flags 0.
        QVERIFY(!data.contains(QStringLiteral("otp-flags")));
        QVERIFY(!data.contains(QStringLiteral("2fa-flags")));
    }
};

QTEST_GUILESS_MAIN(VpnSchemaTest)

#include "tst_vpnschema.moc"
