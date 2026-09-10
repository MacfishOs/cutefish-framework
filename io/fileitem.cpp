#include "fileitem.h"
#include "error.h"
#include "filesystemurl.h"
#include <QLocale>
#include <QMimeDatabase>

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
    return isDir() ? QStringLiteral("folder") : determineMimeType().iconName();
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

} // namespace Cutefish::IO
