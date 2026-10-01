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

#include "handler.h"
#include "configuration.h"
#include "networkclient.h"
#include "wifiutils.h"

#include <QDBusConnection>
#include <QDBusPendingCallWatcher>
#include <QDebug>
#include <QTimer>
#include <QUuid>

using namespace NetworkDBus;

namespace {

// The shell's secret agent, which asks for a new network's password up front.
const QString promptService = QStringLiteral("com.cutefish.SecretAgent");
const QString promptPath = QStringLiteral("/SecretAgent");
const QString promptInterface = QStringLiteral("com.cutefish.SecretAgent");

const QString wirelessSecurity = QStringLiteral("802-11-wireless-security");

// Activations and profile changes may wait on an administrator password.
constexpr int kAuthorizedCallTimeoutMs = 2 * 60 * 1000;
constexpr int kForgetWatchTimeoutMs = 2 * 60 * 1000;

QVariant objectPath(const QString &path)
{
    return QVariant::fromValue(QDBusObjectPath(path.isEmpty() ? QStringLiteral("/") : path));
}

// At most 32 bytes, cut on a character boundary.
QByteArray ssidBytes(const QString &name)
{
    QByteArray bytes = name.toUtf8();
    if (bytes.size() <= 32)
        return bytes;
    int size = 32;
    while (size > 0 && (static_cast<uchar>(bytes.at(size)) & 0xc0) == 0x80)
        --size;
    return bytes.left(size);
}

} // namespace

Handler::Handler(QObject *parent)
    : QObject(parent)
    , m_client(NetworkClient::instance())
{
    connect(m_client, &NetworkClient::changed, this, &Handler::update);
    connect(m_client, &NetworkClient::activeStateChanged, this,
            [this](const QString &active, uint state, uint) { activeStateChanged(active, state); });
    connect(m_client, &NetworkClient::objectRemoved, this, [this](const QString &path) {
        // Gone without a final state: the activation never made it.
        if (m_forget.contains(path))
            activeStateChanged(path, ActiveDeactivated);
    });
    connect(m_client, &NetworkClient::readyChanged, this, [this] {
        if (!m_client->ready()) {
            m_forget.clear();
            return;
        }
        if (m_scanRequested) {
            m_scanRequested = false;
            requestScan(m_scanInterface);
        }
    });
    update();
}

void Handler::activateConnection(const QString &connection, const QString &device,
                                 const QString &specificObject)
{
    m_client->call(managerPath, managerInterface, QStringLiteral("ActivateConnection"),
                   {objectPath(connection), objectPath(device), objectPath(specificObject)}, this,
                   {}, kAuthorizedCallTimeoutMs);
}

void Handler::addAndActivateConnection(const QString &device, const QString &specificObject,
                                       const QString &password)
{
    addAndActivateWireless(device, specificObject, password, true);
}

