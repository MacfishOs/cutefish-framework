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
#include <QtQml/qqmlregistration.h>

class NetworkClient;

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
    // Adds a VPN filled in by hand; see VpnUtils::manualProfile() for the fields.
    void addVpn(const QVariantMap &form);
    // What addVpn() can set up: {kind, label, installed, package}.
    QVariantList vpnKinds() const;
    // The user name a VPN signs in with, which lives in its profile rather than with the
    // passwords.
    void setVpnUserName(const QString &connection, const QString &userName);

Q_SIGNALS:
    void vpnAdded(const QString &uuid);
    void vpnAddFailed(const QString &message);
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
    void activeStateChanged(const QString &active, uint state);
    void update();
    QString hotspotActiveConnection() const;
    QString hotspotDevice(uint *capabilities = nullptr) const;
    void hotspotRequestFinished(const QDBusMessage &reply, int activePathArgument);

    NetworkClient *m_client;
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
