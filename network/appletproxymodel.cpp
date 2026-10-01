/*
    Copyright 2013-2014 Jan Grulich <jgrulich@redhat.com>
    Copyright 2021 Reven Martin <aj@cutefishos.com>

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

#include "appletproxymodel.h"
#include "enums.h"
#include "networkmodel.h"
#include "wifiutils.h"

#include <QDateTime>

AppletProxyModel::AppletProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setFilterCaseSensitivity(Qt::CaseInsensitive);
    sort(0, Qt::DescendingOrder);
}

AppletProxyModel::Type AppletProxyModel::type() const
{
    return m_type;
}

void AppletProxyModel::setType(Type type)
{
    if (m_type == type)
        return;
    m_type = type;
    Q_EMIT typeChanged();
    invalidateFilter();
}

void AppletProxyModel::setSourceModel(QAbstractItemModel *model)
{
    m_roles.clear();
    if (model) {
        const QHash<int, QByteArray> names = model->roleNames();
        for (auto it = names.cbegin(); it != names.cend(); ++it)
            m_roles.insert(it.value(), it.key());
    }
    QSortFilterProxyModel::setSourceModel(model);
}

QVariant AppletProxyModel::value(const QModelIndex &index, const QByteArray &role) const
{
    const auto it = m_roles.constFind(role);
    return it == m_roles.cend() ? QVariant() : sourceModel()->data(index, it.value());
}

bool AppletProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    const QModelIndex index = sourceModel()->index(sourceRow, 0, sourceParent);

    // Ports are filtered out when not searching, which keeps the state of results clear.
    if (value(index, "slave").toBool() && filterRegularExpression().pattern().isEmpty())
        return false;

    const int type = value(index, "type").toInt();
    if ((m_type == WiredType && type != Enums::Wired) || (m_type == WirelessType && type != Enums::Wireless)
        || m_type == UnknownType)
        return false;

    const int itemType = value(index, "itemType").toInt();
    if (itemType != NetworkModel::AvailableConnection && itemType != NetworkModel::AvailableAccessPoint)
        return false;

    if (filterRegularExpression().pattern().isEmpty())
        return true;

    return value(index, "itemUniqueName").toString().contains(filterRegularExpression().pattern());
}

bool AppletProxyModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    // Sorted descending: whatever compares greater comes first.
    const bool leftConnected = value(left, "connectionState").toInt() == NetworkModel::Activated;
    const bool rightConnected = value(right, "connectionState").toInt() == NetworkModel::Activated;
    if (leftConnected != rightConnected)
        return rightConnected;

    const int leftState = value(left, "connectionState").toInt();
    const int rightState = value(right, "connectionState").toInt();
    if (leftState != rightState)
        return leftState > rightState;

    const bool leftSaved = !value(left, "uuid").toString().isEmpty();
    const bool rightSaved = !value(right, "uuid").toString().isEmpty();
    if (leftSaved != rightSaved)
        return rightSaved;

    const QDateTime leftDate = value(left, "timeStamp").toDateTime();
    const QDateTime rightDate = value(right, "timeStamp").toDateTime();
    if (leftDate != rightDate)
        return leftDate < rightDate;

    // By the icon's step, so rows move only when what they show changes.
    const int leftSignal = WifiUtils::strengthStep(value(left, "signal").toInt());
    const int rightSignal = WifiUtils::strengthStep(value(right, "signal").toInt());
    if (leftSignal != rightSignal)
        return leftSignal < rightSignal;

    return QString::localeAwareCompare(value(left, "name").toString(), value(right, "name").toString()) > 0;
}
