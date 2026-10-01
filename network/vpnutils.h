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

// The kinds of VPN that can be set up by hand: {kind, label, installed, package}.
CUTEFISH_NETWORK_EXPORT QVariantList manualKinds();
// A profile from the Add VPN form: "kind", "name", "server", "userName", "password", and
// per kind "authentication", "caFile", "certFile", "keyFile", "keyPassword",
// "presharedKey", "privateKey", "publicKey", "address", "allowedIps" and "dns". Passwords
// are the agent's to keep; empty with `error` set when something required is missing.
CUTEFISH_NETWORK_EXPORT NMVariantMapMap manualProfile(const QVariantMap &form, QString *error);

// Whether a file's start reads as a wg-quick configuration.
CUTEFISH_NETWORK_EXPORT bool isWireGuardConfig(const QByteArray &content);
// A WireGuard profile from a wg-quick configuration, read as NetworkManager's
// nm_conn_wireguard_import() reads it; empty with `error` set when it isn't one. The id is
// `name`, and the interface is named after it.
CUTEFISH_NETWORK_EXPORT NMVariantMapMap wireGuardProfile(const QByteArray &content, const QString &name,
                                                         QString *error);

} // namespace VpnUtils
