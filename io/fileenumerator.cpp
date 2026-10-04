#include "fileenumerator.h"

#include "async.h"
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
        if (S_ISREG(st.st_mode))
            ctx.allocated += std::min<quint64>(st.st_size, quint64(st.st_blocks) * 512);
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

quint64 FileEnumerator::size(const QString &path, OperationContext &ctx)
{
    if (path == "/")
        return sizeAt(AT_FDCWD, "/", path, ctx);

    Parent parent(path);
    return sizeAt(parent.fd.value, parent.name, path, ctx);
}

} // namespace Cutefish::IO
