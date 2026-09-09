// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Upstream: chromium/src/ash/display/cros_display_config.h
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a
//
// Adapted for Cutefish: these fixed Qt D-Bus value types mirror the native
// compositor schema without linking the framework to compositor internals.

#pragma once

#include <QDBusArgument>
#include <QList>
#include <QString>
#include <QStringList>

namespace cutefish::display::ipc {

struct Mode {
    int width = 0;
    int height = 0;
    int refreshMilliHz = 0;
    int pixelClockKHz = 0;
    int horizontalTotal = 0;
    int verticalTotal = 0;
    uint flags = 0;
    bool preferred = false;
    bool current = false;
    QString name;
};

struct RequestedMode {
    int width = 0;
    int height = 0;
    int refreshMilliHz = 0;
    int pixelClockKHz = 0;
    int horizontalTotal = 0;
    int verticalTotal = 0;
    uint flags = 0;
    bool preferred = false;
    QString name;
};

struct PhysicalDisplay {
    QString sessionId;
    QString persistentKey;
    QString portKey;
    QString edidKey;
    QString connectorName;
    QString devicePath;
    QString baseConnectorKey;
    QStringList pathTopology;
    QString make;
    QString model;
    QString serialNumber;
    int physicalWidthMm = 0;
    int physicalHeightMm = 0;
    uint connectionType = 0;
    uint panelOrientation = 0;
    uint capabilities = 0;
    QList<Mode> modes;
};

struct LogicalDisplay {
    QString id;
    QString waylandName;
    QStringList destinations;
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    int workX = 0;
    int workY = 0;
    int workWidth = 0;
    int workHeight = 0;
    uint scale120 = 120;
    uint rotation = 0;
    bool primary = false;
};

struct DisplayRequest {
    QString id;
    bool enabled = true;
    RequestedMode mode;
    uint scale120 = 120;
    uint rotation = 0;
};

struct Placement {
    QString displayId;
    QString parentId;
    uint edge = 0;
    int offset = 0;
};

using MirrorGroups = QList<QStringList>;

QDBusArgument &operator<<(QDBusArgument &, const Mode &);
const QDBusArgument &operator>>(const QDBusArgument &, Mode &);
QDBusArgument &operator<<(QDBusArgument &, const RequestedMode &);
const QDBusArgument &operator>>(const QDBusArgument &, RequestedMode &);
QDBusArgument &operator<<(QDBusArgument &, const PhysicalDisplay &);
const QDBusArgument &operator>>(const QDBusArgument &, PhysicalDisplay &);
QDBusArgument &operator<<(QDBusArgument &, const LogicalDisplay &);
const QDBusArgument &operator>>(const QDBusArgument &, LogicalDisplay &);
QDBusArgument &operator<<(QDBusArgument &, const DisplayRequest &);
const QDBusArgument &operator>>(const QDBusArgument &, DisplayRequest &);
QDBusArgument &operator<<(QDBusArgument &, const Placement &);
const QDBusArgument &operator>>(const QDBusArgument &, Placement &);
void registerTypes();

} // namespace cutefish::display::ipc

Q_DECLARE_METATYPE(cutefish::display::ipc::Mode)
Q_DECLARE_METATYPE(cutefish::display::ipc::RequestedMode)
Q_DECLARE_METATYPE(cutefish::display::ipc::PhysicalDisplay)
Q_DECLARE_METATYPE(cutefish::display::ipc::LogicalDisplay)
Q_DECLARE_METATYPE(cutefish::display::ipc::DisplayRequest)
Q_DECLARE_METATYPE(cutefish::display::ipc::Placement)
Q_DECLARE_METATYPE(QList<cutefish::display::ipc::Mode>)
Q_DECLARE_METATYPE(QList<cutefish::display::ipc::PhysicalDisplay>)
Q_DECLARE_METATYPE(QList<cutefish::display::ipc::LogicalDisplay>)
Q_DECLARE_METATYPE(QList<cutefish::display::ipc::DisplayRequest>)
Q_DECLARE_METATYPE(QList<cutefish::display::ipc::Placement>)
Q_DECLARE_METATYPE(cutefish::display::ipc::MirrorGroups)
