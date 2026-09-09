// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Upstream: chromium/src/ash/display/cros_display_config.cc
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a
//
// Adapted for Cutefish: service-owner watching and pending D-Bus replies
// replace synchronous KScreen operations and reconnect after compositor
// restarts.

#include "displayclient.h"

#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>

#include "display_config_interface.h"

using namespace cutefish::display;

DisplayClient::DisplayClient(QObject *parent)
    : QObject(parent)
    , m_serviceName(qEnvironmentVariable("CUTEFISH_DISPLAY_CONFIG_SERVICE",
                                         QStringLiteral("com.cutefish.DisplayConfig")))
{
    ipc::registerTypes();
    m_operationTimeout.setSingleShot(true);
    m_operationTimeout.setInterval(15000);
    connect(&m_operationTimeout, &QTimer::timeout, this, [this] {
        finishOperation(m_operationToken,
                        {false, false, 1, tr("The display operation timed out."), {}});
        refresh();
    });
    connect(QDBusConnection::sessionBus().interface(),
            &QDBusConnectionInterface::serviceOwnerChanged, this,
            [this](const QString &name, const QString &, const QString &) {
                if (name != m_serviceName)
                    return;
                const bool wasAvailable = available();
                connectInterface();
                if (wasAvailable != available())
                    emit availabilityChanged();
                refresh();
            });
    connectInterface();
    refresh();
}

bool DisplayClient::available() const
{
    return m_interface && m_interface->isValid() && !m_state.instanceId.isEmpty();
}

void DisplayClient::connectInterface()
{
    ++m_refreshSerial;
    ++m_serviceEpoch;
    m_state = {};
    emit stateChanged();
    if (m_applyPending || !m_operationToken.isEmpty() || m_completion)
        finishOperation(m_operationToken,
                        {false, false, 1, tr("The display service restarted."), {}});
    delete m_interface;
    m_interface = new ComCutefishDisplayConfig1Interface(
            m_serviceName, QStringLiteral("/com/cutefish/DisplayConfig1"),
            QDBusConnection::sessionBus(), this);
    connect(m_interface, &ComCutefishDisplayConfig1Interface::StateChanged, this, [this] {
        refresh();
    });
    connect(m_interface, &ComCutefishDisplayConfig1Interface::PreviewChanged, this,
            [this](const QString &, qulonglong, qulonglong, qulonglong) {
                refresh();
            });
    connect(m_interface, &ComCutefishDisplayConfig1Interface::OperationFinished, this,
            [this](const QString &token, bool succeeded, bool degraded, uint errorCode,
                   const QString &message, const QStringList &affected, qulonglong generation,
                   qulonglong revision) {
                const OperationResult result {succeeded, degraded,   errorCode, message,
                                              affected,  generation, revision};
                if (m_applyPending)
                    m_earlyResults.insert(token, result);
                else if (token == m_operationToken)
                    queueCompletion(token, result);
                refresh();
            });
}

void DisplayClient::refresh()
{
    const auto serial = ++m_refreshSerial;
    if (!m_interface || !m_interface->isValid()) {
        m_state = {};
        emit stateChanged();
        return;
    }
    auto pending = m_interface->GetState();
    auto *watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, serial] {
        QDBusPendingReply<QString, QString, qulonglong, qulonglong, QList<ipc::PhysicalDisplay>,
                          QList<ipc::LogicalDisplay>, QList<ipc::Placement>, QString, qulonglong>
                reply = *watcher;
        watcher->deleteLater();
        if (serial != m_refreshSerial)
            return;
        if (reply.isError()) {
            m_state = {};
            emit stateChanged();
            emit availabilityChanged();
            if (m_applyPending || !m_operationToken.isEmpty() || m_completion)
                finishOperation(m_operationToken, {false, false, 1, reply.error().message(), {}});
            return;
        }
        const bool wasAvailable = available();
        m_state = {reply.argumentAt<0>(), reply.argumentAt<1>(), reply.argumentAt<2>(),
                   reply.argumentAt<3>(), reply.argumentAt<4>(), reply.argumentAt<5>(),
                   reply.argumentAt<6>(), reply.argumentAt<7>(), reply.argumentAt<8>()};
        emit stateChanged();
        if (wasAvailable != available())
            emit availabilityChanged();
        completeIfCurrent();
    });
}

QString DisplayClient::apply(const QList<ipc::DisplayRequest> &displays, const QString &primary,
                             const QList<ipc::Placement> &placements,
                             const ipc::MirrorGroups &mirrorGroups)
{
    if (!available() || m_applyPending || !m_operationToken.isEmpty() || m_completion)
        return {};
    m_applyPending = true;
    m_operationToken.clear();
    m_earlyResults.clear();
    m_operationTimeout.start();
    const auto epoch = m_serviceEpoch;
    const auto operation = ++m_operationSerial;
    auto pending = m_interface->SetConfiguration(m_state.instanceId, m_state.topologyGeneration,
                                                 m_state.appliedRevision, displays, primary,
                                                 placements, mirrorGroups);
    auto *watcher = new QDBusPendingCallWatcher(pending, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, epoch, operation] {
        QDBusPendingReply<QString> reply = *watcher;
        watcher->deleteLater();
        if (epoch != m_serviceEpoch || operation != m_operationSerial || !m_applyPending)
            return;
        m_applyPending = false;
        if (reply.isError()) {
            queueCompletion({}, {false, false, 1, reply.error().message(), {}});
        } else {
            m_operationToken = reply.value();
            const auto result = m_earlyResults.constFind(m_operationToken);
            if (result != m_earlyResults.cend()) {
                const auto completed = *result;
                queueCompletion(m_operationToken, completed);
            }
            m_earlyResults.clear();
        }
    });
    return QStringLiteral("pending");
}

void DisplayClient::finishOperation(const QString &token, const OperationResult &result)
{
    const QString completedToken = token;
    m_applyPending = false;
    m_operationToken.clear();
    m_earlyResults.clear();
    m_completion.reset();
    m_operationTimeout.stop();
    emit operationFinished(completedToken, result.succeeded, result.degraded, result.errorCode,
                           result.message, result.affected);
}

void DisplayClient::queueCompletion(const QString &token, const OperationResult &result)
{
    m_operationToken = token;
    m_completion = result;
    refresh();
}

void DisplayClient::completeIfCurrent()
{
    if (!m_completion)
        return;
    if (m_completion->generation
        && (m_state.topologyGeneration < m_completion->generation
            || (m_state.topologyGeneration == m_completion->generation
                && m_state.appliedRevision < m_completion->revision)))
        return;
    const auto result = *m_completion;
    finishOperation(m_operationToken, result);
}

void DisplayClient::confirm(const QString &token)
{
    if (available())
        m_interface->ConfirmConfiguration(token);
}

void DisplayClient::revert(const QString &token)
{
    if (available())
        m_interface->RevertConfiguration(token);
}

void DisplayClient::identify(const QStringList &displayIds)
{
    if (available())
        m_interface->IdentifyDisplays(displayIds);
}
