#include "localfilesystem.h"
#include "fileenumerator.h"
#include "posix_p.h"

#include <QUuid>

#include <deque>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/syscall.h>

namespace Cutefish::IO {

using namespace Internal;

// A mount is a different filesystem, or a bind of one; erasing through it would empty it.
static bool isMountRoot(int parent, const QByteArray &name, const struct stat &st)
{
    struct statx stx;
    if (::statx(parent, name, AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT, 0, &stx) == 0
        && (stx.stx_attributes_mask & STATX_ATTR_MOUNT_ROOT))
        return stx.stx_attributes & STATX_ATTR_MOUNT_ROOT;

    struct stat parentSt;
    return ::fstat(parent, &parentSt) == 0 && parentSt.st_dev != st.st_dev;
}

void Internal::eraseAt(int parent, const QByteArray &name, const QString &path,
                       OperationContext &ctx, bool cleanup)
{
    ctx.checkpoint(path);

    struct stat st;
    if (::fstatat(parent, name, &st, AT_SYMLINK_NOFOLLOW) < 0) {
        if (errno == ENOENT)
            return;

        throw Failure(errno, path);
    }

    if (S_ISDIR(st.st_mode)) {
        if (isMountRoot(parent, name, st))
            throw Failure(EBUSY, path);

        Fd fd(::openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        check(fd.value, path);

        if (cleanup)
            check(::fchmod(fd.value, 0700), path);

        children(fd.value, path, [&](const QByteArray &child, const QString &childPath) {
            eraseAt(fd.value, child, childPath, ctx, cleanup);
        });
    }

    ctx.checkpoint(path);
    check(::unlinkat(parent, name, S_ISDIR(st.st_mode) ? AT_REMOVEDIR : 0), path);
    ++ctx.files;
    ctx.advance(0);
}

// Copies drive their own writeback so the page cache holds a bounded amount of unwritten data:
// progress follows the disk, and a slow drive can be ejected soon after the copy ends.
static constexpr off_t kWritebackChunk = 8 * 1024 * 1024;
static constexpr off_t kWritebackLimit = 32 * 1024 * 1024;
static constexpr size_t kWritebackFiles = 256;

namespace {

class Writeback
{
public:
    Writeback() = default;

    ~Writeback()
    {
        for (const auto &range : m_ranges)
            ::close(range.fd);
    }

    Writeback(const Writeback &) = delete;
    Writeback &operator=(const Writeback &) = delete;

    // Starts writing the range back at once and waits only for the oldest ranges, across files,
    // so the drive always has work queued and nothing forces a journal commit or cache flush.
    void push(int fd, off_t offset, off_t length, off_t bytes)
    {
        const int owned = ::fcntl(fd, F_DUPFD_CLOEXEC, 0);
        if (owned < 0)
            return;

        ::sync_file_range(owned, offset, length, SYNC_FILE_RANGE_WRITE);
        m_ranges.push_back({owned, offset, length, bytes});
        m_pending += bytes;

        while (m_pending > kWritebackLimit || m_ranges.size() > kWritebackFiles) {
            const auto range = m_ranges.front();
            m_ranges.pop_front();

            ::sync_file_range(range.fd, range.offset, range.length,
                              SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE
                                      | SYNC_FILE_RANGE_WAIT_AFTER);
            ::posix_fadvise(range.fd, range.offset, range.length, POSIX_FADV_DONTNEED);
            ::close(range.fd);
            m_pending -= range.bytes;
        }
    }

private:
    struct Range
    {
        int fd;
        off_t offset;
        off_t length;
        off_t bytes;
    };

    std::deque<Range> m_ranges;
    off_t m_pending = 0;
};

} // namespace

static void copyAt(int sourceParent, const QByteArray &source, int targetParent, const QByteArray &target,
                   const QString &path, OperationContext &ctx, Writeback &writeback)
{
    ctx.checkpoint(path);

    struct stat st;
    check(::fstatat(sourceParent, source, &st, AT_SYMLINK_NOFOLLOW), path);

    if (S_ISLNK(st.st_mode)) {
        QByteArray link(256, Qt::Uninitialized);

        for (;;) {
            ssize_t length = ::readlinkat(sourceParent, source, link.data(), link.size());
            check(length, path);

            if (length < link.size()) {
                link.resize(length);
                break;
            }

            link.resize(link.size() * 2);
        }

        check(::symlinkat(link, targetParent, target), path);
    } else if (S_ISDIR(st.st_mode)) {
        Fd input(::openat(sourceParent, source, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        check(input.value, path);
        check(::mkdirat(targetParent, target, 0700), path);

        Fd output(::openat(targetParent, target, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        check(output.value, path);

        children(input.value, path, [&](const QByteArray &child, const QString &childPath) {
            copyAt(input.value, child, output.value, child, childPath, ctx, writeback);
        });

        check(::fchmod(output.value, st.st_mode & 0777), path);
        const timespec times[] = {st.st_atim, st.st_mtim};
        check(::futimens(output.value, times), path);
    } else if (S_ISREG(st.st_mode)) {
        Fd input(::openat(sourceParent, source, O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC));
        check(input.value, path);

        struct stat opened;
        check(::fstat(input.value, &opened), path);
        if (!S_ISREG(opened.st_mode) || opened.st_ino != st.st_ino || opened.st_dev != st.st_dev)
            throw Failure(ESTALE, path);

        Fd output(::openat(targetParent, target,
                           O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        check(output.value, path);

        QByteArray buffer;
        off_t position = 0;
        bool sparse = true;
        bool inKernel = true;

        off_t flushed = 0;
        off_t unflushed = 0;
        auto wrote = [&](off_t bytes) {
            unflushed += bytes;
            if (unflushed < kWritebackChunk)
                return;

            writeback.push(output.value, flushed, position - flushed, unflushed);
            flushed = position;
            unflushed = 0;
        };

        // Filesystems that share extents (Btrfs, XFS) clone the whole file; the rest refuse.
        if (opened.st_size > 0 && ::ioctl(output.value, FICLONE, input.value) == 0) {
            ctx.advance(opened.st_size);
            position = opened.st_size;
        }

        while (position < opened.st_size) {
            ctx.checkpoint(path);
            off_t extentEnd = opened.st_size;

            if (sparse) {
                const off_t data = ::lseek(input.value, position, SEEK_DATA);
                if (data < 0 && errno == ENXIO) {
                    ctx.advance(opened.st_size - position);
                    position = opened.st_size;
                    break;
                }

                if (data < 0 && (errno == EINVAL || errno == ENOTSUP))
                    sparse = false;
                else {
                    if (data < 0)
                        throw Failure(errno, path);

                    ctx.advance(data - position);
                    position = data;

                    extentEnd = ::lseek(input.value, position, SEEK_HOLE);
                    if (extentEnd < 0)
                        throw Failure(errno, path);

                    extentEnd = std::min(extentEnd, opened.st_size);
                }
            }

            while (position < extentEnd) {
                ctx.checkpoint(path);

                if (inKernel) {
                    off_t from = position;
                    off_t to = position;
                    const size_t length = std::min<off_t>(4 * 1024 * 1024, extentEnd - position);
                    const ssize_t copied =
                            ::copy_file_range(input.value, &from, output.value, &to, length, 0);

                    if (copied < 0 && errno == EINTR)
                        continue;

                    if (copied > 0) {
                        ctx.advance(copied);
                        position += copied;
                        wrote(copied);
                        continue;
                    }

                    // Refused (another filesystem, no support) or cut short: the buffered copy
                    // takes over and reports a genuine failure itself.
                    inKernel = false;
                }

                if (buffer.isEmpty())
                    buffer.resize(1024 * 1024);

                ssize_t count =
                        ::pread(input.value, buffer.data(),
                                std::min<off_t>(buffer.size(), extentEnd - position), position);
                if (count < 0 && errno == EINTR)
                    continue;

                check(count, path);
                if (!count)
                    throw Failure(ESTALE, path);

                ssize_t offset = 0;
                while (offset < count) {
                    ctx.checkpoint(path);

                    const ssize_t written = ::pwrite(output.value, buffer.constData() + offset,
                                                     count - offset, position + offset);
                    if (written < 0 && errno == EINTR)
                        continue;

                    check(written, path);
                    if (!written)
                        throw Failure(EIO, path);

                    offset += written;
                    ctx.advance(written);
                }

                position += count;
                wrote(count);
            }
        }

        if (unflushed)
            writeback.push(output.value, flushed, position - flushed, unflushed);

        check(::ftruncate(output.value, opened.st_size), path);
        check(::fchmod(output.value, st.st_mode & 0777), path);
        const timespec times[] = {st.st_atim, st.st_mtim};
        check(::futimens(output.value, times), path);
    } else {
        throw Failure(ENOTSUP, path);
    }

    ++ctx.files;
}

static void validateDestination(const QString &source, const QString &destination)
{
    const auto src = QFileInfo(source).canonicalFilePath();
    const auto dstParent = QFileInfo(QFileInfo(destination).absolutePath()).canonicalFilePath();
    if (!src.isEmpty() && !QFileInfo(source).isSymbolicLink()
        && (dstParent == src || dstParent.startsWith(src + '/')))
        throw Failure(EINVAL, destination);

    struct stat a, b;
    if (::lstat(native(source), &a) == 0 && ::lstat(native(destination), &b) == 0
        && a.st_dev == b.st_dev && a.st_ino == b.st_ino)
        throw Failure(EINVAL, destination);
}

static void mergeStaged(int source, int target, const QString &path, OperationContext &ctx)
{
    children(source, path, [&](const QByteArray &name, const QString &childPath) {
        ctx.checkpoint(childPath);

        struct stat from, to;
        check(::fstatat(source, name, &from, AT_SYMLINK_NOFOLLOW), childPath);
        const bool exists = ::fstatat(target, name, &to, AT_SYMLINK_NOFOLLOW) == 0;

        if (S_ISDIR(from.st_mode) && exists && S_ISDIR(to.st_mode)) {
            Fd input(::openat(source, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
            Fd output(::openat(target, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
            check(input.value, childPath);
            check(output.value, childPath);
            check(::fchmod(input.value, 0700), childPath);
            mergeStaged(input.value, output.value, childPath, ctx);
            check(::unlinkat(source, name, AT_REMOVEDIR), childPath);
        } else {
            check(::renameat(source, name, target, name), childPath);
        }
    });
}

// "name (n).ext", the name an item kept beside an existing one takes.
static QByteArray keptName(const QString &destination, int suffix)
{
    const QFileInfo info(destination);
    const QString extension =
            info.isDir() || info.suffix().isEmpty() ? QString() : '.' + info.suffix();
    const QString base = extension.isEmpty() ? info.fileName() : info.completeBaseName();
    return native(base + QString(" (%1)").arg(suffix) + extension);
}

static QString publish(int parent, const QByteArray &temporary, const QString &source,
                       const QString &destination, ConflictPolicy policy, OperationContext &ctx)
{
    QByteArray name = native(QFileInfo(destination).fileName());
    int suffix = 1;

    for (;;) {
        if (::syscall(SYS_renameat2, parent, temporary.constData(), parent, name.constData(),
                      RENAME_NOREPLACE)
            == 0)
            return QFileInfo(destination).absolutePath() + '/' + QFile::decodeName(name);

        if (errno != EEXIST)
            throw Failure(errno, destination);

        if (policy == ConflictPolicy::Ask) {
            policy = ctx.conflict ? ctx.conflict(source, destination) : ConflictPolicy::Fail;
            ctx.checkpoint(destination);
        }

        if (policy == ConflictPolicy::Skip)
            return {};

        if (policy == ConflictPolicy::Fail)
            throw Failure(EEXIST, destination);

        if (policy == ConflictPolicy::Replace) {
            // Copies are not synced one by one; the replacement must be on disk before the
            // original goes.
            check(::syncfs(parent), destination);

            struct stat from, to;
            check(::fstatat(parent, temporary, &from, AT_SYMLINK_NOFOLLOW), destination);
            check(::fstatat(parent, name, &to, AT_SYMLINK_NOFOLLOW), destination);

            if (S_ISDIR(from.st_mode) && S_ISDIR(to.st_mode)) {
                Fd input(::openat(parent, temporary,
                                  O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
                Fd output(::openat(parent, name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
                check(input.value, destination);
                check(output.value, destination);
                check(::fchmod(input.value, 0700), destination);
                mergeStaged(input.value, output.value, destination, ctx);
                check(::unlinkat(parent, temporary, AT_REMOVEDIR), destination);
            } else {
                check(::renameat(parent, temporary, parent, name), destination);
            }

            return destination;
        }

        name = keptName(destination, suffix++);
    }
}

QString LocalFileSystem::copy(const QString &source, const QString &destination,
                              ConflictPolicy policy, OperationContext &ctx)
{
    // Pasting an item back into its own folder always means "make a copy":
    // there is nothing to replace, so never ask.
    const bool duplicate = QDir::cleanPath(source) == QDir::cleanPath(destination);
    if (duplicate && policy == ConflictPolicy::Ask)
        policy = ConflictPolicy::KeepBoth;
    if (!duplicate || policy != ConflictPolicy::KeepBoth)
        validateDestination(source, destination);

    ctx.checkpoint(source);
    Parent input(source), output(destination);
    QByteArray temporary = ".cutefish-copy-" + QUuid::createUuid().toByteArray(QUuid::Id128);

    try {
        Writeback writeback;
        copyAt(input.fd.value, input.name, output.fd.value, temporary, source, ctx, writeback);
        ctx.checkpoint(source);

        auto result = publish(output.fd.value, temporary, source, destination, policy, ctx);
        if (!result.isEmpty())
            return result;
    } catch (...) {
        OperationContext cleanup;
        try {
            eraseAt(output.fd.value, temporary, destination, cleanup, true);
        } catch (...) {
        }
        throw;
    }

    OperationContext cleanup;
    eraseAt(output.fd.value, temporary, destination, cleanup, true);
    return {};
}

QString LocalFileSystem::link(const QString &source, const QString &destination,
                              ConflictPolicy policy, OperationContext &ctx)
{
    ctx.checkpoint(source);
    Parent output(destination);
    const QByteArray temporary = ".cutefish-link-" + QUuid::createUuid().toByteArray(QUuid::Id128);
    check(::symlinkat(native(source), output.fd.value, temporary), destination);

    try {
        const auto result = publish(output.fd.value, temporary, source, destination, policy, ctx);
        if (result.isEmpty())
            ::unlinkat(output.fd.value, temporary, 0);

        return result;
    } catch (...) {
        ::unlinkat(output.fd.value, temporary, 0);
        throw;
    }
}

QString LocalFileSystem::move(const QString &source, const QString &destination,
                              ConflictPolicy policy, OperationContext &ctx)
{
    validateDestination(source, destination);
    ctx.checkpoint(source);
    Parent input(source), output(destination);

    if (::syscall(SYS_renameat2, input.fd.value, input.name.constData(), output.fd.value,
                  output.name.constData(), RENAME_NOREPLACE)
        == 0)
        return destination;

    const int code = errno;
    if (code == EEXIST && policy == ConflictPolicy::Ask) {
        policy = ctx.conflict ? ctx.conflict(source, destination) : ConflictPolicy::Fail;
        ctx.checkpoint(destination);
    }

    if (code == EEXIST && policy == ConflictPolicy::Skip)
        return {};

    if (code == EEXIST && policy == ConflictPolicy::Fail)
        throw Failure(code, destination);

    if (code == EEXIST && policy == ConflictPolicy::KeepBoth) {
        for (int suffix = 1;; ++suffix) {
            const QByteArray name = keptName(destination, suffix);
            if (::syscall(SYS_renameat2, input.fd.value, input.name.constData(), output.fd.value,
                          name.constData(), RENAME_NOREPLACE)
                == 0)
                return QFileInfo(destination).absolutePath() + '/' + QFile::decodeName(name);
            if (errno != EEXIST)
                break;
        }
    }

    if (code == EEXIST && policy == ConflictPolicy::Replace) {
        if (::renameat(input.fd.value, input.name, output.fd.value, output.name) == 0)
            return destination;
        if (errno != EXDEV && errno != ENOTEMPTY && errno != EEXIST)
            throw Failure(errno, destination);
    } else if (code != EXDEV && code != EEXIST)
        throw Failure(code, destination);

    auto result = copy(source, destination, policy, ctx);

    // Once a cross-device copy is published, finish removal without cancellation.
    if (!result.isEmpty()) {
        // The source is the only durable copy until the target reaches the disk.
        check(::syncfs(output.fd.value), result);
        OperationContext removal;
        remove(source, removal);
    }

    return result;
}

void LocalFileSystem::remove(const QString &path, OperationContext &ctx)
{
    Parent parent(path);
    eraseAt(parent.fd.value, parent.name, path, ctx);
}

void LocalFileSystem::mkdir(const QString &path)
{
    check(::mkdir(native(path), 0777), path);
}

FileItemList LocalFileSystem::enumerate(const QUrl &url, bool hidden)
{
    return FileEnumerator::list(url, hidden);
}

quint64 LocalFileSystem::directorySize(const QString &path, OperationContext &ctx)
{
    return FileEnumerator::size(path, ctx);
}

} // namespace Cutefish::IO
