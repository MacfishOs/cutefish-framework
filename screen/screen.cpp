#include "screen.h"

#include <QDateTime>

#include "outputmodel.h"

KScreenScreen::KScreenScreen(QObject *parent)
    : QObject(parent)
    , m_client(this)
    , m_outputModel(new DisplayOutputModel(this))
{
    m_countdown.setInterval(250);
    connect(&m_countdown, &QTimer::timeout, this, &KScreenScreen::stateChanged);
    connect(&m_client, &DisplayClient::availabilityChanged, this, &KScreenScreen::stateChanged);
    connect(&m_client, &DisplayClient::stateChanged, this, &KScreenScreen::updateState);
    connect(m_outputModel, &DisplayOutputModel::dirtyChanged, this, &KScreenScreen::stateChanged);
    connect(m_outputModel, &DisplayOutputModel::dirtyChanged, this, [this] {
        if (dirty())
            apply();
    });
    connect(&m_client, &DisplayClient::operationFinished, this,
            [this](const QString &, bool succeeded, bool degraded, uint, const QString &message,
                   const QStringList &) {
                m_busy = false;
                m_error = succeeded ? (degraded ? message : QString()) : message;
                m_outputModel->setState(m_client.state());
                emit stateChanged();
            });
}

bool KScreenScreen::dirty() const
{
    return m_outputModel->dirty();
}

bool KScreenScreen::canApply() const
{
    return available() && !m_busy && dirty() && !pendingConfirmation();
}

int KScreenScreen::confirmationSeconds() const
{
    if (!pendingConfirmation())
        return 0;
    return qMax(0,
                int((qint64(m_client.state().previewDeadlineMs)
                     - QDateTime::currentMSecsSinceEpoch() + 999)
                    / 1000));
}

void KScreenScreen::updateState()
{
    const auto &state = m_client.state();
    if (!m_client.available()) {
        m_busy = false;
        m_error = tr("The display service is unavailable.");
    }
    const bool topologyChanged = !m_instanceId.isEmpty()
            && (m_instanceId != state.instanceId
                || m_topologyGeneration != state.topologyGeneration);
    if (!m_outputModel->dirty() || topologyChanged || m_busy || pendingConfirmation())
        m_outputModel->setState(state);
    if (topologyChanged)
        m_error = tr("Displays changed. The previous draft was discarded.");
    m_instanceId = state.instanceId;
    m_topologyGeneration = state.topologyGeneration;
    pendingConfirmation() ? m_countdown.start() : m_countdown.stop();
    emit stateChanged();
}

void KScreenScreen::apply()
{
    if (!canApply())
        return;
    m_busy = true;
    m_error.clear();
    emit stateChanged();
    if (m_client.apply(m_outputModel->requests(), m_outputModel->primary(),
                       m_outputModel->placements(), m_outputModel->mirrorGroups())
                .isEmpty()) {
        m_busy = false;
        m_error = tr("The display service is unavailable.");
        emit stateChanged();
    }
}

void KScreenScreen::discard()
{
    if (!m_busy) {
        m_outputModel->discard();
        m_error.clear();
        emit stateChanged();
    }
}

void KScreenScreen::keep()
{
    if (pendingConfirmation())
        m_client.confirm(m_client.state().previewToken);
}

void KScreenScreen::revert()
{
    if (pendingConfirmation())
        m_client.revert(m_client.state().previewToken);
}

void KScreenScreen::identify()
{
    QStringList ids;
    for (const auto &display : m_client.state().logical)
        ids.append(display.id);
    m_client.identify(ids);
    emit identifyRequested(ids);
}
