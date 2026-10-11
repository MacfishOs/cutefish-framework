// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "deviceusage.h"
#include "pipewirecapture.h"
#include "videodevicewatch.h"

#include <QCoreApplication>
#include <QVariantMap>

#include <algorithm>

namespace CutefishPrivacy {

DeviceUsage::DeviceUsage(QObject *parent)
    : DeviceUsage(new PipeWireCapture, new VideoDeviceWatch, parent)
{
}

DeviceUsage::DeviceUsage(PipeWireCapture *capture, VideoDeviceWatch *videoDevices, QObject *parent)
    : QObject(parent)
    , m_capture(capture)
    , m_videoDevices(videoDevices)
{
    m_capture->setParent(this);
    m_videoDevices->setParent(this);
    connect(m_capture, &PipeWireCapture::usesChanged, this, &DeviceUsage::update);
    connect(m_videoDevices, &VideoDeviceWatch::usesChanged, this, &DeviceUsage::update);
    update();
}

DeviceUsage *DeviceUsage::instance()
{
    static DeviceUsage *usage = new DeviceUsage(QCoreApplication::instance());
    return usage;
}

QVariantList DeviceUsage::usesList() const
{
    QVariantList list;
    for (const DeviceUse &use : m_uses) {
        list.append(QVariantMap {
            {QStringLiteral("kind"), int(use.kind)},
            {QStringLiteral("pid"), use.pid},
            {QStringLiteral("appId"), use.appId},
            {QStringLiteral("name"), use.name},
        });
    }
    return list;
}

bool DeviceUsage::isUsed(DeviceUse::Kind kind) const
{
    return std::any_of(m_uses.cbegin(), m_uses.cend(), [kind](const DeviceUse &use) {
        return use.kind == kind;
    });
}

void DeviceUsage::update()
{
    DeviceUses uses = m_capture->uses();
    for (const DeviceUse &use : m_videoDevices->uses()) {
        if (!uses.contains(use))
            uses.append(use);
    }
    std::stable_sort(uses.begin(), uses.end(), [](const DeviceUse &a, const DeviceUse &b) {
        return a.kind < b.kind;
    });
    if (uses != m_uses) {
        m_uses = uses;
        Q_EMIT usesChanged();
    }
}

}
