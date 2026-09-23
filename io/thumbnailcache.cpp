#include "thumbnailcache.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUrl>

namespace {
struct Flavor
{
    int size;
    const char *name;
};

// Sizes and names follow the freedesktop.org Thumbnail Managing Standard.
constexpr Flavor flavors[] = {
    {128, "normal"},
    {256, "large"},
    {512, "x-large"},
    {1024, "xx-large"},
};

const QString uriKey = QStringLiteral("Thumb::URI");
const QString mtimeKey = QStringLiteral("Thumb::MTime");
const QString sizeKey = QStringLiteral("Thumb::Size");

const Flavor &flavorFor(const QSize &requestedSize)
{
    const int extent = qMax(requestedSize.width(), requestedSize.height());
    for (const Flavor &flavor : flavors) {
        if (extent <= flavor.size)
            return flavor;
    }
    return flavors[std::size(flavors) - 1];
}

bool ensurePrivateDirectory(const QString &path)
{
    if (QFileInfo(path).isDir())
        return true;

    // Thumbnails leak the content of private files, so the spec requires owner-only access.
    return QDir().mkpath(path)
            && QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner
                                                   | QFileDevice::ExeOwner);
}

// QImageReader::text() splits keys at the first colon, so only a decoded image sees Thumb::*.
bool isCurrent(const QImage &image, const QString &uri, qint64 mtime, qint64 size)
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

void setMetadata(QImage &image, const QString &uri, qint64 mtime, qint64 size)
{
    image.setText(uriKey, uri);
    image.setText(mtimeKey, QString::number(mtime));
    image.setText(sizeKey, QString::number(size));
}
}

ThumbnailCache *ThumbnailCache::self()
{
    static ThumbnailCache cache;
    return &cache;
}

Thumbnail ThumbnailCache::requestThumbnail(const QString &filePath, const QSize &requestedSize)
{
    if (!requestedSize.isValid() || requestedSize.isEmpty())
        return {};

    const QUrl url(filePath);
    const QString path = url.isLocalFile() ? url.toLocalFile() : filePath;
    const QFileInfo info(path);
    if (!info.isFile())
        return {};

    const Flavor &flavor = flavorFor(requestedSize);
    const QSize cacheSize(flavor.size, flavor.size);
    const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
            + QStringLiteral("/thumbnails");

    // The spec forbids thumbnailing the cache itself, which would otherwise feed on its output.
    if (info.absoluteFilePath().startsWith(root + '/'))
        return {{}, generateThumbnail(path, cacheSize)};

    const QByteArray encodedUri = QUrl::fromLocalFile(info.absoluteFilePath()).toEncoded();
    const QString uri = QString::fromLatin1(encodedUri);
    const QString name = QString::fromLatin1(
                                 QCryptographicHash::hash(encodedUri, QCryptographicHash::Md5).toHex())
            + QStringLiteral(".png");
    const qint64 mtime = info.lastModified().toSecsSinceEpoch();
    const qint64 size = info.size();

    const QString directory = root + '/' + QLatin1StringView(flavor.name);
    const QString target = directory + '/' + name;
    const QImage cached(target);
    if (isCurrent(cached, uri, mtime, size))
        return {target, cached};

    const QString failDirectory = root + QStringLiteral("/fail/cutefish");
    const QString failTarget = failDirectory + '/' + name;
    if (isCurrent(QImage(failTarget), uri, mtime, size))
        return {};

    QImage image = generateThumbnail(path, cacheSize);
    if (image.isNull()) {
        // Remember the failure so broken images are not decoded again on every scroll.
        QImage marker(1, 1, QImage::Format_ARGB32);
        marker.fill(Qt::transparent);
        setMetadata(marker, uri, mtime, size);
        if (ensurePrivateDirectory(root) && ensurePrivateDirectory(failDirectory))
            writeCacheFile(failTarget, marker);
        return {};
    }

    setMetadata(image, uri, mtime, size);

    // An unwritable cache must not hide the thumbnail; only the path goes missing.
    if (!ensurePrivateDirectory(root) || !ensurePrivateDirectory(directory))
        return {{}, image};
    return {writeCacheFile(target, image), image};
}

QImage ThumbnailCache::generateThumbnail(const QString &source, const QSize &requestedSize)
{
    QImageReader reader(source);
    const QSize originalSize = reader.size();
    if (!originalSize.isValid() || requestedSize.isEmpty())
        return {};

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
