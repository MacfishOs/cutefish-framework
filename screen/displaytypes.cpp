// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
//
// Upstream: chromium/src/ash/display/cros_display_config.cc
// Revision: 7acd40ddefff49c9d36144999aafdbb674dbd07a
//
// Adapted for Cutefish: serializers implement the compositor's versioned
// session D-Bus tuples without Ash or Mojo.

#include "displaytypes.h"

#include <QDBusMetaType>

namespace cutefish::display::ipc {
#define WRITE(Type, Fields)                                                                        \
    QDBusArgument &operator<<(QDBusArgument &a, const Type &v)                                     \
    {                                                                                              \
        a.beginStructure();                                                                        \
        a Fields;                                                                                  \
        a.endStructure();                                                                          \
        return a;                                                                                  \
    }
#define READ(Type, Fields)                                                                         \
    const QDBusArgument &operator>>(const QDBusArgument &a, Type &v)                               \
    {                                                                                              \
        a.beginStructure();                                                                        \
        a Fields;                                                                                  \
        a.endStructure();                                                                          \
        return a;                                                                                  \
    }
WRITE(Mode, << v.width << v.height << v.refreshMilliHz << v.pixelClockKHz << v.horizontalTotal
            << v.verticalTotal << v.flags << v.preferred << v.current << v.name)
READ(Mode, >> v.width >> v.height >> v.refreshMilliHz >> v.pixelClockKHz >> v.horizontalTotal
             >> v.verticalTotal >> v.flags >> v.preferred >> v.current >> v.name)
WRITE(RequestedMode, << v.width << v.height << v.refreshMilliHz << v.pixelClockKHz
                     << v.horizontalTotal << v.verticalTotal << v.flags << v.preferred << v.name)
READ(RequestedMode, >> v.width >> v.height >> v.refreshMilliHz >> v.pixelClockKHz
             >> v.horizontalTotal >> v.verticalTotal >> v.flags >> v.preferred >> v.name)
WRITE(PhysicalDisplay, << v.sessionId << v.persistentKey << v.portKey << v.edidKey
                       << v.connectorName << v.devicePath << v.baseConnectorKey << v.pathTopology
                       << v.make << v.model << v.serialNumber << v.physicalWidthMm
                       << v.physicalHeightMm << v.connectionType << v.panelOrientation
                       << v.capabilities << v.recommendedScale120 << v.modes)
READ(PhysicalDisplay, >> v.sessionId >> v.persistentKey >> v.portKey >> v.edidKey >> v.connectorName
             >> v.devicePath >> v.baseConnectorKey >> v.pathTopology >> v.make >> v.model
             >> v.serialNumber >> v.physicalWidthMm >> v.physicalHeightMm >> v.connectionType
             >> v.panelOrientation >> v.capabilities >> v.recommendedScale120 >> v.modes)
WRITE(LogicalDisplay, << v.id << v.waylandName << v.destinations << v.x << v.y << v.width
                      << v.height << v.workX << v.workY << v.workWidth << v.workHeight << v.scale120
                      << v.rotation << v.primary)
READ(LogicalDisplay, >> v.id >> v.waylandName >> v.destinations >> v.x >> v.y >> v.width >> v.height
             >> v.workX >> v.workY >> v.workWidth >> v.workHeight >> v.scale120 >> v.rotation
             >> v.primary)
WRITE(DisplayRequest, << v.id << v.enabled << v.mode << v.scale120 << v.rotation)
READ(DisplayRequest, >> v.id >> v.enabled >> v.mode >> v.scale120 >> v.rotation)
WRITE(Placement, << v.displayId << v.parentId << v.edge << v.offset)
READ(Placement, >> v.displayId >> v.parentId >> v.edge >> v.offset)
#undef WRITE
#undef READ

void registerTypes()
{
    qDBusRegisterMetaType<Mode>();
    qDBusRegisterMetaType<RequestedMode>();
    qDBusRegisterMetaType<PhysicalDisplay>();
    qDBusRegisterMetaType<LogicalDisplay>();
    qDBusRegisterMetaType<DisplayRequest>();
    qDBusRegisterMetaType<Placement>();
    qDBusRegisterMetaType<QList<Mode>>();
    qDBusRegisterMetaType<QList<PhysicalDisplay>>();
    qDBusRegisterMetaType<QList<LogicalDisplay>>();
    qDBusRegisterMetaType<QList<DisplayRequest>>();
    qDBusRegisterMetaType<QList<Placement>>();
    qDBusRegisterMetaType<MirrorGroups>();
}

} // namespace cutefish::display::ipc
