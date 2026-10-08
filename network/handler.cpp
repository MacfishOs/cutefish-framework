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
#include "secretstore.h"
#include "vpnschema.h"
#include "vpnutils.h"
#include "wifiutils.h"

#include <QDBusConnection>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QTimer>
#include <QUuid>


using namespace NetworkDBus;

// The shell's secret agent, which asks for a new network's password up front.
static const QString promptService = QStringLiteral("com.cutefish.SecretAgent");
static const QString promptPath = QStringLiteral("/SecretAgent");
static const QString promptInterface = QStringLiteral("com.cutefish.SecretAgent");

static const QString wirelessSecurity = QStringLiteral("802-11-wireless-security");

// Activations and profile changes may wait on an administrator password.
static constexpr int kAuthorizedCallTimeoutMs = 2 * 60 * 1000;
static constexpr int kForgetWatchTimeoutMs = 2 * 60 * 1000;

static QVariant objectPath(const QString &path)
{
    return QVariant::fromValue(QDBusObjectPath(path.isEmpty() ? QStringLiteral("/") : path));
}

// At most 32 bytes, cut on a character boundary.
static QByteArray ssidBytes(const QString &name)
{
    QByteArray bytes = name.toUtf8();
    if (bytes.size() <= 32)
        return bytes;
    int size = 32;
    while (size > 0 && (static_cast<uchar>(bytes.at(size)) & 0xc0) == 0x80)
        --size;
    return bytes.left(size);
}

