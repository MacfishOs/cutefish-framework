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

#ifndef CUTEFISH_NETWORK_NETWORKMODEL_H
#define CUTEFISH_NETWORK_NETWORKMODEL_H

#include <network_export.h>

#include <QAbstractListModel>
#include <QDateTime>
#include <QtQml/qqmlregistration.h>

class NetworkClient;

// The wired profiles and Wi-Fi networks of every managed device. Rows are worked out
// again from NetworkClient's cache after each change and only the differences are
// reported, so a row stays put while its access points come and go.
class CUTEFISH_NETWORK_EXPORT NetworkModel : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit NetworkModel(QObject *parent = nullptr);

    enum ItemRole {
        ConnectionDetailsRole = Qt::UserRole + 1,
        ConnectionIconRole,
        ConnectionPathRole,
        ConnectionStateRole,
        DeviceName,
        DevicePathRole,
        DeviceStateRole,
        DuplicateRole,
        ItemUniqueNameRole,
        ItemTypeRole,
        LastUsedRole,
        LastUsedDateOnlyRole,
        NameRole,
        SecurityTypeRole,
        SecurityTypeStringRole,
        SectionRole,
        SignalRole,
        SlaveRole,
        SsidRole,
        SpecificPathRole,
        TimeStampRole,
        TypeRole,
        UniRole,
        UuidRole,
        VpnState,
        VpnType,
        RxBytesRole,
        TxBytesRole,
        IpAddressRole,
        IpV6AddressRole,
        GateWayRole,
        NameServerRole,
        MacAddressRole,
    };
    Q_ENUM(ItemRole)

    enum ConnectionStatus {
        UnknownState = 0,
        Activating,
        Activated,
        Deactivating,
        Deactivated,
    };
    Q_ENUM(ConnectionStatus)

    enum ItemType {
        UnavailableConnection = 0,
        AvailableConnection,
        AvailableAccessPoint,
    };
    Q_ENUM(ItemType)

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    struct Item {
        QString key;
        int type = 0;
        int itemType = UnavailableConnection;
        QString name;
        QString uniqueName;
        QString ssid;
        QString uuid;
        QString connectionPath;
        QString devicePath;
        QString deviceName;
        uint deviceState = 0;
        QString specificPath;
        int connectionState = Deactivated;
        int securityType = -1;
        int signal = 0;
        QDateTime timestamp;
        QString icon;
        QString ipV4Address;
        QString ipV6Address;
        QString gateway;
        QString nameServer;
        QString macAddress;
    };

    void rebuild();
    void addWired(const QString &device, QList<Item> &items) const;
    void addWireless(const QString &device, QList<Item> &items) const;
    void fillDetails(Item &item) const;
    QVariant itemData(const Item &item, int role) const;

    NetworkClient *m_client;
    QList<Item> m_items;
};

#endif // CUTEFISH_NETWORK_NETWORKMODEL_H
