// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "videodevicewatch.h"

#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSet>
#include <QSocketNotifier>

#include <algorithm>
#include <tuple>

#include <dirent.h>
#include <limits.h>
#include <sys/inotify.h>
#include <unistd.h>

namespace CutefishPrivacy {

static bool isVideoDevice(const QString &name)
{
    static const QRegularExpression pattern(QStringLiteral("^video[0-9]+$"));
    return pattern.match(name).hasMatch();
}

VideoDeviceWatch::VideoDeviceWatch(const QString &deviceDirectory, const QString &processDirectory,
                                   QObject *parent)
    : QObject(parent)
    , m_deviceDirectory(QDir(deviceDirectory).canonicalPath())
    , m_processDirectory(processDirectory)
{
    m_settle.setSingleShot(true);
    m_settle.setInterval(SettleDelay);
    connect(&m_settle, &QTimer::timeout, this, &VideoDeviceWatch::scan);

    m_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_fd < 0)
        return;
    m_directoryWatch = inotify_add_watch(m_fd, QFile::encodeName(m_deviceDirectory).constData(),
                                         IN_CREATE | IN_DELETE | IN_MOVED_TO | IN_MOVED_FROM);
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &VideoDeviceWatch::readEvents);
    watchDevices();
    scan();
}

VideoDeviceWatch::~VideoDeviceWatch()
{
    if (m_fd >= 0)
        close(m_fd);
}

void VideoDeviceWatch::readEvents()
{
    alignas(inotify_event) char buffer[4096];
    bool devicesChanged = false;
    bool accessed = false;
    for (;;) {
        const ssize_t length = read(m_fd, buffer, sizeof(buffer));
        if (length <= 0)
            break;
        for (ssize_t offset = 0; offset < length;) {
            const auto *event = reinterpret_cast<const inotify_event *>(buffer + offset);
            offset += sizeof(inotify_event) + event->len;
            if (event->wd == m_directoryWatch) {
                if (event->len > 0 && isVideoDevice(QFile::decodeName(event->name)))
                    devicesChanged = true;
            } else if (event->mask & IN_IGNORED) {
                m_devices.remove(event->wd);
                devicesChanged = true;
            } else {
                accessed = true;
            }
        }
    }
    if (devicesChanged)
        watchDevices();
    if ((devicesChanged || accessed) && !m_settle.isActive())
        m_settle.start();
}

void VideoDeviceWatch::watchDevices()
{
    const QStringList names = QDir(m_deviceDirectory).entryList(QDir::System | QDir::Files);
    for (const QString &name : names) {
        if (!isVideoDevice(name))
            continue;
        const QString path = m_deviceDirectory + QLatin1Char('/') + name;
        // A device already watched keeps its descriptor: inotify returns it again.
        const int watch = inotify_add_watch(m_fd, QFile::encodeName(path).constData(),
                                            IN_OPEN | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE);
        if (watch >= 0)
            m_devices.insert(watch, path);
    }
}

void VideoDeviceWatch::scan()
{
    const QSet<QString> devices(m_devices.cbegin(), m_devices.cend());
    DeviceUses uses;
    if (!devices.isEmpty()) {
        const QStringList processes = QDir(m_processDirectory).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &process : processes) {
            bool ok = false;
            const quint32 pid = process.toUInt(&ok);
            if (!ok)
                continue;
            // Read raw: QDir would stat every descriptor's target.
            const QByteArray descriptors = QFile::encodeName(m_processDirectory + QLatin1Char('/') + process)
                + "/fd";
            DIR *directory = opendir(descriptors.constData());
            if (!directory)
                continue;
            bool holds = false;
            while (const dirent *entry = readdir(directory)) {
                if (entry->d_name[0] == '.')
                    continue;
                char target[PATH_MAX];
                const QByteArray path = descriptors + '/' + entry->d_name;
                const ssize_t length = readlink(path.constData(), target, sizeof(target));
                if (length > 0 && devices.contains(QFile::decodeName(QByteArray(target, length)))) {
                    holds = true;
                    break;
                }
            }
            closedir(directory);
            if (!holds)
                continue;
            QFile comm(m_processDirectory + QLatin1Char('/') + process + QStringLiteral("/comm"));
            const QString name = comm.open(QIODevice::ReadOnly) ? QString::fromUtf8(comm.readAll()).trimmed()
                                                                : QString();
            // PipeWire holds the devices it serves; its graph names the clients.
            if (name == QLatin1String("pipewire") || name == QLatin1String("wireplumber"))
                continue;
            DeviceUse use;
            use.kind = DeviceUse::Camera;
            use.pid = pid;
            use.name = name;
            uses.append(use);
        }
    }
    std::sort(uses.begin(), uses.end(), [](const DeviceUse &a, const DeviceUse &b) {
        return std::tie(a.pid, a.name) < std::tie(b.pid, b.name);
    });
    if (uses != m_uses) {
        m_uses = uses;
        Q_EMIT usesChanged();
    }
}

}
