// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "../iputils.h"

#include <QTest>
#include <QtEndian>

static QVariant addresses(const QList<QPair<QString, uint>> &entries)
{
    QList<QVariantMap> list;
    for (const auto &entry : entries)
        list << QVariantMap {{QStringLiteral("address"), entry.first}, {QStringLiteral("prefix"), entry.second}};
    return QVariant::fromValue(list);
}

static NMVariantMapMap automaticProfile()
{
    return {
        {QStringLiteral("connection"), {{QStringLiteral("id"), QStringLiteral("Home")}}},
        {QStringLiteral("ipv4"),
         {{QStringLiteral("method"), QStringLiteral("auto")},
          {QStringLiteral("address-data"), addresses({})},
          {QStringLiteral("addresses"), QVariant::fromValue(QList<QList<uint>>())}}},
        {QStringLiteral("ipv6"), {{QStringLiteral("method"), QStringLiteral("auto")}}},
    };
}

static QStringList methodValues(const QVariant &methods)
{
    QStringList values;
    for (const QVariant &method : methods.toList())
        values << method.toMap().value(QStringLiteral("value")).toString();
    return values;
}

class IpUtilsTest : public QObject
{
    Q_OBJECT

private slots:
    void readsAnAutomaticProfile()
    {
        const QVariantMap form = IpUtils::form(automaticProfile());
        QCOMPARE(form.value(QStringLiteral("ipv4Method")).toString(), QStringLiteral("auto"));
        QCOMPARE(form.value(QStringLiteral("ipv4Address")).toString(), QString());
        QCOMPARE(form.value(QStringLiteral("automaticDns")).toBool(), true);
        QCOMPARE(form.value(QStringLiteral("dns")).toString(), QString());
        QCOMPARE(methodValues(form.value(QStringLiteral("ipv4Methods"))),
                 QStringList({QStringLiteral("auto"), QStringLiteral("manual"), QStringLiteral("disabled")}));

        // A profile without an ipv6 setting is "auto" once NetworkManager normalizes it.
        NMVariantMapMap profile = automaticProfile();
        profile.remove(QStringLiteral("ipv6"));
        QCOMPARE(IpUtils::form(profile).value(QStringLiteral("ipv6Method")).toString(), QStringLiteral("auto"));
    }

    void readsAManualProfile()
    {
        NMVariantMapMap profile = automaticProfile();
        profile[QStringLiteral("ipv4")] = {
            {QStringLiteral("method"), QStringLiteral("manual")},
            {QStringLiteral("address-data"), addresses({{QStringLiteral("192.168.1.20"), 24}, {QStringLiteral("10.0.0.2"), 8}})},
            {QStringLiteral("gateway"), QStringLiteral("192.168.1.1")},
            {QStringLiteral("dns"), QVariant::fromValue(QList<uint> {qToBigEndian(0x01010101u)})},
            {QStringLiteral("ignore-auto-dns"), true},
        };
        QByteArray server(16, 0);
        server[0] = char(0x26);
        server[1] = char(0x06);
        server[15] = 1;
        profile[QStringLiteral("ipv6")] = {
            {QStringLiteral("method"), QStringLiteral("ignore")},
            {QStringLiteral("dns"), QVariant::fromValue(QList<QByteArray> {server})},
        };

        const QVariantMap form = IpUtils::form(profile);
        QCOMPARE(form.value(QStringLiteral("ipv4Address")).toString(), QStringLiteral("192.168.1.20"));
        QCOMPARE(form.value(QStringLiteral("ipv4Netmask")).toString(), QStringLiteral("255.255.255.0"));
        QCOMPARE(form.value(QStringLiteral("ipv4Gateway")).toString(), QStringLiteral("192.168.1.1"));
        QCOMPARE(form.value(QStringLiteral("automaticDns")).toBool(), false);
        QCOMPARE(form.value(QStringLiteral("dns")).toString(), QStringLiteral("1.1.1.1, 2606::1"));
        // A method the form doesn't offer stays a choice while the profile has it.
        QCOMPARE(methodValues(form.value(QStringLiteral("ipv6Methods"))),
                 QStringList({QStringLiteral("auto"), QStringLiteral("manual"), QStringLiteral("disabled"),
                              QStringLiteral("ignore")}));
    }

    void prefersDnsData()
    {
        NMVariantMapMap profile = automaticProfile();
        profile[QStringLiteral("ipv4")].insert(QStringLiteral("dns"), QVariant::fromValue(QList<uint> {qToBigEndian(0x01010101u)}));
        profile[QStringLiteral("ipv4")].insert(QStringLiteral("dns-data"),
                                               QStringList {QStringLiteral("1.1.1.1#one.one.one.one")});
        QCOMPARE(IpUtils::form(profile).value(QStringLiteral("dns")).toString(), QStringLiteral("1.1.1.1#one.one.one.one"));
    }

