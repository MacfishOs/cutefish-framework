// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

namespace CutefishPrivacy {

// One process using one capture device.
struct DeviceUse {
    enum Kind { Camera, Microphone, SystemAudio };

    Kind kind = Camera;
    quint32 pid = 0;
    // The sandboxed application's id, as the portal granted it access.
    QString appId;
    // What the process calls itself; a fallback for one no application claims.
    QString name;

    bool operator==(const DeviceUse &other) const = default;
};

using DeviceUses = QList<DeviceUse>;

}
