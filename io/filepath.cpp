#include "filepath.h"

#include "error.h"

#include <cerrno>

namespace Cutefish::IO {

FilePath::FilePath(QByteArray bytes)
    : m_bytes(std::move(bytes))
{
    if (m_bytes.isEmpty() || m_bytes.contains('\0'))
        throw Failure(EINVAL, {});
}

FilePath FilePath::parent() const
{
    auto path = m_bytes;
    while (path.size() > 1 && path.endsWith('/'))
        path.chop(1);

    auto slash = path.lastIndexOf('/');
    return FilePath(slash < 0 ? QByteArray(".") : slash == 0 ? QByteArray("/") : path.left(slash));
}

FilePath FilePath::child(const QByteArray &name) const
{
    if (name.isEmpty() || name == "." || name == ".." || name.contains('/') || name.contains('\0'))
        throw Failure(EINVAL, {});

    return FilePath(m_bytes + (m_bytes.endsWith('/') ? "" : "/") + name);
}

} // namespace Cutefish::IO
