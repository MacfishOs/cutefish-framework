#include "fileenumerator.h"
#include "filesystemurl.h"
#include "posix_p.h"
#include <sys/stat.h>

namespace Cutefish::IO {
using namespace Internal;

namespace {
quint64 sizeAt(int parent, const QByteArray &name, const QString &path, OperationContext &ctx)
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
        ctx.advance(size);
    }
    ++ctx.files;
    return size;
}

} // namespace

FileItemList FileEnumerator::list(const QUrl &url, bool hidden, OperationContext *context)
{
    if (context)
        context->checkpoint();
    const auto path = FileSystemUrl(url).localPath();
    Fd fd(::open(native(path), O_RDONLY | O_DIRECTORY | O_CLOEXEC));
    if (fd.value < 0 && errno == ENOENT && url.scheme() == "trash" && url.path() == "/")
        return {};
    check(fd.value, path);
    FileItemList result;
    children(fd.value, path, [&](const QByteArray &name, const QString &) {
        if (context)
            context->checkpoint(path);
        if (!hidden && name.startsWith('.'))
            return;
        auto child = url;
        child.setPath(url.path() + (url.path().endsWith('/') ? "" : "/") + QFile::decodeName(name));
        result.append(FileItem(child));
    });
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
