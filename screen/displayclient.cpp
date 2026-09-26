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

#include <utility>

#include <QDBusConnectionInterface>
#include <QDBusPendingCallWatcher>
#include <QDBusServiceWatcher>

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
    auto *watcher = new QDBusServiceWatcher(m_serviceName, QDBusConnection::sessionBus(),
                                            QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &oldOwner, const QString &newOwner) {
                // A notification queued before this client looked the owner up is stale.
                if (oldOwner != m_owner)
                    return;
                m_owner = newOwner;
                const bool wasAvailable = available();
                connectInterface();
                if (wasAvailable != available())
                    emit availabilityChanged();
                refresh();
            });
    m_owner = QDBusConnection::sessionBus().interface()->serviceOwner(m_serviceName).value();
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
    m_loaded = false;
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
                if (const Completion done = m_requests.take(token))
                    done(succeeded, ipc::ErrorCode(errorCode));
                else if (m_requestsInFlight)
                    m_earlyRequestResults.insert(token, result);
                if (m_applyPending)
                    m_earlyResults.insert(token, result);
                else if (token == m_operationToken)
                    queueCompletion(token, result);
                refresh();
            });
    connect(m_interface, &ComCutefishDisplayConfig1Interface::IdentifyRequested, this,
            &DisplayClient::identifyRequested);
    // Last, so a completion that asks again reaches the new owner.
    failRequests();
}

void DisplayClient::refresh()
{
    const auto serial = ++m_refreshSerial;
    if (!m_interface || !m_interface->isValid()) {
        m_state = {};
        m_loaded = true;
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
        m_loaded = true;
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

bool DisplayClient::setDisplayPower(bool on, Completion done)
{
    if (!m_interface || !m_interface->isValid())
        return false;
    // The compositor's power states: 0 is on, 1 is off.
    return sendRequest(m_interface->SetDisplayPower({}, on ? 0 : 1), std::move(done));
}

bool DisplayClient::setDockedLidClosed(bool closed, Completion done)
{
    if (!m_interface || !m_interface->isValid())
        return false;
    return sendRequest(m_interface->SetDockedLidClosed(closed), std::move(done));
}

bool DisplayClient::sendRequest(const QDBusPendingCall &call, Completion done)
{
    ++m_requestsInFlight;
    const auto epoch = m_serviceEpoch;
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, epoch, done = std::move(done)] {
                QDBusPendingReply<QString> reply = *watcher;
                watcher->deleteLater();
                // Sent to a previous owner, whose OperationFinished never reaches this interface.
                if (epoch != m_serviceEpoch) {
                    if (done)
                        done(false, ipc::ErrorCode::Unavailable);
                    return;
                }

                // A rejected request reports OperationFinished before its reply.
                std::optional<OperationResult> early;
                if (!reply.isError() && m_earlyRequestResults.contains(reply.value()))
                    early = m_earlyRequestResults.take(reply.value());
                if (--m_requestsInFlight == 0)
                    m_earlyRequestResults.clear();

                if (!done)
                    return;
                if (reply.isError())
                    done(false, ipc::ErrorCode::Unavailable);
                else if (early)
                    done(early->succeeded, ipc::ErrorCode(early->errorCode));
                else
                    m_requests.insert(reply.value(), done);
            });
    return true;
}

void DisplayClient::failRequests()
{
    const auto requests = std::exchange(m_requests, {});
    m_requestsInFlight = 0;
    m_earlyRequestResults.clear();
    for (const Completion &done : requests)
        done(false, ipc::ErrorCode::Unavailable);
}
