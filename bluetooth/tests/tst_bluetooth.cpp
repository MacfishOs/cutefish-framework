#include "../bluetoothdevicesmodel.h"

#include <QDBusArgument>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QSignalSpy>
#include <QTest>
#include <memory>

#include "fakebluez.h"

class BluetoothTest : public QObject
{
    Q_OBJECT
private slots:

    void init()
    {
        server = std::make_unique<QDBusConnection>(QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("fake-bluez")));
        QVERIFY(server->isConnected());
        fake = std::make_unique<FakeBluez>(*server);
        QVERIFY(server->registerVirtualObject(QStringLiteral("/"), fake.get(),
                                              QDBusConnection::SubPath));
        QVERIFY(server->registerService(QStringLiteral("org.bluez")));
        manager = std::make_unique<BluetoothManager>(QDBusConnection::sessionBus(), false);
        QTRY_VERIFY(manager->operational());
        QTRY_COMPARE(manager->devices().size(), 1);
    }

    void cleanup()
    {
        manager.reset();
        server->unregisterService(QStringLiteral("org.bluez"));
        server->unregisterObject(QStringLiteral("/"), QDBusConnection::UnregisterTree);
        fake.reset();
        server.reset();
        QDBusConnection::disconnectFromBus(QStringLiteral("fake-bluez"));
    }

    void availabilityDoesNotDependOnPower()
    {
        QVERIFY(manager->available());
        QSignalSpy availability(manager.get(), &BluetoothManager::availableChanged);
        manager->setEnabled(false);
        QTRY_VERIFY(!manager->enabled());
        QVERIFY(manager->available());
        QCOMPARE(availability.size(), 0);
        const auto properties = fake->objects.value(QDBusObjectPath(adapter));
        fake->remove(adapter, {adapterInterface});
        QTRY_VERIFY(!manager->available());
        QVERIFY(manager->operational());
        QCOMPARE(availability.size(), 1);
        fake->add(adapter, properties);
        QTRY_VERIFY(manager->available());
        QCOMPARE(availability.size(), 2);
        QVERIFY(server->unregisterService(QStringLiteral("org.bluez")));
        QTRY_VERIFY(!manager->available());
        QVERIFY(!manager->operational());
    }

    void propertiesAndHotplug()
    {
        BluetoothDevicesModel model;
        model.setManager(manager.get());
        QCOMPARE(model.rowCount(), 1);
        QVERIFY(manager->enabled());
        QCOMPARE(manager->devices().first()->name(), QStringLiteral("Headphones"));
        auto *entry = manager->object(device);
        QCOMPARE(entry->batteryPercentage(), -1);
        fake->add(device,
                  {{QStringLiteral("org.bluez.Battery1"),
                    {{"Percentage", QVariant::fromValue(quint8(72))}}}});
        QTRY_COMPARE(entry->batteryPercentage(), 72);
        fake->remove(device, {QStringLiteral("org.bluez.Battery1")});
        QTRY_COMPARE(entry->batteryPercentage(), -1);
        fake->set(device, deviceInterface, QStringLiteral("Alias"), QStringLiteral("Renamed"));
        QTRY_COMPARE(entry->name(), QStringLiteral("Renamed"));
        manager->setEnabled(false);
        QTRY_VERIFY(!manager->enabled());
        manager->setEnabled(true);
        QTRY_VERIFY(manager->enabled());
        fake->remove(adapter, {adapterInterface});
        QTRY_COMPARE(manager->adapters().size(), 0);
        QTRY_COMPARE(model.rowCount(), 0);
    }

    void discoveryOwnership()
    {
        fake->set(adapter, adapterInterface, QStringLiteral("Discovering"), true);
        QTRY_VERIFY(manager->scanning());
        QVERIFY(!fake->calls.contains(QStringLiteral("StartDiscovery")));
        manager->setDiscoveryRequested(true);
        QTRY_COMPARE(fake->calls.count(QStringLiteral("StartDiscovery")), 1);
        manager->setDiscoveryRequested(true);
        manager->setDiscoveryRequested(false);
        QTRY_COMPARE(fake->calls.count(QStringLiteral("StopDiscovery")), 1);
        manager->setDiscoveryRequested(false);
        QCOMPARE(fake->calls.count(QStringLiteral("StopDiscovery")), 1);
    }

    void pairTrustConnectForget()
    {
        manager->setAgentEnabled(true);
        QTRY_VERIFY(manager->agentReady());
        auto *entry = manager->object(device);
        entry->pair();
        QTRY_VERIFY(entry->paired() && entry->trusted() && entry->connected() && !entry->busy());
        QVERIFY(fake->calls.indexOf(QStringLiteral("Pair"))
                < fake->calls.indexOf(QStringLiteral("Set")));
        QVERIFY(fake->calls.indexOf(QStringLiteral("Set"))
                < fake->calls.indexOf(QStringLiteral("Connect")));
        entry->disconnectFromDevice();
        QTRY_VERIFY(!entry->connected() && !entry->busy());
        entry->forget();
        QTRY_VERIFY(manager->devices().isEmpty());
        QCOMPARE(fake->lastPath, adapter);
    }

    void failedPairDoesNotTrust()
    {
        manager->setAgentEnabled(true);
        QTRY_VERIFY(manager->agentReady());
        fake->failMethod = QStringLiteral("Pair");
        auto *entry = manager->object(device);
        entry->pair();
        QTRY_VERIFY(!entry->busy());
        QVERIFY(!entry->trusted());
        QVERIFY(!fake->calls.contains(QStringLiteral("Connect")));
        QVERIFY(manager->errorText().contains(QStringLiteral("Injected failure")));
    }

    void confirmationAndValidation()
    {
        manager->setAgentEnabled(true);
        QTRY_VERIFY(manager->agentReady());
        const QVariant path = QVariant::fromValue(QDBusObjectPath(device));
        QDBusPendingCallWatcher confirmation(
                fake->ask(QStringLiteral("RequestConfirmation"), {path, quint32(42)}));
        QTRY_COMPARE(manager->pairingCode(), QStringLiteral("000042"));
        QVERIFY(!confirmation.isFinished());
        manager->respondPairing(true);
        QTRY_VERIFY(confirmation.isFinished());
        QVERIFY(!confirmation.isError());
        QDBusPendingCallWatcher passkey(fake->ask(QStringLiteral("RequestPasskey"), {path}));
        QTRY_COMPARE(manager->pairingKind(), QStringLiteral("RequestPasskey"));
        manager->respondPairing(true, QStringLiteral("1000000"));
        QVERIFY(!passkey.isFinished());
        manager->respondPairing(true, QStringLiteral("000123"));
        QTRY_VERIFY(passkey.isFinished());
        QCOMPARE(passkey.reply().signature(), QStringLiteral("u"));
        QCOMPARE(passkey.reply().arguments().first().toUInt(), 123u);
        QDBusPendingCallWatcher pin(fake->ask(QStringLiteral("RequestPinCode"), {path}));
        QTRY_COMPARE(manager->pairingKind(), QStringLiteral("RequestPinCode"));
        manager->respondPairing(true, QString());
        QVERIFY(!pin.isFinished());
        manager->respondPairing(true, QStringLiteral("abc123"));
        QTRY_VERIFY(pin.isFinished());
        QCOMPARE(pin.reply().arguments().first().toString(), QStringLiteral("abc123"));
    }

    void cancellationAndConcurrentRequests()
    {
        manager->setAgentEnabled(true);
        QTRY_VERIFY(manager->agentReady());
        const QVariant path = QVariant::fromValue(QDBusObjectPath(device));
        QDBusPendingCallWatcher first(fake->ask(QStringLiteral("RequestAuthorization"), {path}));
        QTRY_COMPARE(manager->pairingKind(), QStringLiteral("RequestAuthorization"));
        QDBusPendingCallWatcher second(fake->ask(QStringLiteral("RequestPinCode"), {path}));
        QTRY_VERIFY(second.isFinished());
        QCOMPARE(second.error().name(), QStringLiteral("org.bluez.Error.Rejected"));
        manager->respondPairing(false);
        QTRY_VERIFY(first.isFinished());
        QCOMPARE(first.error().name(), QStringLiteral("org.bluez.Error.Rejected"));
        QDBusPendingCallWatcher display(
                fake->ask(QStringLiteral("DisplayPasskey"),
                          {path, quint32(7), QVariant::fromValue(quint16(2))}));
        QTRY_VERIFY(display.isFinished());
        QCOMPARE(manager->pairingCode(), QStringLiteral("000007"));
        QCOMPARE(manager->pairingEntered(), 2);
        QDBusPendingCallWatcher cancel(fake->ask(QStringLiteral("Cancel"), {}));
        QTRY_VERIFY(cancel.isFinished());
        QVERIFY(manager->pairingKind().isEmpty());
    }

    void restartAndPendingOperation()
    {
        manager->setAgentEnabled(true);
        QTRY_VERIFY(manager->agentReady());
        fake->holdPair = true;
        QPointer<BluetoothObject> oldDevice = manager->object(device);
        oldDevice->pair();
        QTRY_VERIFY(fake->pendingPair.type() == QDBusMessage::MethodCallMessage);
        QVERIFY(server->unregisterService(QStringLiteral("org.bluez")));
        QTRY_VERIFY(!manager->operational());
        QTRY_VERIFY(oldDevice.isNull());
        QVERIFY(manager->devices().isEmpty());
        QVERIFY(server->registerService(QStringLiteral("org.bluez")));
        QTRY_VERIFY(manager->operational() && manager->agentReady());
        QCOMPARE(manager->devices().size(), 1);
        server->send(fake->pendingPair.createReply());
        QTest::qWait(30);
        QVERIFY(!manager->devices().first()->trusted());
        QVERIFY(!fake->calls.contains(QStringLiteral("Connect")));
    }

    void closingPageCancelsPendingPair()
    {
        manager->setAgentEnabled(true);
        QTRY_VERIFY(manager->agentReady());
        fake->holdPair = true;
        auto *entry = manager->object(device);
        entry->pair();
        QTRY_VERIFY(fake->pendingPair.type() == QDBusMessage::MethodCallMessage);
        manager->setAgentEnabled(false);
        QTRY_VERIFY(fake->calls.contains(QStringLiteral("CancelPairing")));
        QTRY_VERIFY(!entry->busy());
        QVERIFY(!entry->trusted());
    }

    void rejectForeignAgentCaller()
    {
        manager->setAgentEnabled(true);
        QTRY_VERIFY(manager->agentReady());
        auto message = QDBusMessage::createMethodCall(fake->agentOwner, fake->agentPath,
                                                      QStringLiteral("org.bluez.Agent1"),
                                                      QStringLiteral("RequestAuthorization"));
        message << QVariant::fromValue(QDBusObjectPath(device));
        auto foreign = QDBusConnection::connectToBus(QDBusConnection::SessionBus,
                                                     QStringLiteral("foreign-client"));
        QDBusPendingCallWatcher reply(foreign.asyncCall(message));
        QTRY_VERIFY(reply.isFinished());
        QCOMPARE(reply.error().name(), QStringLiteral("org.bluez.Error.Rejected"));
        QVERIFY(manager->pairingKind().isEmpty());
        QDBusConnection::disconnectFromBus(QStringLiteral("foreign-client"));
    }

    void snapshotPreservesConcurrentEvents()
    {
        QVERIFY(server->unregisterService(QStringLiteral("org.bluez")));
        QTRY_VERIFY(!manager->operational());
        fake->holdSnapshot = true;
        QVERIFY(server->registerService(QStringLiteral("org.bluez")));
        QTRY_VERIFY(fake->pendingSnapshot.type() == QDBusMessage::MethodCallMessage);
        fake->set(device, deviceInterface, QStringLiteral("Alias"), QStringLiteral("New name"));
        fake->add(device, {{QStringLiteral("org.bluez.Battery1"), {{"Percentage", quint8(50)}}}});
        server->send(fake->pendingSnapshot.createReply({QVariant::fromValue(fake->snapshot)}));
        QTRY_VERIFY(manager->operational());
        QCOMPARE(manager->object(device)->name(), QStringLiteral("New name"));
        QCOMPARE(manager->object(device)->batteryPercentage(), 50);
    }

    void invalidatedPropertiesDoNotOverwriteNewSignals()
    {
        fake->holdGetAll = true;
        auto signal = QDBusMessage::createSignal(device,
                                                 QStringLiteral("org.freedesktop.DBus.Properties"),
                                                 QStringLiteral("PropertiesChanged"));
        signal << deviceInterface << QVariantMap {} << QStringList {QStringLiteral("Alias")};
        server->send(signal);
        QTRY_VERIFY(fake->pendingGetAll.type() == QDBusMessage::MethodCallMessage);
        fake->set(device, deviceInterface, QStringLiteral("Alias"), QStringLiteral("New alias"));
        QTRY_COMPARE(manager->object(device)->name(), QStringLiteral("New alias"));
        server->send(fake->pendingGetAll.createReply(QVariantList {fake->oldProperties}));
        QTest::qWait(30);
        QCOMPARE(manager->object(device)->name(), QStringLiteral("New alias"));
    }

    void failedDiscoveryCanBeRetried()
    {
        fake->failMethod = QStringLiteral("StartDiscovery");
        manager->setDiscoveryRequested(true);
        QTRY_VERIFY(!manager->discoveryRequested());
        QVERIFY(manager->errorText().contains(QStringLiteral("Injected failure")));
        fake->failMethod.clear();
        manager->setDiscoveryRequested(true);
        QTRY_VERIFY(manager->scanning());
        fake->failMethod = QStringLiteral("StopDiscovery");
        manager->setDiscoveryRequested(false);
        QTRY_VERIFY(fake->calls.contains(QStringLiteral("StopDiscovery")));
        fake->failMethod.clear();
        QTRY_VERIFY(!manager->scanning());
    }

    void multipleAdapters()
    {
        const QString secondAdapter = QStringLiteral("/org/bluez/hci1");
        const QString secondDevice = secondAdapter + QStringLiteral("/dev_AA_BB_CC_DD_EE_FF");
        fake->add(secondAdapter, {{adapterInterface, {{"Powered", true}}}});
        fake->add(secondDevice,
                  {{deviceInterface,
                    {{"Address", "AA:BB:CC:DD:EE:FF"},
                     {"Adapter", QVariant::fromValue(QDBusObjectPath(secondAdapter))}}}});
        QTRY_COMPARE(manager->devices().size(), 2);
        manager->object(secondDevice)->connectToDevice();
        QTRY_VERIFY(manager->object(secondDevice)->connected());
        QVERIFY(!manager->object(device)->connected());
    }

private:
    std::unique_ptr<QDBusConnection> server;
    std::unique_ptr<FakeBluez> fake;
    std::unique_ptr<BluetoothManager> manager;
};

QTEST_GUILESS_MAIN(BluetoothTest)
#include "tst_bluetooth.moc"
