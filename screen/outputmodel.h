// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Upstream: chromium/src/ash/display/cros_display_config.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a
//
// Adapted for Cutefish: stable-ID draft rows expose native modes, layout,
// primary selection, scale, rotation, and software mirror requests to QML.

#pragma once

#include <QAbstractListModel>
#include <QPoint>
#include <QSize>

#include "displayclient.h"

class DisplayOutputModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum OutputRoles {
        EnabledRole = Qt::UserRole + 1,
        InternalRole,
        PrimaryRole,
        SizeRole,
        PositionRole,
        NormalizedPositionRole,
        AutoRotateRole,
        AutoRotateOnlyInTabletModeRole,
        RotationRole,
        ScaleRole,
        ResolutionIndexRole,
        ResolutionsRole,
        RefreshRateIndexRole,
        RefreshRatesRole,
        ReplicationSourceModelRole,
        ReplicationSourceIndexRole,
        ReplicasModelRole,
        IdRole,
        CapabilitiesRole,
        SupportedScalesRole,
        DrmConnectorRole,
        RecommendedResolutionIndexRole,
        RecommendedScaleRole
    };

    explicit DisplayOutputModel(QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QHash<int, QByteArray> roleNames() const override;
    void setState(const DisplayClient::State &state);
    void discard();

    bool dirty() const { return m_dirty; }

    // A display switched to another mode may show nothing at all, so that
    // draft is previewed and kept only on confirmation.
    bool needsConfirmation() const;

    QList<cutefish::display::ipc::DisplayRequest> requests() const;
    QList<cutefish::display::ipc::Placement> placements() const;
    cutefish::display::ipc::MirrorGroups mirrorGroups() const;
    QString primary() const;
signals:
    void dirtyChanged();

private:
    struct Entry {
        cutefish::display::ipc::PhysicalDisplay physical;
        bool enabled = false;
        bool primary = false;
        QPoint position;
        uint scale120 = 120;
        uint rotation = 0;
        int modeIndex = 0;
        QString mirrorSource;
    };

    QString name(const Entry &entry) const;
    const cutefish::display::ipc::Mode *selectedMode(const Entry &entry) const;
    QList<int> resolutionModes(const Entry &entry) const;
    QList<int> refreshModes(const Entry &entry) const;
    void markDirty(const QModelIndex &index, int role);
    DisplayClient::State m_applied;
    QList<Entry> m_entries;
    bool m_dirty = false;
    bool m_layoutDirty = false;
};
