#include "fileenumerator.h"

#include "async.h"
#include "filesystemurl.h"
#include "posix_p.h"
#include "trashdirectory_p.h"

#include <QStringList>

#include <algorithm>
#include <deque>
#include <optional>
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

    // The folders listed together: trash:/ is the files/ of every trash.
    struct Folder
    {
        QString path;
        QUrl url;
        // Set for the files/ of a trash, whose items have info files.
        std::optional<TrashDirectory> trash;
        QList<QByteArray> names;
        QString urlBase;
        QString pathBase;
    };

    std::vector<Folder> folders;
    const bool trashRoot =
            url.scheme() == QLatin1String("trash") && url.path() == QLatin1String("/");

    if (trashRoot) {
        FileSystemUrl validated(url);

        for (const auto &trash : trashDirectories()) {
            auto folderUrl = url;
            folderUrl.setPath('/' + trash.id);
            folders.push_back({trash.path + "/files", folderUrl, trash});
        }
    } else {
        folders.push_back({FileSystemUrl(url).localPath(), url});

        if (url.scheme() == QLatin1String("trash")) {
            QString inside;
            const auto trash = trashDirectory(url, &inside);
            if (inside.isEmpty())
                folders.front().trash = trash;
        }
    }

    for (auto &folder : folders) {
        const auto &path = folder.path;
        // Nothing trashed yet is no error, and a volume's trash that cannot be read
        // leaves the rest of the trash.
        const bool skippable = trashRoot && !folder.trash->topdir.isEmpty();

        Fd fd(::open(native(path), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
        if (fd.value < 0 && trashRoot && (errno == ENOENT || skippable))
            continue;
        check(fd.value, path);

        try {
            children(fd.value, path, [&](const QByteArray &name) {
                if (context)
                    context->checkpoint(path);

                if (!hidden && name.startsWith('.'))
                    return;

                folder.names.append(name);
            });
        } catch (const Failure &failure) {
            if (!skippable || failure.error.code == ECANCELED)
                throw;

            folder.names.clear();
        }
    }

    // Where each folder's items start in the listing.
    std::vector<qsizetype> starts;
    qsizetype count = 0;
    for (auto &folder : folders) {
        starts.push_back(count);
        count += folder.names.size();
        folder.urlBase = folder.url.path() + (folder.url.path().endsWith('/') ? "" : "/");
        folder.pathBase = folder.path + (folder.path.endsWith('/') ? "" : "/");
    }

    // Building the items costs more than reading the names: a large folder spreads it.
    constexpr qsizetype BlockSize = 256;
    const qsizetype blocks = (count + BlockSize - 1) / BlockSize;
    std::atomic<qsizetype> nextBlock = 0;

    FileItemList result(count);
    FileItem *const items = result.data();

    runOnFreeThreads(count >= LargeFolder ? blockHelpers(blocks) : 0,
                     [&](const std::atomic_bool &stop) {
        for (qsizetype block = nextBlock++; block < blocks && !stop; block = nextBlock++) {
            if (context)
                context->checkpoint();

            const qsizetype end = qMin(count, (block + 1) * BlockSize);
            for (qsizetype index = block * BlockSize; index < end; ++index) {
                const auto at = std::upper_bound(starts.cbegin(), starts.cend(), index) - 1;
                const Folder &folder = folders[at - starts.cbegin()];

                const QString decoded = QFile::decodeName(folder.names.at(index - *at));
                auto child = folder.url;
                child.setPath(folder.urlBase + decoded);

                FileItem &item = items[index];
                item = FileItem(child, folder.pathBase + decoded, decoded);

                // A small file each; the trash is listed seldom.
                TrashInfo info;
                if (folder.trash && readTrashInfo(*folder.trash, decoded, &info)) {
                    item.m_originalPath = info.path;
                    item.m_deletionTime = info.deletionTime;
                }
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
