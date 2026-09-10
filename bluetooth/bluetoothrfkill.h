#pragma once
#include <QMap>
#include <QObject>

class BluetoothManager;

class BluetoothRfkill : public QObject
{
public:
    explicit BluetoothRfkill(BluetoothManager *manager);
    ~BluetoothRfkill() override;
    bool hardwareBlocked() const;
    bool softwareBlocked() const;
    bool unblock();

private:
    void readEvents();
    BluetoothManager *m_manager;
    int m_fd = -1;
    QMap<quint32, QPair<bool, bool>> m_states;
};
