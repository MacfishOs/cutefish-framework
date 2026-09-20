#include "audiofiltermodel.h"
#include "audioobject.h"

AudioFilterModel::AudioFilterModel(QObject *parent)
    : SortFilterModel(parent)
{
}

void AudioFilterModel::setFilterOutInactiveDevices(bool hide)
{
    if (m_hideInactive == hide)
        return;
    m_hideInactive = hide;
    invalidateFilter();
    Q_EMIT filtersChanged();
}

void AudioFilterModel::setFilters(const QVariantList &filters)
{
    if (m_filters == filters)
        return;
    m_filters = filters;
    invalidateFilter();
    Q_EMIT filtersChanged();
}

int AudioFilterModel::role(const QByteArray &name) const
{
    return sourceModel() ? sourceModel()->roleNames().key(name, -1) : -1;
}

bool AudioFilterModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    if (!sourceModel())
        return false;
    const QModelIndex index = sourceModel()->index(row, 0, parent);
    for (const QVariant &value : m_filters) {
        const QVariantMap filter = value.toMap();
        const int filterRole = role(filter.value("role").toByteArray());
        if (filterRole < 0 || sourceModel()->data(index, filterRole) != filter.value("value"))
            return false;
    }
    if (m_hideInactive) {
        const QVariantList ports = sourceModel()->data(index, role("Ports")).toList();
        bool unavailable = !ports.isEmpty();
        for (const QVariant &port : ports)
            unavailable &= port.toMap().value("availability").toInt() == CutefishAudio::AudioObject::Unavailable;
        if (unavailable)
            return false;
    }
    return SortFilterModel::filterAcceptsRow(row, parent);
}
