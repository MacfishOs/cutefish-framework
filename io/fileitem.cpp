#include "fileitem.h"
#include "error.h"
#include "filesystemurl.h"
#include <QDir>
#include <QLocale>
#include <QMimeDatabase>
#include <QStandardPaths>

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

QMimeType FileItem::determineMimeType() const
{
    return QMimeDatabase().mimeTypeForFile(m_info);
}

QString FileItem::iconName() const
{
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
    static const QList<QPair<QStandardPaths::StandardLocation, QString>> folders = {
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
    const QString cleaned = QDir::cleanPath(path);
    // Home first: disabled XDG directories resolve to the home directory itself.
    for (const auto &[location, icon] : folders) {
        const QString standard = QStandardPaths::writableLocation(location);
        if (!standard.isEmpty() && QDir::cleanPath(standard) == cleaned)
            return icon;
    }
    return {};
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
