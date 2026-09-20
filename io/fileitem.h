#pragma once
#include <QDateTime>
#include <QFileInfo>
#include <QMimeType>
#include <QUrl>

namespace Cutefish::IO {
class FileItem
{
public:
    enum FileTime {
        CreationTime,
        ModificationTime,
        AccessTime
    };

    FileItem() = default;
    explicit FileItem(const QUrl &url);

    QUrl url() const { return m_url; }

    QUrl mostLocalUrl() const { return QUrl::fromLocalFile(localPath()); }

    QString localPath() const { return m_info.filePath(); }

    QString name() const { return m_url.fileName(); }

    QString text() const { return m_displayName.isEmpty() ? name() : m_displayName; }
    void setDisplayName(const QString &name) { m_displayName = name; }
    void setIconName(const QString &icon) { m_iconName = icon; }
    QUrl targetUrl() const { return m_targetUrl; }
    void setTargetUrl(const QUrl &url) { m_targetUrl = url; }

    bool isFile() const { return m_info.isFile(); }

    bool isNull() const { return m_url.isEmpty(); }

    bool isLocalFile() const { return m_url.isLocalFile(); }

    bool isDir() const { return m_info.isDir(); }

    bool isLink() const { return m_info.isSymbolicLink(); }

    bool isHidden() const { return m_info.isHidden(); }

    bool isWritable() const { return m_info.isWritable(); }

    bool isDesktopFile() const { return name().endsWith(".desktop"); }

    quint64 size() const { return m_info.size(); }

    QDateTime time(FileTime type) const;
    QMimeType determineMimeType() const;
    void prepareMetadata();

    QString mimetype() const { return determineMimeType().name(); }
    QString typeDescription() const { return m_typeDescription; }

    QMimeType currentMimeType() const { return determineMimeType(); }

    QString iconName() const;

private:
    QUrl m_url;
    QFileInfo m_info;
    QMimeType m_mimeType;
    QString m_displayName;
    QString m_iconName;
    QString m_typeDescription;
    QUrl m_targetUrl;
};

using FileItemList = QList<FileItem>;

class FileItemListProperties
{
public:
    explicit FileItemListProperties(const FileItemList &items)
        : m_items(items)
    {
    }

    bool supportsMoving() const;

    bool supportsDeleting() const { return supportsMoving(); }

private:
    FileItemList m_items;
};

QString convertSize(quint64 size);

// Empty unless path is the home directory or an XDG user directory.
QString standardFolderIconName(const QString &path);
bool isStandardFolder(const QUrl &url);
bool canMoveStandardFolderTo(const QUrl &source, const QUrl &destination);
} // namespace Cutefish::IO
Q_DECLARE_METATYPE(Cutefish::IO::FileItem)
