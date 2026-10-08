#include "filewatcher.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cerrno>
#include <fcntl.h>
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

                const auto filter = m_watches.constFind(event->wd);
                const bool unrelatedEntry = filter != m_watches.cend() && !filter->all
                        && event->len > 0 && !filter->names.contains(QByteArray(event->name));

                if (!(event->mask & IN_IGNORED) && !unrelatedEntry)
                    dirty = true;

                if ((event->mask & (IN_IGNORED | IN_DELETE_SELF | IN_MOVE_SELF))
                    && filter != m_watches.cend())
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
    delete m_mountNotifier;

    if (m_fd >= 0)
        ::close(m_fd);

    if (m_mountTable >= 0)
        ::close(m_mountTable);
}

void FileWatcher::clear()
{
    for (auto it = m_watches.cbegin(); it != m_watches.cend(); ++it)
        ::inotify_rm_watch(m_fd, it.key());

    m_watches.clear();
    m_paths.clear();
    m_entries.clear();
    m_mounts = false;
    m_valid = false;

    delete m_mountNotifier;
    m_mountNotifier = nullptr;

    if (m_mountTable >= 0)
        ::close(m_mountTable);
    m_mountTable = -1;
}

bool FileWatcher::watch(const QStringList &paths, const QHash<QString, QList<QByteArray>> &entries,
                        bool mounts)
{
    // Adding a directory watch walks every child dentry; keep one that still holds.
    if (m_valid && paths == m_paths && entries == m_entries && mounts == m_mounts)
        return true;

    clear();

    if (m_fd < 0) {
        emit error(tr("Cannot initialize filesystem watching"));
        return false;
    }

    QHash<QString, Filter> wanted;
    for (const auto &path : paths) {
        const QString clean = QDir::cleanPath(path);
        const QFileInfo info(clean);
        wanted[clean].all = true;

        if (info.absolutePath() != clean)
            wanted[info.absolutePath()].names.append(QFile::encodeName(info.fileName()));
    }

    for (auto it = entries.cbegin(); it != entries.cend(); ++it)
        wanted[QDir::cleanPath(it.key())].names.append(it.value());

    QStringList unwatched;

    for (auto it = wanted.cbegin(); it != wanted.cend(); ++it) {
        const int wd = ::inotify_add_watch(m_fd, QFile::encodeName(it.key()),
                                           IN_CREATE | IN_DELETE | IN_MOVED_FROM | IN_MOVED_TO
                                                   | IN_ATTRIB | IN_CLOSE_WRITE | IN_DELETE_SELF
                                                   | IN_MOVE_SELF | IN_ONLYDIR);
        if (wd >= 0) {
            // Two paths can be one folder, which has one watch.
            Filter &filter = m_watches[wd];
            filter.all = filter.all || it->all;
            filter.names.append(it->names);
        } else if (it->all) {
            unwatched.append(it.key());
        }
    }

    m_paths = paths;
    m_entries = entries;
    m_mounts = mounts;
    m_valid = unwatched.isEmpty();

    // The mount table reports a change as an exceptional condition.
    if (mounts) {
        m_mountTable = ::open("/proc/self/mountinfo", O_RDONLY | O_CLOEXEC);
        if (m_mountTable >= 0) {
            m_mountNotifier = new QSocketNotifier(m_mountTable, QSocketNotifier::Exception, this);
            connect(m_mountNotifier, &QSocketNotifier::activated, this, &FileWatcher::changed);
        }
    }

    if (!unwatched.isEmpty())
        emit error(tr("Cannot watch %1; polling for changes").arg(unwatched.join(", ")));

    return m_valid;
}

} // namespace Cutefish::IO
