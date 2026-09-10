#include "bluetoothdevicesmodel.h"
#include <algorithm>

BluetoothDevicesModel::BluetoothDevicesModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

void BluetoothDevicesModel::setManager(BluetoothManager *manager)
{
    if (m_manager == manager)
        return;
    if (m_manager)
        disconnect(m_manager, nullptr, this, nullptr);
    m_manager = manager;
    if (manager) {
        connect(manager, &BluetoothManager::devicesChanged, this, &BluetoothDevicesModel::rebuild);
        connect(manager, &QObject::destroyed, this, &BluetoothDevicesModel::rebuild);
    }
    rebuild();
    emit managerChanged();
}

int BluetoothDevicesModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_devices.size();
}

QHash<int, QByteArray> BluetoothDevicesModel::roleNames() const
{
    return {{Qt::UserRole, "device"},
            {Qt::UserRole + 1, "name"},
            {Qt::UserRole + 2, "address"},
            {Qt::UserRole + 3, "paired"},
            {Qt::UserRole + 4, "connected"},
            {Qt::UserRole + 5, "ubi"},
            {Qt::UserRole + 6, "batteryPercentage"}};
}

QVariant BluetoothDevicesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_devices.size())
        return {};
    auto *device = m_devices.at(index.row()).data();
    if (!device)
        return {};
    switch (role) {
    case Qt::UserRole:
        return QVariant::fromValue(device);
    case Qt::DisplayRole:
    case Qt::UserRole + 1:
        return device->name();
    case Qt::UserRole + 2:
        return device->address();
    case Qt::UserRole + 3:
        return device->paired();
    case Qt::UserRole + 4:
        return device->connected();
    case Qt::UserRole + 5:
        return device->path();
    case Qt::UserRole + 6:
        return device->batteryPercentage();
    default:
        return {};
    }
}

void BluetoothDevicesModel::rebuild()
{
    QList<QPointer<BluetoothObject>> devices;
    if (m_manager) {
        for (auto *device : m_manager->devices())
            devices.append(device);
    }
    std::sort(devices.begin(), devices.end(), [](const auto &left, const auto &right) {
        if (left->connected() != right->connected())
            return left->connected();
        if (left->paired() != right->paired())
            return left->paired();
        const int comparison = QString::localeAwareCompare(left->name(), right->name());
        return comparison ? comparison < 0 : left->path() < right->path();
    });
    if (devices != m_devices) {
        beginResetModel();
        m_devices = devices;
        endResetModel();
        emit countChanged();
    } else if (!devices.isEmpty()) {
        emit dataChanged(index(0), index(devices.size() - 1));
    }
}
