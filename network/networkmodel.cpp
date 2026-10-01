/*
    Copyright 2013-2018 Jan Grulich <jgrulich@redhat.com>

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

#include "networkmodel.h"
#include "networkclient.h"
#include "vpnutils.h"
#include "wifiutils.h"

#include <QHostAddress>
#include <QSet>
#include <QtEndian>

#include <cstring>

using namespace NetworkDBus;

namespace {

const QString connectionSetting = QStringLiteral("connection");
const QString wirelessSetting = QStringLiteral("802-11-wireless");
const QString wirelessSecurity = QStringLiteral("802-11-wireless-security");

// How networks are told apart: by what joining one asks for, not by the exact scheme, so
// a router whose bands differ only in WPA2 and WPA3 is still one network.
enum class SecurityClass {
    Open,
    Wep,
    Personal,
    Enterprise,
};

SecurityClass classOf(Enums::SecurityType type)
{
    switch (type) {
    case Enums::StaticWep:
    case Enums::DynamicWep:
    case Enums::Leap:
        return SecurityClass::Wep;
    case Enums::WpaPsk:
    case Enums::Wpa2Psk:
    case Enums::SAE:
        return SecurityClass::Personal;
    case Enums::WpaEap:
    case Enums::Wpa2Eap:
    case Enums::Wpa3SuiteB192:
        return SecurityClass::Enterprise;
    default:
        return SecurityClass::Open;
    }
}

SecurityClass profileClass(const NetworkClient *client, const QString &profile)
{
    if (!client->settings(profile).contains(wirelessSecurity))
        return SecurityClass::Open;

    const QString keyMgmt = client->setting(profile, wirelessSecurity, QStringLiteral("key-mgmt")).toString();
    if (keyMgmt == QLatin1String("none") || keyMgmt == QLatin1String("ieee8021x"))
        return SecurityClass::Wep;
    if (keyMgmt == QLatin1String("wpa-eap") || keyMgmt == QLatin1String("wpa-eap-suite-b-192"))
        return SecurityClass::Enterprise;
    if (keyMgmt == QLatin1String("owe"))
        return SecurityClass::Open;
    return SecurityClass::Personal;
}

bool isPort(const NetworkClient *client, const QString &profile)
{
    return !client->setting(profile, connectionSetting, QStringLiteral("controller")).toString().isEmpty()
            || !client->setting(profile, connectionSetting, QStringLiteral("master")).toString().isEmpty();
}

QDateTime lastUsed(const NetworkClient *client, const QString &profile)
{
    const qulonglong seconds = client->setting(profile, connectionSetting, QStringLiteral("timestamp")).toULongLong();
    return seconds ? QDateTime::fromSecsSinceEpoch(qint64(seconds)) : QDateTime();
}

// The profiles NetworkManager would activate on the device, and the one it has active.
QStringList profilesFor(const NetworkClient *client, const QString &device, const QString &type)
{
    QStringList profiles;
    QStringList candidates = client->property(device, deviceInterface, QStringLiteral("AvailableConnections")).toStringList();
    const QString active = client->activeConnection(device);
    if (!active.isEmpty())
        candidates << NetworkClient::path(client->property(active, activeInterface, QStringLiteral("Connection")));

    for (const QString &profile : std::as_const(candidates)) {
        if (profiles.contains(profile) || !client->readable(profile) || isPort(client, profile)
            || client->setting(profile, connectionSetting, QStringLiteral("type")).toString() != type)
            continue;
        profiles << profile;
    }
    return profiles;
}

QString firstAddress(const QVariantList &addressData, bool skipLinkLocal)
{
    QString fallback;
    for (const QVariant &entry : addressData) {
        const QString address = entry.toMap().value(QStringLiteral("address")).toString();
        if (!skipLinkLocal || !QHostAddress(address).isLinkLocal())
            return address;
        if (fallback.isEmpty())
            fallback = address;
    }
    return fallback;
}

} // namespace

NetworkModel::NetworkModel(QObject *parent)
    : QAbstractListModel(parent)
    , m_client(NetworkClient::instance())
{
    connect(m_client, &NetworkClient::changed, this, &NetworkModel::rebuild);
    connect(m_client, &NetworkClient::activeStateChanged, this,
            [this](const QString &active, uint state, uint reason) {
                const QString profile = NetworkClient::path(
                        m_client->property(active, activeInterface, QStringLiteral("Connection")));
                if (!VpnUtils::isVpn(m_client, profile))
                    return;
                if (state == ActiveActivating) {
                    m_vpnFailures.remove(profile);
                } else if (state == ActiveDeactivated) {
                    const QString message = VpnUtils::failureMessage(reason);
                    if (!message.isEmpty())
                        m_vpnFailures.insert(profile, message);
                }
            });
    rebuild();
}

int NetworkModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_items.size();
}

QVariant NetworkModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return {};
    return itemData(m_items.at(index.row()), role);
}

QVariant NetworkModel::itemData(const Item &item, int role) const
{
    switch (role) {
    case ConnectionIconRole:
        return item.icon;
    case ConnectionPathRole:
        return item.connectionPath;
    case ConnectionStateRole:
        return item.connectionState;
    case DeviceName:
        return item.deviceName;
    case DevicePathRole:
        return item.devicePath;
    case DeviceStateRole:
        return item.deviceState;
    case DuplicateRole:
    case SlaveRole:
        return false;
    case ItemUniqueNameRole:
        return item.uniqueName;
    case ItemTypeRole:
        return item.itemType;
    case NameRole:
        return item.name;
    case SecurityTypeRole:
        return item.securityType;
    case SecurityTypeStringRole:
        return WifiUtils::securityLabel(Enums::SecurityType(item.securityType));
    case SignalRole:
        return item.signal;
    case SsidRole:
        return item.ssid;
    case SpecificPathRole:
        return item.specificPath;
    case TimeStampRole:
        return item.timestamp;
    case TypeRole:
        return item.type;
    case UniRole:
        return item.connectionPath;
    case UuidRole:
        return item.uuid;
    case IpAddressRole:
        return item.ipV4Address;
    case IpV6AddressRole:
        return item.ipV6Address;
    case GateWayRole:
        return item.gateway;
    case NameServerRole:
        return item.nameServer;
    case MacAddressRole:
        return item.macAddress;
    case VpnType:
        return item.vpnType;
    case StatusMessageRole:
        return item.statusMessage;
    case CanActivateRole:
        return item.canActivate;
    case VpnUsesUserNameRole:
        return item.vpnUsesUserName;
    case VpnUserNameRole:
        return item.vpnUserName;
    case RxBytesRole:
    case TxBytesRole:
    case VpnState:
        return 0;
    case ConnectionDetailsRole:
        return QStringList();
    case LastUsedRole:
    case LastUsedDateOnlyRole:
    case SectionRole:
        return QString();
    default:
        return {};
    }
}

QHash<int, QByteArray> NetworkModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractListModel::roleNames();
    roles[ConnectionDetailsRole] = "connectionDetails";
    roles[ConnectionIconRole] = "connectionIcon";
    roles[ConnectionPathRole] = "connectionPath";
    roles[ConnectionStateRole] = "connectionState";
    roles[DeviceName] = "deviceName";
    roles[DevicePathRole] = "devicePath";
    roles[DeviceStateRole] = "deviceState";
    roles[DuplicateRole] = "duplicate";
    roles[ItemUniqueNameRole] = "itemUniqueName";
    roles[ItemTypeRole] = "itemType";
    roles[LastUsedRole] = "lastUsed";
    roles[LastUsedDateOnlyRole] = "lastUsedDateOnly";
    roles[NameRole] = "name";
    roles[SectionRole] = "section";
    roles[SignalRole] = "signal";
    roles[SlaveRole] = "slave";
    roles[SsidRole] = "ssid";
    roles[SpecificPathRole] = "specificPath";
    roles[SecurityTypeRole] = "securityType";
    roles[SecurityTypeStringRole] = "securityTypeString";
    roles[TimeStampRole] = "timeStamp";
    roles[TypeRole] = "type";
    roles[UniRole] = "uni";
    roles[UuidRole] = "uuid";
    roles[VpnState] = "vpnState";
    roles[VpnType] = "vpnType";
    roles[RxBytesRole] = "rxBytes";
    roles[TxBytesRole] = "txBytes";
    roles[IpAddressRole] = "ipAddress";
    roles[IpV6AddressRole] = "ipV6Address";
    roles[GateWayRole] = "gateWay";
    roles[NameServerRole] = "nameServer";
    roles[MacAddressRole] = "macAddress";
    roles[StatusMessageRole] = "statusMessage";
    roles[CanActivateRole] = "canActivate";
    roles[VpnUsesUserNameRole] = "vpnUsesUserName";
    roles[VpnUserNameRole] = "vpnUserName";
    return roles;
}

void NetworkModel::rebuild()
{
    QList<Item> items;
    if (m_client->ready()) {
        for (const QString &device : m_client->devices()) {
            if (!m_client->property(device, deviceInterface, QStringLiteral("Managed")).toBool())
                continue;
            if (m_client->deviceType(device) == DeviceEthernet)
                addWired(device, items);
            else if (m_client->deviceType(device) == DeviceWifi)
                addWireless(device, items);
        }
        addVpns(items);
    }

    // A name shown twice, say on two adapters, gets its device added.
    QHash<QString, int> names;
    for (const Item &item : std::as_const(items))
        ++names[item.name];
    for (Item &item : items) {
        item.uniqueName = names.value(item.name) > 1 && !item.deviceName.isEmpty()
                ? QStringLiteral("%1 (%2)").arg(item.name, item.deviceName)
                : item.name;
        fillDetails(item);
    }

    QHash<QString, int> wanted;
    for (int i = 0; i < items.size(); ++i)
        wanted.insert(items.at(i).key, i);

    for (int row = m_items.size() - 1; row >= 0; --row) {
        if (wanted.contains(m_items.at(row).key))
            continue;
        beginRemoveRows(QModelIndex(), row, row);
        m_items.removeAt(row);
        endRemoveRows();
    }

    const QList<int> roles = roleNames().keys();
    QSet<QString> present;
    for (int row = 0; row < m_items.size(); ++row) {
        const Item &next = items.at(wanted.value(m_items.at(row).key));
        present.insert(next.key);

        QList<int> changed;
        for (const int role : roles) {
            if (itemData(m_items.at(row), role) != itemData(next, role))
                changed << role;
        }
        if (changed.isEmpty())
            continue;
        m_items[row] = next;
        const QModelIndex index = this->index(row);
        Q_EMIT dataChanged(index, index, changed);
    }

    for (const Item &item : std::as_const(items)) {
        if (present.contains(item.key))
            continue;
        beginInsertRows(QModelIndex(), m_items.size(), m_items.size());
        m_items.append(item);
        endInsertRows();
    }
}

void NetworkModel::addWired(const QString &device, QList<Item> &items) const
{
    const QString active = m_client->activeConnection(device);
    const QString activeProfile = NetworkClient::path(m_client->property(active, activeInterface, QStringLiteral("Connection")));

    for (const QString &profile : profilesFor(m_client, device, QStringLiteral("802-3-ethernet"))) {
        Item item;
        item.type = Enums::Wired;
        item.itemType = AvailableConnection;
        item.uuid = m_client->setting(profile, connectionSetting, QStringLiteral("uuid")).toString();
        item.key = device + QLatin1Char('|') + item.uuid;
        item.name = m_client->setting(profile, connectionSetting, QStringLiteral("id")).toString();
        item.connectionPath = profile;
        item.devicePath = device;
        item.deviceName = m_client->property(device, deviceInterface, QStringLiteral("Interface")).toString();
        item.deviceState = m_client->deviceState(device);
        item.securityType = Enums::NoneSecurity;
        item.timestamp = lastUsed(m_client, profile);
        if (profile == activeProfile)
            item.connectionState = m_client->property(active, activeInterface, QStringLiteral("State")).toInt();
        item.icon = item.connectionState == Activated ? QStringLiteral("network-wired-activated")
                                                     : QStringLiteral("network-wired");
        items << item;
    }
}

void NetworkModel::addWireless(const QString &device, QList<Item> &items) const
{
    struct Network {
        QByteArray ssid;
        SecurityClass securityClass = SecurityClass::Open;
        Enums::SecurityType security = Enums::UnknownSecurity;
        QString accessPoint;
        int strength = -1;
        QString profile;
        QDateTime timestamp;
    };

    const auto ap = [this](const QString &path, const char *name) {
        return m_client->property(path, accessPointInterface, QLatin1String(name));
    };

    const QString deviceName = m_client->property(device, deviceInterface, QStringLiteral("Interface")).toString();
    const QString hwAddress = m_client->property(device, deviceInterface, QStringLiteral("HwAddress")).toString();
    const uint capabilities = m_client->property(device, wirelessInterface, QStringLiteral("WirelessCapabilities")).toUInt();
    const QString activeAp = NetworkClient::path(m_client->property(device, wirelessInterface, QStringLiteral("ActiveAccessPoint")));
    const QString active = m_client->activeConnection(device);
    const QString activeProfile = NetworkClient::path(m_client->property(active, activeInterface, QStringLiteral("Connection")));
    const int activeState = active.isEmpty() ? int(Deactivated)
                                             : m_client->property(active, activeInterface, QStringLiteral("State")).toInt();

    QList<Network> networks;
    int activeNetwork = -1;
    for (const QString &path : m_client->accessPoints(device)) {
        const QByteArray ssid = WifiUtils::ssidKey(ap(path, "Ssid").toByteArray());
        const uint rsn = ap(path, "RsnFlags").toUInt();
        // Hidden networks have no name to show; our own hotspot shows up as an access point
        // with this adapter's address; and an OWE transition network's encrypted half is
        // joined through its open one.
        if (WifiUtils::ssidHidden(ssid) || ap(path, "Mode").toUInt() != ModeInfrastructure
            || (!hwAddress.isEmpty() && ap(path, "HwAddress").toString().compare(hwAddress, Qt::CaseInsensitive) == 0)
            || ((rsn & KeyMgmtOwe) && (rsn & KeyMgmtOweTm)))
            continue;

        const Enums::SecurityType security = WifiUtils::security(capabilities, ap(path, "Flags").toUInt(),
                                                                 ap(path, "WpaFlags").toUInt(), rsn,
                                                                 ModeInfrastructure);
        const SecurityClass securityClass = classOf(security);
        const int strength = ap(path, "Strength").toInt();

        int index = -1;
        for (int i = 0; i < networks.size(); ++i) {
            if (networks.at(i).ssid == ssid && networks.at(i).securityClass == securityClass) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            networks.append({ssid, securityClass});
            index = networks.size() - 1;
        }

        // The access point in use stands for its network; otherwise the strongest does.
        Network &network = networks[index];
        if (path == activeAp) {
            activeNetwork = index;
        } else if (activeNetwork == index || strength <= network.strength) {
            continue;
        }
        network.accessPoint = path;
        network.strength = strength;
        network.security = security;
    }

    QList<QString> unmatched;
    for (const QString &profile : profilesFor(m_client, device, wirelessSetting)) {
        const QString mode = m_client->setting(profile, wirelessSetting, QStringLiteral("mode")).toString();
        if (!mode.isEmpty() && mode != QLatin1String("infrastructure"))
            continue;

        const QByteArray ssid = WifiUtils::ssidKey(m_client->setting(profile, wirelessSetting, QStringLiteral("ssid")).toByteArray());
        const SecurityClass securityClass = profileClass(m_client, profile);
        const bool isActive = profile == activeProfile;

        int index = isActive ? activeNetwork : -1;
        int sameName = -1;
        for (int i = 0; index < 0 && i < networks.size(); ++i) {
            if (networks.at(i).ssid != ssid)
                continue;
            if (networks.at(i).securityClass == securityClass)
                index = i;
            else
                sameName = sameName == -1 ? i : -2;
        }
        // A lone network of that name is the profile's, whatever its scheme says now.
        if (index < 0 && sameName >= 0)
            index = sameName;
        if (index < 0) {
            if (isActive)
                unmatched << profile;
            continue;
        }

        // Several profiles for one network: the active one, else the one used last.
        Network &network = networks[index];
        const QDateTime timestamp = lastUsed(m_client, profile);
        if (!network.profile.isEmpty() && !isActive
            && (network.profile == activeProfile || network.timestamp >= timestamp))
            continue;
        network.profile = profile;
        network.timestamp = timestamp;
    }

    const auto addItem = [&](const Network &network, const QString &key) {
        Item item;
        item.type = Enums::Wireless;
        item.key = key;
        item.ssid = WifiUtils::ssidLabel(network.ssid);
        item.devicePath = device;
        item.deviceName = deviceName;
        item.deviceState = m_client->deviceState(device);
        item.specificPath = network.accessPoint;
        item.securityType = network.security;
        item.signal = qMax(network.strength, 0);
        item.icon = WifiUtils::strengthIcon(item.signal);
        if (network.profile.isEmpty()) {
            item.itemType = AvailableAccessPoint;
            item.name = item.ssid;
        } else {
            item.itemType = AvailableConnection;
            item.connectionPath = network.profile;
            item.uuid = m_client->setting(network.profile, connectionSetting, QStringLiteral("uuid")).toString();
            item.name = m_client->setting(network.profile, connectionSetting, QStringLiteral("id")).toString();
            item.timestamp = network.timestamp;
        }
        // Another user's private profile is still in use here, just not ours to change.
        const bool inUse = !active.isEmpty()
                && (network.profile.isEmpty() ? !network.accessPoint.isEmpty() && network.accessPoint == activeAp
                                              : network.profile == activeProfile);
        if (inUse)
            item.connectionState = activeState;
        items << item;
    };

    for (const Network &network : std::as_const(networks)) {
        const char securityClass = '0' + char(network.securityClass);
        addItem(network, device + QLatin1Char('|') + QString::fromLatin1(network.ssid.toHex()) + QLatin1Char('|')
                        + QLatin1Char(securityClass));
    }

    // Connected to a network no scan has listed, a hidden one for instance.
    for (const QString &profile : std::as_const(unmatched)) {
        Network network;
        network.ssid = WifiUtils::ssidKey(m_client->setting(profile, wirelessSetting, QStringLiteral("ssid")).toByteArray());
        network.profile = profile;
        network.timestamp = lastUsed(m_client, profile);
        network.accessPoint = activeAp;
        switch (profileClass(m_client, profile)) {
        case SecurityClass::Open:
            network.security = Enums::NoneSecurity;
            break;
        case SecurityClass::Wep:
            network.security = Enums::StaticWep;
            break;
        case SecurityClass::Personal:
            network.security = Enums::Wpa2Psk;
            break;
        case SecurityClass::Enterprise:
            network.security = Enums::Wpa2Eap;
            break;
        }
        if (!activeAp.isEmpty())
            network.strength = ap(activeAp, "Strength").toInt();
        addItem(network, device + QLatin1Char('|') + profile);
    }
}

void NetworkModel::addVpns(QList<Item> &items) const
{
    for (const QString &profile : m_client->connections()) {
        if (!VpnUtils::isVpn(m_client, profile))
            continue;

        Item item;
        item.type = Enums::Vpn;
        item.itemType = AvailableConnection;
        item.uuid = m_client->setting(profile, connectionSetting, QStringLiteral("uuid")).toString();
        item.key = QStringLiteral("vpn|") + item.uuid;
        item.name = m_client->setting(profile, connectionSetting, QStringLiteral("id")).toString();
        item.connectionPath = profile;
        item.securityType = Enums::NoneSecurity;
        item.timestamp = lastUsed(m_client, profile);
        item.connectionState = VpnUtils::state(m_client, VpnUtils::activeConnection(m_client, profile));
        item.icon = QStringLiteral("network-vpn");

        const QString kind = VpnUtils::kind(m_client, profile);
        item.vpnType = VpnUtils::kindLabel(kind);
        const QString service = m_client->setting(profile, QStringLiteral("vpn"), QStringLiteral("service-type")).toString();
        const bool installed = kind == QLatin1String("wireguard") || VpnUtils::pluginInstalled(service);

        const QVariantMap data = m_client->setting(profile, QStringLiteral("vpn"), QStringLiteral("data")).toMap();
        QMap<QString, QString> strings;
        for (auto it = data.cbegin(); it != data.cend(); ++it)
            strings.insert(it.key(), it.value().toString());
        const QString userNameKey = VpnUtils::userNameKey(service, strings);
        item.vpnUsesUserName = !userNameKey.isEmpty();
        item.vpnUserName = strings.value(userNameKey);
        if (!installed) {
            item.canActivate = false;
            item.statusMessage = tr("Install %1 to use this VPN.").arg(VpnUtils::package(kind));
        } else if (kind == QLatin1String("openconnect")) {
            item.canActivate = false;
            item.statusMessage = tr("Signing in to OpenConnect VPNs isn't supported yet.");
        } else {
            item.statusMessage = m_vpnFailures.value(profile);
        }
        items << item;
    }
}

void NetworkModel::fillDetails(Item &item) const
{
    // Addresses are those of the device the network is up on.
    if (item.connectionState != Activated)
        return;

    const QString device = item.devicePath;
    const QString ip4 = NetworkClient::path(m_client->property(device, deviceInterface, QStringLiteral("Ip4Config")));
    const QString ip6 = NetworkClient::path(m_client->property(device, deviceInterface, QStringLiteral("Ip6Config")));

    item.ipV4Address = firstAddress(m_client->property(ip4, ip4ConfigInterface, QStringLiteral("AddressData")).toList(), false);
    item.ipV6Address = firstAddress(m_client->property(ip6, ip6ConfigInterface, QStringLiteral("AddressData")).toList(), true);

    item.gateway = m_client->property(ip4, ip4ConfigInterface, QStringLiteral("Gateway")).toString();
    if (item.gateway.isEmpty())
        item.gateway = m_client->property(ip6, ip6ConfigInterface, QStringLiteral("Gateway")).toString();

    const QVariantList nameservers = m_client->property(ip4, ip4ConfigInterface, QStringLiteral("NameserverData")).toList();
    item.nameServer = firstAddress(nameservers, false);
    if (item.nameServer.isEmpty()) {
        // Network byte order, as NetworkManager before 1.14 only offered it.
        const QVariantList legacy = m_client->property(ip4, ip4ConfigInterface, QStringLiteral("Nameservers")).toList();
        if (!legacy.isEmpty())
            item.nameServer = QHostAddress(qFromBigEndian(legacy.first().toUInt())).toString();
    }
    if (item.nameServer.isEmpty()) {
        const QVariantList servers = m_client->property(ip6, ip6ConfigInterface, QStringLiteral("Nameservers")).toList();
        if (!servers.isEmpty() && servers.first().toByteArray().size() == 16) {
            Q_IPV6ADDR address;
            std::memcpy(address.c, servers.first().toByteArray().constData(), 16);
            item.nameServer = QHostAddress(address).toString();
        }
    }

    item.macAddress = m_client->property(device, deviceInterface, QStringLiteral("HwAddress")).toString();
}
