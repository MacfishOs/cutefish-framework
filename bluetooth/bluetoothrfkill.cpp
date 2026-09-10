#include "bluetoothrfkill.h"
#include "bluetoothmanager.h"
#include <QSocketNotifier>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <linux/rfkill.h>
#include <unistd.h>

BluetoothRfkill::BluetoothRfkill(BluetoothManager *manager)
    : QObject(manager)
    , m_manager(manager)
{
    m_fd = ::open("/dev/rfkill", O_RDONLY | O_NONBLOCK | O_CLOEXEC);
    if (m_fd < 0)
        return;
    auto *notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(notifier, &QSocketNotifier::activated, this, [this] {
        readEvents();
    });
    readEvents();
}

BluetoothRfkill::~BluetoothRfkill()
{
    if (m_fd >= 0)
        ::close(m_fd);
}

bool BluetoothRfkill::hardwareBlocked() const
{
    return std::any_of(m_states.cbegin(), m_states.cend(), [](auto state) {
        return state.second;
    });
}

bool BluetoothRfkill::softwareBlocked() const
{
    return std::any_of(m_states.cbegin(), m_states.cend(), [](auto state) {
        return state.first;
    });
}

bool BluetoothRfkill::unblock()
{
    if (!softwareBlocked())
        return true;
    const int fd = ::open("/dev/rfkill", O_WRONLY | O_CLOEXEC);
    rfkill_event event {};
    event.type = RFKILL_TYPE_BLUETOOTH;
    event.op = RFKILL_OP_CHANGE_ALL;
    const bool ok = fd >= 0 && ::write(fd, &event, sizeof(event)) == sizeof(event);
    const int error = errno;
    if (fd >= 0)
        ::close(fd);
    if (!ok)
        m_manager->fail(tr("Cannot unblock Bluetooth: %1")
                                .arg(QString::fromLocal8Bit(std::strerror(error))));
    return ok;
}

void BluetoothRfkill::readEvents()
{
    rfkill_event event {};
    while (::read(m_fd, &event, sizeof(event)) == sizeof(event)) {
        if (event.op == RFKILL_OP_DEL)
            m_states.remove(event.idx);
        else if (event.type == RFKILL_TYPE_BLUETOOTH)
            m_states[event.idx] = {event.soft != 0, event.hard != 0};
    }
    emit m_manager->changed();
}
