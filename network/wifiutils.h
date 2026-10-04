#pragma once

#include "enums.h"

#include <network_export.h>

#include <QByteArray>
#include <QString>
#include <QVariantMap>

namespace WifiUtils {

// SSIDs are bytes: a label is for display only and never goes back to NetworkManager.
CUTEFISH_NETWORK_EXPORT QString ssidLabel(const QByteArray &ssid);
// Empty, all NUL bytes, or the single space some routers broadcast.
CUTEFISH_NETWORK_EXPORT bool ssidHidden(const QByteArray &ssid);
// What two SSIDs are compared by: old kernels appended a NUL byte.
CUTEFISH_NETWORK_EXPORT QByteArray ssidKey(const QByteArray &ssid);

// The strongest scheme both the device and the access point support, as NetworkManager's
// nm_utils_security_valid() decides; the AP's flags alone when the device reports no
// capabilities.
CUTEFISH_NETWORK_EXPORT Enums::SecurityType security(uint deviceCapabilities, uint apFlags,
                                                     uint wpaFlags, uint rsnFlags, uint mode);
CUTEFISH_NETWORK_EXPORT bool enterprise(Enums::SecurityType type);
// Networks whose one secret the session's prompt asks for: a PSK, SAE password or WEP key.
CUTEFISH_NETWORK_EXPORT bool needsPassword(Enums::SecurityType type);
CUTEFISH_NETWORK_EXPORT QString securityLabel(Enums::SecurityType type);
// The 802-1x setting for signing in with a user name and password: PEAP or TTLS with
// MSCHAPv2 inside, whichever the server offers.
CUTEFISH_NETWORK_EXPORT QVariantMap passwordEap(const QString &identity, const QString &password,
                                                uint passwordFlags);

// 0, 33, 66 or 100: the step of the signal icon, which the lists sort by.
CUTEFISH_NETWORK_EXPORT int strengthStep(int strength);
CUTEFISH_NETWORK_EXPORT QString strengthIcon(int strength);

CUTEFISH_NETWORK_EXPORT bool pskValid(const QString &psk);
CUTEFISH_NETWORK_EXPORT QString generatedPassword();

} // namespace WifiUtils
