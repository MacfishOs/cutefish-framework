#include "fileitem.h"

#include "error.h"
#include "filesystemurl.h"

#include <QDir>
#include <QHash>
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
    if (isNull())
        return {};

    return m_mimeType.isValid() ? m_mimeType : QMimeDatabase().mimeTypeForFile(m_info);
}

void FileItem::prepareMetadata()
{
    if (isNull())
        return;

    m_info.stat();
    m_mimeType = QMimeDatabase().mimeTypeForFile(m_info);
    m_typeDescription = m_mimeType.comment();
    m_iconName = iconName();
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
