#include "volumemanager.h"

#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusVirtualObject>
#include <QSignalSpy>
#include <QtTest>

using namespace Cutefish::IO;

namespace {
const QString service = "org.freedesktop.UDisks2";
const QString root = "/org/freedesktop/UDisks2";
const QString disk = root + "/block_devices/sdb1";
const QString drive = root + "/drives/usb";
} // namespace

class FakeUDisks : public QDBusVirtualObject
{
public:
    VolumeObjects objects;
    QStringList calls;
    bool fail = false;

    QString introspect(const QString &) const override { return {}; }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &bus) override
    {
        if (message.member() == "GetManagedObjects") {
            bus.send(message.createReply(QVariant::fromValue(objects)));
            return true;
        }
        calls.append(message.member());
        if (message.arguments().size() != 1 || message.signature() != "a{sv}") {
            bus.send(message.createErrorReply("org.freedesktop.DBus.Error.InvalidArgs",
                                              "Expected options map"));
        } else if (fail) {
            bus.send(message.createErrorReply("org.freedesktop.UDisks2.Error.NotAuthorized",
                                              "Permission denied"));
        } else if (message.member() == "Mount") {
            bus.send(message.createReply(QVariant(QString::fromUtf8("/media/USB 测试"))));
        } else {
            bus.send(message.createReply());
        }
        return true;
    }
};

class VolumeManagerTest : public QObject
{
    Q_OBJECT
private slots:

    void lifecycleAndOperations()
    {
        auto bus = QDBusConnection::sessionBus();
        QVERIFY(bus.isConnected());
        qDBusRegisterMetaType<VolumeInterfaces>();
        qDBusRegisterMetaType<VolumeObjects>();
        qDBusRegisterMetaType<QList<QByteArray>>();
        FakeUDisks fake;
        fake.objects[QDBusObjectPath(drive)][service + ".Drive"] = {
                {"Removable", true}, {"Ejectable", true}, {"Optical", true}};
        fake.objects[QDBusObjectPath(disk)][service + ".Block"] = {
                {"Drive", QVariant::fromValue(QDBusObjectPath(drive))},
                {"PreferredDevice", QByteArray("/dev/sdb1\0", 10)},
                {"IdLabel", "USB"},
                {"Size", qulonglong(4096)}};
        fake.objects[QDBusObjectPath(disk)][service + ".Filesystem"] = {
                {"MountPoints", QVariant::fromValue(QList<QByteArray>())}};
        QVERIFY(bus.registerVirtualObject(root, &fake, QDBusConnection::SubPath));
        QVERIFY(bus.registerService(service));
        VolumeManager manager(bus);
        QSignalSpy added(&manager, &VolumeManager::volumeAdded);
        QSignalSpy changed(&manager, &VolumeManager::volumeChanged);
        QSignalSpy removed(&manager, &VolumeManager::volumeRemoved);
        QSignalSpy done(&manager, &VolumeManager::operationFinished);
        QSignalSpy errors(&manager, &VolumeManager::operationFailed);
        QTRY_COMPARE(added.size(), 1);
        QCOMPARE(manager.volume(disk).device, "/dev/sdb1");
        QCOMPARE(manager.volume(disk).name, "USB");
        QVERIFY(manager.volume(disk).optical);
        manager.mount(disk);
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(done.last().at(2).toString(), QString::fromUtf8("/media/USB 测试"));
        fake.fail = true;
        manager.unmount(disk);
        QTRY_COMPARE(errors.size(), 1);
        QCOMPARE(done.size(), 1);
        fake.fail = false;
        QByteArray mount = QString::fromUtf8("/media/USB 测试").toUtf8();
        mount.append('\0');
        fake.objects[QDBusObjectPath(disk)][service + ".Filesystem"]["MountPoints"] =
                QVariant::fromValue(QList<QByteArray> {mount});
        auto signal = QDBusMessage::createSignal(disk, "org.freedesktop.DBus.Properties",
                                                 "PropertiesChanged");
        signal << service + ".Filesystem"
               << fake.objects.value(QDBusObjectPath(disk)).value(service + ".Filesystem")
               << QStringList();
        QVERIFY(bus.send(signal));
        QTRY_VERIFY(!changed.isEmpty());
        QCOMPARE(manager.volume(disk).mountPoints,
                 QStringList {QString::fromUtf8("/media/USB 测试")});
        fake.calls.clear();
        manager.eject(disk);
        QTRY_COMPARE(done.size(), 2);
        QCOMPARE(fake.calls, QStringList({"Unmount", "Eject"}));
        fake.fail = true;
        fake.calls.clear();
        manager.eject(disk);
        QTRY_COMPARE(errors.size(), 2);
        QCOMPARE(fake.calls, QStringList({"Unmount"}));
        manager.mount("/missing");
        QCOMPARE(errors.size(), 3);
        fake.fail = false;
        fake.objects[QDBusObjectPath(drive)][service + ".Drive"]["Ejectable"] = false;
        fake.objects[QDBusObjectPath(drive)][service + ".Drive"]["CanPowerOff"] = true;
        auto driveChange = QDBusMessage::createSignal(drive, "org.freedesktop.DBus.Properties",
                                                      "PropertiesChanged");
        driveChange << service + ".Drive"
                    << fake.objects.value(QDBusObjectPath(drive)).value(service + ".Drive")
                    << QStringList();
        QVERIFY(bus.send(driveChange));
        QTRY_VERIFY(manager.volume(disk).canPowerOff);
        fake.calls.clear();
        manager.eject(disk);
        manager.mount(disk);
        QCOMPARE(errors.size(), 4);
        QTRY_COMPARE(done.size(), 3);
        QCOMPARE(fake.calls, QStringList({"Unmount", "PowerOff"}));
        QVERIFY(bus.unregisterService(service));
        QTRY_COMPARE(removed.size(), 1);
        QVERIFY(manager.volumes().isEmpty());
        QVERIFY(bus.registerService(service));
        QTRY_COMPARE(added.size(), 2);
        fake.objects.remove(QDBusObjectPath(disk));
        auto removal = QDBusMessage::createSignal(root, "org.freedesktop.DBus.ObjectManager",
                                                  "InterfacesRemoved");
        removal << QVariant::fromValue(QDBusObjectPath(disk))
                << QStringList {service + ".Block", service + ".Filesystem"};
        QVERIFY(bus.send(removal));
        QTRY_COMPARE(removed.size(), 2);
        QVERIFY(manager.volumes().isEmpty());
        bus.unregisterService(service);
        bus.unregisterObject(root, QDBusConnection::UnregisterTree);
    }
};
QTEST_GUILESS_MAIN(VolumeManagerTest)
#include "tst_volumemanager.moc"
