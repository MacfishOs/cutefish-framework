#include "fakebluez.h"

#include <QGuiApplication>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageProvider>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

class TestHardware : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool available READ available CONSTANT)
    Q_PROPERTY(bool wirelessHwEnabled READ available CONSTANT)
    Q_PROPERTY(bool ready READ available NOTIFY readyChanged)
    Q_PROPERTY(bool hotspotCapable READ available NOTIFY hotspotSupportedChanged)
public:
    explicit TestHardware(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    bool available() const { return true; }

Q_SIGNALS:
    void readyChanged();
    void hotspotSupportedChanged();
};

// Stands in for the network models the sidebar watches for a wired device.
class TestNetworkModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int type MEMBER m_type)
    Q_PROPERTY(QObject *sourceModel MEMBER m_sourceModel)
public:
    enum Type {
        UnknownType,
        WiredType,
        WirelessType
    };
    Q_ENUM(Type)

    explicit TestNetworkModel(QObject *parent = nullptr)
        : QAbstractListModel(parent)
    {
    }

    int rowCount(const QModelIndex & = {}) const override { return 0; }
    QVariant data(const QModelIndex &, int) const override { return {}; }

private:
    int m_type = UnknownType;
    QObject *m_sourceModel = nullptr;
};

class TestIconProvider : public QQuickImageProvider
{
public:
    TestIconProvider()
        : QQuickImageProvider(QQuickImageProvider::Image)
    {
    }

    QImage requestImage(const QString &, QSize *size, const QSize &) override
    {
        QImage image(1, 1, QImage::Format_ARGB32);
        image.fill(Qt::transparent);
        if (size)
            *size = image.size();
        return image;
    }
};

class BluetoothQmlTest : public QObject
{
    Q_OBJECT
private slots:

    void initTestCase()
    {
        qmlRegisterType<TestHardware>("Cutefish.Settings", 1, 0, "Battery");
        qmlRegisterType<TestHardware>("Cutefish.Settings", 1, 0, "Touchpad");
        qmlRegisterType<TestHardware>("Cutefish.Settings", 1, 0, "Touchscreen");
        qmlRegisterType<TestHardware>("Cutefish.Network", 1, 0, "EnabledConnections");
        qmlRegisterType<TestHardware>("Cutefish.Network", 1, 0, "Handler");
        qmlRegisterType<TestNetworkModel>("Cutefish.Network", 1, 0, "NetworkModel");
        qmlRegisterType<TestNetworkModel>("Cutefish.Network", 1, 0, "AppletProxyModel");
        qmlRegisterType<TestNetworkModel>("Cutefish.Settings", 1, 0, "NetworkMockModel");
        // Registered from C++ by the settings application too.
        qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(SETTINGS_BLUETOOTH_PAGE))
                                         .resolved(QUrl(QStringLiteral("../Metrics.qml"))),
                                 "Cutefish.Settings", 1, 0, "Metrics");
        qmlProtectModule("Cutefish.Settings", 1);
        qmlProtectModule("Cutefish.Network", 1);
    }

    void sidebarAvailability()
    {
        auto bus = QDBusConnection::sessionBus();
        FakeBluez fake(bus);
        QVERIFY(bus.registerVirtualObject(QStringLiteral("/"), &fake, QDBusConnection::SubPath));
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(BLUETOOTH_IMPORT_PATH));
        QQmlComponent host(&engine);
        host.setData(R"(
            import QtQuick
            import QtQuick.Controls
            ApplicationWindow {
                title: "Settings"
                property int headerInset: 40
                header: Item { height: 40 }
            }
        )",
                     QUrl());
        std::unique_ptr<QObject> window(host.create());
        QVERIFY2(window, qPrintable(host.errorString()));
        engine.rootContext()->setContextProperty(QStringLiteral("rootWindow"), window.get());
        const auto sidebarUrl = QUrl::fromLocalFile(QStringLiteral(SETTINGS_BLUETOOTH_PAGE))
                                        .resolved(QUrl(QStringLiteral("../SideBar.qml")));
        QQmlComponent component(&engine, sidebarUrl);
        std::unique_ptr<QObject> sidebar(component.create());
        QVERIFY2(sidebar, qPrintable(component.errorString()));
        auto *model = qvariant_cast<QObject *>(sidebar->property("model"));
        QVERIFY(model);
        auto *list = qobject_cast<QAbstractItemModel *>(model);
        QVERIFY(list);
        const int nameRole = list->roleNames().key("name");
        auto indexOf = [list, nameRole](const QString &name) {
            for (int row = 0; row < list->rowCount(); ++row) {
                if (list->data(list->index(row, 0), nameRole).toString() == name)
                    return row;
            }
            return -1;
        };
        auto *manager =
                engine.singletonInstance<BluetoothManager *>("Cutefish.Bluetooth", "Manager");
        QVERIFY(manager);
        QTest::qWait(30);
        QVERIFY(!manager->available());
        QCOMPARE(indexOf(QStringLiteral("bluetooth")), -1);
        sidebar->setProperty("currentIndex", indexOf(QStringLiteral("sound")));
        QVERIFY(bus.registerService(QStringLiteral("org.bluez")));
        QTRY_VERIFY(manager->available());
        QTRY_VERIFY(indexOf(QStringLiteral("bluetooth")) >= 0);
        QCOMPARE(sidebar->property("currentIndex").toInt(), indexOf(QStringLiteral("sound")));
        manager->setEnabled(false);
        QTRY_VERIFY(!manager->enabled());
        QVERIFY(indexOf(QStringLiteral("bluetooth")) >= 0);
        sidebar->setProperty("currentIndex", indexOf(QStringLiteral("bluetooth")));
        QSignalSpy removed(sidebar.get(), SIGNAL(currentPageRemoved()));
        const auto adapterProperties = fake.objects.value(QDBusObjectPath(adapter));
        fake.remove(adapter, {adapterInterface});
        QTRY_COMPARE(indexOf(QStringLiteral("bluetooth")), -1);
        QCOMPARE(sidebar->property("currentIndex").toInt(), 0);
        QCOMPARE(removed.size(), 1);
        fake.add(adapter, adapterProperties);
        QTRY_VERIFY(indexOf(QStringLiteral("bluetooth")) >= 0);
        QVERIFY(bus.unregisterService(QStringLiteral("org.bluez")));
        QTRY_COMPARE(indexOf(QStringLiteral("bluetooth")), -1);
        bus.unregisterObject(QStringLiteral("/"), QDBusConnection::UnregisterTree);
    }

    void settingsPage()
    {
        auto bus = QDBusConnection::sessionBus();
        FakeBluez fake(bus);
        QVERIFY(bus.registerVirtualObject(QStringLiteral("/"), &fake, QDBusConnection::SubPath));
        QVERIFY(bus.registerService(QStringLiteral("org.bluez")));
        QQmlEngine engine;
        engine.addImportPath(QStringLiteral(BLUETOOTH_IMPORT_PATH));
        engine.addImageProvider(QStringLiteral("icontheme"), new TestIconProvider);
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        QQmlComponent host(&engine);
        host.setData(R"(
            import QtQuick
            import QtQuick.Controls
            ApplicationWindow {
                width: 900
                height: 650
                visible: true
                property int headerInset: 40
                header: Item { height: 40 }
                background: Rectangle { color: "white" }
            }
        )",
                     QUrl());
        std::unique_ptr<QObject> window(host.create());
        QVERIFY2(window, qPrintable(host.errorString()));
        engine.rootContext()->setContextProperty(QStringLiteral("rootWindow"), window.get());
        QQmlComponent component(&engine,
                                QUrl::fromLocalFile(QStringLiteral(SETTINGS_BLUETOOTH_PAGE)));
        std::unique_ptr<QObject> page(component.create());
        QVERIFY2(page, qPrintable(component.errorString()));
        auto *item = qobject_cast<QQuickItem *>(page.get());
        QVERIFY(item);
        item->setParentItem(qobject_cast<QQuickWindow *>(window.get())->contentItem());
        item->setWidth(900);
        item->setHeight(600);
        auto *manager =
                engine.singletonInstance<BluetoothManager *>("Cutefish.Bluetooth", "Manager");
        QVERIFY(manager);
        QTRY_VERIFY(manager->operational());
        QTRY_COMPARE(manager->devices().size(), 1);
        QTRY_VERIFY(manager->scanning());
        // Pairing prompts belong to the shell's agent, not to the page.
        QVERIFY(!manager->agentEnabled());
        item->setVisible(false);
        QTRY_VERIFY(!manager->scanning());
        item->setVisible(true);
        QTRY_VERIFY(manager->scanning());
        QTest::qWait(50);
        for (const auto &arguments : warnings) {
            const auto errors = qvariant_cast<QList<QQmlError>>(arguments.first());
            for (const auto &error : errors)
                qWarning().noquote() << error.toString();
        }
        QCOMPARE(warnings.size(), 0);
        page.reset();
        bus.unregisterService(QStringLiteral("org.bluez"));
        bus.unregisterObject(QStringLiteral("/"), QDBusConnection::UnregisterTree);
    }
};

int main(int argc, char **argv)
{
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
    QGuiApplication app(argc, argv);
    BluetoothQmlTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_bluetoothqml.moc"