    void checksTheForm_data()
    {
        QTest::addColumn<QString>("method");
        QTest::addColumn<QString>("address");
        QTest::addColumn<QString>("netmask");
        QTest::addColumn<QString>("gateway");
        QTest::addColumn<QString>("dns");
        QTest::addColumn<bool>("complete");
        QTest::addColumn<bool>("message");

        QTest::newRow("automatic") << "auto" << "" << "" << "" << "" << true << false;
        QTest::newRow("manual, empty") << "manual" << "" << "" << "" << "" << false << false;
        QTest::newRow("manual, no netmask") << "manual" << "192.168.1.20" << "" << "" << "" << false << false;
        QTest::newRow("manual") << "manual" << "192.168.1.20" << "255.255.255.0" << "192.168.1.1" << "" << true << false;
        QTest::newRow("prefix length") << "manual" << "192.168.1.20" << "24" << "" << "" << true << false;
        QTest::newRow("prefix with slash") << "manual" << "192.168.1.20" << "/24" << "" << "" << true << false;
        QTest::newRow("short address") << "manual" << "192.168.1" << "24" << "" << "" << false << true;
        QTest::newRow("octet too big") << "manual" << "192.168.1.256" << "24" << "" << "" << false << true;
        QTest::newRow("no host") << "manual" << "0.0.0.0" << "24" << "" << "" << false << true;
        QTest::newRow("holes in the netmask") << "manual" << "192.168.1.20" << "255.0.255.0" << "" << "" << false << true;
        QTest::newRow("prefix too long") << "manual" << "192.168.1.20" << "33" << "" << "" << false << true;
        QTest::newRow("IPv6 router") << "manual" << "192.168.1.20" << "24" << "fe80::1" << "" << false << true;
        QTest::newRow("servers") << "auto" << "" << "" << "" << "1.1.1.1, 8.8.8.8 2606:4700::1111" << true << false;
        QTest::newRow("server name") << "auto" << "" << "" << "" << "1.1.1.1#one.one.one.one" << true << false;
        QTest::newRow("not a server") << "auto" << "" << "" << "" << "dns.example" << false << true;
        QTest::newRow("IPv4 server, IPv4 off") << "disabled" << "" << "" << "" << "1.1.1.1" << false << true;
        QTest::newRow("IPv6 server, IPv4 off") << "disabled" << "" << "" << "" << "2606:4700::1111" << true << false;
    }

    void checksTheForm()
    {
        QFETCH(QString, method);
        QFETCH(QString, address);
        QFETCH(QString, netmask);
        QFETCH(QString, gateway);
        QFETCH(QString, dns);
        QFETCH(bool, complete);
        QFETCH(bool, message);

        QVariantMap form = IpUtils::form(automaticProfile());
        form.insert(QStringLiteral("ipv4Method"), method);
        form.insert(QStringLiteral("ipv4Address"), address);
        form.insert(QStringLiteral("ipv4Netmask"), netmask);
        form.insert(QStringLiteral("ipv4Gateway"), gateway);
        form.insert(QStringLiteral("dns"), dns);

        const QVariantMap result = IpUtils::check(form);
        QCOMPARE(result.value(QStringLiteral("complete")).toBool(), complete);
        QCOMPARE(!result.value(QStringLiteral("message")).toString().isEmpty(), message);
    }

    void checksIpv6()
    {
        QVariantMap form = IpUtils::form(automaticProfile());
        form.insert(QStringLiteral("ipv6Method"), QStringLiteral("manual"));
        form.insert(QStringLiteral("ipv6Address"), QStringLiteral("2001:db8::20"));
        // The usual prefix length when none is given.
        QVERIFY(IpUtils::check(form).value(QStringLiteral("complete")).toBool());
        form.insert(QStringLiteral("ipv6Prefix"), QStringLiteral("129"));
        QVERIFY(!IpUtils::check(form).value(QStringLiteral("complete")).toBool());
        form.insert(QStringLiteral("ipv6Prefix"), QStringLiteral("64"));
        form.insert(QStringLiteral("ipv6Address"), QStringLiteral("fe80::1%eth0"));
        QVERIFY(!IpUtils::check(form).value(QStringLiteral("message")).toString().isEmpty());
        form.insert(QStringLiteral("ipv6Address"), QStringLiteral("192.168.1.20"));
        QVERIFY(!IpUtils::check(form).value(QStringLiteral("message")).toString().isEmpty());
    }

