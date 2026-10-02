#include "../wifiutils.h"

#include <QTest>

class WifiUtilsTest : public QObject
{
    Q_OBJECT

private slots:
    void ssidLabels()
    {
        QCOMPARE(WifiUtils::ssidLabel(QByteArray("Café")), QStringLiteral("Café"));
        // GB18030 bytes of "中文", which a zh_CN system tries after UTF-8.
        QCOMPARE(WifiUtils::ssidLabel(QByteArray("\xd6\xd0\xce\xc4")), QStringLiteral("中文"));
        QCOMPARE(WifiUtils::ssidLabel(QByteArray("Lab\0", 4)), QStringLiteral("Lab"));
        QCOMPARE(WifiUtils::ssidLabel(QByteArray("a\nb")), QStringLiteral("a�b"));
    }

    void hiddenSsids()
    {
        QVERIFY(WifiUtils::ssidHidden(QByteArray()));
        QVERIFY(WifiUtils::ssidHidden(QByteArray("\0\0\0", 3)));
        QVERIFY(WifiUtils::ssidHidden(QByteArray(" ")));
        QVERIFY(!WifiUtils::ssidHidden(QByteArray("  ")));
        QVERIFY(!WifiUtils::ssidHidden(QByteArray("Home")));
    }

    void security_data()
    {
        QTest::addColumn<uint>("capabilities");
        QTest::addColumn<uint>("flags");
        QTest::addColumn<uint>("wpa");
        QTest::addColumn<uint>("rsn");
        QTest::addColumn<int>("expected");

        // WEP40, WEP104, TKIP, CCMP, WPA and RSN.
        const uint caps = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20;
        const uint ccmp = 0x8 | 0x80;
        QTest::newRow("open") << caps << 0u << 0u << 0u << int(Enums::NoneSecurity);
        QTest::newRow("wep") << caps << 1u << 0u << 0u << int(Enums::StaticWep);
        QTest::newRow("wpa2") << caps << 1u << 0u << (ccmp | 0x100) << int(Enums::Wpa2Psk);
        QTest::newRow("wpa2/wpa3") << caps << 1u << 0u << (ccmp | 0x100 | 0x400) << int(Enums::SAE);
        QTest::newRow("enterprise") << caps << 1u << 0u << (ccmp | 0x200) << int(Enums::Wpa2Eap);
        QTest::newRow("owe transition") << caps << 0u << 0u << 0x1000u << int(Enums::OWE);
        QTest::newRow("wpa") << caps << 1u << (0x4u | 0x40u | 0x100u) << 0u << int(Enums::WpaPsk);
        // A device that reports no capabilities still gets a lock for a secured network.
        QTest::newRow("no capabilities") << 0u << 1u << 0u << (ccmp | 0x100) << int(Enums::Wpa2Psk);
    }

    void security()
    {
        QFETCH(uint, capabilities);
        QFETCH(uint, flags);
        QFETCH(uint, wpa);
        QFETCH(uint, rsn);
        QFETCH(int, expected);
        QCOMPARE(int(WifiUtils::security(capabilities, flags, wpa, rsn, 2)), expected);
    }

    void secrets()
    {
        QVERIFY(WifiUtils::pskValid(QStringLiteral("12345678")));
        QVERIFY(!WifiUtils::pskValid(QStringLiteral("1234567")));
        QVERIFY(WifiUtils::pskValid(QString(64, QLatin1Char('a'))));
        QVERIFY(!WifiUtils::pskValid(QString(64, QLatin1Char('g'))));
        QCOMPARE(WifiUtils::generatedPassword().size(), 12);
        QVERIFY(WifiUtils::pskValid(WifiUtils::generatedPassword()));
    }

    void strength()
    {
        QCOMPARE(WifiUtils::strengthIcon(0), QStringLiteral("network-wireless-connected-00"));
        QCOMPARE(WifiUtils::strengthIcon(33), QStringLiteral("network-wireless-connected-33"));
        QCOMPARE(WifiUtils::strengthIcon(34), QStringLiteral("network-wireless-connected-66"));
        QCOMPARE(WifiUtils::strengthIcon(90), QStringLiteral("network-wireless-connected-100"));
    }
};

int main(int argc, char **argv)
{
    // The locale picks the charsets tried after UTF-8.
    qputenv("LC_ALL", "zh_CN.UTF-8");
    QCoreApplication app(argc, argv);
    WifiUtilsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_wifiutils.moc"
