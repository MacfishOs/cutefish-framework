#include "bluetoothdbustypes.h"
#include <QDBusMetaType>

void registerBluetoothDBusTypes()
{
    qDBusRegisterMetaType<BluetoothInterfaces>();
    qDBusRegisterMetaType<BluetoothObjects>();
}
