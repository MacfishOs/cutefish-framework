#pragma once

#include "fakenetworkmanager.h"

// One Ethernet device with its profile up, and one idle Wi-Fi device in range of a saved
// network on two access points, an open one, an enterprise one, a WPA2 one and a hidden one.
namespace Fixture {

const QString wiredDevice = FakeNM::managerPath + QStringLiteral("/Devices/1");
const QString wifiDevice = FakeNM::managerPath + QStringLiteral("/Devices/2");
const QString wiredProfile = FakeNM::managerPath + QStringLiteral("/Settings/1");
const QString homeProfile = FakeNM::managerPath + QStringLiteral("/Settings/2");
const QString wiredActive = FakeNM::managerPath + QStringLiteral("/ActiveConnection/1");
const QString ip4Config = FakeNM::managerPath + QStringLiteral("/IP4Config/1");
const QString homeStrong = FakeNM::managerPath + QStringLiteral("/AccessPoint/1");
const QString homeWeak = FakeNM::managerPath + QStringLiteral("/AccessPoint/2");
const QString cafe = FakeNM::managerPath + QStringLiteral("/AccessPoint/3");
const QString hidden = FakeNM::managerPath + QStringLiteral("/AccessPoint/4");
const QString library = FakeNM::managerPath + QStringLiteral("/AccessPoint/5");
const QString office = FakeNM::managerPath + QStringLiteral("/AccessPoint/6");

// NM_802_11_AP_SEC_*: CCMP pairwise and group ciphers with PSK or 802.1X key management.
constexpr uint rsnPsk = 0x8 | 0x80 | 0x100;
constexpr uint rsnEap = 0x8 | 0x80 | 0x200;
// WEP40, WEP104, TKIP, CCMP, WPA, RSN and AP.
constexpr uint wifiCapabilities = 0x1 | 0x2 | 0x4 | 0x8 | 0x10 | 0x20 | 0x40;

inline QVariant addressData(const QString &address)
{
    QVariantMap entry {{QStringLiteral("address"), address}, {QStringLiteral("prefix"), 24u}};
    return QVariant::fromValue(QList<QVariantMap> {entry});
}

inline NMVariantMapMap accessPoint(const QByteArray &ssid, uint flags, uint rsn, uchar strength)
{
    static int address = 0;
    return {{FakeNM::accessPoint,
             {{QStringLiteral("Ssid"), ssid},
              {QStringLiteral("Flags"), flags},
              {QStringLiteral("WpaFlags"), 0u},
              {QStringLiteral("RsnFlags"), rsn},
              {QStringLiteral("Strength"), QVariant::fromValue(strength)},
              {QStringLiteral("Mode"), 2u},
              {QStringLiteral("HwAddress"), QStringLiteral("10:00:00:00:00:%1").arg(++address, 2, 10, QLatin1Char('0'))},
              {QStringLiteral("Frequency"), 2412u}}}};
}

inline void populate(FakeNetworkManager &nm)
{
    nm.settings[wiredProfile] = {
        {QStringLiteral("connection"),
         {{QStringLiteral("id"), QStringLiteral("Wired connection 1")},
          {QStringLiteral("uuid"), QStringLiteral("u-wired")},
          {QStringLiteral("type"), QStringLiteral("802-3-ethernet")},
          {QStringLiteral("timestamp"), qulonglong(1000)}}},
        {QStringLiteral("ipv4"), {{QStringLiteral("method"), QStringLiteral("auto")}}},
    };
    nm.add(wiredProfile, {{FakeNM::connection, {{QStringLiteral("Flags"), 0u}}}}, false);

    nm.settings[homeProfile] = {
        {QStringLiteral("connection"),
         {{QStringLiteral("id"), QStringLiteral("Home")},
          {QStringLiteral("uuid"), QStringLiteral("u-home")},
          {QStringLiteral("type"), QStringLiteral("802-11-wireless")},
          {QStringLiteral("permissions"), QStringList {QStringLiteral("user:alice:")}},
          {QStringLiteral("timestamp"), qulonglong(2000)}}},
        {QStringLiteral("802-11-wireless"),
         {{QStringLiteral("ssid"), QByteArray("Home")}, {QStringLiteral("mode"), QStringLiteral("infrastructure")}}},
        {QStringLiteral("802-11-wireless-security"),
         {{QStringLiteral("key-mgmt"), QStringLiteral("wpa-psk")}, {QStringLiteral("psk-flags"), 1u}}},
        {QStringLiteral("ipv4"),
         {{QStringLiteral("method"), QStringLiteral("manual")},
          {QStringLiteral("address-data"), addressData(QStringLiteral("10.0.0.2"))}}},
    };
    nm.add(homeProfile, {{FakeNM::connection, {{QStringLiteral("Flags"), 0u}}}}, false);

    nm.add(wiredActive,
           {{FakeNM::active,
             {{QStringLiteral("Connection"), FakeNM::path(wiredProfile)},
              {QStringLiteral("Devices"), FakeNM::paths({wiredDevice})},
              {QStringLiteral("State"), 2u},
              {QStringLiteral("Type"), QStringLiteral("802-3-ethernet")},
              {QStringLiteral("Vpn"), false},
              {QStringLiteral("Id"), QStringLiteral("Wired connection 1")}}}},
           false);
    nm.add(ip4Config,
           {{QStringLiteral("org.freedesktop.NetworkManager.IP4Config"),
             {{QStringLiteral("AddressData"), addressData(QStringLiteral("192.168.1.5"))},
              {QStringLiteral("Gateway"), QStringLiteral("192.168.1.1")},
              {QStringLiteral("NameserverData"), addressData(QStringLiteral("192.168.1.1"))}}}},
           false);
    nm.add(wiredDevice,
           {{FakeNM::device,
             {{QStringLiteral("DeviceType"), 1u},
              {QStringLiteral("Interface"), QStringLiteral("eth0")},
              {QStringLiteral("State"), 100u},
              {QStringLiteral("Managed"), true},
              {QStringLiteral("Autoconnect"), true},
              {QStringLiteral("HwAddress"), QStringLiteral("02:00:00:00:00:01")},
              {QStringLiteral("ActiveConnection"), FakeNM::path(wiredActive)},
              {QStringLiteral("AvailableConnections"), FakeNM::paths({wiredProfile})},
              {QStringLiteral("Ip4Config"), FakeNM::path(ip4Config)},
              {QStringLiteral("Ip6Config"), FakeNM::path({})}}}},
           false);

    nm.add(homeStrong, accessPoint("Home", 0x1, rsnPsk, 70), false);
    nm.add(homeWeak, accessPoint("Home", 0x1, rsnPsk, 40), false);
    nm.add(cafe, accessPoint("Cafe", 0x0, 0, 50), false);
    nm.add(hidden, accessPoint(QByteArray(), 0x1, rsnPsk, 60), false);
    nm.add(library, accessPoint("Library", 0x1, rsnEap, 30), false);
    nm.add(office, accessPoint("Office", 0x1, rsnPsk, 20), false);
    nm.add(wifiDevice,
           {{FakeNM::device,
             {{QStringLiteral("DeviceType"), 2u},
              {QStringLiteral("Interface"), QStringLiteral("wlan0")},
              {QStringLiteral("State"), 30u},
              {QStringLiteral("Managed"), true},
              {QStringLiteral("Autoconnect"), true},
              {QStringLiteral("HwAddress"), QStringLiteral("02:00:00:00:00:02")},
              {QStringLiteral("ActiveConnection"), FakeNM::path({})},
              {QStringLiteral("AvailableConnections"), FakeNM::paths({homeProfile})},
              {QStringLiteral("Ip4Config"), FakeNM::path({})},
              {QStringLiteral("Ip6Config"), FakeNM::path({})}}},
            {FakeNM::wireless,
             {{QStringLiteral("WirelessCapabilities"), wifiCapabilities},
              {QStringLiteral("Mode"), 2u},
              {QStringLiteral("ActiveAccessPoint"), FakeNM::path({})},
              {QStringLiteral("AccessPoints"),
               FakeNM::paths({homeStrong, homeWeak, cafe, hidden, library, office})}}}},
           false);

    nm.objects[FakeNM::managerPath][FakeNM::manager][QStringLiteral("PrimaryConnection")] = FakeNM::path(wiredActive);
    nm.objects[FakeNM::managerPath][FakeNM::manager][QStringLiteral("PrimaryConnectionType")] =
            QStringLiteral("802-3-ethernet");
}

} // namespace Fixture
