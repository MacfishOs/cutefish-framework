// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Upstream: chromium/src/ash/display/cros_display_config.cc
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a
//
// Adapted for Cutefish: stable native display requests replace KScreen state.

#include "outputmodel.h"

#include <algorithm>
#include <climits>

#include <QFileInfo>
#include <QRect>
#include <QSet>

using namespace cutefish::display;

DisplayOutputModel::DisplayOutputModel(QObject *parent)
    : QAbstractListModel(parent)
{
}

int DisplayOutputModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QString DisplayOutputModel::name(const Entry &e) const
{
    const auto plainName = [this](const Entry &entry) {
        if (entry.physical.connectionType == 1)
            return tr("Built-in Display");
        // The compositor falls back to the connector when the EDID has no name.
        return entry.physical.model.isEmpty() ? entry.physical.connectorName
                                              : entry.physical.model;
    };
    const QString plain = plainName(e);
    for (const auto &other : m_entries) {
        // Two of the same monitor are told apart by where they are plugged in.
        if (other.physical.sessionId != e.physical.sessionId && plainName(other) == plain
            && !e.physical.connectorName.isEmpty() && plain != e.physical.connectorName)
            return QStringLiteral("%1 (%2)").arg(plain, e.physical.connectorName);
    }
    return plain;
}

const ipc::Mode *DisplayOutputModel::selectedMode(const Entry &e) const
{
    return e.modeIndex >= 0 && e.modeIndex < e.physical.modes.size()
            ? &e.physical.modes.at(e.modeIndex)
            : nullptr;
}

QList<int> DisplayOutputModel::resolutionModes(const Entry &e) const
{
    QList<int> result;
    QList<QSize> seen;
    for (int i = 0; i < e.physical.modes.size(); ++i) {
        const auto &m = e.physical.modes.at(i);
        const QSize s(m.width, m.height);
        if (!seen.contains(s)) {
            seen.append(s);
            result.append(i);
        }
    }
    std::sort(result.begin(), result.end(), [&e](int a, int b) {
        const auto &x = e.physical.modes.at(a);
        const auto &y = e.physical.modes.at(b);
        return qint64(x.width) * x.height > qint64(y.width) * y.height;
    });
    return result;
}

QList<int> DisplayOutputModel::refreshModes(const Entry &e) const
{
    QList<int> result;
    const auto *selected = selectedMode(e);
    if (!selected)
        return result;
    for (int i = 0; i < e.physical.modes.size(); ++i) {
        const auto &m = e.physical.modes.at(i);
        if (m.width == selected->width && m.height == selected->height)
            result.append(i);
    }
    std::sort(result.begin(), result.end(), [&e](int a, int b) {
        return e.physical.modes.at(a).refreshMilliHz < e.physical.modes.at(b).refreshMilliHz;
    });
    return result;
}

