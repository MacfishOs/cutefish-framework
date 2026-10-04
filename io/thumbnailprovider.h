// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QQuickAsyncImageProvider>
#include <QQuickImageProvider>

class ThumbnailProvider : public QQuickAsyncImageProvider
{
public:
    QQuickImageResponse *requestImageResponse(const QString &id,
                                              const QSize &requestedSize) override;

    /**
     * Whether the thumbnail @p source ("image://<name>/<id>") at @p requestedSize
     * device pixels is in the disk cache, so CachedThumbnailProvider can read it
     * synchronously and a view show it in its first frame.
     */
    static bool isCached(const QUrl &source, const QSize &requestedSize);
};

/** Serves thumbnails from the disk cache on the calling thread and never generates one. */
class CachedThumbnailProvider : public QQuickImageProvider
{
public:
    CachedThumbnailProvider();

    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;
};
