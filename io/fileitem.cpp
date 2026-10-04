#include "fileitem.h"

#include "error.h"
#include "filesystemurl.h"

#include <QDir>
#include <QFile>
#include <QHash>
#include <QLocale>
#include <QMimeDatabase>
#include <QStandardPaths>
#include <QTimeZone>

namespace Cutefish::IO {

FileItem::FileItem(const QUrl &url)
{
    try {
        m_info = QFileInfo(FileSystemUrl(url).localPath());
        m_info.refresh();
        m_url = url;
    } catch (const Failure &) {
    }
}

QDateTime FileItem::time(FileTime type) const
{
    return type == CreationTime  ? m_info.birthTime()
            : type == AccessTime ? m_info.lastRead()
                                 : m_info.lastModified();
}

QDateTime FileItem::time(FileTime type, const QTimeZone &zone) const
{
    return m_info.fileTime(type == CreationTime  ? QFile::FileBirthTime
                                   : type == AccessTime ? QFile::FileAccessTime
                                                        : QFile::FileModificationTime,
                           zone);
}

QMimeType FileItem::determineMimeType() const
{
    if (isNull())
        return {};

    return m_mimeType.isValid() ? m_mimeType : QMimeDatabase().mimeTypeForFile(m_info);
}

void FileItem::prepareMetadata()
{
    prepareMetadata(FileItem(), nullptr);
}

bool FileItem::prepareMetadata(const FileItem &previous, TypeDetails *details)
{
    if (isNull())
        return false;

    // What QFileInfo::stat() reads, less the read and execute access(2) checks no
    // accessor uses.
    m_info.refresh();
    const bool link = m_info.isSymLink();
    m_info.size();
    m_info.isHidden();
    m_info.isWritable();

    m_stamp = {};
    if (m_info.isDir() || m_info.isFile()) {
        const QTimeZone utc(QTimeZone::UTC);
        m_stamp.size = m_info.size();
        m_stamp.modified = m_info.fileTime(QFile::FileModificationTime, utc).toMSecsSinceEpoch();
        m_stamp.changed = m_info.fileTime(QFile::FileMetadataChangeTime, utc).toMSecsSinceEpoch();
        m_stamp.kind = (m_info.isDir() ? 1 : 2) | (link ? 4 : 0);
    }

    // Content sniffing reads the file; the same entry with the same size and times
    // has the same contents.
    if (m_stamp.kind && m_stamp == previous.m_stamp && m_url == previous.m_url) {
        m_mimeType = previous.m_mimeType;
        m_typeDescription = previous.m_typeDescription;
        m_iconName = previous.m_iconName;
        m_displayName = previous.m_displayName;
        m_targetUrl = previous.m_targetUrl;
        return true;
    }

    detectType(details);
    return false;
}

void FileItem::detectType(TypeDetails *details)
{
    const QMimeDatabase database;

    if (m_info.isDir()) {
        m_mimeType = database.mimeTypeForName(QStringLiteral("inode/directory"));
    } else if (m_info.isFile()) {
        // mimeTypeForFile() would stat the file again to rule out devices.
        QFile file(m_info.filePath());
        m_mimeType = database.mimeTypeForFileNameAndData(m_info.filePath(), &file);
    } else {
        m_mimeType = database.mimeTypeForFile(m_info);
    }

    // A type's description and icon cost a database lookup each, and the
    // description a walk over the UI languages.
    QString typeIcon;
    const auto known = details ? details->constFind(m_mimeType.name()) : TypeDetails::const_iterator();

    if (details && known != details->cend()) {
        m_typeDescription = known->first;
        typeIcon = known->second;
    } else {
        m_typeDescription = m_mimeType.comment();
        typeIcon = m_mimeType.iconName();

        if (details)
            details->insert(m_mimeType.name(), {m_typeDescription, typeIcon});
    }

    if (m_iconName.isEmpty())
        m_iconName = isDir() ? iconName() : typeIcon;
}

QString FileItem::iconName() const
{
    if (!m_iconName.isEmpty())
        return m_iconName;

    if (!isDir())
        return determineMimeType().iconName();

    const QString standard = standardFolderIconName(localPath());
    return standard.isEmpty() ? QStringLiteral("folder") : standard;
}

bool FileItemListProperties::supportsMoving() const
{
    if (m_items.isEmpty())
        return false;

    for (const auto &item : m_items)
        if (item.isNull() || !QFileInfo(QFileInfo(item.localPath()).absolutePath()).isWritable())
            return false;

    return true;
}

QString convertSize(quint64 size)
{
    return QLocale().formattedDataSize(size);
}

QString standardFolderIconName(const QString &path)
{
    if (path.isEmpty())
        return {};

    // QStandardPaths reads user-dirs.dirs again on every call. The session writes
    // that file before it starts the desktop, so once per process is enough.
    static const QHash<QString, QString> icons = [] {
        const QList<QPair<QStandardPaths::StandardLocation, QString>> folders = {
                {QStandardPaths::HomeLocation, QStringLiteral("folder-home")},
                {QStandardPaths::DesktopLocation, QStringLiteral("folder-desktop")},
                {QStandardPaths::DocumentsLocation, QStringLiteral("folder-documents")},
                {QStandardPaths::DownloadLocation, QStringLiteral("folder-download")},
                {QStandardPaths::MusicLocation, QStringLiteral("folder-music")},
                {QStandardPaths::PicturesLocation, QStringLiteral("folder-picture")},
                {QStandardPaths::MoviesLocation, QStringLiteral("folder-video")},
                {QStandardPaths::TemplatesLocation, QStringLiteral("folder-templates")},
                {QStandardPaths::PublicShareLocation, QStringLiteral("folder-publicshare")},
        };

        QHash<QString, QString> result;
        for (const auto &[location, icon] : folders) {
            const QString standard = QStandardPaths::writableLocation(location);
            // Home first: disabled XDG directories resolve to the home directory itself.
            if (!standard.isEmpty() && !result.contains(QDir::cleanPath(standard)))
                result.insert(QDir::cleanPath(standard), icon);
        }
        return result;
    }();

    return icons.value(QDir::cleanPath(path));
}

bool isStandardFolder(const QUrl &url)
{
    return url.isLocalFile() && !standardFolderIconName(url.toLocalFile()).isEmpty();
}

bool canMoveStandardFolderTo(const QUrl &source, const QUrl &destination)
{
    if (!isStandardFolder(source))
        return true;

    const QString home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (home.isEmpty() || !source.isLocalFile() || !destination.isLocalFile())
        return false;

    const QString cleanedHome = QDir::cleanPath(home);
    return QDir::cleanPath(source.toLocalFile()) != cleanedHome
            && QDir::cleanPath(destination.toLocalFile()) == cleanedHome;
}

} // namespace Cutefish::IO
