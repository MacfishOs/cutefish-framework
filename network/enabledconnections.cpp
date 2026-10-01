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

#include "enabledconnections.h"
#include "networkclient.h"

using namespace NetworkDBus;

EnabledConnections::EnabledConnections(QObject *parent)
    : QObject(parent)
    , m_client(NetworkClient::instance())
{
    connect(m_client, &NetworkClient::changed, this, &EnabledConnections::update);
    update();
}

void EnabledConnections::update()
{
    const auto set = [](bool &field, bool value, auto emitter) {
        if (field == value)
            return;
        field = value;
        emitter(value);
    };

    const bool ready = m_client->ready();
    set(m_networkingEnabled, ready && m_client->manager(QStringLiteral("NetworkingEnabled")).toBool(),
        [this](bool value) { Q_EMIT networkingEnabled(value); });
    set(m_wirelessEnabled, ready && m_client->manager(QStringLiteral("WirelessEnabled")).toBool(),
        [this](bool value) { Q_EMIT wirelessEnabled(value); });
    set(m_wirelessHwEnabled, ready && m_client->manager(QStringLiteral("WirelessHardwareEnabled")).toBool(),
        [this](bool value) { Q_EMIT wirelessHwEnabled(value); });
    set(m_wwanEnabled, ready && m_client->manager(QStringLiteral("WwanEnabled")).toBool(),
        [this](bool value) { Q_EMIT wwanEnabled(value); });
    set(m_wwanHwEnabled, ready && m_client->manager(QStringLiteral("WwanHardwareEnabled")).toBool(),
        [this](bool value) { Q_EMIT wwanHwEnabled(value); });

    bool available = false;
    bool enabled = false;
    for (const QString &device : m_client->devices()) {
        if (m_client->deviceType(device) != DeviceEthernet
            || !m_client->property(device, deviceInterface, QStringLiteral("Managed")).toBool())
            continue;

        available = true;

        // Disconnecting a device clears its autoconnect flag, so this tracks the user's switch.
        const uint state = m_client->deviceState(device);
        if (m_client->property(device, deviceInterface, QStringLiteral("Autoconnect")).toBool()
            || (state >= DevicePrepare && state <= DeviceActivated))
            enabled = true;
    }
    if (available != m_wiredAvailable || enabled != m_wiredEnabled) {
        m_wiredAvailable = available;
        m_wiredEnabled = enabled;
        Q_EMIT wiredChanged();
    }

    if (ready != m_ready) {
        m_ready = ready;
        Q_EMIT readyChanged();
    }
}