Handler::Handler(QObject *parent)
    : QObject(parent)
    , m_client(NetworkClient::instance())
{
    connect(m_client, &NetworkClient::changed, this, &Handler::update);
    connect(m_client, &NetworkClient::activeStateChanged, this,
            [this](const QString &active, uint state, uint reason) {
                activeStateChanged(active, state);
                if (state == ActiveDeactivated)
                    reportVpnFailure(active, reason);
            });
    connect(m_client, &NetworkClient::objectRemoved, this, [this](const QString &path) {
        // Gone without a final state: the activation never made it.
        if (m_forget.contains(path) || path == m_hotspotActivation)
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

    const QString name = WifiUtils::ssidLabel(
            m_client->property(specificObject, accessPointInterface, QStringLiteral("Ssid")).toByteArray());
    const Enums::SecurityType security = accessPointSecurity(device, specificObject);

    // Enterprise networks need a user name; see addAndActivateEnterprise().
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

    addAndActivate(settings, device, specificObject);
}

void Handler::addAndActivateEnterprise(const QString &device, const QString &specificObject,
                                       const QString &userName, const QString &password)
{
    if (!m_client->has(specificObject, accessPointInterface))
        return;

    // A 192-bit network signs in with EAP-TLS, which needs certificates.
    const Enums::SecurityType security = accessPointSecurity(device, specificObject);
    if (security != Enums::WpaEap && security != Enums::Wpa2Eap)
        return;

    QVariantMap connection {
        {QStringLiteral("id"),
         WifiUtils::ssidLabel(m_client->property(specificObject, accessPointInterface, QStringLiteral("Ssid")).toByteArray())},
    };
    const bool shared = m_client->sharesNewConnections();
    if (!shared)
        connection.insert(QStringLiteral("permissions"), QStringList {NetworkClient::userPermission()});

    // NetworkManager completes the SSID and wpa-eap key management from the access point;
    // only the 802.1X sign-in has to be spelt out.
    NMVariantMapMap settings;
    settings.insert(QStringLiteral("connection"), connection);
    settings.insert(QStringLiteral("802-1x"),
                    WifiUtils::passwordEap(userName, password, shared ? SecretSystemOwned : SecretAgentOwned));
    addAndActivate(settings, device, specificObject);
}

QVariantMap Handler::wirelessNetwork(const QString &accessPoint) const
{
    if (!m_client->has(accessPoint, accessPointInterface))
        return {};

    for (const QString &device : m_client->devices()) {
        if (m_client->deviceType(device) != DeviceWifi || !m_client->accessPoints(device).contains(accessPoint))
            continue;
        return {{QStringLiteral("device"), device},
                {QStringLiteral("name"),
                 WifiUtils::ssidLabel(m_client->property(accessPoint, accessPointInterface, QStringLiteral("Ssid")).toByteArray())},
                {QStringLiteral("security"), int(accessPointSecurity(device, accessPoint))}};
    }
    return {};
}

Enums::SecurityType Handler::accessPointSecurity(const QString &device, const QString &accessPoint) const
{
    const auto ap = [&](const char *name) {
        return m_client->property(accessPoint, accessPointInterface, QLatin1String(name)).toUInt();
    };
    return WifiUtils::security(
            m_client->property(device, wirelessInterface, QStringLiteral("WirelessCapabilities")).toUInt(),
            ap("Flags"), ap("WpaFlags"), ap("RsnFlags"), ap("Mode"));
}

void Handler::addAndActivate(const NMVariantMapMap &settings, const QString &device,
                             const QString &specificObject)
{
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
    if (!m_hotspotActivation.isEmpty() && active == m_hotspotActivation) {
        if (state == ActiveDeactivated) {
            m_hotspotActivation.clear();
            Q_EMIT hotspotFailed(tr("The hotspot could not be started. The Wi-Fi adapter may not "
                                    "support it on this channel or with this password."));
        } else if (state == ActiveActivated) {
            m_hotspotActivation.clear();
        }
    }

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

void Handler::reportVpnFailure(const QString &active, uint reason)
{
    if (!m_client->property(active, activeInterface, QStringLiteral("Vpn")).toBool())
        return;
    // A lost uplink (DEVICE_DISCONNECTED) takes the VPN down without a failure of its own.
    if (reason == 3)
        return;
    const QString message = VpnUtils::failureMessage(reason);
    if (message.isEmpty())
        return;
    Q_EMIT vpnFailed(m_client->property(active, activeInterface, QStringLiteral("Id")).toString(), message);
}

void Handler::deactivateConnection(const QString &connection, const QString &device)
{
    for (const QString &active : m_client->activeConnections()) {
        const QString profile = NetworkClient::path(
                m_client->property(active, activeInterface, QStringLiteral("Connection")));
        const QStringList devices = m_client->property(active, activeInterface, QStringLiteral("Devices")).toStringList();
        const bool vpn = m_client->property(active, activeInterface, QStringLiteral("Vpn")).toBool();

        const bool matches = connection.isEmpty() ? devices.contains(device) : profile == connection;
        if (!matches)
            continue;

        // A VPN, or a profile named without a device (WireGuard brings its own), goes down
        // alone; disconnecting a device also keeps it from connecting again by itself.
        if (vpn || device.isEmpty()) {
            m_client->call(managerPath, managerInterface, QStringLiteral("DeactivateConnection"),
                           {objectPath(active)}, this, {}, kAuthorizedCallTimeoutMs);
        } else if (devices.contains(device)) {
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
    uint capabilities = 0;
    const QString device = hotspotDevice(&capabilities);
    if (device.isEmpty()) {
        Q_EMIT hotspotFailed(tr("No Wi-Fi adapter can run a hotspot."));
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
                       [this](const QDBusMessage &reply) { hotspotRequestFinished(reply, 1); },
                       kAuthorizedCallTimeoutMs);
        return;
    }

    m_client->call(existing, connectionInterface, QStringLiteral("Update"), {QVariant::fromValue(settings)},
                   this, [this, existing, device](const QDBusMessage &reply) {
                       if (reply.type() != QDBusMessage::ReplyMessage) {
                           hotspotRequestFinished(reply, 0);
                           return;
                       }
                       m_client->call(managerPath, managerInterface, QStringLiteral("ActivateConnection"),
                                      {objectPath(existing), objectPath(device), objectPath(QString())}, this,
                                      [this](const QDBusMessage &reply) { hotspotRequestFinished(reply, 0); },
                                      kAuthorizedCallTimeoutMs);
                   },
                   kAuthorizedCallTimeoutMs);
}

void Handler::hotspotRequestFinished(const QDBusMessage &reply, int activePathArgument)
{
    if (reply.type() != QDBusMessage::ReplyMessage) {
        // Cancelling the administrator password prompt is no failure to report.
        if (reply.errorName() != QLatin1String("org.freedesktop.NetworkManager.PermissionDenied"))
            Q_EMIT hotspotFailed(reply.errorMessage());
        return;
    }

    const QString active = reply.arguments().value(activePathArgument).value<QDBusObjectPath>().path();
    const uint state = m_client->property(active, activeInterface, QStringLiteral("State")).toUInt();
    if (state == ActiveActivated)
        return;
    m_hotspotActivation = active;
    // Its state may have changed before this reply arrived.
    if (state == ActiveDeactivated || state == ActiveDeactivating)
        activeStateChanged(active, ActiveDeactivated);
}

void Handler::stopHotspot()
{
    m_hotspotActivation.clear();
    const QString active = hotspotActiveConnection();
    if (active.isEmpty())
        return;
    m_client->call(managerPath, managerInterface, QStringLiteral("DeactivateConnection"),
                   {objectPath(active)}, this, {}, kAuthorizedCallTimeoutMs);
}

void Handler::importVpn(const QUrl &file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();

    QFile config(path);
    if (config.open(QIODevice::ReadOnly | QIODevice::Text) && config.size() < 1024 * 1024) {
        const QByteArray content = config.readAll();
        if (VpnUtils::isWireGuardConfig(content)) {
            QString error;
            const NMVariantMapMap profile = VpnUtils::wireGuardProfile(content, QFileInfo(path).completeBaseName(), &error);
            if (profile.isEmpty())
                Q_EMIT vpnAddFailed(error);
            else
                addPrivateVpn(profile);
            return;
        }
    }

    // NetworkManager's own readers, the ones nmcli and both big desktops use; whichever
    // reads the file first wins.
    QStringList plugins = VpnUtils::installedPlugins();
    const QString suffix = QFileInfo(path).suffix().toLower();
    const QString likely = suffix == QLatin1String("ovpn") || suffix == QLatin1String("conf") ? QStringLiteral("openvpn")
            : suffix == QLatin1String("pcf")                                                 ? QStringLiteral("vpnc")
                                                                                              : QString();
    if (plugins.removeAll(likely))
        plugins.prepend(likely);

    if (plugins.isEmpty()) {
        Q_EMIT vpnAddFailed(tr("No VPN plugin can read this file. Install the one for your VPN, "
                                  "for example network-manager-openvpn for OpenVPN."));
        return;
    }
    importVpnAs(path, plugins, QString());
}

void Handler::importVpnAs(const QString &file, QStringList types, const QString &error)
{
    if (types.isEmpty()) {
        Q_EMIT vpnAddFailed(error.isEmpty() ? tr("This file isn't a VPN configuration this system can read.")
                                               : error);
        return;
    }

    const QString type = types.takeFirst();

    // A plugin's reader runs in nmcli: NetworkManager offers no way to call it over D-Bus.
    auto *process = new QProcess(this);
    process->setProgram(QStringLiteral("nmcli"));
    process->setArguments({QStringLiteral("connection"), QStringLiteral("import"), QStringLiteral("type"), type,
                           QStringLiteral("file"), file});
    connect(process, &QProcess::finished, this,
            [this, process, file, types, error](int code, QProcess::ExitStatus status) {
                process->deleteLater();
                const QString output = QString::fromUtf8(process->readAllStandardOutput());
                const QString message = QString::fromUtf8(process->readAllStandardError()).trimmed();
                if (status == QProcess::NormalExit && code == 0) {
                    const auto uuid = QRegularExpression(QStringLiteral("[0-9a-f]{8}(-[0-9a-f]{4}){3}-[0-9a-f]{12}"))
                                              .match(output);
                    Q_EMIT vpnAdded(uuid.captured());
                    return;
                }
                importVpnAs(file, types, message.isEmpty() ? error : message);
            });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError failure) {
        if (failure != QProcess::FailedToStart)
            return;
        process->deleteLater();
        Q_EMIT vpnAddFailed(tr("nmcli, which reads VPN files, is not installed."));
    });
    process->start();
}

QVariantList Handler::vpnKinds() const
{
    return VpnUtils::manualKinds();
}

static QMap<QString, QString> vpnData(const NMVariantMapMap &settings)
{
    // Straight from D-Bus the a{ss} is still a QDBusArgument.
    const QVariantMap current = NetworkDBus::plain(settings.value(QStringLiteral("vpn")).value(QStringLiteral("data"))).toMap();
    QMap<QString, QString> data;
    for (auto it = current.cbegin(); it != current.cend(); ++it)
        data.insert(it.key(), it.value().toString());
    return data;
}

QVariantMap Handler::vpnForm(const QString &kind, const QString &connection) const
{
    NMVariantMapMap settings;
    QString shortName = kind;
    if (!connection.isEmpty()) {
        settings = m_client->settings(connection);
        shortName = VpnUtils::kind(m_client, connection);
    }
    const bool wireguard = shortName == QLatin1String("wireguard");
    const QVariantMap info = settings.value(QStringLiteral("connection"));
    const bool ownNetworkOnly = settings.value(QStringLiteral("ipv4")).value(QStringLiteral("never-default")).toBool()
            || settings.value(QStringLiteral("ipv6")).value(QStringLiteral("never-default")).toBool();

    QVariantMap form {
        {QStringLiteral("kind"), shortName},
        {QStringLiteral("label"), VpnUtils::kindLabel(shortName)},
        {QStringLiteral("installed"), wireguard || VpnUtils::pluginInstalled(QStringLiteral("org.freedesktop.NetworkManager.") + shortName)},
        {QStringLiteral("package"), VpnUtils::package(shortName)},
        {QStringLiteral("name"), info.value(QStringLiteral("id")).toString()},
        {QStringLiteral("shared"), !connection.isEmpty() && info.value(QStringLiteral("permissions")).toStringList().isEmpty()},
        {QStringLiteral("ownNetworkOnly"), ownNetworkOnly},
        {QStringLiteral("exportable"), wireguard || VpnSchema::exportable(shortName)},
        {QStringLiteral("exportPatterns"), wireguard ? QStringList {QStringLiteral("*.conf")} : VpnSchema::importPatterns(shortName)},
    };
    if (wireguard)
        form.insert(QStringLiteral("wireguard"), VpnUtils::wireGuardForm(settings));
    else
        form.insert(QStringLiteral("fields"), VpnSchema::formFields(shortName, vpnData(settings)));
    return form;
}

void Handler::getSecrets(const QString &connection, const QString &setting,
                         std::function<void(const NMVariantMapMap &)> done)
{
    m_client->call(connection, connectionInterface, QStringLiteral("GetSecrets"), {setting}, this,
                   [done](const QDBusMessage &reply) {
                       // A profile without secrets of that kind, or one this user may not
                       // read them from, gives an error: the form then starts empty.
                       if (reply.type() == QDBusMessage::ReplyMessage)
                           done(qdbus_cast<NMVariantMapMap>(reply.arguments().value(0)));
                       else
                           done({});
                   },
                   kAuthorizedCallTimeoutMs);
}

void Handler::loadVpnSecrets(const QString &connection)
{
    const QString uuid = m_client->setting(connection, QStringLiteral("connection"), QStringLiteral("uuid")).toString();
    if (VpnUtils::kind(m_client, connection) == QLatin1String("wireguard")) {
        getSecrets(connection, QStringLiteral("wireguard"), [this, connection](const NMVariantMapMap &secrets) {
            const QVariantMap wireguard = secrets.value(QStringLiteral("wireguard"));
            QVariantMap presharedKeys;
            const auto peers = wireguard.value(QStringLiteral("peers")).value<QList<QVariantMap>>();
            for (const QVariantMap &peer : peers) {
                if (peer.contains(QStringLiteral("preshared-key")))
                    presharedKeys.insert(peer.value(QStringLiteral("public-key")).toString(), peer.value(QStringLiteral("preshared-key")));
            }
            Q_EMIT vpnSecretsLoaded(connection, {{QStringLiteral("privateKey"), wireguard.value(QStringLiteral("private-key")).toString()},
                                                 {QStringLiteral("presharedKeys"), presharedKeys}});
        });
        return;
    }

    // System-owned secrets come from NetworkManager, this user's from the keyring.
    getSecrets(connection, QStringLiteral("vpn"), [this, connection, uuid](const NMVariantMapMap &secrets) {
        const QVariantMap system = NetworkDBus::plain(secrets.value(QStringLiteral("vpn")).value(QStringLiteral("secrets"))).toMap();
        if (!m_secrets)
            m_secrets = new SecretStore(this);
        m_secrets->load(uuid, QStringLiteral("vpn"), [this, connection, system](const QVariantMap &stored) {
            QVariantMap all = system;
            all.insert(stored);
            Q_EMIT vpnSecretsLoaded(connection, all);
        });
    });
}

void Handler::saveVpn(const QString &connection, const QVariantMap &form)
{
    const bool adding = connection.isEmpty();
    const auto fail = [this, adding](const QString &message) {
        if (adding)
            Q_EMIT vpnAddFailed(message);
        else
            Q_EMIT vpnSaveFailed(message);
    };

    NMVariantMapMap profile = adding ? NMVariantMapMap() : m_client->settings(connection);
    const QString kind = adding ? form.value(QStringLiteral("kind")).toString() : VpnUtils::kind(m_client, connection);
    const bool shared = form.value(QStringLiteral("shared")).toBool();
    const QString name = form.value(QStringLiteral("name")).toString().trimmed();
    QString error;

    if (kind == QLatin1String("wireguard")) {
        profile = VpnUtils::wireGuardSettings(form, profile, &error);
        if (profile.isEmpty())
            return fail(error);
    } else {
        QMap<QString, QString> data = vpnData(profile);
        QMap<QString, QString> secrets;
        // A shared profile's passwords are NetworkManager's to keep, a private one's this
        // user's: a storage choice from the other arrangement moves over.
        QVariantMap storage = form.value(QStringLiteral("storage")).toMap();
        for (auto it = storage.begin(); it != storage.end(); ++it) {
            const uint flags = it->toUInt();
            if (shared && flags == SecretAgentOwned)
                *it = uint(SecretSystemOwned);
            else if (!shared && flags == SecretSystemOwned)
                *it = uint(SecretAgentOwned);
        }
        if (!VpnSchema::apply(kind, form.value(QStringLiteral("values")).toMap(), storage,
                              form.value(QStringLiteral("secrets")).toMap(), shared, data, secrets, &error))
            return fail(error);

        QVariantMap info = profile.value(QStringLiteral("connection"));
        if (adding) {
            info.insert(QStringLiteral("uuid"), QUuid::createUuid().toString(QUuid::WithoutBraces));
            info.insert(QStringLiteral("type"), QStringLiteral("vpn"));
            // Named after the server, as the file imports are named after their file.
            QString fallback;
            for (const VpnSchema::Field &field : VpnSchema::fields(kind)) {
                if (field.required && fallback.isEmpty())
                    fallback = data.value(field.key);
            }
            info.insert(QStringLiteral("id"), fallback.isEmpty() ? VpnUtils::kindLabel(kind) : fallback);
        }
        profile[QStringLiteral("connection")] = info;

        QVariantMap vpn = profile.value(QStringLiteral("vpn"));
        vpn.insert(QStringLiteral("service-type"), QStringLiteral("org.freedesktop.NetworkManager.") + kind);
        vpn.insert(QStringLiteral("data"), QVariant::fromValue(data));
        // Without secrets in the update, NetworkManager keeps the stored ones.
        if (secrets.isEmpty())
            vpn.remove(QStringLiteral("secrets"));
        else
            vpn.insert(QStringLiteral("secrets"), QVariant::fromValue(secrets));
        profile[QStringLiteral("vpn")] = vpn;
        for (const QString &family : {QStringLiteral("ipv4"), QStringLiteral("ipv6")}) {
            if (!profile.value(family).contains(QStringLiteral("method")))
                profile[family].insert(QStringLiteral("method"), QStringLiteral("auto"));
        }
    }

    if (!name.isEmpty())
        profile[QStringLiteral("connection")].insert(QStringLiteral("id"), name);
    profile[QStringLiteral("connection")].insert(QStringLiteral("permissions"),
                                                 shared ? QStringList() : QStringList {NetworkClient::userPermission()});
    const bool ownNetworkOnly = form.value(QStringLiteral("ownNetworkOnly")).toBool();
    for (const QString &family : {QStringLiteral("ipv4"), QStringLiteral("ipv6")}) {
        if (ownNetworkOnly)
            profile[family].insert(QStringLiteral("never-default"), true);
        else
            profile[family].remove(QStringLiteral("never-default"));
    }

    if (adding) {
        addPrivateVpn(profile);
        return;
    }
    m_client->call(connection, connectionInterface, QStringLiteral("Update"), {QVariant::fromValue(profile)}, this,
                   [this, connection](const QDBusMessage &reply) {
                       if (reply.type() == QDBusMessage::ReplyMessage)
                           Q_EMIT vpnSaved(connection);
                       else
                           Q_EMIT vpnSaveFailed(reply.errorMessage());
                   },
                   kAuthorizedCallTimeoutMs);
}

void Handler::exportVpn(const QString &connection, const QUrl &file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (VpnUtils::kind(m_client, connection) == QLatin1String("wireguard")) {
        getSecrets(connection, QStringLiteral("wireguard"), [this, connection, path](const NMVariantMapMap &secrets) {
            NMVariantMapMap settings = m_client->settings(connection);
            const QVariantMap wireguard = secrets.value(QStringLiteral("wireguard"));
            if (!wireguard.contains(QStringLiteral("private-key"))) {
                Q_EMIT vpnExportFailed(tr("The private key can't be read, so the file would be incomplete."));
                return;
            }
            settings[QStringLiteral("wireguard")].insert(QStringLiteral("private-key"), wireguard.value(QStringLiteral("private-key")));
            // Peers come with their preshared keys only here.
            if (wireguard.contains(QStringLiteral("peers")))
                settings[QStringLiteral("wireguard")].insert(QStringLiteral("peers"), wireguard.value(QStringLiteral("peers")));

            QFile out(path);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate) || out.write(VpnUtils::wireGuardConfig(settings)) < 0) {
                Q_EMIT vpnExportFailed(out.errorString());
                return;
            }
            // The file holds the private key.
            out.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner);
            Q_EMIT vpnExported(path);
        });
        return;
    }

    // The plugin's writer runs in nmcli, like its reader.
    const QString uuid = m_client->setting(connection, QStringLiteral("connection"), QStringLiteral("uuid")).toString();
    auto *process = new QProcess(this);
    process->setProgram(QStringLiteral("nmcli"));
    process->setArguments({QStringLiteral("connection"), QStringLiteral("export"), QStringLiteral("uuid"), uuid, path});
    connect(process, &QProcess::finished, this, [this, process, path](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        const QString message = QString::fromUtf8(process->readAllStandardError()).trimmed();
        if (status == QProcess::NormalExit && code == 0)
            Q_EMIT vpnExported(path);
        else
            Q_EMIT vpnExportFailed(message.isEmpty() ? tr("The VPN couldn't be written to a file.") : message);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError failure) {
        if (failure != QProcess::FailedToStart)
            return;
        process->deleteLater();
        Q_EMIT vpnExportFailed(tr("nmcli, which writes VPN files, is not installed."));
    });
    process->start();
}

