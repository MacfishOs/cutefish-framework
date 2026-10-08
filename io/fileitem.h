#pragma once

#include <QDateTime>
#include <QFileInfo>
#include <QHash>
#include <QMimeType>
#include <QUrl>

#include <utility>

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

    QUrl url() const
    {
        return m_url;
    }

    QUrl mostLocalUrl() const
    {
        return QUrl::fromLocalFile(localPath());
    }

    QString localPath() const
    {
        return m_info.filePath();
    }

    QString name() const
    {
        return m_name;
    }

    QString text() const
    {
        return m_displayName.isEmpty() ? name() : m_displayName;
    }

    void setDisplayName(const QString &name)
    {
        m_displayName = name;
    }

    void setIconName(const QString &icon)
    {
        m_iconName = icon;
    }

    QUrl targetUrl() const
    {
        return m_targetUrl;
    }

    void setTargetUrl(const QUrl &url)
    {
        m_targetUrl = url;
    }

    bool isFile() const
    {
        return m_info.isFile();
    }

    bool isNull() const
    {
        return m_url.isEmpty();
    }

    bool isLocalFile() const
    {
        return m_url.isLocalFile();
    }

    bool isDir() const
    {
        return m_info.isDir();
    }

    bool isLink() const
    {
        return m_info.isSymbolicLink();
    }

    bool isHidden() const
    {
        return m_info.isHidden();
    }

    bool isWritable() const
    {
        return m_info.isWritable();
    }

    bool isDesktopFile() const
    {
        return name().endsWith(".desktop");
    }

    quint64 size() const
    {
        return m_info.size();
    }

    // For an item at the top of a trash, where it was trashed from and when, as its
    // info file says; empty otherwise.
    QString originalPath() const
    {
        return m_originalPath;
    }

    QDateTime deletionTime() const
    {
        return m_deletionTime;
    }

    QDateTime time(FileTime type) const;
    // The same instant in zone. UTC skips the local time conversion, which stats
    // the time zone file on every call.
    QDateTime time(FileTime type, const QTimeZone &zone) const;
    QMimeType determineMimeType() const;

    // Descriptions and icon names by MIME type, shared by the items of one listing.
    using TypeDetails = QHash<QString, std::pair<QString, QString>>;

    // Reads the metadata the accessors use and detects the type.
    void prepareMetadata();
    // The same, but when previous is this file, unchanged, its type, icon and names
    // are kept instead; returns whether they were.
    bool prepareMetadata(const FileItem &previous, TypeDetails *details);

    QString mimetype() const
    {
        return determineMimeType().name();
    }

    // Whether both were prepared from a file or directory of the same kind, size and
    // times, which answers for size(), isDir(), isLink() and the modification time.
    bool sameStat(const FileItem &other) const
    {
        return m_stamp.kind && m_stamp == other.m_stamp;
    }

    QString typeDescription() const
    {
        return m_typeDescription;
    }

    QMimeType currentMimeType() const
    {
        return determineMimeType();
    }

    QString iconName() const;

private:
    friend class FileEnumerator;

    // For a child the enumerator found in a directory it already resolved; name is
    // what url's fileName() decodes to.
    FileItem(const QUrl &url, const QString &localPath, const QString &name)
        : m_url(url)
        , m_name(name)
        , m_info(localPath)
    {
    }

    // What prepareMetadata() saw, so a later listing can compare without reading
    // m_info, which other threads may share.
    struct Stamp
    {
        qint64 size = -1;
        qint64 modified = 0;
        qint64 changed = 0;
        // Set only for a directory or regular file; links are followed.
        uint kind = 0;

        bool operator==(const Stamp &other) const
        {
            return size == other.size && modified == other.modified && changed == other.changed
                    && kind == other.kind;
        }
    };

    void detectType(TypeDetails *details);

    QUrl m_url;
    // The URL's file name, which sorting and refreshes ask for many times.
    QString m_name;
    QFileInfo m_info;
    Stamp m_stamp;
    QMimeType m_mimeType;
    QString m_displayName;
    QString m_iconName;
    QString m_typeDescription;
    QUrl m_targetUrl;
    QString m_originalPath;
    QDateTime m_deletionTime;
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

    bool supportsDeleting() const
    {
        return supportsMoving();
    }

private:
    FileItemList m_items;
};

QString convertSize(quint64 size);

// Empty unless path is the home directory or an XDG user directory, as they were
// on the first call.
QString standardFolderIconName(const QString &path);
bool isStandardFolder(const QUrl &url);
bool canMoveStandardFolderTo(const QUrl &source, const QUrl &destination);

} // namespace Cutefish::IO

Q_DECLARE_METATYPE(Cutefish::IO::FileItem)
