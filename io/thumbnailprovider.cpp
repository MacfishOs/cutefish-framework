// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnailprovider.h"
#include "thumbnailcache.h"

#include <QImage>
#include <QQuickTextureFactory>
#include <QThreadPool>
#include <QUrl>

#include <atomic>

static QThreadPool &thumbnailPool()
{
    static QThreadPool pool;
    static const bool configured = [] {
        pool.setMaxThreadCount(2);
        return true;
    }();

    Q_UNUSED(configured)
    return pool;
}

namespace {

class ThumbnailResponse final : public QQuickImageResponse
{
public:
    ThumbnailResponse(const QString &id, const QSize &requestedSize)
    {
        thumbnailPool().start([this, id, requestedSize] {
            if (!m_cancelled.load() && requestedSize.isValid()) {
                Thumbnail thumbnail = ThumbnailCache::self()->requestThumbnail(id, requestedSize);
                if (!m_cancelled.load())
                    m_image = std::move(thumbnail.image);
            }

            // Qt keeps the response alive until finished, including cancelled requests.
            emit finished();
        });
    }

    void cancel() override
    {
        m_cancelled.store(true);
    }

    QQuickTextureFactory *textureFactory() const override
    {
        return QQuickTextureFactory::textureFactoryForImage(m_image);
    }

private:
    QImage m_image;
    std::atomic_bool m_cancelled = false;
};

} // namespace

QQuickImageResponse *ThumbnailProvider::requestImageResponse(const QString &id,
                                                             const QSize &requestedSize)
{
    return new ThumbnailResponse(id, requestedSize);
}

bool ThumbnailProvider::isCached(const QUrl &source, const QSize &requestedSize)
{
    // The id an image provider is given for this source, as QQuickPixmap derives it.
    const QString id = source.toString(QUrl::RemoveScheme | QUrl::RemoveAuthority).mid(1);
    return ThumbnailCache::self()->hasCachedThumbnail(id, requestedSize);
}

CachedThumbnailProvider::CachedThumbnailProvider()
    : QQuickImageProvider(QQuickImageProvider::Image)
{
}

QImage CachedThumbnailProvider::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    const QImage image = ThumbnailCache::self()->cachedThumbnail(id, requestedSize).image;
    if (size)
        *size = image.size();
    return image;
}