void Handler::addAndActivateWireless(const QString &device, const QString &specificObject,
                                     const QString &password, bool askForPassword)
{
    if (!m_client->has(specificObject, accessPointInterface))
        return;

    const auto ap = [&](const char *name) {
        return m_client->property(specificObject, accessPointInterface, QLatin1String(name));
    };
    const QByteArray ssid = ap("Ssid").toByteArray();
    const QString name = WifiUtils::ssidLabel(ssid);
    const Enums::SecurityType security = WifiUtils::security(
            m_client->property(device, wirelessInterface, QStringLiteral("WirelessCapabilities")).toUInt(),
            ap("Flags").toUInt(), ap("WpaFlags").toUInt(), ap("RsnFlags").toUInt(), ap("Mode").toUInt());

    // Enterprise networks need an identity and certificates, which Settings asks for.
    if (WifiUtils::enterprise(security))
        return;

    // Asked for before anything is added: activating first would save the network
    // and show it connecting while the user is still typing, or has cancelled.
    if (askForPassword && password.isEmpty() && WifiUtils::needsPassword(security)) {
        auto message = QDBusMessage::createMethodCall(promptService, promptPath, promptInterface,
                                                      QStringLiteral("ConnectWireless"));
        message << device << specificObject << name << (security == Enums::StaticWep);

        auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
        connect(watcher, &QDBusPendingCallWatcher::finished, this,
                [this, device, specificObject](QDBusPendingCallWatcher *watcher) {
                    watcher->deleteLater();
                    // Without the shell there is no one to ask first; NetworkManager
                    // asks whichever agent there is once activation needs the secret.
                    if (watcher->isError())
                        addAndActivateWireless(device, specificObject, QString(), false);
                });
        return;
    }

    // NetworkManager completes the rest from the access point: the SSID's own bytes, the
    // mode, and the key management, which for a WPA2/WPA3 network is wpa-psk so drivers
    // without SAE still connect.
    QVariantMap connection {{QStringLiteral("id"), name}};
    // A shared profile keeps its password in NetworkManager for every user; a private
    // one keeps it in the user's keyring through the secret agent.
    const bool shared = m_client->sharesNewConnections();
    if (!shared)
        connection.insert(QStringLiteral("permissions"), QStringList {NetworkClient::userPermission()});

    NMVariantMapMap settings;
    settings.insert(QStringLiteral("connection"), connection);
    if (WifiUtils::needsPassword(security)) {
        const uint secretFlags = shared ? SecretSystemOwned : SecretAgentOwned;
        QVariantMap secrets;
        if (security == Enums::StaticWep) {
            secrets.insert(QStringLiteral("wep-key-flags"), secretFlags);
            if (!password.isEmpty()) {
                secrets.insert(QStringLiteral("wep-key0"), password);
                // A 5 or 13 character, or 10 or 26 digit key; not a passphrase.
                secrets.insert(QStringLiteral("wep-key-type"), 1u);
            }
        } else {
            secrets.insert(QStringLiteral("psk-flags"), secretFlags);
            if (!password.isEmpty())
                secrets.insert(QStringLiteral("psk"), password);
        }
        settings.insert(wirelessSecurity, secrets);
    }

    m_client->call(managerPath, managerInterface, QStringLiteral("AddAndActivateConnection"),
                   {QVariant::fromValue(settings), objectPath(device), objectPath(specificObject)}, this,
                   [this](const QDBusMessage &reply) {
                       if (reply.type() != QDBusMessage::ReplyMessage) {
                           qWarning() << "Cannot join the network:" << reply.errorMessage();
                           return;
                       }
                       forgetUnlessActivated(NetworkClient::path(reply.arguments().value(0)),
                                             NetworkClient::path(reply.arguments().value(1)));
                   },
                   kAuthorizedCallTimeoutMs);
}

void Handler::forgetUnlessActivated(const QString &connection, const QString &active)
{
    // A wrong password the user then gave up on would otherwise stay behind as a known
    // network. The active connection arrived before this reply did.
    if (connection.isEmpty())
        return;
    if (active.isEmpty() || !m_client->has(active, activeInterface)) {
        removeConnection(connection);
        return;
    }

    m_forget.insert(active, connection);
    activeStateChanged(active, m_client->property(active, activeInterface, QStringLiteral("State")).toUInt());
    // An activation NetworkManager drops without a final state is let go of.
    QTimer::singleShot(kForgetWatchTimeoutMs, this, [this, active] { m_forget.remove(active); });
}

void Handler::activeStateChanged(const QString &active, uint state)
{
    const auto it = m_forget.constFind(active);
    if (it == m_forget.cend())
        return;

    if (state == ActiveDeactivated) {
        const QString connection = it.value();
        m_forget.erase(it);
        removeConnection(connection);
    } else if (state == ActiveActivated) {
        m_forget.erase(it);
    }
}

void Handler::deactivateConnection(const QString &connection, const QString &device)
{
    for (const QString &active : m_client->activeConnections()) {
        const QString profile = NetworkClient::path(
                m_client->property(active, activeInterface, QStringLiteral("Connection")));
        const QStringList devices = m_client->property(active, activeInterface, QStringLiteral("Devices")).toStringList();
        const bool vpn = m_client->property(active, activeInterface, QStringLiteral("Vpn")).toBool();

        const bool matches = connection.isEmpty() ? devices.contains(device) : profile == connection;
        if (!matches || (!vpn && !devices.contains(device)))
            continue;

        if (vpn) {
            m_client->call(managerPath, managerInterface, QStringLiteral("DeactivateConnection"),
                           {objectPath(active)}, this, {}, kAuthorizedCallTimeoutMs);
        } else {
            m_client->call(device, deviceInterface, QStringLiteral("Disconnect"), {}, this, {},
                           kAuthorizedCallTimeoutMs);
        }
    }
}

