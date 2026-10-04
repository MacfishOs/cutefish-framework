#pragma once

#include "operationcontext.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <cerrno>
#include <dirent.h>
#include <fcntl.h>
#include <memory>
#include <type_traits>
#include <unistd.h>

namespace Cutefish::IO::Internal {

struct Fd
{
    int value;

    explicit Fd(int fd)
        : value(fd)
    {
    }

    ~Fd()
    {
        if (value >= 0)
            ::close(value);
    }

    Fd(const Fd &) = delete;
    Fd &operator=(const Fd &) = delete;
};

inline void check(int result, const QString &path)
{
    if (result < 0)
        throw Failure(errno, path);
}

inline QByteArray native(const QString &path)
{
    auto bytes = QFile::encodeName(path);
    if (bytes.isEmpty() || bytes.contains('\0'))
        throw Failure(EINVAL, path);

    return bytes;
}

struct Parent
{
    Fd fd;
    QByteArray name;

    explicit Parent(const QString &path)
        : fd(::open(native(QFileInfo(path).absolutePath()), O_RDONLY | O_DIRECTORY | O_CLOEXEC))
        , name(native(QFileInfo(path).fileName()))
    {
        check(fd.value, path);

        if (name == "." || name == ".." || QDir::cleanPath(path) == "/")
            throw Failure(EINVAL, path);
    }
};

template <class Visitor> void children(int fd, const QString &path, Visitor visitor)
{
    int duplicate = ::openat(fd, ".", O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    check(duplicate, path);

    DIR *raw = ::fdopendir(duplicate);
    if (!raw) {
        ::close(duplicate);
        throw Failure(errno, path);
    }

    auto closeDir = [](DIR *value) {
        ::closedir(value);
    };
    std::unique_ptr<DIR, decltype(closeDir)> dir(raw, closeDir);

    while (true) {
        errno = 0;
        auto *entry = ::readdir(dir.get());
        if (!entry) {
            check(errno ? -1 : 0, path);
            break;
        }

        QByteArray name(entry->d_name);
        if (name == "." || name == "..")
            continue;

        if constexpr (std::is_invocable_v<Visitor, const QByteArray &>)
            visitor(name);
        else
            visitor(name, path + '/' + QFile::decodeName(name));
    }
}

void eraseAt(int parent, const QByteArray &name, const QString &path, OperationContext &ctx,
             bool cleanup = false);

} // namespace Cutefish::IO::Internal
