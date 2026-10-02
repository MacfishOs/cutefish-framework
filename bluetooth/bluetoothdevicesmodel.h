#pragma once

#include "bluetoothmanager.h"
#include <QAbstractListModel>
#include <QPointer>

class BluetoothDevicesModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DevicesModelPrivate)
    Q_PROPERTY(BluetoothManager *manager READ manager WRITE setManager NOTIFY managerChanged)
    Q_PROPERTY(Filter filter READ filter WRITE setFilter NOTIFY filterChanged)
    // Leaves out devices whose alias is only their address: mostly anonymous
    // BLE advertisers nobody can pick out.
    Q_PROPERTY(bool namedOnly READ namedOnly WRITE setNamedOnly NOTIFY filterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Filter {
        AllDevices,
        PairedDevices,
        UnpairedDevices
    };
    Q_ENUM(Filter)

    explicit BluetoothDevicesModel(QObject *parent = nullptr);

    BluetoothManager *manager() const { return m_manager; }
    Filter filter() const { return m_filter; }
    bool namedOnly() const { return m_namedOnly; }

    void setManager(BluetoothManager *manager);
    void setFilter(Filter filter);
    void setNamedOnly(bool namedOnly);
    int rowCount(const QModelIndex &parent = {}) const override;

    int count() const { return rowCount(); }

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
signals:
    void managerChanged();
    void filterChanged();
    void countChanged();

private:
    bool accepts(const BluetoothObject *device) const;
    void rebuild();
    void insertDevice(int row, BluetoothObject *device);
    void removeDevice(int row);
    QPointer<BluetoothManager> m_manager;
    QList<QPointer<BluetoothObject>> m_devices;
    Filter m_filter = AllDevices;
    bool m_namedOnly = false;
};