void Handler::removeConnection(const QString &connection)
{
    const QString uuid = m_client->setting(connection, QStringLiteral("connection"), QStringLiteral("uuid")).toString();

    // Port profiles go with the profile they belong to.
    if (!uuid.isEmpty()) {
        for (const QString &port : m_client->connections()) {
            const QString controller =
                    m_client->setting(port, QStringLiteral("connection"), QStringLiteral("controller")).toString();
            if (controller == uuid
                || m_client->setting(port, QStringLiteral("connection"), QStringLiteral("master")).toString() == uuid)
                m_client->call(port, connectionInterface, QStringLiteral("Delete"), {}, this, {},
                               kAuthorizedCallTimeoutMs);
        }
    }

    m_client->call(connection, connectionInterface, QStringLiteral("Delete"), {}, this, {},
                   kAuthorizedCallTimeoutMs);
}

void Handler::requestScan(const QString &interface)
{
    if (!m_client->ready()) {
        m_scanRequested = true;
        m_scanInterface = interface;
        return;
    }

    if (!m_client->manager(QStringLiteral("WirelessEnabled")).toBool())
        return;

    // NetworkManager rate-limits and defers scans itself; it only refuses devices that
    // cannot scan, and one in AP mode would drop the hotspot's clients.
    for (const QString &device : m_client->devices()) {
        if (m_client->deviceType(device) != DeviceWifi
            || m_client->deviceState(device) < DeviceDisconnected
            || m_client->property(device, wirelessInterface, QStringLiteral("Mode")).toUInt() == ModeAp)
            continue;
        if (!interface.isEmpty()
            && m_client->property(device, deviceInterface, QStringLiteral("Interface")).toString() != interface)
            continue;

        m_client->call(device, wirelessInterface, QStringLiteral("RequestScan"), {QVariantMap()}, this);
    }
}

void Handler::enableNetworking(bool enable)
{
    m_client->call(managerPath, managerInterface, QStringLiteral("Enable"), {enable}, this, {},
                   kAuthorizedCallTimeoutMs);
}

void Handler::enableWireless(bool enable)
{
    m_client->setProperty(managerPath, managerInterface, QStringLiteral("WirelessEnabled"), enable, this);
}

void Handler::enableWired(bool enable)
{
    for (const QString &device : m_client->devices()) {
        if (m_client->deviceType(device) != DeviceEthernet
            || !m_client->property(device, deviceInterface, QStringLiteral("Managed")).toBool())
            continue;

        // Disconnecting clears the device's autoconnect flag and activating sets it again, so the
        // flag is written only when neither applies; writing it next to either races the other.
        const uint state = m_client->deviceState(device);
        if (!enable && state >= DevicePrepare && state <= DeviceActivated) {
            m_client->call(device, deviceInterface, QStringLiteral("Disconnect"), {}, this, {},
                           kAuthorizedCallTimeoutMs);
        } else if (enable && state == DeviceDisconnected) {
            // "/" lets NetworkManager pick the device's best connection, even one that doesn't autoconnect.
            activateConnection(QString(), device, QString());
        } else {
            m_client->setProperty(device, deviceInterface, QStringLiteral("Autoconnect"), enable, this);
        }
    }
}

void Handler::enableWwan(bool enable)
{
    m_client->setProperty(managerPath, managerInterface, QStringLiteral("WwanEnabled"), enable, this);
}

