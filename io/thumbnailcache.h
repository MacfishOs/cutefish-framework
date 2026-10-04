// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

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

    /** Whether Qt's image readers or an installed thumbnailer handle @p mimeType. */
    bool canThumbnail(const QString &mimeType);

    Thumbnail requestThumbnail(const QString &filePath, const QSize &requestedSize);

    /** Whether a cache entry newer than @p filePath exists; it may still turn out stale. */
    bool hasCachedThumbnail(const QString &filePath, const QSize &requestedSize);
    /** The current cache entry only, never generating one; cheap enough for the GUI thread. */
    Thumbnail cachedThumbnail(const QString &filePath, const QSize &requestedSize);
    QImage generateThumbnail(const QString &source, const QSize &requestedSize);
    QString writeCacheFile(const QString &path, const QImage &image);
};
