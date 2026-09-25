/*
 * Copyright (C) 2021 CutefishOS Team.
 *
 * Author:     cutefishos <cutefishos@foxmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "activeconnection.h"

ActiveConnection::ActiveConnection(QObject *parent)
    : QObject(parent)
    , m_wirelessNetwork(nullptr)
{
    statusChanged(NetworkManager::status());
    connect(NetworkManager::notifier(), &NetworkManager::Notifier::statusChanged, this, &ActiveConnection::statusChanged);
}

void ActiveConnection::statusChanged(NetworkManager::Status status)
{
    if (status == NetworkManager::Connected) {
        NetworkManager::ActiveConnection::Ptr activeConnection = NetworkManager::primaryConnection();

        if (activeConnection) {
            NetworkManager::ConnectionSettings::ConnectionType type = activeConnection->type();
            if ((type == NetworkManager::ConnectionSettings::Wireless) && activeConnection->isValid()) {
                NetworkManager::Connection::Ptr selectedConnection = activeConnection->connection();
                m_wirelessName = selectedConnection->name();
                emit wirelessNameChanged();

                updateWirelessIcon(NetworkManager::findNetworkInterface(activeConnection->devices().first()));
            }
        }

    } else {
        m_wirelessName.clear();
        emit wirelessNameChanged();
    }
}

void ActiveConnection::updateWirelessIcon(NetworkManager::Device::Ptr device)
{
    if (!device)
        return;

    // clear
    if (m_wirelessNetwork) {
        disconnect(m_wirelessNetwork.data());
    }

    NetworkManager::WirelessDevice::Ptr wifiDevice = device.objectCast<NetworkManager::WirelessDevice>();
    NetworkManager::AccessPoint::Ptr ap = wifiDevice->activeAccessPoint();

    m_wirelessNetwork = wifiDevice->findNetwork(m_wirelessName);

    if (m_wirelessNetwork) {
        updateWirelessIconForSignalStrength(m_wirelessNetwork->signalStrength());
        connect(m_wirelessNetwork.data(), &NetworkManager::WirelessNetwork::signalStrengthChanged,
                this, &ActiveConnection::updateWirelessIconForSignalStrength, Qt::UniqueConnection);
    }
}

void ActiveConnection::updateWirelessIconForSignalStrength(int strength)
{
    int iconStrength = 0;

    if (strength == 0)
        iconStrength = 0;
    else if (strength <= 33)
        iconStrength = 33;
    else if (strength <= 66)
        iconStrength = 66;
    else
        iconStrength = 100;

    m_wirelessIcon = QString("network-wireless-connected-%1").arg(iconStrength, 2, 10, QLatin1Char('0'));
    emit wirelessIconChanged();
}
