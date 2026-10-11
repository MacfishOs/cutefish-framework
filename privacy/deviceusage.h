// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "deviceuse.h"

#include <QObject>
#include <QVariantList>

namespace CutefishPrivacy {

class PipeWireCapture;
class VideoDeviceWatch;

/**
 * Which processes use a camera, a microphone, or record the system's audio.
 * Each entry of uses is a map of kind, pid, appId and name, cameras first.
 */
class DeviceUsage : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool camera READ camera NOTIFY usesChanged)
    Q_PROPERTY(bool microphone READ microphone NOTIFY usesChanged)
    Q_PROPERTY(bool systemAudio READ systemAudio NOTIFY usesChanged)
    Q_PROPERTY(QVariantList uses READ usesList NOTIFY usesChanged)

public:
    enum Kind { Camera = DeviceUse::Camera, Microphone = DeviceUse::Microphone,
                SystemAudio = DeviceUse::SystemAudio };
    Q_ENUM(Kind)

    explicit DeviceUsage(QObject *parent = nullptr);
    DeviceUsage(PipeWireCapture *capture, VideoDeviceWatch *videoDevices, QObject *parent = nullptr);
    static DeviceUsage *instance();

    DeviceUses uses() const { return m_uses; }
    bool camera() const { return isUsed(DeviceUse::Camera); }
    bool microphone() const { return isUsed(DeviceUse::Microphone); }
    bool systemAudio() const { return isUsed(DeviceUse::SystemAudio); }
    QVariantList usesList() const;

Q_SIGNALS:
    void usesChanged();

private:
    bool isUsed(DeviceUse::Kind kind) const;
    void update();

    PipeWireCapture *m_capture;
    VideoDeviceWatch *m_videoDevices;
    DeviceUses m_uses;
};

}
