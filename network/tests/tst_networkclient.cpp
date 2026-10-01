#include "../enums.h"
#include "../handler.h"
#include "../networkclient.h"
#include "../networkmodel.h"
#include "../wirelessitemsettings.h"
#include "fixture.h"

#include <QTest>

#include <memory>

using namespace Fixture;

class NetworkClientTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        server = std::make_unique<QDBusConnection>(QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("fake-network-manager")));
        QVERIFY(server->isConnected());
        nm = std::make_unique<FakeNetworkManager>(*server);
        populate(*nm);
        QVERIFY(server->registerVirtualObject(QStringLiteral("/org/freedesktop"), nm.get(),
                                              QDBusConnection::SubPath));
        QVERIFY(server->registerService(FakeNM::manager));
        QTRY_VERIFY(NetworkClient::instance()->ready());
        QCOMPARE(NetworkClient::instance()->owner(), server->baseService());
    }

    void cleanup()
    {
        server->unregisterService(FakeNM::manager);
        server->unregisterObject(QStringLiteral("/org/freedesktop"), QDBusConnection::UnregisterTree);
        QTRY_VERIFY(!NetworkClient::instance()->ready());
        nm.reset();
        server.reset();
        QDBusConnection::disconnectFromBus(QStringLiteral("fake-network-manager"));
    }

    void listsNetworksByName()
    {
        NetworkModel model;
        QTRY_COMPARE(model.rowCount(), 5);

        // Both of its access points make one network, shown by the stronger one.
        const QModelIndex home = row(model, QStringLiteral("Home"));
        QVERIFY(home.isValid());
        QCOMPARE(home.data(NetworkModel::ItemTypeRole).toInt(), int(NetworkModel::AvailableConnection));
        QCOMPARE(home.data(NetworkModel::ConnectionPathRole).toString(), homeProfile);
        QCOMPARE(home.data(NetworkModel::SpecificPathRole).toString(), homeStrong);
        QCOMPARE(home.data(NetworkModel::SignalRole).toInt(), 70);
        QCOMPARE(home.data(NetworkModel::SecurityTypeRole).toInt(), int(Enums::Wpa2Psk));

        const QModelIndex cafeRow = row(model, QStringLiteral("Cafe"));
        QCOMPARE(cafeRow.data(NetworkModel::ItemTypeRole).toInt(), int(NetworkModel::AvailableAccessPoint));
        QCOMPARE(cafeRow.data(NetworkModel::SecurityTypeRole).toInt(), int(Enums::NoneSecurity));
        QCOMPARE(row(model, QStringLiteral("Library")).data(NetworkModel::SecurityTypeRole).toInt(),
                 int(Enums::Wpa2Eap));

        const QModelIndex wired = row(model, QStringLiteral("Wired connection 1"));
        QCOMPARE(wired.data(NetworkModel::TypeRole).toInt(), int(Enums::Wired));
        QCOMPARE(wired.data(NetworkModel::ConnectionStateRole).toInt(), int(NetworkModel::Activated));
        QCOMPARE(wired.data(NetworkModel::IpAddressRole).toString(), QStringLiteral("192.168.1.5"));
        QCOMPARE(wired.data(NetworkModel::GateWayRole).toString(), QStringLiteral("192.168.1.1"));
        QCOMPARE(wired.data(NetworkModel::NameServerRole).toString(), QStringLiteral("192.168.1.1"));
        QCOMPARE(wired.data(NetworkModel::MacAddressRole).toString(), QStringLiteral("02:00:00:00:00:01"));
    }

    void hiddenAccessPointJoinsOnceNamed()
    {
        NetworkModel model;
        QTRY_COMPARE(model.rowCount(), 5);
        nm->set(hidden, FakeNM::accessPoint, QStringLiteral("Ssid"), QByteArray("Garden"));
        QTRY_VERIFY(row(model, QStringLiteral("Garden")).isValid());
        QCOMPARE(model.rowCount(), 6);
    }

    void rowStaysWhileItsAccessPointsChange()
    {
        NetworkModel model;
        QTRY_COMPARE(model.rowCount(), 5);
        const QPersistentModelIndex home = row(model, QStringLiteral("Home"));

        nm->set(wifiDevice, FakeNM::wireless, QStringLiteral("AccessPoints"),
                FakeNM::paths({homeWeak, cafe, hidden, library, office}));
        QTRY_COMPARE(home.data(NetworkModel::SpecificPathRole).toString(), homeWeak);
        QVERIFY(home.isValid());
        QCOMPARE(home.data(NetworkModel::SignalRole).toInt(), 40);
    }

    void unreadableProfileStillShowsItsNetworkInUse()
    {
        NetworkModel model;
        QTRY_COMPARE(model.rowCount(), 5);

        const QString theirs = FakeNM::managerPath + QStringLiteral("/Settings/50");
        const QString active = FakeNM::managerPath + QStringLiteral("/ActiveConnection/50");
        nm->privateConnections.insert(theirs);
        nm->add(theirs, {{FakeNM::connection, {{QStringLiteral("Flags"), 0u}}}});
        nm->add(active, {{FakeNM::active,
                          {{QStringLiteral("Connection"), FakeNM::path(theirs)},
                           {QStringLiteral("SpecificObject"), FakeNM::path(cafe)},
                           {QStringLiteral("Devices"), FakeNM::paths({wifiDevice})},
                           {QStringLiteral("State"), 2u},
                           {QStringLiteral("Type"), QStringLiteral("802-11-wireless")},
                           {QStringLiteral("Vpn"), false}}}});
        nm->set(wifiDevice, FakeNM::wireless, QStringLiteral("ActiveAccessPoint"), FakeNM::path(cafe));
        nm->set(wifiDevice, FakeNM::device, QStringLiteral("ActiveConnection"), FakeNM::path(active));

        QTRY_COMPARE(row(model, QStringLiteral("Cafe")).data(NetworkModel::ConnectionStateRole).toInt(),
                     int(NetworkModel::Activated));
        QVERIFY(row(model, QStringLiteral("Cafe")).data(NetworkModel::ConnectionPathRole).toString().isEmpty());
        QVERIFY(!NetworkClient::instance()->readable(theirs));
    }

    void joiningSendsATypedPartialProfile()
    {
        Handler handler;
        handler.addAndActivateConnection(wifiDevice, office, QStringLiteral("secret pass"));
        QTRY_COMPARE(nm->count(QStringLiteral("AddAndActivateConnection")), 1);

        const QDBusMessage call = nm->last(QStringLiteral("AddAndActivateConnection"));
        QCOMPARE(call.signature(), QStringLiteral("a{sa{sv}}oo"));
        QCOMPARE(call.arguments().value(2).value<QDBusObjectPath>().path(), office);
        const auto settings = qdbus_cast<NMVariantMapMap>(call.arguments().value(0));

        // NetworkManager fills in the SSID's bytes and the key management from the access point.
        QVERIFY(!settings.contains(QStringLiteral("802-11-wireless")));
        const QVariantMap security = settings.value(QStringLiteral("802-11-wireless-security"));
        QVERIFY(!security.contains(QStringLiteral("key-mgmt")));
        QCOMPARE(security.value(QStringLiteral("psk")).toString(), QStringLiteral("secret pass"));
        QCOMPARE(security.value(QStringLiteral("psk-flags")).metaType(), QMetaType::fromType<uint>());
        QCOMPARE(security.value(QStringLiteral("psk-flags")).toUInt(), 1u);
        QCOMPARE(settings.value(QStringLiteral("connection")).value(QStringLiteral("id")).toString(),
                 QStringLiteral("Office"));
        QCOMPARE(settings.value(QStringLiteral("connection")).value(QStringLiteral("permissions")).toStringList(),
                 QStringList {NetworkClient::userPermission()});
    }

    void failedJoinIsForgotten()
    {
        Handler handler;
        handler.addAndActivateConnection(wifiDevice, office, QStringLiteral("wrong pass"));
        QTRY_COMPARE(nm->count(QStringLiteral("AddAndActivateConnection")), 1);
        QTRY_COMPARE(NetworkClient::instance()->activeConnections().size(), 2);
        QString active;
        for (const QString &path : NetworkClient::instance()->activeConnections()) {
            if (path != wiredActive)
                active = path;
        }
        QVERIFY(!active.isEmpty());

        // NM_ACTIVE_CONNECTION_STATE_DEACTIVATED for NO_SECRETS.
        nm->emitSignal(active, FakeNM::active, QStringLiteral("StateChanged"), {4u, 9u});
        QTRY_COMPARE(nm->count(QStringLiteral("Delete")), 1);
        QVERIFY(nm->last(QStringLiteral("Delete")).path().startsWith(FakeNM::managerPath + QStringLiteral("/Settings/")));
    }

    void updateKeepsWhatItDoesNotChange()
    {
        WirelessItemSettings item;
        item.setPath(homeProfile);
        QVERIFY(item.autoConnect());
        QVERIFY(!item.availableToOtherUsers());

        item.setAutoConnect(false);
        QTRY_COMPARE(nm->count(QStringLiteral("Update")), 1);

        const auto settings = qdbus_cast<NMVariantMapMap>(nm->last(QStringLiteral("Update")).arguments().value(0));
        QCOMPARE(settings.value(QStringLiteral("connection")).value(QStringLiteral("autoconnect")).toBool(), false);
        QCOMPARE(settings.value(QStringLiteral("802-11-wireless")).value(QStringLiteral("ssid")).toByteArray(),
                 QByteArray("Home"));
        QCOMPARE(settings.value(QStringLiteral("802-11-wireless-security")).value(QStringLiteral("psk-flags")).metaType(),
                 QMetaType::fromType<uint>());
        // A value NetworkManager sent in a container type goes back in that type.
        const auto addressData = settings.value(QStringLiteral("ipv4")).value(QStringLiteral("address-data")).value<QDBusArgument>();
        QCOMPARE(addressData.currentSignature(), QStringLiteral("aa{sv}"));

        // The profile NetworkManager reports after the update is what the switch shows.
        QTRY_VERIFY(!item.autoConnect());
    }

    void scansOnlyDevicesThatCan()
    {
        Handler handler;
        handler.requestScan();
        QTRY_COMPARE(nm->count(QStringLiteral("RequestScan")), 1);
        QCOMPARE(nm->last(QStringLiteral("RequestScan")).path(), wifiDevice);

        nm->set(wifiDevice, FakeNM::device, QStringLiteral("State"), 20u);
        QTRY_COMPARE(NetworkClient::instance()->deviceState(wifiDevice), 20u);
        handler.requestScan();
        QTest::qWait(200);
        QCOMPARE(nm->count(QStringLiteral("RequestScan")), 1);
    }

    void reloadsAfterRestart()
    {
        NetworkModel model;
        QTRY_COMPARE(model.rowCount(), 5);

        server->unregisterService(FakeNM::manager);
        QTRY_VERIFY(!NetworkClient::instance()->ready());
        QTRY_COMPARE(model.rowCount(), 0);

        QVERIFY(server->registerService(FakeNM::manager));
        QTRY_VERIFY(NetworkClient::instance()->ready());
        QTRY_COMPARE(model.rowCount(), 5);
        QCOMPARE(nm->count(QStringLiteral("GetManagedObjects")), 2);
    }

private:
    static QModelIndex row(const NetworkModel &model, const QString &name)
    {
        for (int i = 0; i < model.rowCount(); ++i) {
            const QModelIndex index = model.index(i);
            if (index.data(NetworkModel::NameRole).toString() == name)
                return index;
        }
        return {};
    }

    std::unique_ptr<QDBusConnection> server;
    std::unique_ptr<FakeNetworkManager> nm;
};

int main(int argc, char **argv)
{
    // NetworkManager is served on the session bus of dbus-run-session.
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
    QCoreApplication app(argc, argv);
    NetworkClientTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_networkclient.moc"
