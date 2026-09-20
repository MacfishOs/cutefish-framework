#include "screenrecordingpermissions.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

class ScreenRecordingPermissionsTest : public QObject
{
    Q_OBJECT

private slots:
    void recordsAndGrants()
    {
        QTemporaryDir directory;
        ScreenRecordingPermissions store(directory.path() + "/privacy.conf");
        QVERIFY(!store.grant("org.example.App"));

        store.record("org.example.App");
        auto grant = store.grant("org.example.App");
        QVERIFY(grant);
        QVERIFY(!grant->allowed);

        store.allow("org.example.App", {"eDP-1", "HDMI-A-1"});
        grant = store.grant("org.example.App");
        QVERIFY(grant->allowed);
        QCOMPARE(grant->monitors, QStringList({"eDP-1", "HDMI-A-1"}));

        // Recording again keeps an existing grant.
        store.record("org.example.App");
        QVERIFY(store.grant("org.example.App")->allowed);

        store.setAllowed("org.example.App", false);
        QVERIFY(!store.grant("org.example.App")->allowed);
        QCOMPARE(store.grant("org.example.App")->monitors.size(), 2);

        store.record("com.other.Tool");
        QCOMPARE(store.grants().size(), 2);
        store.remove("org.example.App");
        QCOMPARE(store.grants().size(), 1);
        QCOMPARE(store.grants().first().appId, "com.other.Tool");
    }

    void ignoresEmptyIds()
    {
        QTemporaryDir directory;
        ScreenRecordingPermissions store(directory.path() + "/privacy.conf");
        store.allow("", {"eDP-1"});
        QVERIFY(store.grants().isEmpty());
        QVERIFY(!store.grant(""));
    }

    void notifiesOtherReaders()
    {
        QTemporaryDir directory;
        const QString path = directory.path() + "/privacy.conf";
        ScreenRecordingPermissions reader(path);
        QSignalSpy spy(&reader, &ScreenRecordingPermissions::changed);

        ScreenRecordingPermissions writer(path);
        writer.allow("org.example.App", {"eDP-1"});
        QTRY_VERIFY(spy.count() >= 1);
        QVERIFY(reader.grant("org.example.App")->allowed);

        spy.clear();
        writer.setAllowed("org.example.App", false);
        QTRY_VERIFY(spy.count() >= 1);
        QVERIFY(!reader.grant("org.example.App")->allowed);
    }
};

QTEST_GUILESS_MAIN(ScreenRecordingPermissionsTest)
#include "tst_screenrecordingpermissions.moc"
