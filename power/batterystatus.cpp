// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "batterystatus.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

static const QString service = QStringLiteral("com.cutefish.Services");
static const QString path = QStringLiteral("/com/cutefish/Services/Battery");
static const QString interface = QStringLiteral("com.cutefish.Services.Battery");

static const QString upowerService = QStringLiteral("org.freedesktop.UPower");
static const QString upowerPath = QStringLiteral("/org/freedesktop/UPower");
static const QString propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");

static BatteryStatus *s_instance = nullptr;

BatteryStatus *BatteryStatus::instance()
{
    if (!s_instance)
        s_instance = new BatteryStatus;

    return s_instance;
}

BatteryStatus::BatteryStatus(QObject *parent)
    : QObject(parent)
{
    // The signals only say that something moved; one GetAll brings every value up to date.
    QDBusConnection bus = QDBusConnection::sessionBus();
    for (const char *signal : {"chargeStateChanged", "chargePercentChanged", "capacityChanged",
                               "remainingTimeChanged", "lastChargedPercentChanged",
                               "lastChargedSecsChanged"}) {
        bus.connect(service, path, interface, QString::fromLatin1(signal), this,
                    SLOT(fetchState()));
    }

    QDBusConnection::systemBus().connect(
            upowerService, upowerPath, propertiesInterface, QStringLiteral("PropertiesChanged"),
            this, SLOT(onUPowerPropertiesChanged(QString, QVariantMap, QStringList)));

    fetchState();
    fetchOnBattery();
}

bool BatteryStatus::ready() const
{
    return m_ready;
}

bool BatteryStatus::available() const
{
    return m_available;
}

bool BatteryStatus::onBattery() const
{
    return m_onBattery;
}

int BatteryStatus::chargeState() const
{
    return m_chargeState;
}

int BatteryStatus::chargePercent() const
{
    return m_chargePercent;
}

int BatteryStatus::capacity() const
{
    return m_capacity;
}

QString BatteryStatus::statusString() const
{
    return m_statusString;
}

int BatteryStatus::lastChargedPercent() const
{
    return m_lastChargedPercent;
}

qint64 BatteryStatus::lastChargedSecs() const
{
    return m_lastChargedSecs;
}

QString BatteryStatus::udi() const
{
    return m_udi;
}

void BatteryStatus::refresh()
{
    QDBusConnection::sessionBus().asyncCall(
            QDBusMessage::createMethodCall(service, path, interface, QStringLiteral("refresh")));
}

void BatteryStatus::fetchState()
{
    QDBusMessage message = QDBusMessage::createMethodCall(service, path, propertiesInterface,
                                                          QStringLiteral("GetAll"));
    message << interface;

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message),
                                                this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusPendingReply<QVariantMap> reply = *call;

        if (!reply.isError()) {
            applyState(reply.value());

            if (!m_available) {
                m_available = true;
                emit availableChanged();
            }
        }

        if (!m_ready) {
            m_ready = true;
            emit readyChanged();
        }
    });
}

void BatteryStatus::applyState(const QVariantMap &properties)
{
    const int chargeState = properties.value(QStringLiteral("chargeState"), m_chargeState).toInt();
    const int chargePercent =
            properties.value(QStringLiteral("chargePercent"), m_chargePercent).toInt();
    const int capacity = properties.value(QStringLiteral("capacity"), m_capacity).toInt();
    const QString statusString =
            properties.value(QStringLiteral("statusString"), m_statusString).toString();
    const int lastChargedPercent =
            properties.value(QStringLiteral("lastChargedPercent"), m_lastChargedPercent).toInt();
    const qint64 lastChargedSecs =
            properties.value(QStringLiteral("lastChargedSecs"), m_lastChargedSecs).toLongLong();
    const QString udi = properties.value(QStringLiteral("udi"), m_udi).toString();

    if (m_chargeState != chargeState) {
        m_chargeState = chargeState;
        emit chargeStateChanged();
    }
    if (m_chargePercent != chargePercent) {
        m_chargePercent = chargePercent;
        emit chargePercentChanged();
    }
    if (m_capacity != capacity) {
        m_capacity = capacity;
        emit capacityChanged();
    }
    if (m_statusString != statusString) {
        m_statusString = statusString;
        emit statusStringChanged();
    }
    if (m_lastChargedPercent != lastChargedPercent || m_lastChargedSecs != lastChargedSecs) {
        m_lastChargedPercent = lastChargedPercent;
        m_lastChargedSecs = lastChargedSecs;
        emit lastChargedChanged();
    }
    if (m_udi != udi) {
        m_udi = udi;
        emit udiChanged();
    }
}

void BatteryStatus::fetchOnBattery()
{
    QDBusMessage message = QDBusMessage::createMethodCall(upowerService, upowerPath,
                                                          propertiesInterface, QStringLiteral("Get"));
    message << upowerService << QStringLiteral("OnBattery");

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message),
                                                this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusPendingReply<QDBusVariant> reply = *call;
        if (!reply.isError())
            setOnBattery(reply.value().variant().toBool());
    });
}

void BatteryStatus::setOnBattery(bool onBattery)
{
    if (onBattery == m_onBattery)
        return;

    m_onBattery = onBattery;

    // The service's figures depend on the power source.
    refresh();

    emit onBatteryChanged();
}

void BatteryStatus::onUPowerPropertiesChanged(const QString &upowerInterface,
                                              const QVariantMap &changed,
                                              const QStringList &invalidated)
{
    Q_UNUSED(upowerInterface);

    const auto it = changed.constFind(QStringLiteral("OnBattery"));
    if (it != changed.cend())
        setOnBattery(it->toBool());
    else if (invalidated.contains(QStringLiteral("OnBattery")))
        fetchOnBattery();
}
