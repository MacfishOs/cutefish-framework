#include "filewatcher.h"
#include <QFile>
#include <QFileInfo>
#include <cerrno>
#include <sys/inotify.h>
#include <unistd.h>

namespace Cutefish::IO {
FileWatcher::FileWatcher(QObject *parent)
    : QObject(parent)
{
    m_fd = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (m_fd < 0)
        return;
    m_notifier = new QSocketNotifier(m_fd, QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, [this] {
        alignas(inotify_event) char buffer[64 * 1024];
        bool dirty = false;
        for (;;) {
            const auto length = ::read(m_fd, buffer, sizeof(buffer));
            if (length < 0 && errno == EINTR)
                continue;
            if (length <= 0)
                break;
            for (ssize_t offset = 0; offset < length;) {
                const auto *event = reinterpret_cast<const inotify_event *>(buffer + offset);
                if (event->mask & IN_Q_OVERFLOW)
                    emit error(tr("Filesystem watch queue overflow; refreshing directory"));
                if (!(event->mask & IN_IGNORED))
                    dirty = true;
                offset += sizeof(inotify_event) + event->len;
            }
        }
        if (dirty)
            emit changed();
    });
}

FileWatcher::~FileWatcher()
{
    delete m_notifier;
    if (m_fd >= 0)
        ::close(m_fd);
}

void FileWatcher::clear()
{
    for (int wd : m_watches)
        ::inotify_rm_watch(m_fd, wd);
    m_watches.clear();
}

bool FileWatcher::watch(const QString &path)
{
    clear();
    if (m_fd < 0) {
        emit error(tr("Cannot initialize filesystem watching"));
        return false;
    }
    bool watched = false;
    for (const auto &entry : {path, QFileInfo(path).absolutePath()}) {
        const int wd = ::inotify_add_watch(m_fd, QFile::encodeName(entry),
                                           IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO
                                                   | IN_ATTRIB | IN_CLOSE_WRITE | IN_DELETE_SELF
                                                   | IN_MOVE_SELF | IN_ONLYDIR);
        if (wd >= 0) {
            if (!m_watches.contains(wd))
                m_watches.append(wd);
            if (entry == path)
                watched = true;
        }
    }
    if (!watched)
        emit error(tr("Cannot watch %1; polling for changes").arg(path));
    return watched;
}

} // namespace Cutefish::IO
