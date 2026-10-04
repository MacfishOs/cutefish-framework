#include "directorylister.h"

#include "async.h"
#include "fileenumerator.h"
#include "filesystemurl.h"

#include <QDir>
#include <QStandardPaths>
#include <QTimeZone>

namespace Cutefish::IO {

namespace {

// The MIME database as of a listing. QMimeDatabase rereads it at most every five
// seconds, so types detected sooner after a change may be the old ones.
struct TypeDatabase
{
    QString version;
    bool settled = false;
};

TypeDatabase currentTypeDatabase()
{
    const QStringList paths =
            QStandardPaths::locateAll(QStandardPaths::GenericDataLocation,
                                      QStringLiteral("mime/mime.cache"))
            + QStandardPaths::locateAll(QStandardPaths::GenericDataLocation,
                                        QStringLiteral("mime/packages"),
                                        QStandardPaths::LocateDirectory);

    TypeDatabase database;
    qint64 newest = 0;
    for (const QString &path : paths) {
        const qint64 modified =
                QFileInfo(path).lastModified(QTimeZone::UTC).toMSecsSinceEpoch();
        newest = qMax(newest, modified);
        database.version += path + u' ' + QString::number(modified) + u'\n';
    }
    database.settled = QDateTime::currentMSecsSinceEpoch() - newest > 6000;

    return database;
}

QString existingAncestor(QString path)
{
    while (!QFileInfo(path).isDir()) {
        const QString parent = QFileInfo(path).absolutePath();
        if (parent == path)
            break;
        path = parent;
    }

    return path;
}

} // namespace

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
    connect(&m_watch, &FileWatcher::error, this, &DirectoryLister::reportError);
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
    m_listed = false;
    m_lastError.clear();

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

void DirectoryLister::watchDirectory()
{
    m_watch.clear();

    try {
        const QString path = FileSystemUrl(m_url).localPath();

        // A path that is gone gets no watch and no polling; the listing either
        // moves to its parent or reports it once.
        if (!QFileInfo(path).isDir() || m_watch.watch(path))
            m_retry.stop();
        else
            m_retry.start();
    } catch (const Failure &) {
        m_retry.stop();
    }
}

// Polling repeats the same failure every interval; show it once.
void DirectoryLister::reportError(const QString &message)
{
    if (message == m_lastError)
        return;

    m_lastError = message;
    emit error(message);
}

void DirectoryLister::refresh()
{
    if (m_url.isEmpty())
        return;

    if (m_future) {
        m_pending = true;
        return;
    }

    watchDirectory();

    const quint64 generation = m_generation;
    auto context = std::make_shared<OperationContext>();
    m_context = context;

    auto *watcher = new QFutureWatcher<Listing>(this);
    m_future = watcher;

    connect(watcher, &QFutureWatcher<Listing>::finished, this, [this, watcher, generation] {
        if (generation != m_generation || watcher != m_future)
            return;

        const auto listing = watcher->result();
        watcher->deleteLater();
        m_future = nullptr;
        m_context.reset();

        m_typeDatabase = listing.typeDatabase;
        m_typesSettled = listing.typesSettled;

        if (listing.url.isValid() && listing.url != m_url) {
            m_url = listing.url;
            watchDirectory();

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
            reportError(listing.error.message + ": " + listing.error.path);
            if (generation == m_generation)
                emit canceled();
        } else {
            m_listed = true;
            emit completed();
        }

        if (generation == m_generation && m_pending) {
            m_pending = false;
            refresh();
        }
    });

    // A refresh of the listed directory keeps the detected types of unchanged files.
    watcher->setFuture(Internal::runAsync([url = m_url, hidden = m_hidden, context,
                                           preparer = m_itemPreparer, listed = m_listed,
                                           previous = m_listed ? m_items : FileItemList(),
                                           knownDatabase = m_typeDatabase,
                                           knownSettled = m_typesSettled] {
        Listing result;
        const TypeDatabase database = currentTypeDatabase();
        result.typeDatabase = database.version;
        result.typesSettled = database.settled;

        try {
            context->checkpoint();
            result.url = url;

            if (url.isLocalFile()) {
                const QFileInfo target(url.toLocalFile());

                if (target.isFile())
                    result.url = QUrl::fromLocalFile(target.absolutePath());
                else if (listed && !target.exists())
                    result.url = QUrl::fromLocalFile(existingAncestor(target.absoluteFilePath()));
            }

            result.items = FileEnumerator::list(result.url, hidden, context.get());
            result.rootItem = FileItem(result.url);
            result.rootItem.prepareMetadata();

            const FileItem none;
            QHash<QString, const FileItem *> previousByName;
            if (result.url == url && knownSettled && database.version == knownDatabase) {
                previousByName.reserve(previous.size());
                // Names that decode to the same text cannot tell their files apart.
                for (const auto &item : previous) {
                    const FileItem *&entry = previousByName[item.name()];
                    entry = entry ? &none : &item;
                }
            }

            FileItem::TypeDetails details;

            for (auto &item : result.items) {
                context->checkpoint();
                const FileItem *old = previousByName.value(item.name(), &none);
                if (!item.prepareMetadata(*old, &details) && preparer)
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
