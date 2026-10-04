// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QElapsedTimer>
#include <QMimeType>
#include <QMutex>
#include <QString>

#include <memory>

namespace Cutefish::IO::Internal {

/**
 * QMimeDatabase::mimeTypeForFileNameAndData() for a file on disk, matched
 * outside QMimeDatabase's process-wide lock so listings can detect types on
 * several threads. Qt also compiles a regular expression per file and complex
 * glob, and compares magic values at every offset of their ranges.
 *
 * The globs and magic rules come from the same mime.cache files, in the same
 * order and with the same results as Qt 6's binary provider, including the
 * globs a local directory deletes. Where Qt would load an XML database, Qt
 * matches.
 */
class MimeSniffer
{
public:
    static MimeSniffer *self();

    QMimeType mimeTypeForFile(const QString &path);

private:
    struct Database;

    std::shared_ptr<const Database> database();

    QMutex m_mutex;
    QElapsedTimer m_lastCheck;
    std::shared_ptr<const Database> m_database;
};

} // namespace Cutefish::IO::Internal
