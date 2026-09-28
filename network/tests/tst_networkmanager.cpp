#include "../handler.h"
#include "../networkmodel.h"
#include "fakenetworkmanager.h"

#include <QPointer>
#include <QStandardPaths>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <memory>

class NetworkManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        server = std::make_unique<QDBusConnection>(QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("fake-network-manager")));
        QVERIFY(server->isConnected());
        networkManager = new FakeNetworkManager(*server);
        // Register before moveToThread: Qt creates a child of the object here, which can
        // only be parented while the object still lives on this thread.
        QVERIFY(server->registerVirtualObject(managerPath, networkManager,
                                              QDBusConnection::SubPath));
        networkManager->moveToThread(&networkManagerThread);
        connect(&networkManagerThread, &QThread::finished, networkManager, &QObject::deleteLater);
        networkManagerThread.start();
        QVERIFY(server->registerService(QStringLiteral("org.freedesktop.NetworkManager")));
    }

    void cleanupTestCase()
    {
        server->unregisterService(QStringLiteral("org.freedesktop.NetworkManager"));
        server->unregisterObject(managerPath, QDBusConnection::UnregisterTree);
        networkManagerThread.quit();
        networkManagerThread.wait();
        server.reset();
        QDBusConnection::disconnectFromBus(QStringLiteral("fake-network-manager"));
    }

    void removedDeviceIsReleased()
    {
        NetworkModel model;
        QPointer<NetworkManager::Device> device =
                NetworkManager::findNetworkInterface(wiredPath).data();
        QVERIFY(device);
        networkManager->removeDevice(wiredPath);
        QTRY_VERIFY(device.isNull());
    }

    void scanRetryTimerBelongsToHandler()
    {
        auto handler = std::make_unique<Handler>();
        handler->requestScan();
        handler->requestScan();
        QPointer<QTimer> retry = handler->findChild<QTimer *>();
        QVERIFY(retry);
        QVERIFY(retry->isActive());
        handler.reset();
        QVERIFY(retry.isNull());
    }

private:
    std::unique_ptr<QDBusConnection> server;
    QThread networkManagerThread;
    FakeNetworkManager *networkManager = nullptr;
};

int main(int argc, char **argv)
{
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
    // Handler clears a stale hotspot path in the user's network configuration.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    NetworkManagerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_networkmanager.moc"
