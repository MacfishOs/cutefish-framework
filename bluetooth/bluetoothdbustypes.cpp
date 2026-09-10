#include "bluetoothdbustypes.h"
#include <QDBusArgument>
#include <QDBusMetaType>

namespace {
QDBusArgument &operator<<(QDBusArgument &argument, const BluetoothInterfaces &interfaces)
{
    argument.beginMap(QMetaType::fromType<QString>(), QMetaType::fromType<QVariantMap>());
    for (auto it = interfaces.cbegin(); it != interfaces.cend(); ++it) {
        argument.beginMapEntry();
        argument << it.key() << it.value();
        argument.endMapEntry();
    }
    argument.endMap();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, BluetoothInterfaces &interfaces)
{
    argument.beginMap();
    while (!argument.atEnd()) {
        QString name;
        QVariantMap properties;
        argument.beginMapEntry();
        argument >> name >> properties;
        argument.endMapEntry();
        interfaces.insert(name, properties);
    }
    argument.endMap();
    return argument;
}

QDBusArgument &operator<<(QDBusArgument &argument, const BluetoothObjects &objects)
{
    argument.beginMap(QMetaType::fromType<QDBusObjectPath>(),
                      QMetaType::fromType<BluetoothInterfaces>());
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        argument.beginMapEntry();
        argument << it.key() << it.value();
        argument.endMapEntry();
    }
    argument.endMap();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, BluetoothObjects &objects)
{
    argument.beginMap();
    while (!argument.atEnd()) {
        QDBusObjectPath path;
        BluetoothInterfaces interfaces;
        argument.beginMapEntry();
        argument >> path >> interfaces;
        argument.endMapEntry();
        objects.insert(path, interfaces);
    }
    argument.endMap();
    return argument;
}
} // namespace

void registerBluetoothDBusTypes()
{
    qDBusRegisterMetaType<BluetoothInterfaces>();
    qDBusRegisterMetaType<BluetoothObjects>();
}
