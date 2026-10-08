#include "filesystemurl.h"

#include "error.h"
#include "trashdirectory_p.h"

#include <QFileInfo>
#include <QStandardPaths>
#include <cerrno>

namespace Cutefish::IO {

FileSystemUrl::FileSystemUrl(QUrl url)
    : m_url(std::move(url))
{
    if (!m_url.isValid() || m_url.hasQuery() || m_url.hasFragment() || !m_url.userInfo().isEmpty()
        || m_url.port() != -1 || (!m_url.host().isEmpty() && m_url.host() != "localhost")
        || (!m_url.isLocalFile() && m_url.scheme() != "trash") || !m_url.path().startsWith('/')
        || m_url.path().contains(QChar(0)))
        throw Failure(ENOTSUP, m_url.toString());

    if (m_url.scheme() == "trash") {
        for (const auto &part : m_url.path().split('/'))
            if (part == ".." || part == ".")
                throw Failure(EINVAL, m_url.toString());

        if (m_url.path() != QLatin1String("/"))
            Internal::trashDirectory(m_url);
    }
}

QString FileSystemUrl::trashRoot()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + "/Trash";
}

QString FileSystemUrl::localPath() const
{
    if (m_url.isLocalFile())
        return m_url.toLocalFile();

    if (m_url.path() == QLatin1String("/"))
        return trashRoot() + "/files";

    QString inside;
    const auto trash = Internal::trashDirectory(m_url, &inside);
    return trash.path + "/files" + (inside.isEmpty() ? QString() : '/' + inside);
}

QUrl upUrl(const QUrl &url)
{
    auto result = url.adjusted(QUrl::StripTrailingSlash);
    result.setPath(QFileInfo(result.path()).path());
    return result;
}

} // namespace Cutefish::IO
