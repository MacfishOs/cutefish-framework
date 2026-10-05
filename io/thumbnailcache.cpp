// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnailcache.h"
#include "thumbnailers_p.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

namespace {

struct Flavor
{
    int size;
    const char *name;
};

} // namespace

// Sizes and names follow the freedesktop.org Thumbnail Managing Standard.
static constexpr Flavor flavors[] = {
    {128, "normal"},
    {256, "large"},
    {512, "x-large"},
    {1024, "xx-large"},
};

static const QString uriKey = QStringLiteral("Thumb::URI");
static const QString mtimeKey = QStringLiteral("Thumb::MTime");
static const QString sizeKey = QStringLiteral("Thumb::Size");

static const Flavor &flavorFor(const QSize &requestedSize)
{
    const int extent = qMax(requestedSize.width(), requestedSize.height());
    for (const Flavor &flavor : flavors) {
        if (extent <= flavor.size)
            return flavor;
    }

    return flavors[std::size(flavors) - 1];
}

static bool ensurePrivateDirectory(const QString &path)
{
    if (QFileInfo(path).isDir())
        return true;

    // Thumbnails leak the content of private files, so the spec requires owner-only access.
    return QDir().mkpath(path)
            && QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                   | QFileDevice::ExeOwner);
}

// QImageReader::text() splits keys at the first colon, so only a decoded image sees Thumb::*.
static bool isCurrent(const QImage &image, const QString &uri, qint64 mtime, qint64 size)
{
    if (image.isNull() || image.text(uriKey) != uri)
        return false;

    // Other implementations may write a fractional MTime.
    bool valid = false;
    const double cachedMtime = image.text(mtimeKey).toDouble(&valid);
    if (!valid || qint64(cachedMtime) != mtime)
        return false;

    const QString cachedSize = image.text(sizeKey);
    return cachedSize.isEmpty() || cachedSize.toLongLong() == size;
}

static bool isReadableMimeType(const QString &mimeType)
{
    static const QList<QByteArray> readable = QImageReader::supportedMimeTypes();
    return readable.contains(mimeType.toLatin1());
}

static void setMetadata(QImage &image, const QString &uri, qint64 mtime, qint64 size)
{
    image.setText(uriKey, uri);
    image.setText(mtimeKey, QString::number(mtime));
    image.setText(sizeKey, QString::number(size));
}

namespace {

// Where the thumbnail of one file at one size lives in the cache.
struct Entry
{
    bool valid = false;
    bool insideCache = false;
    QString path;
    QSize cacheSize;
    QString uri;
    qint64 mtime = 0;
    qint64 size = 0;
    QString root;
    QString directory;
    QString target;
    QString failDirectory;
    QString failTarget;
};

} // namespace

static Entry locate(const QString &filePath, const QSize &requestedSize)
{
    Entry entry;
    if (!requestedSize.isValid() || requestedSize.isEmpty())
        return entry;

    const QUrl url(filePath);
    entry.path = url.isLocalFile() ? url.toLocalFile() : filePath;
    const QFileInfo info(entry.path);
    if (!info.isFile())
        return entry;

    const Flavor &flavor = flavorFor(requestedSize);
    entry.valid = true;
    entry.cacheSize = QSize(flavor.size, flavor.size);
    entry.root = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
            + QStringLiteral("/thumbnails");
    entry.insideCache = info.absoluteFilePath().startsWith(entry.root + '/');

    const QByteArray encodedUri = QUrl::fromLocalFile(info.absoluteFilePath()).toEncoded();
    const QString name =
            QString::fromLatin1(QCryptographicHash::hash(encodedUri, QCryptographicHash::Md5).toHex())
            + QStringLiteral(".png");
    entry.uri = QString::fromLatin1(encodedUri);
    entry.mtime = info.lastModified().toSecsSinceEpoch();
    entry.size = info.size();
    entry.directory = entry.root + '/' + QLatin1StringView(flavor.name);
    entry.target = entry.directory + '/' + name;
    entry.failDirectory = entry.root + QStringLiteral("/fail/cutefish");
    entry.failTarget = entry.failDirectory + '/' + name;
    return entry;
}