    void writesAManualSetup()
    {
        const NMVariantMapMap existing = automaticProfile();
        QVariantMap form = IpUtils::form(existing);
        form.insert(QStringLiteral("ipv4Method"), QStringLiteral("manual"));
        form.insert(QStringLiteral("ipv4Address"), QStringLiteral(" 192.168.1.20 "));
        form.insert(QStringLiteral("ipv4Netmask"), QStringLiteral("255.255.255.0"));
        form.insert(QStringLiteral("ipv4Gateway"), QStringLiteral("192.168.1.1"));

        bool changed = false;
        QString error;
        const NMVariantMapMap profile = IpUtils::settings(form, existing, &changed, &error);
        QVERIFY2(!profile.isEmpty(), qPrintable(error));
        QVERIFY(changed);

        const QVariantMap ipv4 = profile.value(QStringLiteral("ipv4"));
        QCOMPARE(ipv4.value(QStringLiteral("method")).toString(), QStringLiteral("manual"));
        QCOMPARE(ipv4.value(QStringLiteral("gateway")).toString(), QStringLiteral("192.168.1.1"));
        // The daemon would take the deprecated key over the new one.
        QVERIFY(!ipv4.contains(QStringLiteral("addresses")));
        QCOMPARE(ipv4.value(QStringLiteral("address-data")).metaType(), QMetaType::fromType<QList<QVariantMap>>());
        const QList<QVariantMap> data = ipv4.value(QStringLiteral("address-data")).value<QList<QVariantMap>>();
        QCOMPARE(data.size(), 1);
        QCOMPARE(data.first().value(QStringLiteral("address")).toString(), QStringLiteral("192.168.1.20"));
        QCOMPARE(data.first().value(QStringLiteral("prefix")).metaType(), QMetaType::fromType<uint>());
        QCOMPARE(data.first().value(QStringLiteral("prefix")).toUInt(), 24u);
        // Only what changed is written.
        QCOMPARE(profile.value(QStringLiteral("ipv6")), existing.value(QStringLiteral("ipv6")));
        QVERIFY(!ipv4.contains(QStringLiteral("ignore-auto-dns")));

        // The form read back from what was written is the same, so saving it again changes nothing.
        NMVariantMapMap stored = profile;
        IpUtils::settings(IpUtils::form(stored), stored, &changed, &error);
        QVERIFY(!changed);
    }

    void keepsFurtherAddresses()
    {
        NMVariantMapMap existing = automaticProfile();
        existing[QStringLiteral("ipv4")] = {
            {QStringLiteral("method"), QStringLiteral("manual")},
            {QStringLiteral("address-data"), addresses({{QStringLiteral("192.168.1.20"), 24}, {QStringLiteral("10.0.0.2"), 8}})},
        };
        QVariantMap form = IpUtils::form(existing);
        form.insert(QStringLiteral("ipv4Address"), QStringLiteral("192.168.1.30"));

        bool changed = false;
        const NMVariantMapMap profile = IpUtils::settings(form, existing, &changed, nullptr);
        const QList<QVariantMap> data = profile.value(QStringLiteral("ipv4")).value(QStringLiteral("address-data")).value<QList<QVariantMap>>();
        QCOMPARE(data.size(), 2);
        QCOMPARE(data.at(0).value(QStringLiteral("address")).toString(), QStringLiteral("192.168.1.30"));
        QCOMPARE(data.at(1).value(QStringLiteral("address")).toString(), QStringLiteral("10.0.0.2"));

        // Back to automatic, as GNOME's editor does it: no addresses of its own.
        form.insert(QStringLiteral("ipv4Method"), QStringLiteral("auto"));
        const NMVariantMapMap automatic = IpUtils::settings(form, existing, &changed, nullptr);
        QVERIFY(!automatic.value(QStringLiteral("ipv4")).contains(QStringLiteral("address-data")));
        QVERIFY(!automatic.value(QStringLiteral("ipv4")).contains(QStringLiteral("gateway")));
    }

