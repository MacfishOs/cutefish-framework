#pragma once

#include <QDBusObjectPath>
#include <QMap>
#include <QVariantMap>

using BluetoothInterfaces = QMap<QString, QVariantMap>;
using BluetoothObjects = QMap<QDBusObjectPath, BluetoothInterfaces>;
Q_DECLARE_METATYPE(BluetoothInterfaces)
Q_DECLARE_METATYPE(BluetoothObjects)

void registerBluetoothDBusTypes();

namespace BluetoothDBus {
inline const QString adapterInterface = QStringLiteral("org.bluez.Adapter1");
inline const QString deviceInterface = QStringLiteral("org.bluez.Device1");
inline const QString propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
inline const QString agentPath = QStringLiteral("/org/cutefish/BluetoothAgent");
inline const QString agentManagerInterface = QStringLiteral("org.bluez.AgentManager1");

} // namespace BluetoothDBus
