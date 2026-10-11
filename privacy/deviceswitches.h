// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>

namespace CutefishPrivacy {

/**
 * The system's camera and microphone switches, kept by cutefish-privacy. A
 * device turned off is off for every user and process until it is turned on.
 */
class DeviceSwitches : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool cameraEnabled READ cameraEnabled WRITE setCameraEnabled NOTIFY cameraEnabledChanged)
    Q_PROPERTY(bool microphoneEnabled READ microphoneEnabled WRITE setMicrophoneEnabled NOTIFY microphoneEnabledChanged)

public:
    explicit DeviceSwitches(QObject *parent = nullptr);
    static DeviceSwitches *instance();

    bool cameraEnabled() const { return m_cameraEnabled; }
    bool microphoneEnabled() const { return m_microphoneEnabled; }

    // Asynchronous: the property follows once the service has switched.
    void setCameraEnabled(bool enabled);
    void setMicrophoneEnabled(bool enabled);

Q_SIGNALS:
    void cameraEnabledChanged();
    void microphoneEnabledChanged();

private Q_SLOTS:
    void onCameraEnabledChanged(bool enabled);
    void onMicrophoneEnabledChanged(bool enabled);

private:
    void call(const QString &method, bool enabled);

    bool m_cameraEnabled = true;
    bool m_microphoneEnabled = true;
};

}