QVariant DisplayOutputModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_entries.size())
        return {};
    const Entry &e = m_entries.at(index.row());
    const auto *mode = selectedMode(e);
    const auto resolutions = resolutionModes(e);
    const auto refreshes = refreshModes(e);
    switch (role) {
    case Qt::DisplayRole:
        return name(e);
    case IdRole:
        return e.physical.sessionId;
    case EnabledRole:
        return e.enabled;
    case InternalRole:
        return e.physical.connectionType == 1;
    case PrimaryRole:
        return e.primary;
    case SizeRole:
        return mode ? QSize(mode->width, mode->height) : QSize();
    case PositionRole:
    case NormalizedPositionRole:
        return e.position;
    case AutoRotateRole:
    case AutoRotateOnlyInTabletModeRole:
        return false;
    case RotationRole:
        return e.rotation == 0 ? 1 : e.rotation == 1 ? 2 : e.rotation == 2 ? 4 : 8;
    case ScaleRole:
        return e.scale120 / 120.0;
    case ResolutionIndexRole:
        for (int i = 0; i < resolutions.size(); ++i) {
            const auto &m = e.physical.modes.at(resolutions.at(i));
            if (mode && m.width == mode->width && m.height == mode->height)
                return i;
        }
        return 0;
    case ResolutionsRole: {
        QStringList v;
        for (int i : resolutions) {
            const auto &m = e.physical.modes.at(i);
            v.append(QStringLiteral("%1 × %2").arg(m.width).arg(m.height));
        }
        return v;
    }
    case RefreshRateIndexRole:
        return refreshes.indexOf(e.modeIndex);
    case RefreshRatesRole: {
        QStringList v;
        for (int i : refreshes)
            v.append(QStringLiteral("%1 Hz").arg(e.physical.modes.at(i).refreshMilliHz / 1000.0, 0,
                                                 'f', 2));
        return v;
    }
    case ReplicationSourceModelRole: {
        QStringList v {QStringLiteral("Extend")};
        for (const auto &o : m_entries)
            if (o.physical.sessionId != e.physical.sessionId)
                v.append(name(o));
        return v;
    }
    case ReplicationSourceIndexRole: {
        if (e.mirrorSource.isEmpty())
            return 0;
        int n = 1;
        for (const auto &o : m_entries) {
            if (o.physical.sessionId == e.physical.sessionId)
                continue;
            if (o.physical.sessionId == e.mirrorSource)
                return n;
            ++n;
        }
        return 0;
    }
    case ReplicasModelRole:
        return QVariantList();
    case CapabilitiesRole:
        return e.physical.capabilities;
    case SupportedScalesRole:
        return QVariantList {1.0, 1.25, 1.5, 1.75, 2.0, 2.5, 3.0, 4.0};
    case DrmConnectorRole: {
        // Sysfs connector name, e.g. "card0-eDP-1".
        const QFileInfo device(e.physical.devicePath);
        if (!device.canonicalFilePath().startsWith(QLatin1String("/dev/dri/card"))
            || e.physical.connectorName.isEmpty())
            return QString();
        return QFileInfo(device.canonicalFilePath()).fileName() + QLatin1Char('-')
                + e.physical.connectorName;
    }
    default:
        return {};
    }
}

void DisplayOutputModel::markDirty(const QModelIndex &index, int role)
{
    QList<int> roles {role};
    if (role == ResolutionIndexRole || role == RefreshRateIndexRole)
        roles = {ResolutionIndexRole, RefreshRateIndexRole, RefreshRatesRole, SizeRole};
    emit dataChanged(index, index, roles);
    if (!m_dirty) {
        m_dirty = true;
        emit dirtyChanged();
    }
}

bool DisplayOutputModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.model() != this || index.row() >= m_entries.size())
        return false;
    Entry &e = m_entries[index.row()];
    switch (role) {
    case EnabledRole: {
        const bool v = value.toBool();
        if (!v) {
            int n = 0;
            for (const auto &i : m_entries)
                n += i.enabled;
            if (n <= 1)
                return false;
        }
        if (e.enabled == v)
            return false;
        e.enabled = v;
        break;
    }
    case PrimaryRole:
        if (!value.toBool() || !e.enabled || e.primary)
            return false;
        for (auto &i : m_entries)
            i.primary = false;
        e.primary = true;
        emit dataChanged(this->index(0), this->index(m_entries.size() - 1), {PrimaryRole});
        break;
    case PositionRole:
    case NormalizedPositionRole:
        if (e.position == value.toPoint())
            return false;
        e.position = value.toPoint();
        break;
    case RotationRole: {
        const int raw = value.toInt();
        const uint r = raw == 1 ? 0 : raw == 2 ? 1 : raw == 4 ? 2 : raw == 8 ? 3 : 99;
        if (r > 3 || e.rotation == r)
            return false;
        e.rotation = r;
        break;
    }
    case ScaleRole: {
        const int s = qRound(value.toDouble() * 120);
        if (s < 120 || s > 480 || e.scale120 == uint(s))
            return false;
        e.scale120 = s;
        break;
    }
    case ResolutionIndexRole: {
        const auto choices = resolutionModes(e);
        const int selected = value.toInt();
        if (selected < 0 || selected >= choices.size())
            return false;
        const auto &t = e.physical.modes.at(choices.at(selected));
        int best = choices.at(selected), difference = INT_MAX;
        for (int i = 0; i < e.physical.modes.size(); ++i) {
            const auto &c = e.physical.modes.at(i);
            if (c.width == t.width && c.height == t.height) {
                const int d = qAbs(c.refreshMilliHz
                                   - (selectedMode(e) ? selectedMode(e)->refreshMilliHz : 0));
                if (d < difference) {
                    best = i;
                    difference = d;
                }
            }
        }
        if (e.modeIndex == best)
            return false;
        e.modeIndex = best;
        break;
    }
    case RefreshRateIndexRole: {
        const auto choices = refreshModes(e);
        const int selected = value.toInt();
        if (selected < 0 || selected >= choices.size() || e.modeIndex == choices.at(selected))
            return false;
        e.modeIndex = choices.at(selected);
        break;
    }
    case ReplicationSourceIndexRole: {
        const int selected = value.toInt();
        QString source;
        int n = 1;
        for (const auto &o : m_entries) {
            if (o.physical.sessionId == e.physical.sessionId)
                continue;
            if (n++ == selected) {
                source = o.physical.sessionId;
                break;
            }
        }
        if (e.mirrorSource == source)
            return false;
        e.mirrorSource = source;
        break;
    }
    default:
        return false;
    }
    m_layoutDirty |= role == PositionRole || role == NormalizedPositionRole || role == EnabledRole
            || role == ReplicationSourceIndexRole;
    if (role == EnabledRole && !e.enabled && e.primary) {
        e.primary = false;
        for (auto &candidate : m_entries) {
            if (candidate.enabled && candidate.mirrorSource.isEmpty()) {
                candidate.primary = true;
                break;
            }
        }
        emit dataChanged(this->index(0), this->index(m_entries.size() - 1), {PrimaryRole});
    }
    markDirty(index, role);
    return true;
}

