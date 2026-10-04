#include "filewatcher.h"

#include <QDir>
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

                const bool unrelatedSibling = event->wd == m_parentWatch && event->len > 0
                        && QByteArray(event->name) != m_parentEntry;

                if (!(event->mask & IN_IGNORED) && !unrelatedSibling)
                    dirty = true;

                if ((event->mask & (IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF))
                    && m_watches.contains(event->wd))
                    m_valid = false;

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
    m_path.clear();
    m_valid = false;
    m_parentWatch = -1;
    m_parentEntry.clear();
}

bool FileWatcher::watch(const QString &path)
{
    // Adding a directory watch walks every child dentry; keep one that still holds.
    if (m_valid && path == m_path)
        return true;

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
            if (!m_watches.contains(wd)) {
                m_watches.append(wd);

                if (entry != path) {
                    m_parentWatch = wd;
                    m_parentEntry = QFile::encodeName(QFileInfo(QDir::cleanPath(path)).fileName());
                }
            }

            if (entry == path)
                watched = true;
        }
    }

    m_path = path;
    m_valid = watched;

    if (!watched)
        emit error(tr("Cannot watch %1; polling for changes").arg(path));

    return watched;
}

} // namespace Cutefish::IO
