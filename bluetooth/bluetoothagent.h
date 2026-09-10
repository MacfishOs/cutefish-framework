#pragma once
#include <QDBusVirtualObject>

class BluetoothManager;

class BluetoothAgent : public QDBusVirtualObject
{
public:
    explicit BluetoothAgent(BluetoothManager *manager);
    QString introspect(const QString &path) const override;
    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override;

private:
    BluetoothManager *m_manager;
};
