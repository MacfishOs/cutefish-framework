// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Upstream: chromium/src/ash/display/cros_display_config.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a
//
// Adapted for Cutefish: an asynchronous Qt D-Bus client watches compositor
// ownership and exposes coherent native display snapshots to the QML model.

#pragma once

#include <optional>

#include <QHash>
#include <QObject>
#include <QTimer>

#include "displaytypes.h"

class ComCutefishDisplayConfig1Interface;

class DisplayClient : public QObject
{
    Q_OBJECT

public:
    struct State {
        QString instanceId;
        QString seatId;
        qulonglong topologyGeneration = 0;
        qulonglong appliedRevision = 0;
        QList<cutefish::display::ipc::PhysicalDisplay> physical;
        QList<cutefish::display::ipc::LogicalDisplay> logical;
        QList<cutefish::display::ipc::Placement> placements;
        QString previewToken;
        qulonglong previewDeadlineMs = 0;
    };

    explicit DisplayClient(QObject *parent = nullptr);
    bool available() const;

    const State &state() const { return m_state; }

    void refresh();
    QString apply(const QList<cutefish::display::ipc::DisplayRequest> &displays,
                  const QString &primary,
                  const QList<cutefish::display::ipc::Placement> &placements,
                  const cutefish::display::ipc::MirrorGroups &mirrorGroups);
    void confirm(const QString &token);
    void revert(const QString &token);
    void identify(const QStringList &displayIds);

signals:
    void availabilityChanged();
    void stateChanged();
    void operationFinished(QString token, bool succeeded, bool degraded, uint errorCode,
                           QString message, QStringList affectedDisplays);

private:
    void connectInterface();

    struct OperationResult {
        bool succeeded;
        bool degraded;
        uint errorCode;
        QString message;
        QStringList affected;
        qulonglong generation = 0;
        qulonglong revision = 0;
    };

    void queueCompletion(const QString &token, const OperationResult &result);
    void completeIfCurrent();
    void finishOperation(const QString &token, const OperationResult &result);

    QString m_serviceName;
    ComCutefishDisplayConfig1Interface *m_interface = nullptr;
    State m_state;
    quint64 m_refreshSerial = 0;
    bool m_applyPending = false;
    QString m_operationToken;
    QHash<QString, OperationResult> m_earlyResults;
    std::optional<OperationResult> m_completion;
    quint64 m_serviceEpoch = 0;
    quint64 m_operationSerial = 0;
    QTimer m_operationTimeout;
};
