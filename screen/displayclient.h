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

#include <functional>
#include <optional>

#include <QDBusPendingCall>
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

    // Called once with the compositor's verdict on a request.
    using Completion = std::function<void(bool succeeded, cutefish::display::ipc::ErrorCode error)>;

    explicit DisplayClient(QObject *parent = nullptr);
    bool available() const;

    // False until the first GetState reply after (re)connecting.
    bool loaded() const { return m_loaded; }

    const State &state() const { return m_state; }

    void refresh();
    QString apply(const QList<cutefish::display::ipc::DisplayRequest> &displays,
                  const QString &primary,
                  const QList<cutefish::display::ipc::Placement> &placements,
                  const cutefish::display::ipc::MirrorGroups &mirrorGroups);
    void confirm(const QString &token);
    void revert(const QString &token);
    void identify(const QStringList &displayIds);

    // Powers every display on or off. False when the compositor is not on the bus,
    // and then `done` is never called.
    bool setDisplayPower(bool on, Completion done = {});

    // Closing fails with NoEnabledOutput when no external display is in use.
    bool setDockedLidClosed(bool closed, Completion done = {});

signals:
    void availabilityChanged();
    void stateChanged();
    void operationFinished(QString token, bool succeeded, bool degraded, uint errorCode,
                           QString message, QStringList affectedDisplays);
    void identifyRequested(QStringList displayIds);

private:
    void connectInterface();
    bool sendRequest(const QDBusPendingCall &call, Completion done);
    void failRequests();

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
    QString m_owner;
    ComCutefishDisplayConfig1Interface *m_interface = nullptr;
    State m_state;
    bool m_loaded = false;
    quint64 m_refreshSerial = 0;
    bool m_applyPending = false;
    QString m_operationToken;
    QHash<QString, OperationResult> m_earlyResults;
    std::optional<OperationResult> m_completion;
    quint64 m_serviceEpoch = 0;
    quint64 m_operationSerial = 0;
    QTimer m_operationTimeout;

    // Power and lid requests run beside apply and do not wait for a state refresh.
    int m_requestsInFlight = 0;
    QHash<QString, Completion> m_requests;
    QHash<QString, OperationResult> m_earlyRequestResults;
};