    void writesNameServers()
    {
        const NMVariantMapMap existing = automaticProfile();
        QVariantMap form = IpUtils::form(existing);
        form.insert(QStringLiteral("automaticDns"), false);
        form.insert(QStringLiteral("dns"), QStringLiteral("1.1.1.1,8.8.8.8  2606:4700::1111"));

        bool changed = false;
        QString error;
        const NMVariantMapMap profile = IpUtils::settings(form, existing, &changed, &error);
        QVERIFY2(!profile.isEmpty(), qPrintable(error));
        QVERIFY(changed);

        const QVariantMap ipv4 = profile.value(QStringLiteral("ipv4"));
        QCOMPARE(ipv4.value(QStringLiteral("dns")).metaType(), QMetaType::fromType<QList<uint>>());
        QCOMPARE(ipv4.value(QStringLiteral("dns")).value<QList<uint>>(),
                 QList<uint>({qToBigEndian(0x01010101u), qToBigEndian(0x08080808u)}));
        QCOMPARE(ipv4.value(QStringLiteral("ignore-auto-dns")).metaType(), QMetaType::fromType<bool>());
        QCOMPARE(ipv4.value(QStringLiteral("ignore-auto-dns")).toBool(), true);

        const QVariantMap ipv6 = profile.value(QStringLiteral("ipv6"));
        QCOMPARE(ipv6.value(QStringLiteral("dns")).metaType(), QMetaType::fromType<QList<QByteArray>>());
        const QList<QByteArray> servers = ipv6.value(QStringLiteral("dns")).value<QList<QByteArray>>();
        QCOMPARE(servers.size(), 1);
        QCOMPARE(servers.first().toHex(), QByteArray("26064700000000000000000000001111"));
        QCOMPARE(ipv6.value(QStringLiteral("ignore-auto-dns")).toBool(), true);

        // The same servers written differently are no change.
        NMVariantMapMap stored = profile;
        form = IpUtils::form(stored);
        QCOMPARE(form.value(QStringLiteral("dns")).toString(), QStringLiteral("1.1.1.1, 8.8.8.8, 2606:4700::1111"));
        form.insert(QStringLiteral("dns"), QStringLiteral("1.1.1.1 8.8.8.8, 2606:4700::1111"));
        IpUtils::settings(form, stored, &changed, &error);
        QVERIFY(!changed);

        // Emptied, the keys go.
        form.insert(QStringLiteral("dns"), QString());
        form.insert(QStringLiteral("automaticDns"), true);
        const NMVariantMapMap cleared = IpUtils::settings(form, stored, &changed, &error);
        QVERIFY(changed);
        QVERIFY(!cleared.value(QStringLiteral("ipv4")).contains(QStringLiteral("dns")));
        QVERIFY(!cleared.value(QStringLiteral("ipv6")).contains(QStringLiteral("dns")));
        QCOMPARE(cleared.value(QStringLiteral("ipv4")).value(QStringLiteral("ignore-auto-dns")).toBool(), false);
    }

    void writesDnsDataWhereItCameFrom()
    {
        NMVariantMapMap existing = automaticProfile();
        existing[QStringLiteral("ipv4")].insert(QStringLiteral("dns"), QVariant::fromValue(QList<uint> {qToBigEndian(0x01010101u)}));
        existing[QStringLiteral("ipv4")].insert(QStringLiteral("dns-data"), QStringList {QStringLiteral("1.1.1.1")});
        QVariantMap form = IpUtils::form(existing);
        form.insert(QStringLiteral("dns"), QStringLiteral("9.9.9.9#dns.quad9.net"));

        bool changed = false;
        const NMVariantMapMap profile = IpUtils::settings(form, existing, &changed, nullptr);
        const QVariantMap ipv4 = profile.value(QStringLiteral("ipv4"));
        // Sending both, the daemon would take the old "dns".
        QVERIFY(!ipv4.contains(QStringLiteral("dns")));
        QCOMPARE(ipv4.value(QStringLiteral("dns-data")).toStringList(), QStringList {QStringLiteral("9.9.9.9#dns.quad9.net")});
    }

    void turnsAFamilyOff()
    {
        NMVariantMapMap existing = automaticProfile();
        existing[QStringLiteral("ipv6")].insert(QStringLiteral("dns-search"), QStringList {QStringLiteral("example.com")});
        QVariantMap form = IpUtils::form(existing);
        form.insert(QStringLiteral("ipv6Method"), QStringLiteral("disabled"));

        bool changed = false;
        QString error;
        const NMVariantMapMap profile = IpUtils::settings(form, existing, &changed, &error);
        QVERIFY2(!profile.isEmpty(), qPrintable(error));
        const QVariantMap ipv6 = profile.value(QStringLiteral("ipv6"));
        QCOMPARE(ipv6.value(QStringLiteral("method")).toString(), QStringLiteral("disabled"));
        // NetworkManager refuses search domains for a family that is off.
        QVERIFY(!ipv6.contains(QStringLiteral("dns-search")));
    }

    void refusesAnIncompleteForm()
    {
        QVariantMap form = IpUtils::form(automaticProfile());
        form.insert(QStringLiteral("ipv4Method"), QStringLiteral("manual"));
        bool changed = true;
        QString error;
        QVERIFY(IpUtils::settings(form, automaticProfile(), &changed, &error).isEmpty());
        QVERIFY(!error.isEmpty());
    }
};

QTEST_GUILESS_MAIN(IpUtilsTest)
#include "tst_iputils.moc"
