#pragma once

#include "networkdbustypes.h"

#include <network_export.h>

#include <QMap>
#include <QString>
#include <QStringList>

class NetworkClient;

namespace VpnUtils {

// A VPN profile this user may read: a NetworkManager VPN plugin's or WireGuard.
CUTEFISH_NETWORK_EXPORT bool isVpn(const NetworkClient *client, const QString &connection);
// "wireguard", or the plugin's short name, e.g. "openvpn" for
// org.freedesktop.NetworkManager.openvpn.
CUTEFISH_NETWORK_EXPORT QString kind(const NetworkClient *client, const QString &connection);
CUTEFISH_NETWORK_EXPORT QString kindLabel(const QString &kind);
// The Debian package that brings the plugin.
CUTEFISH_NETWORK_EXPORT QString package(const QString &kind);

// Whether NetworkManager has a plugin for the service, as its .name files say.
CUTEFISH_NETWORK_EXPORT bool pluginInstalled(const QString &serviceType);
// Short names of the installed plugins, for importing a file.
CUTEFISH_NETWORK_EXPORT QStringList installedPlugins();
// Where the .name files are looked for; tests point it elsewhere.
CUTEFISH_NETWORK_EXPORT void setPluginDirectories(const QStringList &directories);

// The active connection of the profile, if it is up or on its way.
CUTEFISH_NETWORK_EXPORT QString activeConnection(const NetworkClient *client, const QString &connection);
// An Enums::ConnectionStatus for the active connection, which for a plugin VPN follows
// the VPN's own state.
CUTEFISH_NETWORK_EXPORT int state(const NetworkClient *client, const QString &active);
// What to tell the user about an active connection that ended for this reason; empty when
// they ended it themselves or cancelled its password.
CUTEFISH_NETWORK_EXPORT QString failureMessage(uint reason);

struct SecretField {
    QString key;
    QString label;
    bool secret = true;
};

// What to ask for to connect, as the KDE and GNOME authentication dialogs decide it: the
// hinted secrets when NetworkManager names them, else the plugin's own rules.
CUTEFISH_NETWORK_EXPORT QList<SecretField> secretFields(const QString &serviceType,
                                                        const QMap<QString, QString> &data,
                                                        const QStringList &hints);
// The vpn.data key of the user name the plugin signs in with, if this kind of VPN has
// one; NetworkManager's auth dialogs never ask for it, the profile has to hold it.
CUTEFISH_NETWORK_EXPORT QString userNameKey(const QString &serviceType, const QMap<QString, QString> &data);
// The server's message among the hints, if it sent one.
CUTEFISH_NETWORK_EXPORT QString hintMessage(const QStringList &hints);

// The kinds of VPN a form exists for: {kind, label, installed, package}, WireGuard first.
CUTEFISH_NETWORK_EXPORT QVariantList manualKinds();

// Whether a file's start reads as a wg-quick configuration.
CUTEFISH_NETWORK_EXPORT bool isWireGuardConfig(const QByteArray &content);
// A WireGuard profile from a wg-quick configuration, read as NetworkManager's
// nm_conn_wireguard_import() reads it; empty with `error` set when it isn't one. The id is
// `name`, and the interface is named after it.
CUTEFISH_NETWORK_EXPORT NMVariantMapMap wireGuardProfile(const QByteArray &content, const QString &name,
                                                         QString *error);

// A WireGuard profile as its form shows it: interfaceName, privateKey (empty, a secret),
// addresses and dns as comma-separated text, listenPort, fwmark, mtu (0 for automatic),
// peerRoutes, and peers [{publicKey, presharedKey (empty), hasPresharedKey, endpoint,
// allowedIps, keepalive}].
CUTEFISH_NETWORK_EXPORT QVariantMap wireGuardForm(const NMVariantMapMap &settings);
// The profile the form describes, over `existing` when a profile is edited: a private key
// or preshared key left empty keeps the stored one. Empty with `error` set when a value
// is invalid.
CUTEFISH_NETWORK_EXPORT NMVariantMapMap wireGuardSettings(const QVariantMap &form, const NMVariantMapMap &existing,
                                                          QString *error);
// The profile as a wg-quick file, with its secrets when `settings` holds them.
CUTEFISH_NETWORK_EXPORT QByteArray wireGuardConfig(const NMVariantMapMap &settings);

} // namespace VpnUtils
