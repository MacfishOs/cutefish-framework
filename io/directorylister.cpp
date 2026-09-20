#include "directorylister.h"
#include "async.h"
#include "fileenumerator.h"
#include "filesystemurl.h"

#include <QDir>

namespace Cutefish::IO {
DirectoryLister::DirectoryLister(QObject *parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(40);
    m_retry.setInterval(2000);
    connect(&m_watch, &FileWatcher::changed, this, [this] {
        if (m_autoUpdate)
            m_debounce.start();
    });
    connect(&m_watch, &FileWatcher::error, this, &DirectoryLister::error);
    connect(&m_debounce, &QTimer::timeout, this, &DirectoryLister::refresh);
    connect(&m_retry, &QTimer::timeout, this, [this] {
        if (m_autoUpdate)
            refresh();
    });
}

DirectoryLister::~DirectoryLister()
{
    cancelListing();
}

void DirectoryLister::cancelListing()
{
    if (m_context)
        m_context->canceled.store(true);
    if (m_future) {
        disconnect(m_future, nullptr, this, nullptr);
        // Workers own their inputs and can outlive a view or navigation request.
        m_future->deleteLater();
        m_future = nullptr;
    }
    m_context.reset();
    m_pending = false;
}

void DirectoryLister::openUrl(const QUrl &url)
{
    ++m_generation;
    cancelListing();
    m_debounce.stop();
    m_url = url;
    m_rootItem = {};
    m_items.clear();
    m_watch.clear();
    m_retry.stop();
    emit directoryChanged();
    refresh();
}

void DirectoryLister::updateDirectory(const QUrl &url)
{
    if (url == m_url)
        refresh();
}

void DirectoryLister::stop()
{
    ++m_generation;
    cancelListing();
    m_debounce.stop();
    m_retry.stop();
    m_watch.clear();
    emit canceled();
}

void DirectoryLister::refresh()
{
    if (m_url.isEmpty())
        return;
    if (m_future) {
        m_pending = true;
        return;
    }
    try {
        if (m_watch.watch(FileSystemUrl(m_url).localPath()))
            m_retry.stop();
        else
            m_retry.start();
    } catch (const Failure &) {
        m_watch.clear();
    }
    const quint64 generation = m_generation;
    auto context = std::make_shared<OperationContext>();
    m_context = context;
    auto *watcher = new QFutureWatcher<Listing>(this);
    m_future = watcher;
    connect(watcher, &QFutureWatcher<Listing>::finished, this,
            [this, watcher, generation] {
        if (generation != m_generation || watcher != m_future)
            return;
        const auto listing = watcher->result();
        watcher->deleteLater();
        m_future = nullptr;
        m_context.reset();

        if (listing.url.isValid() && listing.url != m_url) {
            m_url = listing.url;
            m_watch.clear();

            if (m_watch.watch(FileSystemUrl(m_url).localPath()))
                m_retry.stop();
            else
                m_retry.start();

            emit directoryChanged();

            if (generation != m_generation)
                return;
        }

        m_items = listing.items;
        m_rootItem = listing.rootItem;
        emit itemsChanged();
        if (generation != m_generation)
            return;
        if (listing.error) {
            emit error(listing.error.message + ": " + listing.error.path);
            if (generation == m_generation)
                emit canceled();
        } else {
            emit completed();
        }
        if (generation == m_generation && m_pending) {
            m_pending = false;
            refresh();
        }
    });
    watcher->setFuture(Internal::runAsync([url = m_url, hidden = m_hidden, context,
                                         preparer = m_itemPreparer] {
        Listing result;
        try {
            context->checkpoint();
            result.url = url;

            if (url.isLocalFile()) {
                const QFileInfo target(url.toLocalFile());

                if (target.isFile())
                    result.url = QUrl::fromLocalFile(target.absolutePath());
            }

            result.items = FileEnumerator::list(result.url, hidden, context.get());
            result.rootItem = FileItem(result.url);
            result.rootItem.prepareMetadata();
            for (auto &item : result.items) {
                context->checkpoint();
                item.prepareMetadata();
                if (preparer)
                    preparer(item);
            }
        } catch (const Failure &failure) {
            result.error = failure.error;
        }
        return result;
    }));
    emit started();
}

} // namespace Cutefish::IO
