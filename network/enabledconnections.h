/*
    Copyright 2013 Jan Grulich <jgrulich@redhat.com>

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

#ifndef CUTEFISH_NETWORK_ENABLED_CONNECTIONS_H
#define CUTEFISH_NETWORK_ENABLED_CONNECTIONS_H

#include <network_export.h>

#include <QObject>
#include <QtQml/qqmlregistration.h>

class NetworkClient;

class CUTEFISH_NETWORK_EXPORT EnabledConnections : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    // NetworkManager has been read; until then every switch reads off.
    Q_PROPERTY(bool ready READ isReady NOTIFY readyChanged)
    Q_PROPERTY(bool networkingEnabled READ isNetworkingEnabled NOTIFY networkingEnabled)
    Q_PROPERTY(bool wirelessEnabled READ isWirelessEnabled NOTIFY wirelessEnabled)
    Q_PROPERTY(bool wirelessHwEnabled READ isWirelessHwEnabled NOTIFY wirelessHwEnabled)
    Q_PROPERTY(bool wwanEnabled READ isWwanEnabled NOTIFY wwanEnabled)
    Q_PROPERTY(bool wwanHwEnabled READ isWwanHwEnabled NOTIFY wwanHwEnabled)
    Q_PROPERTY(bool wiredAvailable READ isWiredAvailable NOTIFY wiredChanged)
    Q_PROPERTY(bool wiredEnabled READ isWiredEnabled NOTIFY wiredChanged)

public:
    explicit EnabledConnections(QObject *parent = nullptr);

    bool isReady() const { return m_ready; }
    bool isNetworkingEnabled() const { return m_networkingEnabled; }
    bool isWirelessEnabled() const { return m_wirelessEnabled; }
    bool isWirelessHwEnabled() const { return m_wirelessHwEnabled; }
    bool isWwanEnabled() const { return m_wwanEnabled; }
    bool isWwanHwEnabled() const { return m_wwanHwEnabled; }
    bool isWiredAvailable() const { return m_wiredAvailable; }
    bool isWiredEnabled() const { return m_wiredEnabled; }

Q_SIGNALS:
    void readyChanged();
    void networkingEnabled(bool enabled);
    void wirelessEnabled(bool enabled);
    void wirelessHwEnabled(bool enabled);
    void wwanEnabled(bool enabled);
    void wwanHwEnabled(bool enabled);
    void wiredChanged();

private:
    void update();

    NetworkClient *m_client;
    bool m_ready = false;
    bool m_networkingEnabled = false;
    bool m_wirelessEnabled = false;
    bool m_wirelessHwEnabled = false;
    bool m_wwanEnabled = false;
    bool m_wwanHwEnabled = false;
    bool m_wiredAvailable = false;
    bool m_wiredEnabled = false;
};

#endif // CUTEFISH_NETWORK_ENABLED_CONNECTIONS_H
