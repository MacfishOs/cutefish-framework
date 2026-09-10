#include "directorylister.h"
#include "async.h"
#include "fileenumerator.h"
#include "filesystemurl.h"

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
    connect(&m_future, &QFutureWatcher<Listing>::finished, this, [this] {
        const auto listing = m_future.result();
        if (m_runningGeneration == m_generation) {
            m_items = listing.items;
            emit itemsChanged();
            if (listing.error) {
                emit error(listing.error.message + ": " + listing.error.path);
                emit canceled();
            } else
                emit completed();
        }
        if (m_pending) {
            m_pending = false;
            refresh();
        }
    });
}

DirectoryLister::~DirectoryLister()
{
    m_future.waitForFinished();
}

void DirectoryLister::openUrl(const QUrl &url)
{
    ++m_generation;
    m_url = url;
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
    m_pending = false;
    m_debounce.stop();
    m_retry.stop();
    m_watch.clear();
    emit canceled();
}

void DirectoryLister::refresh()
{
    if (m_url.isEmpty())
        return;
    if (m_future.isRunning()) {
        m_pending = true;
        return;
    }
    m_runningGeneration = m_generation;
    try {
        if (m_watch.watch(FileSystemUrl(m_url).localPath()))
            m_retry.stop();
        else
            m_retry.start();
    } catch (const Failure &) {
        m_watch.clear();
    }
    emit started();
    m_future.setFuture(Internal::runAsync([url = m_url, hidden = m_hidden] {
        Listing result;
        try {
            result.items = FileEnumerator::list(url, hidden);
        } catch (const Failure &failure) {
            result.error = failure.error;
        }
        return result;
    }));
}

} // namespace Cutefish::IO
