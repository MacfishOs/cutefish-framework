#include "../configuration.h"
#include "../enums.h"
#include "../handler.h"
#include "../networkclient.h"
#include "../networkmodel.h"
#include "../vpnutils.h"
#include "../wifiutils.h"
#include "../wirelessitemsettings.h"
#include "fakesecrets.h"
#include "fixture.h"

#include <QDBusArgument>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include <QtEndian>

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
        const QString active = otherActive();
        QVERIFY(!active.isEmpty());

        // NM_ACTIVE_CONNECTION_STATE_DEACTIVATED for NO_SECRETS.
        nm->emitSignal(active, FakeNM::active, QStringLiteral("StateChanged"), {4u, 9u});
        QTRY_COMPARE(nm->count(QStringLiteral("Delete")), 1);
        QVERIFY(nm->last(QStringLiteral("Delete")).path().startsWith(FakeNM::managerPath + QStringLiteral("/Settings/")));
    }

    void enterpriseJoinSendsOnlyTheSignIn()
    {
        Handler handler;
        const QVariantMap network = handler.wirelessNetwork(library);
        QCOMPARE(network.value(QStringLiteral("device")).toString(), wifiDevice);
        QCOMPARE(network.value(QStringLiteral("name")).toString(), QStringLiteral("Library"));
        QCOMPARE(network.value(QStringLiteral("security")).toInt(), int(Enums::Wpa2Eap));
        QVERIFY(handler.wirelessNetwork(FakeNM::managerPath + QStringLiteral("/AccessPoint/99")).isEmpty());

        handler.addAndActivateEnterprise(wifiDevice, library, QStringLiteral("alice"), QStringLiteral("secret"));
        QTRY_COMPARE(nm->count(QStringLiteral("AddAndActivateConnection")), 1);

        const QDBusMessage call = nm->last(QStringLiteral("AddAndActivateConnection"));
        QCOMPARE(call.arguments().value(2).value<QDBusObjectPath>().path(), library);
        const auto settings = qdbus_cast<NMVariantMapMap>(call.arguments().value(0));

        // Not a hidden network: NetworkManager fills in the SSID and key management.
        QVERIFY(!settings.contains(QStringLiteral("802-11-wireless")));
        QVERIFY(!settings.contains(QStringLiteral("802-11-wireless-security")));
        const QVariantMap eap = settings.value(QStringLiteral("802-1x"));
        QCOMPARE(eap.value(QStringLiteral("eap")).toStringList(),
                 (QStringList {QStringLiteral("peap"), QStringLiteral("ttls")}));
        QCOMPARE(eap.value(QStringLiteral("phase2-auth")).toString(), QStringLiteral("mschapv2"));
        QCOMPARE(eap.value(QStringLiteral("identity")).toString(), QStringLiteral("alice"));
        QCOMPARE(eap.value(QStringLiteral("password")).toString(), QStringLiteral("secret"));
        QCOMPARE(eap.value(QStringLiteral("password-flags")).metaType(), QMetaType::fromType<uint>());
        QCOMPARE(eap.value(QStringLiteral("password-flags")).toUInt(), 1u);
        QCOMPARE(settings.value(QStringLiteral("connection")).value(QStringLiteral("id")).toString(),
                 QStringLiteral("Library"));

        // NetworkManager's completion took it, and made it a WPA-EAP profile for that network.
        QTRY_COMPARE(NetworkClient::instance()->activeConnections().size(), 2);
        const QString profile = NetworkClient::path(
                nm->objects.value(otherActive()).value(FakeNM::active).value(QStringLiteral("Connection")));
        const NMVariantMapMap completed = nm->settings.value(profile);
        QCOMPARE(completed.value(QStringLiteral("802-11-wireless")).value(QStringLiteral("ssid")).toByteArray(),
                 QByteArray("Library"));
        QCOMPARE(completed.value(QStringLiteral("802-11-wireless-security")).value(QStringLiteral("key-mgmt")).toString(),
                 QStringLiteral("wpa-eap"));

        // A personal network is not joined this way, nor an enterprise one without a user name.
        handler.addAndActivateEnterprise(wifiDevice, office, QStringLiteral("alice"), QStringLiteral("secret"));
        handler.addAndActivateConnection(wifiDevice, library);
        QTest::qWait(100);
        QCOMPARE(nm->count(QStringLiteral("AddAndActivateConnection")), 1);
    }

    // The fake answers AddAndActivateConnection as NetworkManager's completion would, so the
    // joins above are checked against it; these are the profiles it must refuse.
    void fakeCompletesLikeNetworkManager_data()
    {
        QTest::addColumn<QString>("accessPoint");
        QTest::addColumn<NMVariantMapMap>("profile");
        QTest::addColumn<bool>("accepted");

        const QVariantMap signIn = WifiUtils::passwordEap(QStringLiteral("alice"), QStringLiteral("secret"), 1u);
        const QVariantMap psk {{QStringLiteral("psk"), QStringLiteral("secret pass")}, {QStringLiteral("psk-flags"), 1u}};

        QTest::newRow("sign-in, enterprise") << library << NMVariantMapMap {{QStringLiteral("802-1x"), signIn}} << true;
        QTest::newRow("password, personal")
                << office << NMVariantMapMap {{QStringLiteral("802-11-wireless-security"), psk}} << true;
        QTest::newRow("nothing, open") << cafe << NMVariantMapMap {} << true;
        QTest::newRow("sign-in, personal") << office << NMVariantMapMap {{QStringLiteral("802-1x"), signIn}} << false;
        QTest::newRow("password, enterprise")
                << library << NMVariantMapMap {{QStringLiteral("802-11-wireless-security"), psk}} << false;
        QTest::newRow("sign-in, open") << cafe << NMVariantMapMap {{QStringLiteral("802-1x"), signIn}} << false;

        QVariantMap anonymous = signIn;
        anonymous.remove(QStringLiteral("identity"));
        QTest::newRow("no user name") << library << NMVariantMapMap {{QStringLiteral("802-1x"), anonymous}} << false;
        QVariantMap noInner = signIn;
        noInner.remove(QStringLiteral("phase2-auth"));
        QTest::newRow("no inner method") << library << NMVariantMapMap {{QStringLiteral("802-1x"), noInner}} << false;
        QTest::newRow("other network's name")
                << library
                << NMVariantMapMap {{QStringLiteral("802-1x"), signIn},
                                    {QStringLiteral("802-11-wireless"), {{QStringLiteral("ssid"), QByteArray("Office")}}}}
                << false;
    }

    void fakeCompletesLikeNetworkManager()
    {
        QFETCH(QString, accessPoint);
        QFETCH(NMVariantMapMap, profile);
        QFETCH(bool, accepted);

        const QString error =
                FakeNM::completeForAccessPoint(profile, nm->objects.value(accessPoint).value(FakeNM::accessPoint));
        QVERIFY2(error.isEmpty() == accepted, qPrintable(error));
    }

    void updateKeepsWhatItDoesNotChange()
    {
        WirelessItemSettings item;
        item.setPath(homeProfile);
        QVERIFY(item.autoConnect());
        QVERIFY(!item.availableToOtherUsers());

        item.setAutoConnect(false);
        // An unrelated change arriving before the profile is re-read must not
        // put the switch back to what the stale cache says.
        Q_EMIT NetworkClient::instance()->changed();
        QVERIFY(!item.autoConnect());
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

    void ipSettingsAreSavedAndReapplied()
    {
        Handler handler;
        QSignalSpy saved(&handler, &Handler::ipSaved);

        // Read back over D-Bus, the address list is a QDBusArgument.
        const QVariantMap home = handler.ipForm(homeProfile);
        QCOMPARE(home.value(QStringLiteral("ipv4Method")).toString(), QStringLiteral("manual"));
        QCOMPARE(home.value(QStringLiteral("ipv4Address")).toString(), QStringLiteral("10.0.0.2"));
        QCOMPARE(home.value(QStringLiteral("ipv4Netmask")).toString(), QStringLiteral("255.255.255.0"));
        QVERIFY(home.value(QStringLiteral("current")).toMap().isEmpty());

        // The wired profile is up; its form offers the addresses it has now.
        QVariantMap form = handler.ipForm(wiredProfile);
        const QVariantMap current = form.value(QStringLiteral("current")).toMap();
        QCOMPARE(current.value(QStringLiteral("ipv4Address")).toString(), QStringLiteral("192.168.1.5"));
        QCOMPARE(current.value(QStringLiteral("ipv4Netmask")).toString(), QStringLiteral("255.255.255.0"));
        QCOMPARE(current.value(QStringLiteral("ipv4Gateway")).toString(), QStringLiteral("192.168.1.1"));

        form.insert(QStringLiteral("ipv4Method"), QStringLiteral("manual"));
        form.insert(QStringLiteral("ipv4Address"), current.value(QStringLiteral("ipv4Address")));
        form.insert(QStringLiteral("ipv4Netmask"), current.value(QStringLiteral("ipv4Netmask")));
        form.insert(QStringLiteral("ipv4Gateway"), current.value(QStringLiteral("ipv4Gateway")));
        form.insert(QStringLiteral("automaticDns"), false);
        form.insert(QStringLiteral("dns"), QStringLiteral("1.1.1.1"));
        handler.saveIp(wiredProfile, form);

        QTRY_COMPARE(saved.count(), 1);
        QCOMPARE(saved.first().first().toString(), wiredProfile);
        QCOMPARE(nm->last(QStringLiteral("Update")).path(), wiredProfile);
        const auto settings = qdbus_cast<NMVariantMapMap>(nm->last(QStringLiteral("Update")).arguments().value(0));
        const QVariantMap ipv4 = settings.value(QStringLiteral("ipv4"));
        QCOMPARE(ipv4.value(QStringLiteral("method")).toString(), QStringLiteral("manual"));
        QCOMPARE(ipv4.value(QStringLiteral("gateway")).toString(), QStringLiteral("192.168.1.1"));
        QCOMPARE(ipv4.value(QStringLiteral("address-data")).value<QDBusArgument>().currentSignature(), QStringLiteral("aa{sv}"));
        QCOMPARE(ipv4.value(QStringLiteral("dns")).value<QDBusArgument>().currentSignature(), QStringLiteral("au"));
        QCOMPARE(ipv4.value(QStringLiteral("ignore-auto-dns")).toBool(), true);

        // The device it is up on takes the change without disconnecting.
        QTRY_COMPARE(nm->count(QStringLiteral("Reapply")), 1);
        const QDBusMessage reapply = nm->last(QStringLiteral("Reapply"));
        QCOMPARE(reapply.path(), wiredDevice);
        QCOMPARE(reapply.signature(), QStringLiteral("a{sa{sv}}tu"));
        QCOMPARE(nm->count(QStringLiteral("ActivateConnection")), 0);

        // A profile that isn't up is only saved.
        form = handler.ipForm(homeProfile);
        form.insert(QStringLiteral("ipv4Address"), QStringLiteral("10.0.0.3"));
        handler.saveIp(homeProfile, form);
        QTRY_COMPARE(saved.count(), 2);
        QCOMPARE(nm->count(QStringLiteral("Update")), 2);
        QTest::qWait(50);
        QCOMPARE(nm->count(QStringLiteral("Reapply")), 1);

        // Saving what is there changes nothing.
        QTRY_COMPARE(handler.ipForm(homeProfile).value(QStringLiteral("ipv4Address")).toString(), QStringLiteral("10.0.0.3"));
        handler.saveIp(homeProfile, handler.ipForm(homeProfile));
        QCOMPARE(saved.count(), 3);
        QCOMPARE(nm->count(QStringLiteral("Update")), 2);
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
        const QString active = otherActive();
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
        const QString active = otherActive();

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

    void vpnFailureIsReported()
    {
        Handler handler;
        QSignalSpy failed(&handler, &Handler::vpnFailed);
        addVpn(officeVpn, QStringLiteral("Office VPN"), QStringLiteral("org.freedesktop.NetworkManager.openvpn"), 1000);
        const QString active = FakeNM::managerPath + QStringLiteral("/ActiveConnection/61");
        nm->add(active, {{FakeNM::active,
                          {{QStringLiteral("Connection"), FakeNM::path(officeVpn)},
                           {QStringLiteral("Devices"), FakeNM::paths({})},
                           {QStringLiteral("Id"), QStringLiteral("Office VPN")},
                           {QStringLiteral("State"), 1u},
                           {QStringLiteral("Type"), QStringLiteral("vpn")},
                           {QStringLiteral("Vpn"), true}}},
                         {vpnInterface, {{QStringLiteral("VpnState"), 3u}}}});
        QTRY_VERIFY(NetworkClient::instance()->activeConnections().contains(active));

        // NO_SECRETS (the prompt was cancelled) and USER_DISCONNECTED are not failures.
        nm->emitSignal(active, FakeNM::active, QStringLiteral("StateChanged"), {4u, 9u});
        nm->emitSignal(active, FakeNM::active, QStringLiteral("StateChanged"), {4u, 2u});
        nm->emitSignal(active, FakeNM::active, QStringLiteral("StateChanged"), {4u, 10u});
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(failed.first().at(0).toString(), QStringLiteral("Office VPN"));
        QCOMPARE(failed.first().at(1).toString(), VpnUtils::failureMessage(10));
    }

    void vpnFormIsSavedInTheProfile()
    {
        NetworkModel model;
        addVpn(officeVpn, QStringLiteral("Office VPN"), QStringLiteral("org.freedesktop.NetworkManager.openvpn"), 1000);
        QTRY_VERIFY(row(model, QStringLiteral("Office VPN")).isValid());
        QVERIFY(row(model, QStringLiteral("Office VPN")).data(NetworkModel::VpnUsesUserNameRole).toBool());
        QVERIFY(row(model, QStringLiteral("Office VPN")).data(NetworkModel::VpnUserNameRole).toString().isEmpty());

        Handler handler;
        const QVariantMap form = handler.vpnForm(QString(), officeVpn);
        QCOMPARE(form.value(QStringLiteral("kind")).toString(), QStringLiteral("openvpn"));
        QCOMPARE(form.value(QStringLiteral("name")).toString(), QStringLiteral("Office VPN"));
        QVERIFY(form.value(QStringLiteral("shared")).toBool());
        QVERIFY(!form.value(QStringLiteral("ownNetworkOnly")).toBool());
        QVERIFY(form.value(QStringLiteral("exportable")).toBool());
        QVariantMap values;
        for (const QVariant &field : form.value(QStringLiteral("fields")).toList())
            values.insert(field.toMap().value(QStringLiteral("key")).toString(), field.toMap().value(QStringLiteral("value")));
        QCOMPARE(values.value(QStringLiteral("connection-type")).toString(), QStringLiteral("password"));

        QSignalSpy saved(&handler, &Handler::vpnSaved);
        values.insert(QStringLiteral("remote"), QStringLiteral("vpn.example.com"));
        values.insert(QStringLiteral("username"), QStringLiteral("alice"));
        handler.saveVpn(officeVpn, {{QStringLiteral("name"), QStringLiteral("Office HQ")},
                                    {QStringLiteral("shared"), false},
                                    {QStringLiteral("ownNetworkOnly"), true},
                                    {QStringLiteral("values"), values},
                                    {QStringLiteral("storage"), QVariantMap {{QStringLiteral("password"), uint(NetworkDBus::SecretSystemOwned)}}},
                                    {QStringLiteral("secrets"), QVariantMap {{QStringLiteral("password"), QStringLiteral("s3cret")}}}});
        QTRY_COMPARE(saved.size(), 1);
        QCOMPARE(nm->count(QStringLiteral("Update")), 1);
        const auto settings = qdbus_cast<NMVariantMapMap>(nm->last(QStringLiteral("Update")).arguments().value(0));
        const QVariantMap connection = settings.value(QStringLiteral("connection"));
        QCOMPARE(connection.value(QStringLiteral("id")).toString(), QStringLiteral("Office HQ"));
        QCOMPARE(connection.value(QStringLiteral("uuid")).toString(), QStringLiteral("office vpn"));
        QCOMPARE(connection.value(QStringLiteral("permissions")).toStringList(), QStringList {NetworkClient::userPermission()});
        QCOMPARE(settings.value(QStringLiteral("ipv4")).value(QStringLiteral("never-default")).toBool(), true);
        const auto data = settings.value(QStringLiteral("vpn")).value(QStringLiteral("data")).value<QDBusArgument>();
        QCOMPARE(data.currentSignature(), QStringLiteral("a{ss}"));
        const auto values2 = qdbus_cast<QMap<QString, QString>>(data);
        QCOMPARE(values2.value(QStringLiteral("username")), QStringLiteral("alice"));
        // Private now, so the password is the agent's rather than the system's.
        QCOMPARE(values2.value(QStringLiteral("password-flags")), QStringLiteral("1"));
        const auto secrets = qdbus_cast<QMap<QString, QString>>(settings.value(QStringLiteral("vpn")).value(QStringLiteral("secrets")).value<QDBusArgument>());
        QCOMPARE(secrets.value(QStringLiteral("password")), QStringLiteral("s3cret"));
        QTRY_COMPARE(row(model, QStringLiteral("Office HQ")).data(NetworkModel::VpnUserNameRole).toString(), QStringLiteral("alice"));

        QSignalSpy failed(&handler, &Handler::vpnSaveFailed);
        values.insert(QStringLiteral("remote"), QString());
        handler.saveVpn(officeVpn, {{QStringLiteral("values"), values}});
        QCOMPARE(failed.size(), 1);
        QCOMPARE(nm->count(QStringLiteral("Update")), 1);
    }

    void vpnSecretsComeFromBothStores()
    {
        addVpn(officeVpn, QStringLiteral("Office VPN"), QStringLiteral("org.freedesktop.NetworkManager.openvpn"), 1000);
        nm->secrets[officeVpn] = {{QStringLiteral("vpn"), {{QStringLiteral("secrets"), QVariant::fromValue(QMap<QString, QString> {{QStringLiteral("cert-pass"), QStringLiteral("pem")}})}}}};
        // This user's password sits in the keyring under the profile's uuid.
        auto keyring = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("fake-secrets"));
        FakeSecretService secrets;
        secrets.items.insert(secrets.collectionPath + QStringLiteral("/1"),
                             {{{QStringLiteral("connection-uuid"), QStringLiteral("office vpn")},
                               {QStringLiteral("setting-name"), QStringLiteral("vpn")},
                               {QStringLiteral("setting-key"), QStringLiteral("password")}},
                              QByteArrayLiteral("hunter2")});
        QVERIFY(keyring.registerVirtualObject(QStringLiteral("/org/freedesktop/secrets"), &secrets, QDBusConnection::SubPath));
        QVERIFY(keyring.registerService(QStringLiteral("org.freedesktop.secrets")));

        Handler handler;
        QSignalSpy loaded(&handler, &Handler::vpnSecretsLoaded);
        QTRY_VERIFY(!NetworkClient::instance()->settings(officeVpn).isEmpty());
        handler.loadVpnSecrets(officeVpn);
        QTRY_COMPARE(loaded.size(), 1);
        const QVariantMap all = loaded.first().at(1).toMap();
        QCOMPARE(all.value(QStringLiteral("cert-pass")).toString(), QStringLiteral("pem"));
        QCOMPARE(all.value(QStringLiteral("password")).toString(), QStringLiteral("hunter2"));

        keyring.unregisterService(QStringLiteral("org.freedesktop.secrets"));
        keyring.unregisterObject(QStringLiteral("/org/freedesktop/secrets"), QDBusConnection::UnregisterTree);
        QDBusConnection::disconnectFromBus(QStringLiteral("fake-secrets"));
    }

    // Settings fetched over D-Bus keep nested lists as QDBusArgument; the
    // WireGuard editor and exporter read them anyway.
    void wireGuardReadsBackFromDBus()
    {
        const QString path = FakeNM::managerPath + QStringLiteral("/Settings/33");
        nm->settings[path] = {
            {QStringLiteral("connection"),
             {{QStringLiteral("id"), QStringLiteral("Work WG")},
              {QStringLiteral("uuid"), QStringLiteral("work wg")},
              {QStringLiteral("type"), QStringLiteral("wireguard")},
              {QStringLiteral("interface-name"), QStringLiteral("wg0")}}},
            {QStringLiteral("wireguard"),
             {{QStringLiteral("private-key-flags"), uint(NetworkDBus::SecretSystemOwned)},
              {QStringLiteral("mtu"), 1280u},
              {QStringLiteral("peers"), QVariant::fromValue(QList<QVariantMap> {{
                   {QStringLiteral("public-key"), QStringLiteral("xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg=")},
                   {QStringLiteral("preshared-key-flags"), uint(NetworkDBus::SecretSystemOwned)},
                   {QStringLiteral("endpoint"), QStringLiteral("vpn.example:51820")},
                   {QStringLiteral("allowed-ips"), QStringList {QStringLiteral("0.0.0.0/0")}},
                   {QStringLiteral("persistent-keepalive"), 25u},
               }})}}},
            {QStringLiteral("ipv4"),
             {{QStringLiteral("method"), QStringLiteral("manual")},
              {QStringLiteral("address-data"), QVariant::fromValue(QList<QVariantMap> {
                   {{QStringLiteral("address"), QStringLiteral("10.0.0.2")}, {QStringLiteral("prefix"), 24u}}})},
              {QStringLiteral("dns"), QVariant::fromValue(QList<uint> {qToBigEndian(0x0a000001u)})},
              {QStringLiteral("dns-search"), QStringList {QStringLiteral("corp.example")}}}},
            {QStringLiteral("ipv6"),
             {{QStringLiteral("method"), QStringLiteral("manual")},
              {QStringLiteral("address-data"), QVariant::fromValue(QList<QVariantMap> {
                   {{QStringLiteral("address"), QStringLiteral("fd00::2")}, {QStringLiteral("prefix"), 64u}}})},
              {QStringLiteral("dns"), QVariant::fromValue(QList<QByteArray> {
                   QByteArray::fromHex("fd000000000000000000000000000001")})}}},
        };
        nm->add(path, {{FakeNM::connection, {{QStringLiteral("Flags"), 0u}}}});
        QTRY_VERIFY(!NetworkClient::instance()->settings(path).isEmpty());
        const NMVariantMapMap settings = NetworkClient::instance()->settings(path);
        QVERIFY(settings.value(QStringLiteral("wireguard")).value(QStringLiteral("peers")).canConvert<QDBusArgument>());

        const QVariantMap form = VpnUtils::wireGuardForm(settings);
        QCOMPARE(form.value(QStringLiteral("addresses")).toString(), QStringLiteral("10.0.0.2/24, fd00::2/64"));
        QCOMPARE(form.value(QStringLiteral("dns")).toString(), QStringLiteral("10.0.0.1, corp.example, fd00::1"));
        QCOMPARE(form.value(QStringLiteral("mtu")).toUInt(), 1280u);
        const QVariantList peers = form.value(QStringLiteral("peers")).toList();
        QCOMPARE(peers.size(), 1);
        const QVariantMap peer = peers.first().toMap();
        QCOMPARE(peer.value(QStringLiteral("publicKey")).toString(), QStringLiteral("xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg="));
        QCOMPARE(peer.value(QStringLiteral("endpoint")).toString(), QStringLiteral("vpn.example:51820"));
        QCOMPARE(peer.value(QStringLiteral("allowedIps")).toString(), QStringLiteral("0.0.0.0/0"));
        QCOMPARE(peer.value(QStringLiteral("keepalive")).toUInt(), 25u);
        QVERIFY(peer.value(QStringLiteral("hasPresharedKey")).toBool());

        // Saved again with the keys left empty, the retained preshared key stays.
        QString error;
        const NMVariantMapMap edited = VpnUtils::wireGuardSettings(form, settings, &error);
        QVERIFY2(!edited.isEmpty(), qPrintable(error));
        const auto editedPeers = edited.value(QStringLiteral("wireguard")).value(QStringLiteral("peers")).value<QList<QVariantMap>>();
        QCOMPARE(editedPeers.size(), 1);
        QCOMPARE(editedPeers.first().value(QStringLiteral("preshared-key-flags")).toUInt(), uint(NetworkDBus::SecretSystemOwned));

        const QByteArray config = VpnUtils::wireGuardConfig(settings);
        QVERIFY(config.contains("Address = 10.0.0.2/24, fd00::2/64"));
        QVERIFY(config.contains("[Peer]\nPublicKey = xTIBA5rboUvnH4htodjb6e697QjLERt1NAB4mZqp8Dg="));
        QVERIFY(config.contains("AllowedIPs = 0.0.0.0/0"));
    }

    void vpnAddedByHandIsPrivate()
    {
        Handler handler;
        QSignalSpy added(&handler, &Handler::vpnAdded);
        handler.saveVpn(QString(), {{QStringLiteral("kind"), QStringLiteral("l2tp")},
                                    {QStringLiteral("name"), QStringLiteral("Branch")},
                                    {QStringLiteral("values"), QVariantMap {{QStringLiteral("gateway"), QStringLiteral("vpn.example.com")},
                                                                           {QStringLiteral("user"), QStringLiteral("alice")},
                                                                           {QStringLiteral("ipsec-enabled"), true}}},
                                    {QStringLiteral("secrets"), QVariantMap {{QStringLiteral("password"), QStringLiteral("s3cret")},
                                                                            {QStringLiteral("ipsec-psk"), QStringLiteral("shared")}}}});
        QTRY_COMPARE(added.size(), 1);

        const auto settings = qdbus_cast<NMVariantMapMap>(nm->last(QStringLiteral("AddConnection")).arguments().value(0));
        const QVariantMap connection = settings.value(QStringLiteral("connection"));
        QCOMPARE(connection.value(QStringLiteral("id")).toString(), QStringLiteral("Branch"));
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
        QCOMPARE(settings.value(QStringLiteral("ipv4")).value(QStringLiteral("method")).toString(), QStringLiteral("auto"));

        // Unnamed, it goes by its server; shared, it belongs to everyone.
        handler.saveVpn(QString(), {{QStringLiteral("kind"), QStringLiteral("pptp")},
                                    {QStringLiteral("shared"), true},
                                    {QStringLiteral("values"), QVariantMap {{QStringLiteral("gateway"), QStringLiteral("pptp.example.com")}}}});
        QTRY_COMPARE(added.size(), 2);
        const auto second = qdbus_cast<NMVariantMapMap>(nm->last(QStringLiteral("AddConnection")).arguments().value(0));
        QCOMPARE(second.value(QStringLiteral("connection")).value(QStringLiteral("id")).toString(), QStringLiteral("pptp.example.com"));
        QVERIFY(second.value(QStringLiteral("connection")).value(QStringLiteral("permissions")).toStringList().isEmpty());
        const auto secondData = qdbus_cast<QMap<QString, QString>>(second.value(QStringLiteral("vpn")).value(QStringLiteral("data")).value<QDBusArgument>());
        QCOMPARE(secondData.value(QStringLiteral("password-flags")), QStringLiteral("0"));

        QSignalSpy failed(&handler, &Handler::vpnAddFailed);
        handler.saveVpn(QString(), {{QStringLiteral("kind"), QStringLiteral("openvpn")}});
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

    static QString otherActive()
    {
        QString active;
        for (const QString &path : NetworkClient::instance()->activeConnections()) {
            if (path != wiredActive)
                active = path;
        }
        return active;
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
