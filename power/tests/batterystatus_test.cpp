// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "batterystatus.h"

#include <QDBusConnection>
#include <QSignalSpy>
#include <QTest>

namespace {

// cutefish-services' battery object, on a connection of its own as in another process.
class FakeBattery : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.cutefish.Services.Battery")
    Q_PROPERTY(int chargeState MEMBER chargeState)
    Q_PROPERTY(int chargePercent MEMBER chargePercent)
    Q_PROPERTY(int capacity MEMBER capacity)
    Q_PROPERTY(QString statusString MEMBER statusString)
    Q_PROPERTY(int lastChargedPercent MEMBER lastChargedPercent)
    Q_PROPERTY(qlonglong lastChargedSecs MEMBER lastChargedSecs)
    Q_PROPERTY(QString udi MEMBER udi)

public:
    int chargeState = 2;
    int chargePercent = 64;
    int capacity = 93;
    QString statusString = QStringLiteral("Discharging");
    int lastChargedPercent = 100;
    qlonglong lastChargedSecs = 1700000000;
    QString udi = QStringLiteral("/org/freedesktop/UPower/devices/battery_BAT0");
    int refreshes = 0;

public slots:
    void refresh() { ++refreshes; }

signals:
    void chargePercentChanged(int value);
};

} // namespace

class BatteryStatusTest : public QObject
{
    Q_OBJECT

private slots:
    void readyWithoutABattery()
    {
        BatteryStatus status;
        QTRY_VERIFY(status.ready());
        QVERIFY(!status.available());
        QCOMPARE(status.chargePercent(), 0);
    }

    void readsAndFollowsTheService()
    {
        QDBusConnection bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                                            QStringLiteral("fake-battery"));
        FakeBattery battery;
        QVERIFY(bus.registerObject(QStringLiteral("/com/cutefish/Services/Battery"), &battery,
                                   QDBusConnection::ExportAllContents));
        QVERIFY(bus.registerService(QStringLiteral("com.cutefish.Services")));

        BatteryStatus status;
        QSignalSpy available(&status, &BatteryStatus::availableChanged);
        QTRY_VERIFY(status.ready());
        QVERIFY(status.available());
        QCOMPARE(available.count(), 1);
        QCOMPARE(status.chargeState(), 2);
        QCOMPARE(status.chargePercent(), 64);
        QCOMPARE(status.capacity(), 93);
        QCOMPARE(status.statusString(), QStringLiteral("Discharging"));
        QCOMPARE(status.lastChargedPercent(), 100);
        QCOMPARE(status.lastChargedSecs(), 1700000000);
        QCOMPARE(status.udi(), battery.udi);

        // A change signal is only a cue: the values come from a fresh read.
        QSignalSpy percent(&status, &BatteryStatus::chargePercentChanged);
        battery.chargePercent = 65;
        emit battery.chargePercentChanged(0);
        QTRY_COMPARE(status.chargePercent(), 65);
        QCOMPARE(percent.count(), 1);

        status.refresh();
        QTRY_COMPARE(battery.refreshes, 1);

        bus.unregisterService(QStringLiteral("com.cutefish.Services"));
        QDBusConnection::disconnectFromBus(QStringLiteral("fake-battery"));
    }
};

QTEST_GUILESS_MAIN(BatteryStatusTest)
#include "batterystatus_test.moc"
