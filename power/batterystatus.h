// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

// The battery as cutefish-services reports it, and whether the system runs on it
// (UPower's OnBattery). Everything is read asynchronously and kept here, so the
// getters never block.
class BatteryStatus : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    Q_PROPERTY(bool onBattery READ onBattery NOTIFY onBatteryChanged)
    Q_PROPERTY(int chargeState READ chargeState NOTIFY chargeStateChanged)
    Q_PROPERTY(int chargePercent READ chargePercent NOTIFY chargePercentChanged)
    Q_PROPERTY(int capacity READ capacity NOTIFY capacityChanged)
    Q_PROPERTY(QString statusString READ statusString NOTIFY statusStringChanged)
    Q_PROPERTY(int lastChargedPercent READ lastChargedPercent NOTIFY lastChargedChanged)
    Q_PROPERTY(qint64 lastChargedSecs READ lastChargedSecs NOTIFY lastChargedChanged)
    Q_PROPERTY(QString udi READ udi NOTIFY udiChanged)

public:
    static BatteryStatus *instance();

    explicit BatteryStatus(QObject *parent = nullptr);

    // Whether the service has answered once; until then available() is false either way.
    bool ready() const;
    // cutefish-services publishes the battery object only when there is a battery.
    bool available() const;
    bool onBattery() const;

    // The service's enum: NoCharge, Charging, Discharging, FullyCharged.
    int chargeState() const;
    int chargePercent() const;
    int capacity() const;
    QString statusString() const;
    int lastChargedPercent() const;
    qint64 lastChargedSecs() const;
    // The UPower device object path.
    QString udi() const;

    // Asks the service to read the battery again.
    Q_INVOKABLE void refresh();

signals:
    void readyChanged();
    void availableChanged();
    void onBatteryChanged();
    void chargeStateChanged();
    void chargePercentChanged();
    void capacityChanged();
    void statusStringChanged();
    void lastChargedChanged();
    void udiChanged();

private slots:
    void fetchState();
    void onUPowerPropertiesChanged(const QString &upowerInterface, const QVariantMap &changed,
                                   const QStringList &invalidated);

private:
    void applyState(const QVariantMap &properties);
    void fetchOnBattery();
    void setOnBattery(bool onBattery);

    bool m_ready = false;
    bool m_available = false;
    bool m_onBattery = false;
    int m_chargeState = 0;
    int m_chargePercent = 0;
    int m_capacity = 0;
    QString m_statusString;
    int m_lastChargedPercent = 0;
    qint64 m_lastChargedSecs = 0;
    QString m_udi;
};
