// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "deviceuse.h"

#include <QHash>
#include <QObject>
#include <QTimer>

class QSocketNotifier;

namespace CutefishPrivacy {

/**
 * The processes that hold a video device open themselves, outside PipeWire.
 * Opens and closes of the device nodes arrive through inotify; only then are
 * the processes' descriptors read, and an open must outlast a settle delay to
 * count, so the brief opens that probe a device never show.
 */
class VideoDeviceWatch : public QObject
{
    Q_OBJECT

public:
    explicit VideoDeviceWatch(const QString &deviceDirectory = QStringLiteral("/dev"),
                              const QString &processDirectory = QStringLiteral("/proc"),
                              QObject *parent = nullptr);
    ~VideoDeviceWatch() override;

    DeviceUses uses() const { return m_uses; }

    static constexpr int SettleDelay = 1000;

Q_SIGNALS:
    void usesChanged();

private:
    void readEvents();
    void watchDevices();
    void scan();

    QString m_deviceDirectory;
    QString m_processDirectory;
    int m_fd = -1;
    int m_directoryWatch = -1;
    QSocketNotifier *m_notifier = nullptr;
    // Watch descriptor to device path.
    QHash<int, QString> m_devices;
    QTimer m_settle;
    DeviceUses m_uses;
};

}
