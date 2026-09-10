#pragma once

#include "bluetoothmanager.h"
#include <QAbstractListModel>
#include <QPointer>

class BluetoothDevicesModel : public QAbstractListModel
{
    Q_OBJECT
    QML_NAMED_ELEMENT(DevicesModelPrivate)
    Q_PROPERTY(BluetoothManager *manager READ manager WRITE setManager NOTIFY managerChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    explicit BluetoothDevicesModel(QObject *parent = nullptr);

    BluetoothManager *manager() const { return m_manager; }

    void setManager(BluetoothManager *manager);
    int rowCount(const QModelIndex &parent = {}) const override;

    int count() const { return rowCount(); }

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
signals:
    void managerChanged();
    void countChanged();

private:
    void rebuild();
    QPointer<BluetoothManager> m_manager;
    QList<QPointer<BluetoothObject>> m_devices;
};
