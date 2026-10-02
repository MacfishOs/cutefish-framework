#pragma once

#include <network_export.h>

#include <QDBusObjectPath>
#include <QMap>
#include <QVariantMap>

using NMVariantMapMap = QMap<QString, QVariantMap>;
using NMObjects = QMap<QDBusObjectPath, NMVariantMapMap>;

Q_DECLARE_METATYPE(NMVariantMapMap)
Q_DECLARE_METATYPE(NMObjects)

CUTEFISH_NETWORK_EXPORT void registerNetworkDBusTypes();

namespace NetworkDBus {

// A received value with every QDBusArgument read out: maps into QVariantMap, structures and
// other arrays into QVariantList, "as" and "ao" into QStringList, "ay" into QByteArray, and
// object paths into strings.
CUTEFISH_NETWORK_EXPORT QVariant plain(const QVariant &value);

inline const QString service = QStringLiteral("org.freedesktop.NetworkManager");
inline const QString rootPath = QStringLiteral("/org/freedesktop");
inline const QString managerPath = QStringLiteral("/org/freedesktop/NetworkManager");
inline const QString settingsPath = QStringLiteral("/org/freedesktop/NetworkManager/Settings");
inline const QString agentManagerPath = QStringLiteral("/org/freedesktop/NetworkManager/AgentManager");
inline const QString secretAgentPath = QStringLiteral("/org/freedesktop/NetworkManager/SecretAgent");

inline const QString managerInterface = QStringLiteral("org.freedesktop.NetworkManager");
inline const QString settingsInterface = QStringLiteral("org.freedesktop.NetworkManager.Settings");
inline const QString connectionInterface =
        QStringLiteral("org.freedesktop.NetworkManager.Settings.Connection");
inline const QString activeInterface = QStringLiteral("org.freedesktop.NetworkManager.Connection.Active");
inline const QString deviceInterface = QStringLiteral("org.freedesktop.NetworkManager.Device");
inline const QString wirelessInterface = QStringLiteral("org.freedesktop.NetworkManager.Device.Wireless");
inline const QString statisticsInterface =
        QStringLiteral("org.freedesktop.NetworkManager.Device.Statistics");
inline const QString accessPointInterface = QStringLiteral("org.freedesktop.NetworkManager.AccessPoint");
inline const QString ip4ConfigInterface = QStringLiteral("org.freedesktop.NetworkManager.IP4Config");
inline const QString ip6ConfigInterface = QStringLiteral("org.freedesktop.NetworkManager.IP6Config");
inline const QString agentManagerInterface = QStringLiteral("org.freedesktop.NetworkManager.AgentManager");
inline const QString propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
inline const QString objectManagerInterface = QStringLiteral("org.freedesktop.DBus.ObjectManager");

// Values from NetworkManager's nm-dbus-interface.h and nm-setting.h.
enum DeviceType : uint {
    DeviceEthernet = 1,
    DeviceWifi = 2,
};

enum DeviceState : uint {
    DeviceUnknown = 0,
    DeviceUnmanaged = 10,
    DeviceUnavailable = 20,
    DeviceDisconnected = 30,
    DevicePrepare = 40,
    DeviceActivated = 100,
    DeviceDeactivating = 110,
    DeviceFailed = 120,
};

enum ActiveState : uint {
    ActiveUnknown = 0,
    ActiveActivating = 1,
    ActiveActivated = 2,
    ActiveDeactivating = 3,
    ActiveDeactivated = 4,
};

enum WifiMode : uint {
    ModeAdhoc = 1,
    ModeInfrastructure = 2,
    ModeAp = 3,
    ModeMesh = 4,
};

enum ApFlags : uint {
    ApPrivacy = 0x1,
};

enum ApSecurity : uint {
    PairWep40 = 0x1,
    PairWep104 = 0x2,
    PairTkip = 0x4,
    PairCcmp = 0x8,
    GroupWep40 = 0x10,
    GroupWep104 = 0x20,
    GroupTkip = 0x40,
    GroupCcmp = 0x80,
    KeyMgmtPsk = 0x100,
    KeyMgmt8021x = 0x200,
    KeyMgmtSae = 0x400,
    KeyMgmtOwe = 0x800,
    KeyMgmtOweTm = 0x1000,
    KeyMgmtEapSuiteB192 = 0x2000,
};

enum WifiCapabilities : uint {
    CapWep40 = 0x1,
    CapWep104 = 0x2,
    CapTkip = 0x4,
    CapCcmp = 0x8,
    CapWpa = 0x10,
    CapRsn = 0x20,
    CapAp = 0x40,
    CapAdhoc = 0x80,
    CapIbssRsn = 0x2000,
};

enum SecretFlags : uint {
    SecretSystemOwned = 0x0,
    SecretAgentOwned = 0x1,
    SecretNotSaved = 0x2,
    SecretNotRequired = 0x4,
};

enum GetSecretsFlags : uint {
    AllowInteraction = 0x1,
    RequestNew = 0x2,
    UserRequested = 0x4,
    NoErrors = 0x40000000,
};

} // namespace NetworkDBus