Qt::ItemFlags DisplayOutputModel::flags(const QModelIndex &i) const
{
    return i.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable | Qt::ItemIsEditable
                       : Qt::NoItemFlags;
}

QHash<int, QByteArray> DisplayOutputModel::roleNames() const
{
    return {{Qt::DisplayRole, "display"},
            {EnabledRole, "enabled"},
            {InternalRole, "internal"},
            {PrimaryRole, "primary"},
            {SizeRole, "size"},
            {PositionRole, "position"},
            {NormalizedPositionRole, "normalizedPosition"},
            {AutoRotateRole, "autoRotate"},
            {AutoRotateOnlyInTabletModeRole, "autoRotateOnlyInTabletMode"},
            {RotationRole, "rotation"},
            {ScaleRole, "scale"},
            {ResolutionIndexRole, "resolutionIndex"},
            {ResolutionsRole, "resolutions"},
            {RefreshRateIndexRole, "refreshRateIndex"},
            {RefreshRatesRole, "refreshRates"},
            {ReplicationSourceModelRole, "replicationSourceModel"},
            {ReplicationSourceIndexRole, "replicationSourceIndex"},
            {ReplicasModelRole, "replicasModel"},
            {IdRole, "displayId"},
            {CapabilitiesRole, "capabilities"},
            {SupportedScalesRole, "supportedScales"},
            {DrmConnectorRole, "drmConnector"}};
}

void DisplayOutputModel::setState(const DisplayClient::State &state)
{
    m_applied = state;
    m_dirty = false;
    m_layoutDirty = false;
    QSet<QString> connected;
    for (const auto &physical : state.physical)
        connected.insert(physical.sessionId);
    for (int row = m_entries.size() - 1; row >= 0; --row) {
        if (!connected.contains(m_entries[row].physical.sessionId)) {
            beginRemoveRows({}, row, row);
            m_entries.removeAt(row);
            endRemoveRows();
        }
    }
    for (const auto &physical : state.physical) {
        Entry entry;
        for (const auto &previous : m_entries)
            if (previous.physical.sessionId == physical.sessionId) {
                entry = previous;
                break;
            }
        entry.physical = physical;
        entry.enabled = false;
        entry.primary = false;
        entry.mirrorSource.clear();
        for (int i = 0; i < physical.modes.size(); ++i) {
            if (physical.modes[i].current) {
                entry.modeIndex = i;
                break;
            }
        }
        for (const auto &logical : state.logical) {
            if (!logical.destinations.contains(physical.sessionId))
                continue;
            entry.enabled = true;
            entry.primary = logical.primary && logical.destinations.first() == physical.sessionId;
            entry.position = {logical.x, logical.y};
            entry.scale120 = logical.scale120;
            entry.rotation = logical.rotation;
            if (logical.destinations.first() != physical.sessionId)
                entry.mirrorSource = logical.destinations.first();
            break;
        }
        int row = 0;
        while (row < m_entries.size() && m_entries[row].physical.sessionId != physical.sessionId)
            ++row;
        if (row == m_entries.size()) {
            beginInsertRows({}, row, row);
            m_entries.append(entry);
            endInsertRows();
        } else {
            m_entries[row] = entry;
            emit dataChanged(index(row), index(row));
        }
    }
    emit dirtyChanged();
}

