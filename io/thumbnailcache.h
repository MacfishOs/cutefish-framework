#pragma once

#include <QImage>
#include <QSize>
#include <QString>

struct Thumbnail
{
    QString path;
    QImage image;
};

class ThumbnailCache
{
public:
    static ThumbnailCache *self();

    Thumbnail requestThumbnail(const QString &filePath, const QSize &requestedSize);
    QImage generateThumbnail(const QString &source, const QSize &requestedSize);
    QString writeCacheFile(const QString &path, const QImage &image);
};
