/*
    Copyright 2013-2014 Jan Grulich <jgrulich@redhat.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) version 3, or any
    later version accepted by the membership of KDE e.V. (or its
    successor approved by the membership of KDE e.V.), which shall
    act as a proxy defined in Section 6 of version 3 of the license.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef CUTEFISH_NETWORK_HANDLER_H
#define CUTEFISH_NETWORK_HANDLER_H

#include "enums.h"
#include "networkdbustypes.h"

#include <network_export.h>

#include <QDBusMessage>
#include <QHash>
#include <QObject>
#include <QUrl>
#include <QVariantMap>

#include <functional>
#include <QtQml/qqmlregistration.h>

class NetworkClient;
class SecretStore;

// Actions on NetworkManager. Each returns at once; the result shows up in the models as
// NetworkManager reports it.
class CUTEFISH_NETWORK_EXPORT Handler : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // A Wi-Fi adapter can run an access point, or at least an ad-hoc network.
    Q_PROPERTY(bool hotspotCapable READ hotspotCapable NOTIFY hotspotSupportedChanged)
    // Wi-Fi is on and system policy lets this user share a connection.
    Q_PROPERTY(bool hotspotAllowed READ hotspotAllowed NOTIFY hotspotSupportedChanged)
    // createHotspot() can start one now.
    Q_PROPERTY(bool hotspotSupported READ hotspotSupported NOTIFY hotspotSupportedChanged)
    // The Wi-Fi network that starting the hotspot would disconnect from, if any.
    Q_PROPERTY(QString hotspotReplaces READ hotspotReplaces NOTIFY hotspotSupportedChanged)
    Q_PROPERTY(bool hotspotActive READ hotspotActive NOTIFY hotspotActiveChanged)

public:
    explicit Handler(QObject *parent = nullptr);

    bool hotspotCapable() const { return m_hotspotCapable; }
    bool hotspotAllowed() const { return m_hotspotAllowed; }
    bool hotspotSupported() const { return m_hotspotCapable && m_hotspotAllowed; }
    QString hotspotReplaces() const { return m_hotspotReplaces; }
    bool hotspotActive() const { return m_hotspotActive; }

public Q_SLOTS:
    void activateConnection(const QString &connection, const QString &device,
                            const QString &specificObject);
    /**
     * Joins the access point `specificObject` with a new profile. Without a password, a
     * network that needs one has it asked for by the session's secret agent before anything
     * is added, and a new profile that never gets to connect is removed again.
     */
    void addAndActivateConnection(const QString &device, const QString &specificObject,
                                  const QString &password = QString());
    // Joins the WPA or WPA2 Enterprise network `specificObject` with a user name and password.
    void addAndActivateEnterprise(const QString &device, const QString &specificObject,
                                  const QString &userName, const QString &password);
    // The adapter, name and security of an access point in range: {device, name, security};
    // empty once it is gone.
    QVariantMap wirelessNetwork(const QString &accessPoint) const;
    void deactivateConnection(const QString &connection, const QString &device);
    void removeConnection(const QString &connection);
    void requestScan(const QString &interface = QString());

    void enableNetworking(bool enable);
    void enableWireless(bool enable);
    /**
     * Connects or disconnects every managed Ethernet device and keeps it that way,
     * as NetworkManager has no global switch for wired networking.
     */
    void enableWired(bool enable);
    void enableWwan(bool enable);

    void createHotspot();
    void stopHotspot();

    // Adds the VPN a provider's configuration file describes: WireGuard's .conf, or anything
    // an installed NetworkManager plugin can read, such as OpenVPN's .ovpn.
    void importVpn(const QUrl &file);
    // What saveVpn() can set up: {kind, label, installed, package}.
    QVariantList vpnKinds() const;
    // The form for a kind of VPN, filled from `connection` when one is edited: {kind, label,
    // installed, package, name, shared, ownNetworkOnly, exportable, exportPatterns, and
    // fields (VpnSchema::formFields) or wireguard (VpnUtils::wireGuardForm)}.
    QVariantMap vpnForm(const QString &kind, const QString &connection) const;
    // The secrets a profile keeps, for its form; vpnSecretsLoaded() answers with the
    // plugin's keys, or for WireGuard {privateKey, presharedKeys: {publicKey: key}}.
    void loadVpnSecrets(const QString &connection);
    // Adds the VPN the form describes, or saves it over `connection`. The form carries
    // name, shared, ownNetworkOnly, and values, storage and secrets per field, or for
    // WireGuard what VpnUtils::wireGuardSettings() reads. vpnAdded() or vpnSaved() follows.
    void saveVpn(const QString &connection, const QVariantMap &form);
    // Writes the profile as the file its plugin reads, or a wg-quick file.
    void exportVpn(const QString &connection, const QUrl &file);

Q_SIGNALS:
    void vpnAdded(const QString &uuid);
    void vpnAddFailed(const QString &message);
    void vpnSaved(const QString &connection);
    void vpnSaveFailed(const QString &message);
    void vpnSecretsLoaded(const QString &connection, const QVariantMap &secrets);
    void vpnExported(const QString &file);
    void vpnExportFailed(const QString &message);
    // A plugin VPN went down on its own with `message` saying why; a name is the profile's.
    void vpnFailed(const QString &name, const QString &message);
    void hotspotSupportedChanged();
    void hotspotActiveChanged();
    // A hotspot this Handler started failed to come up.
    void hotspotFailed(const QString &message);

private:
    void addAndActivateWireless(const QString &device, const QString &specificObject,
                                const QString &password, bool askForPassword);
    Enums::SecurityType accessPointSecurity(const QString &device, const QString &accessPoint) const;
    void addAndActivate(const NMVariantMapMap &settings, const QString &device,
                        const QString &specificObject);
    void forgetUnlessActivated(const QString &connection, const QString &active);
    void importVpnAs(const QString &file, QStringList types, const QString &error);
    void addPrivateVpn(NMVariantMapMap profile);
    void getSecrets(const QString &connection, const QString &setting,
                    std::function<void(const NMVariantMapMap &)> done);
    void activeStateChanged(const QString &active, uint state);
    void reportVpnFailure(const QString &active, uint reason);
    void update();
    QString hotspotActiveConnection() const;
    QString hotspotDevice(uint *capabilities = nullptr) const;
    void hotspotRequestFinished(const QDBusMessage &reply, int activePathArgument);

    NetworkClient *m_client;
    SecretStore *m_secrets = nullptr;
    bool m_scanRequested = false;
    QString m_scanInterface;
    bool m_hotspotCapable = false;
    bool m_hotspotAllowed = false;
    QString m_hotspotReplaces;
    bool m_hotspotActive = false;
    // The hotspot activation this Handler started, until it is up or has failed.
    QString m_hotspotActivation;
    // New profiles by the active connection of their first activation.
    QHash<QString, QString> m_forget;
};

#endif // CUTEFISH_NETWORK_HANDLER_H
