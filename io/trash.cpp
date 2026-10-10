#include "localfilesystem.h"
#include "posix_p.h"
#include "trashdirectory_p.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QUuid>

#include <optional>
#include <sys/file.h>
#include <sys/stat.h>

namespace Cutefish::IO {

using namespace Internal;

static void ensureHomeTrash()
{
    const auto root = FileSystemUrl::trashRoot();

    for (const auto &path : {root, root + "/files", root + "/info"}) {
        if (!QDir().mkpath(path))
            throw Failure(EACCES, path);

        struct stat st;
        check(::lstat(native(path), &st), path);

        if (!S_ISDIR(st.st_mode) || st.st_uid != ::getuid())
            throw Failure(EACCES, path);

        check(::chmod(native(path), 0700), path);
    }
}

// The home trash may not exist yet; the nearest folder that does stands in for it.
static dev_t homeTrashDevice()
{
    QString path = FileSystemUrl::trashRoot();
    struct stat st;

    while (::stat(native(path), &st) < 0) {
        const QString parent = QFileInfo(path).absolutePath();
        if (parent == path)
            throw Failure(errno, path);

        path = parent;
    }

    return st.st_dev;
}

// The mount point of the filesystem path is on, path being canonical.
static QString mountPointOf(QString path)
{
    while (path != QLatin1String("/")) {
        struct statx stx;
        if (::statx(AT_FDCWD, native(path), AT_SYMLINK_NOFOLLOW | AT_NO_AUTOMOUNT, 0, &stx) == 0
            && (stx.stx_attributes_mask & STATX_ATTR_MOUNT_ROOT)) {
            if (stx.stx_attributes & STATX_ATTR_MOUNT_ROOT)
                return path;
        } else {
            // Kernels before 5.8 do not say; the device changes at a mount.
            struct stat st, parentSt;
            if (::stat(native(path), &st) == 0
                && ::stat(native(QFileInfo(path).absolutePath()), &parentSt) == 0
                && st.st_dev != parentSt.st_dev)
                return path;
        }

        path = QFileInfo(path).absolutePath();
    }

    return path;
}

// Files on the home trash's filesystem go there. Others go to their own volume's
// trash, so trashing frees no space but costs no copy; only a volume that cannot
// take a trash (remote, read-only, no permission) sends them home.
static TrashDirectory trashFor(const QString &source)
{
    const QString parent = QFileInfo(QFileInfo(source).absolutePath()).canonicalFilePath();
    const dev_t home = homeTrashDevice();

    struct stat st;
    if (!parent.isEmpty() && ::stat(native(parent), &st) == 0 && st.st_dev != home) {
        const QString topdir = mountPointOf(parent);

        if (volumeTakesTrash(topdir) && ::stat(native(topdir), &st) == 0
            && st.st_dev != home) {
            for (const bool shared : {true, false}) {
                const auto trash = volumeTrash(topdir, shared);
                if (prepareVolumeTrash(trash))
                    return trash;
            }
        }
    }

    ensureHomeTrash();
    return homeTrash();
}

static bool isInside(const QString &path, const QString &directory)
{
    const QString clean = QDir::cleanPath(path);
    return clean == directory || clean.startsWith(directory + '/');
}

namespace {

class TrashLock
{
public:
    TrashLock(const TrashDirectory &trash, OperationContext &ctx)
        : m_fd(::open(native(trash.path + "/.lock"),
                      O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600))
    {
        check(m_fd.value, trash.path);

        while (::flock(m_fd.value, LOCK_EX | LOCK_NB) < 0) {
            if (errno != EWOULDBLOCK && errno != EINTR)
                throw Failure(errno, trash.path);

            ctx.checkpoint();
            timespec delay {0, 25000000};
            ::nanosleep(&delay, nullptr);
        }

        ctx.checkpoint();
    }

private:
    Fd m_fd;
};

} // namespace

// The trash of an item at the top of one, and its name there.
static TrashDirectory trashedItem(const QUrl &url, QString *name)
{
    FileSystemUrl validated(url);
    const auto trash = trashDirectory(url, name);
    if (name->isEmpty() || name->contains('/'))
        throw Failure(EINVAL, url.toString());

    return trash;
}

static QString infoPath(const TrashDirectory &trash, const QString &name)
{
    return trash.path + "/info/" + name + ".trashinfo";
}

void LocalFileSystem::remove(const QUrl &url, OperationContext &ctx)
{
    const auto path = FileSystemUrl(url).localPath();

    if (url.isLocalFile()) {
        remove(path, ctx);
        return;
    }

    QString inside;
    const auto trash = trashDirectory(url, &inside);
    if (inside.isEmpty())
        throw Failure(EINVAL, url.toString());

    if (trash.topdir.isEmpty())
        ensureHomeTrash();

    TrashLock lock(trash, ctx);
    remove(path, ctx);

    if (!inside.contains('/')) {
        const auto info = infoPath(trash, inside);
        if (::unlink(native(info)) < 0 && errno != ENOENT)
            throw Failure(errno, info);
    }
}

QUrl LocalFileSystem::trash(const QUrl &url, OperationContext &ctx)
{
    if (!url.isLocalFile())
        throw Failure(EINVAL, url.toString());

    const auto source = FileSystemUrl(url).localPath();
    if (isInside(source, FileSystemUrl::trashRoot()))
        throw Failure(EINVAL, source);

    // Neither trash of a volume takes the other, or itself.
    const auto trash = trashFor(source);
    const QString canonical =
            QFileInfo(QFileInfo(source).absolutePath()).canonicalFilePath() + '/' + url.fileName();
    if (!trash.topdir.isEmpty()
        && (isInside(canonical, volumeTrash(trash.topdir, true).path)
            || isInside(canonical, volumeTrash(trash.topdir, false).path)))
        throw Failure(EINVAL, source);

    TrashLock lock(trash, ctx);

    QString name = url.fileName();
    QString info;
    std::unique_ptr<Fd> infoFile;

    for (int i = 0;; ++i) {
        QString base = url.fileName();
        while (QFile::encodeName(base).size() > 180)
            base.chop(1);

        name = base + (i ? '-' + QUuid::createUuid().toString(QUuid::Id128) : QString());
        QFileInfo payload(trash.path + "/files/" + name);
        if (payload.exists() || payload.isSymbolicLink())
            continue;

        info = infoPath(trash, name);
        infoFile = std::make_unique<Fd>(
                ::open(native(info), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        if (infoFile->value >= 0)
            break;

        if (errno != EEXIST)
            throw Failure(errno, info);
    }

    // A volume's trash records paths from its mount point, which can differ next time.
    const QString original = trash.topdir.isEmpty()
            ? QFileInfo(source).absoluteFilePath()
            : QDir(trash.topdir).relativeFilePath(canonical);

    const QByteArray content = "[Trash Info]\nPath=" + QUrl::toPercentEncoding(original, "/")
            + "\nDeletionDate="
            + QDateTime::currentDateTime().toString("yyyy-MM-ddTHH:mm:ss").toUtf8() + '\n';

    try {
        if (::write(infoFile->value, content.constData(), content.size()) != content.size())
            throw Failure(errno ? errno : EIO, info);

        check(::fsync(infoFile->value), info);
        move(source, trash.path + "/files/" + name, ConflictPolicy::Fail, ctx);
    } catch (...) {
        if (!QFileInfo::exists(trash.path + "/files/" + name))
            ::unlink(native(info));
        throw;
    }

    QUrl result("trash:/");
    result.setPath('/' + trash.id + '/' + name);
    return result;
}

QUrl LocalFileSystem::restore(const QUrl &url, ConflictPolicy policy, OperationContext &ctx)
{
    QString name;
    const auto trash = trashedItem(url, &name);
    if (trash.topdir.isEmpty())
        ensureHomeTrash();

    TrashLock lock(trash, ctx);

    const auto info = infoPath(trash, name);
    TrashInfo details;
    if (!readTrashInfo(trash, name, &details))
        throw Failure(EACCES, info);

    if (details.path.isEmpty())
        throw Failure(EINVAL, info);

    // A folder deleted since the item was trashed is made again.
    const QString parent = QFileInfo(details.path).absolutePath();
    if (!QDir().mkpath(parent))
        throw Failure(errno ? errno : EIO, parent);

    auto result = move(FileSystemUrl(url).localPath(), details.path, policy, ctx);
    if (result.isEmpty())
        return {};

    check(::unlink(native(info)), info);
    return QUrl::fromLocalFile(result);
}

// Empties one trash, each item before its info file, so a cancelled run leaves no
// item without one.
static void empty(const TrashDirectory &trash, OperationContext &ctx)
{
    if (trash.topdir.isEmpty())
        ensureHomeTrash();
    else if (!QFileInfo(trash.path).isDir())
        return;

    TrashLock lock(trash, ctx);

    const auto erase = [&](const QString &directory, auto visit) {
        Fd fd(::open(native(directory), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (fd.value < 0 && errno == ENOENT)
            return;

        check(fd.value, directory);
        children(fd.value, directory, [&](const QByteArray &name, const QString &path) {
            visit(fd.value, name, path);
        });
    };

    erase(trash.path + "/files", [&](int fd, const QByteArray &name, const QString &path) {
        eraseAt(fd, name, path, ctx);

        const auto info = infoPath(trash, QFile::decodeName(name));
        if (::unlink(native(info)) < 0 && errno != ENOENT)
            throw Failure(errno, info);
    });

    erase(trash.path + "/info", [&](int fd, const QByteArray &name, const QString &path) {
        if (name.endsWith(".trashinfo"))
            eraseAt(fd, name, path, ctx);
    });
}

// A volume that fails to empty, being read-only say, does not keep the others full.
void LocalFileSystem::emptyTrash(OperationContext &ctx)
{
    std::optional<Failure> failure;

    for (const auto &trash : trashDirectories()) {
        try {
            empty(trash, ctx);
        } catch (const Failure &error) {
            if (error.error.code == ECANCELED)
                throw;

            if (!failure)
                failure = error;
        }
    }

    if (failure)
        throw *failure;
}

} // namespace Cutefish::IO
