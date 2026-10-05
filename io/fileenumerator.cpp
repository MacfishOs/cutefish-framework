#include "fileenumerator.h"

#include "async.h"
#include "filesystemurl.h"
#include "posix_p.h"

#include <QStringList>

#include <algorithm>
#include <deque>
#include <linux/magic.h>
#include <sys/stat.h>
#include <sys/vfs.h>

namespace Cutefish::IO {

using namespace Internal;

static quint64 sizeAt(int parent, const QByteArray &name, const QString &path, OperationContext &ctx)
{
    ctx.checkpoint(path);

    struct stat st;
    check(::fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW), path);

    quint64 size = 0;

    if (S_ISDIR(st.st_mode)) {
        Fd fd(::openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        check(fd.value, path);

        children(fd.value, path, [&](const QByteArray &child, const QString &childPath) {
            size += sizeAt(fd.value, child, childPath, ctx);
        });
    } else if (S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) {
        size = st.st_size;
        if (S_ISREG(st.st_mode))
            ctx.allocated += std::min<quint64>(st.st_size, quint64(st.st_blocks) * 512);
        ctx.advance(size);
    }

    ++ctx.files;
    return size;
}

// Kernel interfaces, whose trees are huge or never end and hold no user files.
static bool isKernelFileSystem(int fd)
{
    struct statfs fs;
    if (::fstatfs(fd, &fs) < 0)
        return false;

    switch (fs.f_type) {
    case PROC_SUPER_MAGIC:
    case SYSFS_MAGIC:
    case DEVPTS_SUPER_MAGIC:
    case CGROUP_SUPER_MAGIC:
    case CGROUP2_SUPER_MAGIC:
    case DEBUGFS_MAGIC:
    case TRACEFS_MAGIC:
    case SECURITYFS_MAGIC:
    case BPF_FS_MAGIC:
    case PSTOREFS_MAGIC:
        return true;
    default:
        return false;
    }
}

FileItemList FileEnumerator::list(const QUrl &url, bool hidden, OperationContext *context)
{
    if (context)
        context->checkpoint();

    const auto path = FileSystemUrl(url).localPath();
    Fd fd(::open(native(path), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (fd.value < 0 && errno == ENOENT && url.scheme() == "trash" && url.path() == "/")
        return {};
    check(fd.value, path);

    QList<QByteArray> names;
    children(fd.value, path, [&](const QByteArray &name) {
        if (context)
            context->checkpoint(path);

        if (!hidden && name.startsWith('.'))
            return;

        names.append(name);
    });

    // Building the items costs more than reading the names: a large folder spreads it.
    constexpr qsizetype BlockSize = 256;
    const qsizetype count = names.size();
    const qsizetype blocks = (count + BlockSize - 1) / BlockSize;
    std::atomic<qsizetype> nextBlock = 0;

    FileItemList result(count);
    FileItem *const items = result.data();
    const QString urlBase = url.path() + (url.path().endsWith('/') ? "" : "/");
    const QString pathBase = path + (path.endsWith('/') ? "" : "/");

    runOnFreeThreads(count >= LargeFolder ? blockHelpers(blocks) : 0,
                     [&](const std::atomic_bool &stop) {
        for (qsizetype block = nextBlock++; block < blocks && !stop; block = nextBlock++) {
            if (context)
                context->checkpoint();

            const qsizetype end = qMin(count, (block + 1) * BlockSize);
            for (qsizetype index = block * BlockSize; index < end; ++index) {
                const QString decoded = QFile::decodeName(names.at(index));
                auto child = url;
                child.setPath(urlBase + decoded);
                items[index] = FileItem(child, pathBase + decoded, decoded);
            }
        }
    });

    return result;
}

FileItemList FileEnumerator::search(const QUrl &url, const QStringList &words, bool hidden,
                                    OperationContext *context,
                                    const std::function<void(FileItem &)> &found)
{
    struct Folder
    {
        QUrl url;
        QString path;
    };

    FileItemList result;
    FileItem::TypeDetails details;
    std::deque<Folder> folders;
    folders.push_back({url, FileSystemUrl(url).localPath()});
    bool first = true;

    while (!folders.empty()) {
        const Folder folder = std::move(folders.front());
        folders.pop_front();

        if (context)
            context->checkpoint();

        const int fd = ::open(native(folder.path),
                              O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (fd < 0 && first)
            check(fd, folder.path);
        first = false;
        if (fd < 0)
            continue;

        if (isKernelFileSystem(fd)) {
            ::close(fd);
            continue;
        }

        DIR *raw = ::fdopendir(fd);
        if (!raw) {
            ::close(fd);
            continue;
        }

        auto closeDir = [](DIR *value) {
            ::closedir(value);
        };
        std::unique_ptr<DIR, decltype(closeDir)> dir(raw, closeDir);

        const QString urlBase = folder.url.path() + (folder.url.path().endsWith('/') ? "" : "/");
        const QString pathBase = folder.path + (folder.path.endsWith('/') ? "" : "/");

        // A folder that fails halfway keeps what it listed.
        while (auto *entry = ::readdir(dir.get())) {
            if (context)
                context->checkpoint();

            const QByteArray name(entry->d_name);
            if (name == "." || name == ".." || (!hidden && name.startsWith('.')))
                continue;

            bool isDir = entry->d_type == DT_DIR;
            if (entry->d_type == DT_UNKNOWN) {
                struct stat st;
                isDir = ::fstatat(::dirfd(dir.get()), entry->d_name, &st, AT_SYMLINK_NOFOLLOW) == 0
                        && S_ISDIR(st.st_mode);
            }

            const QString decoded = QFile::decodeName(name);
            auto child = folder.url;
            child.setPath(urlBase + decoded);

            if (isDir)
                folders.push_back({child, pathBase + decoded});

            const bool matches = std::all_of(words.cbegin(), words.cend(), [&](const QString &word) {
                return decoded.contains(word, Qt::CaseInsensitive);
            });
            if (!matches)
                continue;

            FileItem item(child, pathBase + decoded, decoded);
            item.prepareMetadata(FileItem(), &details);
            if (found)
                found(item);
            result.append(std::move(item));
        }
    }

    return result;
}

quint64 FileEnumerator::size(const QString &path, OperationContext &ctx)
{
    if (path == "/")
        return sizeAt(AT_FDCWD, "/", path, ctx);

    Parent parent(path);
    return sizeAt(parent.fd.value, parent.name, path, ctx);
}

} // namespace Cutefish::IO