void DisplayOutputModel::discard()
{
    setState(m_applied);
}

QList<ipc::DisplayRequest> DisplayOutputModel::requests() const
{
    QList<ipc::DisplayRequest> r;
    for (const auto &e : m_entries) {
        const auto *m = selectedMode(e);
        if (!m)
            continue;
        r.append({e.physical.sessionId,
                  e.enabled,
                  {m->width, m->height, m->refreshMilliHz, m->pixelClockKHz, m->horizontalTotal,
                   m->verticalTotal, m->flags, m->preferred, m->name},
                  e.scale120,
                  e.rotation});
    }
    return r;
}

QString DisplayOutputModel::primary() const
{
    for (const auto &e : m_entries)
        if (e.primary)
            return e.physical.sessionId;
    return {};
}

QList<ipc::Placement> DisplayOutputModel::placements() const
{
    if (!m_layoutDirty)
        return m_applied.placements;
    QList<ipc::Placement> result;
    const QString root = primary();
    const Entry *parent = nullptr;
    for (const auto &entry : m_entries)
        if (entry.physical.sessionId == root)
            parent = &entry;
    if (!parent)
        return result;
    QList<const Entry *> placed {parent};
    const auto logicalSize = [this](const Entry &entry) {
        const auto *mode = selectedMode(entry);
        if (!mode)
            return QSize(1, 1);
        QSize size(mode->width, mode->height);
        if (entry.rotation % 2)
            size.transpose();
        return QSize((size.width() * 120 + entry.scale120 - 1) / entry.scale120,
                     (size.height() * 120 + entry.scale120 - 1) / entry.scale120);
    };
    QHash<QString, QRect> resolved;
    resolved.insert(parent->physical.sessionId, QRect(parent->position, logicalSize(*parent)));
    for (const auto &entry : m_entries) {
        if (!entry.enabled || entry.physical.sessionId == root || !entry.mirrorSource.isEmpty())
            continue;
        int bestDistance = INT_MAX;
        ipc::Placement best;
        QRect bestBounds;
        const QSize childSize = logicalSize(entry);
        for (const auto *candidate : placed) {
            const QSize parentSize = logicalSize(*candidate);
            const QPoint parentPosition = resolved.value(candidate->physical.sessionId).topLeft();
            const QPoint delta = entry.position - parentPosition;
            for (uint edge = 0; edge < 4; ++edge) {
                const bool horizontal = edge % 2;
                const int childExtent = horizontal ? childSize.height() : childSize.width();
                const int parentExtent = horizontal ? parentSize.height() : parentSize.width();
                const int overlap = qMin(32, qMin(childExtent, parentExtent));
                const int cross = horizontal ? delta.y() : delta.x();
                const int offset = qBound(-childExtent + overlap, cross, parentExtent - overlap);
                const int normal = edge == 0 ? -childSize.height()
                        : edge == 1          ? parentSize.width()
                        : edge == 2          ? parentSize.height()
                                             : -childSize.width();
                const int distance =
                        qAbs((horizontal ? delta.x() : delta.y()) - normal) + qAbs(cross - offset);
                const QRect bounds(
                        parentPosition
                                + (horizontal ? QPoint(normal, offset) : QPoint(offset, normal)),
                        childSize);
                bool overlaps = false;
                for (const auto &occupied : resolved)
                    overlaps |= occupied.intersects(bounds);
                if (overlaps)
                    continue;
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = {entry.physical.sessionId, candidate->physical.sessionId, edge, offset};
                    bestBounds = bounds;
                }
            }
        }
        result.append(best);
        placed.append(&entry);
        resolved.insert(entry.physical.sessionId, bestBounds);
    }
    return result;
}

ipc::MirrorGroups DisplayOutputModel::mirrorGroups() const
{
    ipc::MirrorGroups result;
    for (const auto &entry : m_entries) {
        if (!entry.enabled || entry.mirrorSource.isEmpty())
            continue;
        auto group = std::find_if(result.begin(), result.end(), [&](const auto &members) {
            return members.first() == entry.mirrorSource;
        });
        if (group == result.end())
            result.append({entry.mirrorSource, entry.physical.sessionId});
        else
            group->append(entry.physical.sessionId);
    }
    return result;
}