void Handler::createHotspot()
{
    // An idle adapter that can run an access point is best; one in use gets disconnected.
    QString device;
    uint capabilities = 0;
    for (const QString &candidate : m_client->devices()) {
        if (m_client->deviceType(candidate) != DeviceWifi)
            continue;
        const uint caps = m_client->property(candidate, wirelessInterface,
                                             QStringLiteral("WirelessCapabilities")).toUInt();
        const bool idle = m_client->activeConnection(candidate).isEmpty();
        const bool better = device.isEmpty() || ((caps & CapAp) && !(capabilities & CapAp))
                || (bool(caps & CapAp) == bool(capabilities & CapAp) && idle
                    && !m_client->activeConnection(device).isEmpty());
        if (better) {
            device = candidate;
            capabilities = caps;
        }
    }
    if (device.isEmpty()) {
        qWarning() << "Failed to create hotspot: missing wireless device";
        return;
    }

    const bool apMode = capabilities & CapAp;
    const Configuration configuration;
    const QString name = configuration.hotspotName();
    const QString password = configuration.hotspotPassword();
    const QString existing = m_client->hotspotConnection();
    const QString uuid = existing.isEmpty()
            ? QUuid::createUuid().toString(QUuid::WithoutBraces)
            : m_client->setting(existing, QStringLiteral("connection"), QStringLiteral("uuid")).toString();

    NMVariantMapMap settings;
    settings.insert(QStringLiteral("connection"),
                    {{QStringLiteral("id"), name},
                     {QStringLiteral("uuid"), uuid},
                     {QStringLiteral("type"), QStringLiteral("802-11-wireless")},
                     {QStringLiteral("autoconnect"), false}});
    settings.insert(QStringLiteral("802-11-wireless"),
                    {{QStringLiteral("ssid"), ssidBytes(name)},
                     {QStringLiteral("mode"), apMode ? QStringLiteral("ap") : QStringLiteral("adhoc")}});
    // NetworkManager picks a channel that is valid where the machine is.
    if (!password.isEmpty()) {
        QVariantMap security;
        if (apMode) {
            security.insert(QStringLiteral("key-mgmt"), QStringLiteral("wpa-psk"));
            security.insert(QStringLiteral("psk"), password);
            if (capabilities & CapRsn) {
                security.insert(QStringLiteral("proto"), QStringList {QStringLiteral("rsn")});
                security.insert(QStringLiteral("pairwise"), QStringList {QStringLiteral("ccmp")});
                security.insert(QStringLiteral("group"), QStringList {QStringLiteral("ccmp")});
            }
        } else {
            security.insert(QStringLiteral("key-mgmt"), QStringLiteral("none"));
            security.insert(QStringLiteral("wep-key-type"), 2u);
            security.insert(QStringLiteral("wep-key0"), password);
        }
        settings.insert(wirelessSecurity, security);
    }
    settings.insert(QStringLiteral("ipv4"), {{QStringLiteral("method"), QStringLiteral("shared")}});
    settings.insert(QStringLiteral("ipv6"), {{QStringLiteral("method"), QStringLiteral("ignore")}});

    // One profile, kept and updated, rather than a new one each time.
    if (existing.isEmpty()) {
        m_client->call(managerPath, managerInterface, QStringLiteral("AddAndActivateConnection"),
                       {QVariant::fromValue(settings), objectPath(device), objectPath(QString())}, this,
                       {}, kAuthorizedCallTimeoutMs);
        return;
    }

    m_client->call(existing, connectionInterface, QStringLiteral("Update"), {QVariant::fromValue(settings)},
                   this, [this, existing, device](const QDBusMessage &reply) {
                       if (reply.type() == QDBusMessage::ReplyMessage)
                           activateConnection(existing, device, QString());
                       else
                           qWarning() << "Cannot update the hotspot:" << reply.errorMessage();
                   },
                   kAuthorizedCallTimeoutMs);
}

void Handler::stopHotspot()
{
    const QString active = hotspotActiveConnection();
    if (active.isEmpty())
        return;
    m_client->call(managerPath, managerInterface, QStringLiteral("DeactivateConnection"),
                   {objectPath(active)}, this, {}, kAuthorizedCallTimeoutMs);
}

QString Handler::hotspotActiveConnection() const
{
    const QString hotspot = m_client->hotspotConnection();
    if (hotspot.isEmpty())
        return {};
    for (const QString &active : m_client->activeConnections()) {
        const uint state = m_client->property(active, activeInterface, QStringLiteral("State")).toUInt();
        if (NetworkClient::path(m_client->property(active, activeInterface, QStringLiteral("Connection"))) == hotspot
            && (state == ActiveActivating || state == ActiveActivated))
            return active;
    }
    return {};
}

void Handler::update()
{
    bool wifi = false;
    bool idle = false;
    for (const QString &device : m_client->devices()) {
        if (m_client->deviceType(device) != DeviceWifi)
            continue;
        wifi = true;
        idle |= m_client->deviceState(device) == DeviceDisconnected;
    }

    // A hotspot on the adapter that carries the internet connection would cut it off.
    const bool supported = wifi && m_client->manager(QStringLiteral("WirelessEnabled")).toBool()
            && (idle || m_client->manager(QStringLiteral("PrimaryConnectionType")).toString()
                        != QLatin1String("802-11-wireless"));
    if (supported != m_hotspotSupported) {
        m_hotspotSupported = supported;
        Q_EMIT hotspotSupportedChanged(supported);
    }

    const bool active = !hotspotActiveConnection().isEmpty();
    if (active != m_hotspotActive) {
        m_hotspotActive = active;
        Q_EMIT hotspotActiveChanged();
    }
}
