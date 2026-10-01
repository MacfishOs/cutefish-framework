#include "../configuration.h"
#include "../enums.h"
#include "../handler.h"
#include "../networkclient.h"
#include "../networkmodel.h"
#include "../vpnutils.h"
#include "../wirelessitemsettings.h"
#include "fixture.h"

#include <QSignalSpy>
#include <QTemporaryDir>
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

    void hotspotKeepsOneProfile()
    {
        Handler handler;
        QTRY_VERIFY(handler.hotspotCapable());
        QVERIFY(handler.hotspotSupported());
        QVERIFY(handler.hotspotReplaces().isEmpty());
        QVERIFY(!handler.hotspotActive());

        Configuration configuration;
        configuration.setHotspotName(QStringLiteral("Desk"));
        configuration.setHotspotPassword(QStringLiteral("good password"));
        handler.createHotspot();
        QTRY_COMPARE(nm->count(QStringLiteral("AddAndActivateConnection")), 1);

        const QDBusMessage call = nm->last(QStringLiteral("AddAndActivateConnection"));
        QCOMPARE(call.arguments().value(1).value<QDBusObjectPath>().path(), wifiDevice);
        const auto settings = qdbus_cast<NMVariantMapMap>(call.arguments().value(0));
        QCOMPARE(settings.value(QStringLiteral("802-11-wireless")).value(QStringLiteral("mode")).toString(),
                 QStringLiteral("ap"));
        QCOMPARE(settings.value(QStringLiteral("802-11-wireless")).value(QStringLiteral("ssid")).toByteArray(),
                 QByteArray("Desk"));
        QCOMPARE(settings.value(QStringLiteral("802-11-wireless-security")).value(QStringLiteral("psk")).toString(),
                 QStringLiteral("good password"));
        QCOMPARE(settings.value(QStringLiteral("ipv4")).value(QStringLiteral("method")).toString(),
                 QStringLiteral("shared"));
        QTRY_VERIFY(handler.hotspotActive());

        // The next start updates that profile rather than adding another.
        QString active;
        for (const QString &path : NetworkClient::instance()->activeConnections()) {
            if (path != wiredActive)
                active = path;
        }
        nm->remove(active);
        QTRY_VERIFY(!handler.hotspotActive());
        configuration.setHotspotName(QStringLiteral("Desk 2"));
        handler.createHotspot();
        QTRY_COMPARE(nm->count(QStringLiteral("ActivateConnection")), 1);
        QCOMPARE(nm->count(QStringLiteral("AddAndActivateConnection")), 1);
        const auto updated = qdbus_cast<NMVariantMapMap>(nm->last(QStringLiteral("Update")).arguments().value(0));
        QCOMPARE(updated.value(QStringLiteral("802-11-wireless")).value(QStringLiteral("ssid")).toByteArray(),
                 QByteArray("Desk 2"));
        QCOMPARE(nm->last(QStringLiteral("ActivateConnection")).arguments().value(0).value<QDBusObjectPath>().path(),
                 nm->last(QStringLiteral("Update")).path());
        QTRY_VERIFY(handler.hotspotActive());
    }

    void hotspotThatNeverComesUpIsReported()
    {
        Handler handler;
        QSignalSpy failed(&handler, &Handler::hotspotFailed);
        handler.createHotspot();
        QTRY_COMPARE(NetworkClient::instance()->activeConnections().size(), 2);
        QString active;
        for (const QString &path : NetworkClient::instance()->activeConnections()) {
            if (path != wiredActive)
                active = path;
        }

        // NM_ACTIVE_CONNECTION_STATE_DEACTIVATED for DEVICE_DISCONNECTED.
        QTest::qWait(100);
        nm->emitSignal(active, FakeNM::active, QStringLiteral("StateChanged"), {4u, 3u});
        QTRY_COMPARE(failed.size(), 1);
    }

    void hotspotNamesTheNetworkItWouldEnd()
    {
        Handler handler;
        QTRY_VERIFY(handler.hotspotCapable());

        nm->add(homeActive, {{FakeNM::active,
                              {{QStringLiteral("Connection"), FakeNM::path(homeProfile)},
                               {QStringLiteral("Id"), QStringLiteral("Home")},
                               {QStringLiteral("Devices"), FakeNM::paths({wifiDevice})},
                               {QStringLiteral("State"), 2u},
                               {QStringLiteral("Type"), QStringLiteral("802-11-wireless")},
                               {QStringLiteral("Vpn"), false}}}});
        nm->set(wifiDevice, FakeNM::device, QStringLiteral("ActiveConnection"), FakeNM::path(homeActive));
        QTRY_COMPARE(handler.hotspotReplaces(), QStringLiteral("Home"));
        QVERIFY(handler.hotspotSupported());

        nm->permissions[QStringLiteral("org.freedesktop.NetworkManager.wifi.share.protected")] = QStringLiteral("no");
        nm->emitSignal(FakeNM::managerPath, FakeNM::manager, QStringLiteral("CheckPermissions"), {});
        QTRY_VERIFY(!handler.hotspotAllowed());
        QVERIFY(!handler.hotspotSupported());
        QVERIFY(handler.hotspotCapable());
    }

    void vpnRowsFollowTheirState()
    {
        NetworkModel model;
        QTRY_COMPARE(model.rowCount(), 5);
        addVpn(officeVpn, QStringLiteral("Office VPN"), QStringLiteral("org.freedesktop.NetworkManager.openvpn"), 3000);
        addVpn(branchVpn, QStringLiteral("Branch"), QStringLiteral("org.freedesktop.NetworkManager.l2tp"), 2000);
        addWireGuard(homeVpn, QStringLiteral("Home WG"), 1000);
        QTRY_COMPARE(model.rowCount(), 8);

        QModelIndex officeRow = row(model, QStringLiteral("Office VPN"));
        QCOMPARE(officeRow.data(NetworkModel::TypeRole).toInt(), int(Enums::Vpn));
        QCOMPARE(officeRow.data(NetworkModel::VpnType).toString(), QStringLiteral("OpenVPN"));
        QVERIFY(officeRow.data(NetworkModel::CanActivateRole).toBool());
        QCOMPARE(officeRow.data(NetworkModel::ConnectionStateRole).toInt(), int(NetworkModel::Deactivated));

        // No l2tp plugin is installed in this test.
        const QModelIndex branchRow = row(model, QStringLiteral("Branch"));
        QVERIFY(!branchRow.data(NetworkModel::CanActivateRole).toBool());
        QVERIFY(branchRow.data(NetworkModel::StatusMessageRole).toString().contains(QStringLiteral("network-manager-l2tp")));
        QCOMPARE(row(model, QStringLiteral("Home WG")).data(NetworkModel::VpnType).toString(), QStringLiteral("WireGuard"));

        // The tunnel is up when the plugin says so, not when the active connection exists.
        const QString active = FakeNM::managerPath + QStringLiteral("/ActiveConnection/60");
        nm->add(active, {{FakeNM::active,
                          {{QStringLiteral("Connection"), FakeNM::path(officeVpn)},
                           {QStringLiteral("Devices"), FakeNM::paths({})},
                           {QStringLiteral("State"), 2u},
                           {QStringLiteral("Type"), QStringLiteral("vpn")},
                           {QStringLiteral("Vpn"), true}}},
                         {vpnInterface, {{QStringLiteral("VpnState"), 3u}}}});
        QTRY_COMPARE(row(model, QStringLiteral("Office VPN")).data(NetworkModel::ConnectionStateRole).toInt(),
                     int(NetworkModel::Activating));
        nm->set(active, vpnInterface, QStringLiteral("VpnState"), 5u);
        QTRY_COMPARE(row(model, QStringLiteral("Office VPN")).data(NetworkModel::ConnectionStateRole).toInt(),
                     int(NetworkModel::Activated));

        // NM_ACTIVE_CONNECTION_STATE_REASON_LOGIN_FAILED stays on the row until the next try.
        nm->emitSignal(active, FakeNM::active, QStringLiteral("StateChanged"), {4u, 10u});
        nm->remove(active);
        QTRY_VERIFY(!row(model, QStringLiteral("Office VPN")).data(NetworkModel::StatusMessageRole).toString().isEmpty());
        QCOMPARE(row(model, QStringLiteral("Office VPN")).data(NetworkModel::ConnectionStateRole).toInt(),
                 int(NetworkModel::Deactivated));
    }

    void vpnUserNameIsSavedInTheProfile()
    {
        NetworkModel model;
        addVpn(officeVpn, QStringLiteral("Office VPN"), QStringLiteral("org.freedesktop.NetworkManager.openvpn"), 1000);
        QTRY_VERIFY(row(model, QStringLiteral("Office VPN")).isValid());
        QVERIFY(row(model, QStringLiteral("Office VPN")).data(NetworkModel::VpnUsesUserNameRole).toBool());
        QVERIFY(row(model, QStringLiteral("Office VPN")).data(NetworkModel::VpnUserNameRole).toString().isEmpty());

        Handler handler;
        handler.setVpnUserName(officeVpn, QStringLiteral("alice"));
        QTRY_COMPARE(nm->count(QStringLiteral("Update")), 1);
        const auto settings = qdbus_cast<NMVariantMapMap>(nm->last(QStringLiteral("Update")).arguments().value(0));
        const auto data = settings.value(QStringLiteral("vpn")).value(QStringLiteral("data")).value<QDBusArgument>();
        QCOMPARE(data.currentSignature(), QStringLiteral("a{ss}"));
        const auto values = qdbus_cast<QMap<QString, QString>>(data);
        QCOMPARE(values.value(QStringLiteral("username")), QStringLiteral("alice"));
        QTRY_COMPARE(row(model, QStringLiteral("Office VPN")).data(NetworkModel::VpnUserNameRole).toString(),
                     QStringLiteral("alice"));
    }

    void vpnAddedByHandIsPrivate()
    {
        Handler handler;
        QSignalSpy added(&handler, &Handler::vpnAdded);
        handler.addVpn({{QStringLiteral("kind"), QStringLiteral("l2tp")},
                        {QStringLiteral("name"), QStringLiteral("Branch")},
                        {QStringLiteral("server"), QStringLiteral("vpn.example.com")},
                        {QStringLiteral("userName"), QStringLiteral("alice")},
                        {QStringLiteral("password"), QStringLiteral("s3cret")},
                        {QStringLiteral("presharedKey"), QStringLiteral("shared")}});
        QTRY_COMPARE(added.size(), 1);

        const auto settings = qdbus_cast<NMVariantMapMap>(nm->last(QStringLiteral("AddConnection")).arguments().value(0));
        const QVariantMap connection = settings.value(QStringLiteral("connection"));
        QCOMPARE(connection.value(QStringLiteral("type")).toString(), QStringLiteral("vpn"));
        QCOMPARE(connection.value(QStringLiteral("autoconnect")).toBool(), false);
        QCOMPARE(connection.value(QStringLiteral("permissions")).toStringList(), QStringList {NetworkClient::userPermission()});

        const QVariantMap vpn = settings.value(QStringLiteral("vpn"));
        QCOMPARE(vpn.value(QStringLiteral("service-type")).toString(), QStringLiteral("org.freedesktop.NetworkManager.l2tp"));
        const auto data = vpn.value(QStringLiteral("data")).value<QDBusArgument>();
        const auto secrets = vpn.value(QStringLiteral("secrets")).value<QDBusArgument>();
        QCOMPARE(data.currentSignature(), QStringLiteral("a{ss}"));
        QCOMPARE(secrets.currentSignature(), QStringLiteral("a{ss}"));
        const auto dataMap = qdbus_cast<QMap<QString, QString>>(data);
        const auto secretMap = qdbus_cast<QMap<QString, QString>>(secrets);
        QCOMPARE(dataMap.value(QStringLiteral("user")), QStringLiteral("alice"));
        QCOMPARE(dataMap.value(QStringLiteral("password-flags")), QStringLiteral("1"));
        QCOMPARE(dataMap.value(QStringLiteral("ipsec-enabled")), QStringLiteral("yes"));
        QCOMPARE(secretMap.value(QStringLiteral("ipsec-psk")), QStringLiteral("shared"));

        QSignalSpy failed(&handler, &Handler::vpnAddFailed);
        handler.addVpn({{QStringLiteral("kind"), QStringLiteral("openvpn")}, {QStringLiteral("server"), QStringLiteral("x")}});
        QCOMPARE(failed.size(), 1);
    }

    void wireGuardImportsAsPrivateAndManual()
    {
        QTemporaryDir directory;
        QFile file(directory.filePath(QStringLiteral("Home Office.conf")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[Interface]\nPrivateKey = yAnz5TF+lXXJte14tji3zlMNq+hd2rYUIgJBgB3fBmk=\n"
                   "Address = 10.99.0.2/24\nDNS = 10.99.0.1\n\n"
                   "[Peer]\nPublicKey = xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg=\n"
                   "AllowedIPs = 10.99.0.0/24\nEndpoint = 192.0.2.1:51820\n");
        file.close();

        Handler handler;
        QSignalSpy imported(&handler, &Handler::vpnAdded);
        handler.importVpn(QUrl::fromLocalFile(file.fileName()));
        QTRY_COMPARE(imported.size(), 1);
        QCOMPARE(nm->count(QStringLiteral("AddConnection")), 1);

        const QDBusMessage call = nm->last(QStringLiteral("AddConnection"));
        QCOMPARE(call.signature(), QStringLiteral("a{sa{sv}}"));
        const auto settings = qdbus_cast<NMVariantMapMap>(call.arguments().value(0));
        const QVariantMap connection = settings.value(QStringLiteral("connection"));
        QCOMPARE(connection.value(QStringLiteral("id")).toString(), QStringLiteral("Home Office"));
        QCOMPARE(connection.value(QStringLiteral("autoconnect")).toBool(), false);
        QCOMPARE(connection.value(QStringLiteral("permissions")).toStringList(), QStringList {NetworkClient::userPermission()});
        QCOMPARE(connection.value(QStringLiteral("uuid")).toString(), imported.first().first().toString());

        const auto signature = [&](const QString &setting, const QString &key) {
            return settings.value(setting).value(key).value<QDBusArgument>().currentSignature();
        };
        QCOMPARE(signature(QStringLiteral("wireguard"), QStringLiteral("peers")), QStringLiteral("aa{sv}"));
        QCOMPARE(signature(QStringLiteral("ipv4"), QStringLiteral("address-data")), QStringLiteral("aa{sv}"));
        QCOMPARE(signature(QStringLiteral("ipv4"), QStringLiteral("dns")), QStringLiteral("au"));
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
    void addVpn(const QString &path, const QString &name, const QString &service, qulonglong timestamp)
    {
        nm->settings[path] = {
            {QStringLiteral("connection"),
             {{QStringLiteral("id"), name},
              {QStringLiteral("uuid"), name.toLower()},
              {QStringLiteral("type"), QStringLiteral("vpn")},
              {QStringLiteral("timestamp"), timestamp}}},
            {QStringLiteral("vpn"),
             {{QStringLiteral("service-type"), service},
              {QStringLiteral("data"),
               QVariant::fromValue(QMap<QString, QString> {{QStringLiteral("connection-type"), QStringLiteral("password")}})}}},
        };
        nm->add(path, {{FakeNM::connection, {{QStringLiteral("Flags"), 0u}}}});
    }

    void addWireGuard(const QString &path, const QString &name, qulonglong timestamp)
    {
        nm->settings[path] = {
            {QStringLiteral("connection"),
             {{QStringLiteral("id"), name},
              {QStringLiteral("uuid"), name.toLower()},
              {QStringLiteral("type"), QStringLiteral("wireguard")},
              {QStringLiteral("timestamp"), timestamp}}},
            {QStringLiteral("wireguard"), {{QStringLiteral("listen-port"), 0u}}},
        };
        nm->add(path, {{FakeNM::connection, {{QStringLiteral("Flags"), 0u}}}});
    }

    const QString homeActive = FakeNM::managerPath + QStringLiteral("/ActiveConnection/2");
    const QString officeVpn = FakeNM::managerPath + QStringLiteral("/Settings/30");
    const QString branchVpn = FakeNM::managerPath + QStringLiteral("/Settings/31");
    const QString homeVpn = FakeNM::managerPath + QStringLiteral("/Settings/32");
    const QString vpnInterface = QStringLiteral("org.freedesktop.NetworkManager.VPN.Connection");

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
    // Only OpenVPN's plugin is installed.
    QTemporaryDir plugins;
    QFile name(plugins.filePath(QStringLiteral("nm-openvpn-service.name")));
    if (!name.open(QIODevice::WriteOnly))
        return 1;
    name.write("[VPN Connection]\nname=openvpn\nservice=org.freedesktop.NetworkManager.openvpn\n");
    name.close();
    VpnUtils::setPluginDirectories({plugins.path()});
    NetworkClientTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_networkclient.moc"