ThumbnailCache *ThumbnailCache::self()
{
    static ThumbnailCache cache;
    return &cache;
}

bool ThumbnailCache::canThumbnail(const QString &mimeType)
{
    return isReadableMimeType(mimeType) || Thumbnailers::self()->contains(mimeType);
}

Thumbnail ThumbnailCache::requestThumbnail(const QString &filePath, const QSize &requestedSize)
{
    const Entry entry = locate(filePath, requestedSize);
    if (!entry.valid)
        return {};

    // The spec forbids thumbnailing the cache itself, which would otherwise feed on its output.
    if (entry.insideCache)
        return {{}, generateThumbnail(entry.path, entry.cacheSize)};

    const QImage cached(entry.target);
    if (isCurrent(cached, entry.uri, entry.mtime, entry.size))
        return {entry.target, cached};

    if (isCurrent(QImage(entry.failTarget), entry.uri, entry.mtime, entry.size))
        return {};

    QImage image = generateThumbnail(entry.path, entry.cacheSize);
    if (image.isNull()) {
        // Remember the failure so broken images are not decoded again on every scroll.
        QImage marker(1, 1, QImage::Format_ARGB32);
        marker.fill(Qt::transparent);
        setMetadata(marker, entry.uri, entry.mtime, entry.size);

        if (ensurePrivateDirectory(entry.root) && ensurePrivateDirectory(entry.failDirectory))
            writeCacheFile(entry.failTarget, marker);

        return {};
    }

    setMetadata(image, entry.uri, entry.mtime, entry.size);

    // An unwritable cache must not hide the thumbnail; only the path goes missing.
    if (!ensurePrivateDirectory(entry.root) || !ensurePrivateDirectory(entry.directory))
        return {{}, image};

    return {writeCacheFile(entry.target, image), image};
}

bool ThumbnailCache::hasCachedThumbnail(const QString &filePath, const QSize &requestedSize)
{
    const Entry entry = locate(filePath, requestedSize);
    if (!entry.valid || entry.insideCache)
        return false;

    // Without decoding it, an entry older than the file is the likely stale one.
    const QFileInfo cached(entry.target);
    return cached.isFile() && cached.lastModified().toSecsSinceEpoch() >= entry.mtime;
}

Thumbnail ThumbnailCache::cachedThumbnail(const QString &filePath, const QSize &requestedSize)
{
    const Entry entry = locate(filePath, requestedSize);
    if (!entry.valid || entry.insideCache)
        return {};

    const QImage cached(entry.target);
    if (!isCurrent(cached, entry.uri, entry.mtime, entry.size))
        return {};

    return {entry.target, cached};
}

QImage ThumbnailCache::generateThumbnail(const QString &source, const QSize &requestedSize)
{
    if (requestedSize.isEmpty())
        return {};

    QImageReader reader(source);
    const QSize originalSize = reader.size();
    if (!originalSize.isValid()) {
        const QString mimeType = QMimeDatabase().mimeTypeForFile(source).name();
        return Thumbnailers::self()->generate(source, mimeType, requestedSize);
    }

    reader.setQuality(90);
    reader.setAutoTransform(true);
    reader.setScaledSize(originalSize.scaled(requestedSize, Qt::KeepAspectRatio));
    QImage image = reader.read();
    if (image.isNull())
        return {};

    if (image.width() > requestedSize.width() || image.height() > requestedSize.height())
        image = image.scaled(requestedSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);

    return image;
}

QString ThumbnailCache::writeCacheFile(const QString &path, const QImage &image)
{
    if (image.isNull())
        return {};

    // Concurrent requests and separate processes must never see a partial cache entry.
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)
        || !file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner)
        || !image.save(&file, "PNG") || !file.commit())
        return {};

    return path;
}
