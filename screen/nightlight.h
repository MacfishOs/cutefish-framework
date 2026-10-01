// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

// The night light cutefish-services keeps. Its state is read once, without
// blocking, and then follows the service's change signals; a setter shows its
// value at once and the service's answer settles it.
class NightLight : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(qreal temperature READ temperature WRITE setTemperature NOTIFY temperatureChanged)
    Q_PROPERTY(int schedule READ schedule WRITE setSchedule NOTIFY scheduleChanged)
    Q_PROPERTY(int customStart READ customStart WRITE setCustomStart NOTIFY customStartChanged)
    Q_PROPERTY(int customEnd READ customEnd WRITE setCustomEnd NOTIFY customEndChanged)

public:
    enum Schedule {
        Never,
        SunsetToSunrise,
        Custom,
    };
    Q_ENUM(Schedule)

    explicit NightLight(QObject *parent = nullptr);

    bool loaded() const { return m_loaded; }

    // Whether a display can be tinted at all.
    bool available() const { return m_available; }

    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    // How warm, from 0 to 1.
    qreal temperature() const { return m_temperature; }
    void setTemperature(qreal temperature);

    int schedule() const { return m_schedule; }
    void setSchedule(int schedule);

    // Minutes after midnight.
    int customStart() const { return m_customStart; }
    void setCustomStart(int minutes);

    int customEnd() const { return m_customEnd; }
    void setCustomEnd(int minutes);

signals:
    void loadedChanged();
    void availableChanged();
    void enabledChanged();
    void temperatureChanged();
    void scheduleChanged();
    void customStartChanged();
    void customEndChanged();

private slots:
    void load();
    void onEnabledChanged(bool enabled);
    void onTemperatureChanged(double temperature);
    void onScheduleChanged(int schedule);
    void onCustomStartChanged(int minutes);
    void onCustomEndChanged(int minutes);
    void onAvailableChanged(bool available);

private:
    void call(const QString &method, const QVariant &value);

    bool m_loaded = false;
    bool m_available = false;
    bool m_enabled = false;
    qreal m_temperature = 0.5;
    int m_schedule = Never;
    int m_customStart = 18 * 60;
    int m_customEnd = 6 * 60;
};
