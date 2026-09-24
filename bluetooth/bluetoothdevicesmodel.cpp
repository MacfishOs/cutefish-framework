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

void BluetoothDevicesModel::setFilter(Filter filter)
{
    if (m_filter == filter)
        return;
    m_filter = filter;
    rebuild();
    emit filterChanged();
}

void BluetoothDevicesModel::setNamedOnly(bool namedOnly)
{
    if (m_namedOnly == namedOnly)
        return;
    m_namedOnly = namedOnly;
    rebuild();
    emit filterChanged();
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

bool BluetoothDevicesModel::accepts(const BluetoothObject *device) const
{
    if ((m_filter == PairedDevices && !device->paired())
        || (m_filter == UnpairedDevices && device->paired()))
        return false;
    // BlueZ aliases an unnamed device to its address, dashes for colons.
    return !m_namedOnly
            || device->name() != QString(device->address()).replace(QLatin1Char(':'), QLatin1Char('-'));
}

// Rows are inserted, moved and removed one by one rather than reset: a reset
// rebuilds every delegate, and BlueZ removes each nearby device separately
// when the adapter powers off.
void BluetoothDevicesModel::rebuild()
{
    QList<QPointer<BluetoothObject>> devices;
    if (m_manager) {
        for (auto *device : m_manager->devices()) {
            if (accepts(device))
                devices.append(device);
        }
    }
    std::sort(devices.begin(), devices.end(), [](const auto &left, const auto &right) {
        if (left->connected() != right->connected())
            return left->connected();
        if (left->paired() != right->paired())
            return left->paired();
        const int comparison = QString::localeAwareCompare(left->name(), right->name());
        return comparison ? comparison < 0 : left->path() < right->path();
    });

    const int oldCount = m_devices.size();
    for (int row = m_devices.size() - 1; row >= 0; --row) {
        if (!m_devices.at(row) || !devices.contains(m_devices.at(row)))
            removeRow(row);
    }
    for (int row = 0; row < devices.size(); ++row) {
        if (row < m_devices.size() && m_devices.at(row) == devices.at(row))
            continue;
        const int from = m_devices.indexOf(devices.at(row), row);
        if (from < 0) {
            insertRow(row, devices.at(row));
        } else {
            beginMoveRows({}, from, from, {}, row);
            m_devices.move(from, row);
            endMoveRows();
        }
    }
    if (m_devices.size() != oldCount)
        emit countChanged();
}

void BluetoothDevicesModel::insertRow(int row, BluetoothObject *device)
{
    beginInsertRows({}, row, row);
    m_devices.insert(row, device);
    endInsertRows();
    connect(device, &BluetoothObject::changed, this, [this, device] {
        const int changedRow = m_devices.indexOf(device);
        if (changedRow >= 0)
            emit dataChanged(index(changedRow), index(changedRow));
    });
}

void BluetoothDevicesModel::removeRow(int row)
{
    beginRemoveRows({}, row, row);
    const QPointer<BluetoothObject> device = m_devices.takeAt(row);
    endRemoveRows();
    if (device)
        disconnect(device, nullptr, this, nullptr);
}
