#pragma once

#include "sortfiltermodel.h"

class AudioFilterModel : public SortFilterModel
{
    Q_OBJECT
    Q_PROPERTY(bool filterOutInactiveDevices READ filterOutInactiveDevices WRITE setFilterOutInactiveDevices NOTIFY filtersChanged)
    Q_PROPERTY(QVariantList filters READ filters WRITE setFilters NOTIFY filtersChanged)

public:
    explicit AudioFilterModel(QObject *parent = nullptr);
    bool filterOutInactiveDevices() const { return m_hideInactive; }
    void setFilterOutInactiveDevices(bool hide);
    QVariantList filters() const { return m_filters; }
    void setFilters(const QVariantList &filters);
    Q_INVOKABLE int role(const QByteArray &name) const;

Q_SIGNALS:
    void filtersChanged();

protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;

private:
    bool m_hideInactive = false;
    QVariantList m_filters;
};