void Handler::addPrivateVpn(NMVariantMapMap profile)
{
    // As the big desktops do: the VPN is this user's unless the form says otherwise, which
    // needs no administrator password, and comes up only when asked to. A WireGuard profile
    // would otherwise connect the moment it is added, its interface being its own.
    profile[QStringLiteral("connection")].insert(QStringLiteral("autoconnect"), false);
    if (!profile.value(QStringLiteral("connection")).contains(QStringLiteral("permissions")))
        profile[QStringLiteral("connection")].insert(QStringLiteral("permissions"),
                                                     QStringList {NetworkClient::userPermission()});

    const QString uuid = profile.value(QStringLiteral("connection")).value(QStringLiteral("uuid")).toString();
    m_client->call(settingsPath, settingsInterface, QStringLiteral("AddConnection"), {QVariant::fromValue(profile)},
                   this,
                   [this, uuid](const QDBusMessage &reply) {
                       if (reply.type() == QDBusMessage::ReplyMessage)
                           Q_EMIT vpnAdded(uuid);
                       else
                           Q_EMIT vpnAddFailed(reply.errorMessage());
                   },
                   kAuthorizedCallTimeoutMs);
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

QString Handler::hotspotDevice(uint *capabilities) const
{
    // An idle adapter that can run an access point is best; one in use gets disconnected.
    QString device;
    uint deviceCapabilities = 0;
    for (const QString &candidate : m_client->devices()) {
        if (m_client->deviceType(candidate) != DeviceWifi
            || !m_client->property(candidate, deviceInterface, QStringLiteral("Managed")).toBool())
            continue;
        const uint caps = m_client->property(candidate, wirelessInterface,
                                             QStringLiteral("WirelessCapabilities")).toUInt();
        if (!(caps & (CapAp | CapAdhoc)))
            continue;
        const bool idle = m_client->activeConnection(candidate).isEmpty();
        const bool better = device.isEmpty() || ((caps & CapAp) && !(deviceCapabilities & CapAp))
                || (bool(caps & CapAp) == bool(deviceCapabilities & CapAp) && idle
                    && !m_client->activeConnection(device).isEmpty());
        if (better) {
            device = candidate;
            deviceCapabilities = caps;
        }
    }
    if (capabilities)
        *capabilities = deviceCapabilities;
    return device;
}

void Handler::update()
{
    const QString device = hotspotDevice();
    const bool capable = !device.isEmpty();

    // Polkit answers "auth" when an administrator password would do; an unread answer
    // doesn't block either.
    const QString permission = m_client->permission(QStringLiteral("org.freedesktop.NetworkManager.wifi.share.protected"));
    const bool allowed = m_client->manager(QStringLiteral("WirelessEnabled")).toBool()
            && permission != QLatin1String("no");

    // The network this adapter is on goes when the hotspot takes it over.
    QString replaces;
    const QString active = capable ? m_client->activeConnection(device) : QString();
    if (!active.isEmpty()
        && NetworkClient::path(m_client->property(active, activeInterface, QStringLiteral("Connection")))
                != m_client->hotspotConnection())
        replaces = m_client->property(active, activeInterface, QStringLiteral("Id")).toString();

    if (capable != m_hotspotCapable || allowed != m_hotspotAllowed || replaces != m_hotspotReplaces) {
        m_hotspotCapable = capable;
        m_hotspotAllowed = allowed;
        m_hotspotReplaces = replaces;
        Q_EMIT hotspotSupportedChanged();
    }

    const bool hotspot = !hotspotActiveConnection().isEmpty();
    if (hotspot != m_hotspotActive) {
        m_hotspotActive = hotspot;
        Q_EMIT hotspotActiveChanged();
    }
}
