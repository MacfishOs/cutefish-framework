// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "deviceswitches.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDebug>
#include <QFile>

static const QString kService = QStringLiteral("com.cutefish.Privacy");
static const QString kPath = QStringLiteral("/com/cutefish/Privacy");
static const QString kInterface = QStringLiteral("com.cutefish.Privacy");
// The service's state: read here so that showing the switches starts no root process.
static const QString kStateDirectory = QStringLiteral("/var/lib/cutefish-privacy/");

namespace CutefishPrivacy {

DeviceSwitches::DeviceSwitches(QObject *parent)
    : QObject(parent)
    , m_cameraEnabled(!QFile::exists(kStateDirectory + QStringLiteral("camera-off")))
    , m_microphoneEnabled(!QFile::exists(kStateDirectory + QStringLiteral("microphone-off")))
{
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.connect(kService, kPath, kInterface, QStringLiteral("cameraEnabledChanged"),
                this, SLOT(onCameraEnabledChanged(bool)));
    bus.connect(kService, kPath, kInterface, QStringLiteral("microphoneEnabledChanged"),
                this, SLOT(onMicrophoneEnabledChanged(bool)));
}

DeviceSwitches *DeviceSwitches::instance()
{
    static DeviceSwitches *switches = new DeviceSwitches(QCoreApplication::instance());
    return switches;
}

void DeviceSwitches::setCameraEnabled(bool enabled)
{
    call(QStringLiteral("setCameraEnabled"), enabled);
}

void DeviceSwitches::setMicrophoneEnabled(bool enabled)
{
    call(QStringLiteral("setMicrophoneEnabled"), enabled);
}

void DeviceSwitches::onCameraEnabledChanged(bool enabled)
{
    if (m_cameraEnabled == enabled)
        return;

    m_cameraEnabled = enabled;
    Q_EMIT cameraEnabledChanged();
}

void DeviceSwitches::onMicrophoneEnabledChanged(bool enabled)
{
    if (m_microphoneEnabled == enabled)
        return;

    m_microphoneEnabled = enabled;
    Q_EMIT microphoneEnabledChanged();
}

void DeviceSwitches::call(const QString &method, bool enabled)
{
    QDBusMessage message = QDBusMessage::createMethodCall(kService, kPath, kInterface, method);
    message << enabled;

    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [method](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();

        if (watcher->isError())
            qWarning() << "DeviceSwitches:" << method << "failed:" << watcher->error().message();
    });
}

}
