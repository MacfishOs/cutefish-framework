#include "localfilesystem.h"
#include "posix_p.h"

#include <QDateTime>
#include <QUuid>

#include <sys/file.h>
#include <sys/stat.h>

namespace Cutefish::IO {

using namespace Internal;

namespace {

void ensureTrash()
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

class TrashLock
{
public:
    explicit TrashLock(OperationContext &ctx)
        : m_fd(::open(native(FileSystemUrl::trashRoot() + "/.lock"),
                      O_CREAT | O_RDWR | O_NOFOLLOW | O_CLOEXEC, 0600))
    {
        check(m_fd.value, FileSystemUrl::trashRoot());

        while (::flock(m_fd.value, LOCK_EX | LOCK_NB) < 0) {
            if (errno != EWOULDBLOCK && errno != EINTR)
                throw Failure(errno, FileSystemUrl::trashRoot());

            ctx.checkpoint();
            timespec delay {0, 25000000};
            ::nanosleep(&delay, nullptr);
        }

        ctx.checkpoint();
    }

private:
    Fd m_fd;
};

QString trashInfo(const QUrl &url)
{
    if (url.scheme() != "trash" || url.path().mid(1).contains('/') || url.fileName().isEmpty())
        throw Failure(EINVAL, url.toString());

    FileSystemUrl validated(url);
    return FileSystemUrl::trashRoot() + "/info/" + url.fileName() + ".trashinfo";
}

} // namespace

void LocalFileSystem::remove(const QUrl &url, OperationContext &ctx)
{
    const auto path = FileSystemUrl(url).localPath();

    if (url.isLocalFile()) {
        remove(path, ctx);
        return;
    }

    if (url.path() == "/")
        throw Failure(EINVAL, url.toString());

    ensureTrash();
    TrashLock lock(ctx);
    remove(path, ctx);

    if (!url.path().mid(1).contains('/')) {
        const auto info = trashInfo(url);
        if (::unlink(native(info)) < 0 && errno != ENOENT)
            throw Failure(errno, info);
    }
}

QUrl LocalFileSystem::trash(const QUrl &url, OperationContext &ctx)
{
    if (!url.isLocalFile())
        throw Failure(EINVAL, url.toString());

    const auto source = FileSystemUrl(url).localPath();
    if (QDir::cleanPath(source) == FileSystemUrl::trashRoot()
        || QDir::cleanPath(source).startsWith(FileSystemUrl::trashRoot() + '/'))
        throw Failure(EINVAL, source);

    ensureTrash();
    TrashLock lock(ctx);

    QString name = url.fileName();
    QString infoPath;
    std::unique_ptr<Fd> info;

    for (int i = 0;; ++i) {
        QString base = url.fileName();
        while (QFile::encodeName(base).size() > 180)
            base.chop(1);

        name = base + (i ? '-' + QUuid::createUuid().toString(QUuid::Id128) : QString());
        QFileInfo payload(FileSystemUrl::trashRoot() + "/files/" + name);
        if (payload.exists() || payload.isSymbolicLink())
            continue;

        infoPath = FileSystemUrl::trashRoot() + "/info/" + name + ".trashinfo";
        info = std::make_unique<Fd>(::open(
                native(infoPath), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        if (info->value >= 0)
            break;

        if (errno != EEXIST)
            throw Failure(errno, infoPath);
    }

    const QByteArray content = "[Trash Info]\nPath="
            + QUrl::toPercentEncoding(QFileInfo(source).absoluteFilePath(), "/") + "\nDeletionDate="
            + QDateTime::currentDateTime().toString("yyyy-MM-ddTHH:mm:ss").toUtf8() + '\n';

    try {
        if (::write(info->value, content.constData(), content.size()) != content.size())
            throw Failure(errno ? errno : EIO, infoPath);

        check(::fsync(info->value), infoPath);
        move(source, FileSystemUrl::trashRoot() + "/files/" + name, ConflictPolicy::Fail, ctx);
    } catch (...) {
        if (!QFileInfo::exists(FileSystemUrl::trashRoot() + "/files/" + name))
            ::unlink(native(infoPath));
        throw;
    }

    QUrl result("trash:/");
    result.setPath('/' + name);
    return result;
}

QUrl LocalFileSystem::restore(const QUrl &url, ConflictPolicy policy, OperationContext &ctx)
{
    const auto infoPath = trashInfo(url);
    ensureTrash();
    TrashLock lock(ctx);

    QFile info(infoPath);
    if (!info.open(QIODevice::ReadOnly))
        throw Failure(EACCES, infoPath);

    QString destination;
    bool group = false;

    while (!info.atEnd()) {
        auto line = info.readLine(1024 * 1024).trimmed();
        if (line.startsWith('['))
            group = line == "[Trash Info]";
        if (group && line.startsWith("Path="))
            destination = QUrl::fromPercentEncoding(line.mid(5));
    }

    if (!QDir::isAbsolutePath(destination) || destination.contains(QChar(0))
        || QDir::cleanPath(destination) == "/")
        throw Failure(EINVAL, infoPath);

    auto result = move(FileSystemUrl(url).localPath(), destination, policy, ctx);
    if (result.isEmpty())
        return {};

    check(::unlink(native(infoPath)), infoPath);
    return QUrl::fromLocalFile(result);
}

void LocalFileSystem::emptyTrash(OperationContext &ctx)
{
    ensureTrash();
    TrashLock lock(ctx);

    for (const auto &item : enumerate(QUrl("trash:/"))) {
        remove(item.localPath(), ctx);

        const auto info = trashInfo(item.url());
        if (::unlink(native(info)) < 0 && errno != ENOENT)
            throw Failure(errno, info);
    }

    const QString infoDirectory = FileSystemUrl::trashRoot() + "/info";
    Fd info(::open(native(infoDirectory), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
    check(info.value, infoDirectory);

    children(info.value, infoDirectory, [&](const QByteArray &name, const QString &path) {
        if (name.endsWith(".trashinfo"))
            eraseAt(info.value, name, path, ctx);
    });
}

} // namespace Cutefish::IO
