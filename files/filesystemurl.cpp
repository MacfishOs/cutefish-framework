// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files/filesystemurl.h"

#include <cerrno>

#include "base/strings.h"
#include "files/error.h"
#include "trashdirectory_p.h"

namespace cutefish::files {

using namespace cutefish::base;

FileSystemUrl::FileSystemUrl(Url url)
    : m_url(std::move(url))
{
    if (!m_url.isValid() || m_url.hasQuery() || m_url.hasFragment() || !m_url.userInfo().empty()
        || m_url.port() != -1 || (!m_url.host().empty() && m_url.host() != "localhost")
        || (!m_url.isLocalFile() && m_url.scheme() != "trash") || !m_url.path().starts_with('/')
        || m_url.path().find('\0') != std::string::npos)
        throw Failure(ENOTSUP, m_url.toString());

    if (m_url.scheme() == "trash") {
        for (const std::string_view part : strings::splitViews(m_url.path(), '/')) {
            if (part == ".." || part == ".")
                throw Failure(EINVAL, m_url.toString());
        }

        if (m_url.path() != "/")
            Internal::trashDirectory(m_url);
    }
}

std::string FileSystemUrl::trashRoot()
{
    return Internal::homeTrashRoot();
}

std::string FileSystemUrl::localPath() const
{
    if (m_url.isLocalFile())
        return m_url.path();

    if (m_url.path() == "/")
        return trashRoot() + "/files";

    std::string inside;
    const auto trash = Internal::trashDirectory(m_url, &inside);
    return trash.path + "/files" + (inside.empty() ? std::string() : '/' + inside);
}

} // namespace cutefish::files
