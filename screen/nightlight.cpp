// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "nightlight.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>

static const QString s_service = QStringLiteral("com.cutefish.Services");
static const QString s_path = QStringLiteral("/com/cutefish/Services/NightLight");
static const QString s_interface = QStringLiteral("com.cutefish.Services.NightLight");

NightLight::NightLight(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    auto *watcher = new QDBusServiceWatcher(s_service, bus,
                                            QDBusServiceWatcher::WatchForRegistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &NightLight::load);

    bus.connect(s_service, s_path, s_interface, QStringLiteral("enabledChanged"), this,
                SLOT(onEnabledChanged(bool)));
    bus.connect(s_service, s_path, s_interface, QStringLiteral("temperatureChanged"), this,
                SLOT(onTemperatureChanged(double)));
    bus.connect(s_service, s_path, s_interface, QStringLiteral("scheduleChanged"), this,
                SLOT(onScheduleChanged(int)));
    bus.connect(s_service, s_path, s_interface, QStringLiteral("customStartChanged"), this,
                SLOT(onCustomStartChanged(int)));
    bus.connect(s_service, s_path, s_interface, QStringLiteral("customEndChanged"), this,
                SLOT(onCustomEndChanged(int)));
    bus.connect(s_service, s_path, s_interface, QStringLiteral("availableChanged"), this,
                SLOT(onAvailableChanged(bool)));
    bus.connect(s_service, s_path, s_interface, QStringLiteral("sunsetChanged"), this,
                SLOT(onSunsetChanged(int)));
    bus.connect(s_service, s_path, s_interface, QStringLiteral("sunriseChanged"), this,
                SLOT(onSunriseChanged(int)));
    load();
}

void NightLight::load()
{
    QDBusMessage message = QDBusMessage::createMethodCall(
            s_service, s_path, QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("GetAll"));
    message << s_interface;
    auto *watcher =
            new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *call) {
        call->deleteLater();
        const QDBusPendingReply<QVariantMap> reply = *call;
        if (reply.isError())
            return;
        const QVariantMap values = reply.value();
        onEnabledChanged(values.value(QStringLiteral("enabled")).toBool());
        onTemperatureChanged(values.value(QStringLiteral("temperature"), m_temperature).toDouble());
        onScheduleChanged(values.value(QStringLiteral("schedule")).toInt());
        onCustomStartChanged(values.value(QStringLiteral("customStart"), m_customStart).toInt());
        onCustomEndChanged(values.value(QStringLiteral("customEnd"), m_customEnd).toInt());
        onAvailableChanged(values.value(QStringLiteral("available")).toBool());
        onSunsetChanged(values.value(QStringLiteral("sunset"), m_sunset).toInt());
        onSunriseChanged(values.value(QStringLiteral("sunrise"), m_sunrise).toInt());
        if (!m_loaded) {
            m_loaded = true;
            emit loadedChanged();
        }
    });
}

void NightLight::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    onEnabledChanged(enabled);
    call(QStringLiteral("setEnabled"), enabled);
}

void NightLight::setTemperature(qreal temperature)
{
    if (temperature == m_temperature)
        return;
    onTemperatureChanged(temperature);
    call(QStringLiteral("setTemperature"), double(temperature));
}

void NightLight::setSchedule(int schedule)
{
    if (schedule == m_schedule)
        return;
    onScheduleChanged(schedule);
    call(QStringLiteral("setSchedule"), schedule);
}

void NightLight::setCustomStart(int minutes)
{
    if (minutes == m_customStart)
        return;
    onCustomStartChanged(minutes);
    call(QStringLiteral("setCustomStart"), minutes);
}

void NightLight::setCustomEnd(int minutes)
{
    if (minutes == m_customEnd)
        return;
    onCustomEndChanged(minutes);
    call(QStringLiteral("setCustomEnd"), minutes);
}

void NightLight::setPreview(bool preview)
{
    if (preview == m_preview)
        return;
    m_preview = preview;
    emit previewChanged();
    call(QStringLiteral("setPreview"), preview);
}

void NightLight::onEnabledChanged(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    emit enabledChanged();
}

void NightLight::onTemperatureChanged(double temperature)
{
    if (temperature == m_temperature)
        return;
    m_temperature = temperature;
    emit temperatureChanged();
}

void NightLight::onScheduleChanged(int schedule)
{
    if (schedule == m_schedule)
        return;
    m_schedule = schedule;
    emit scheduleChanged();
}

void NightLight::onCustomStartChanged(int minutes)
{
    if (minutes == m_customStart)
        return;
    m_customStart = minutes;
    emit customStartChanged();
}

void NightLight::onCustomEndChanged(int minutes)
{
    if (minutes == m_customEnd)
        return;
    m_customEnd = minutes;
    emit customEndChanged();
}

void NightLight::onAvailableChanged(bool available)
{
    if (available == m_available)
        return;
    m_available = available;
    emit availableChanged();
}

void NightLight::onSunsetChanged(int minutes)
{
    if (minutes == m_sunset)
        return;
    m_sunset = minutes;
    emit sunsetChanged();
}

void NightLight::onSunriseChanged(int minutes)
{
    if (minutes == m_sunrise)
        return;
    m_sunrise = minutes;
    emit sunriseChanged();
}

void NightLight::call(const QString &method, const QVariant &value)
{
    QDBusMessage message = QDBusMessage::createMethodCall(s_service, s_path, s_interface, method);
    message << value;
    QDBusConnection::sessionBus().asyncCall(message);
}
