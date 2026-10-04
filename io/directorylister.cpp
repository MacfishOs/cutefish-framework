#include "directorylister.h"

#include "async.h"
#include "fileenumerator.h"
#include "filesystemurl.h"

#include <QDir>
#include <QStandardPaths>
#include <QTimeZone>

#include <atomic>

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

// Stats every item and reads the files whose names do not decide their type,
// spread over pool threads for a first listing or a large folder.
void prepareItems(FileItemList &items, const QHash<QString, const FileItem *> &previousByName,
                  const std::function<void(FileItem &)> &preparer, const OperationContext *context)
{
    constexpr qsizetype BlockSize = 64;

    // Detached here: the workers write their own items only.
    FileItem *const data = items.data();
    const qsizetype count = items.size();
    const qsizetype blocks = (count + BlockSize - 1) / BlockSize;
    std::atomic<qsizetype> nextBlock = 0;

    // A refresh keeps most types and only stats. Threads cut that from 220 to 40 ms
    // for 50,000 files, for 5-15% more CPU time per refresh; at 10,000 they saved nothing.
    const bool spread = previousByName.isEmpty() || count >= Internal::LargeFolder;

    Internal::runOnFreeThreads(spread ? Internal::blockHelpers(blocks) : 0,
                               [&](const std::atomic_bool &stop) {
        const FileItem none;
        FileItem::TypeDetails details;

        for (qsizetype block = nextBlock++; block < blocks && !stop; block = nextBlock++) {
            const qsizetype end = qMin(count, (block + 1) * BlockSize);

            for (qsizetype index = block * BlockSize; index < end; ++index) {
                context->checkpoint();
                FileItem &item = data[index];
                const FileItem *old = previousByName.value(item.name(), &none);
                if (!item.prepareMetadata(*old, &details) && preparer)
                    preparer(item);
            }
        }
    });
}

} // namespace

DirectoryLister::DirectoryLister(QObject *parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(40);
    m_retry.setInterval(2000);
    m_foundTimer.setInterval(100);

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
    connect(&m_foundTimer, &QTimer::timeout, this, &DirectoryLister::takeFound);
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
    m_foundTimer.stop();
    m_found.reset();
}

void DirectoryLister::takeFound()
{
    FileItemList found;
    {
        const QMutexLocker locker(&m_found->mutex);
        found.swap(m_found->items);
    }

    if (found.isEmpty())
        return;

    m_items.append(found);
    emit itemsChanged();
}

void DirectoryLister::openUrl(const QUrl &url)
{
    ++m_generation;
    cancelListing();
    m_debounce.stop();

    m_url = url;
    m_searchRoot.clear();
    m_searchWords.clear();
    m_rootItem = {};
    m_items.clear();
    m_watch.clear();
    m_retry.stop();
    m_listed = false;
    m_lastError.clear();

    emit directoryChanged();
    refresh();
}

void DirectoryLister::search(const QUrl &root, const QString &text)
{
    const QStringList words = text.simplified().split(u' ', Qt::SkipEmptyParts);
    if (words == m_searchWords && (words.isEmpty() || root == m_searchRoot))
        return;

    ++m_generation;
    cancelListing();
    m_debounce.stop();

    m_searchRoot = words.isEmpty() ? QUrl() : root;
    m_searchWords = words;
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
    // A watch on every folder searched would cost more than searching again.
    if (isSearching()) {
        m_watch.clear();
        m_retry.stop();
        return;
    }

    try {
        const QString path = FileSystemUrl(m_url).localPath();

        // A path that is gone gets no watch and no polling; the listing either
        // moves to its parent or reports it once.
        if (!QFileInfo(path).isDir()) {
            m_watch.clear();
            m_retry.stop();
        } else if (m_watch.watch(path)) {
            m_retry.stop();
        } else {
            m_retry.start();
        }
    } catch (const Failure &) {
        m_watch.clear();
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

    // Later searches of the same words replace the results at once instead of
    // emptying them first.
    std::shared_ptr<Found> found;
    if (isSearching() && !m_listed) {
        found = std::make_shared<Found>();
        m_found = found;
        m_foundTimer.start();
    }

    connect(watcher, &QFutureWatcher<Listing>::finished, this, [this, watcher, generation] {
        if (generation != m_generation || watcher != m_future)
            return;

        const auto listing = watcher->result();
        watcher->deleteLater();
        m_future = nullptr;
        m_context.reset();
        m_foundTimer.stop();
        m_found.reset();

        m_typeDatabase = listing.typeDatabase;
        m_typesSettled = listing.typesSettled;

        if (listing.url.isValid() && listing.url != m_url) {
            m_url = listing.url;
            watchDirectory();

            emit directoryChanged();

            if (generation != m_generation)
                return;
        }

        // Not released in the background: the model frees the old rows, and changes
        // them in place only while it holds them alone.
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
    watcher->setFuture(Internal::runAsync([url = m_url, hidden = m_hidden, context, found,
                                           searchRoot = m_searchRoot, words = m_searchWords,
                                           preparer = m_itemPreparer, listed = m_listed,
                                           previous = m_listed ? m_items : FileItemList(),
                                           knownDatabase = m_typeDatabase,
                                           knownSettled = m_typesSettled]() mutable {
        Listing result;
        const TypeDatabase database = currentTypeDatabase();
        result.typeDatabase = database.version;
        result.typesSettled = database.settled;

        try {
            context->checkpoint();
            result.url = url;

            if (!words.isEmpty()) {
                result.items = FileEnumerator::search(searchRoot, words, hidden, context.get(),
                                                      [&](FileItem &item) {
                    if (preparer)
                        preparer(item);

                    if (found) {
                        const QMutexLocker locker(&found->mutex);
                        found->items.append(item);
                    }
                });
                result.rootItem = FileItem(url);
                result.rootItem.prepareMetadata();
                previous = {};

                return result;
            }

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

            prepareItems(result.items, previousByName, preparer, context.get());
        } catch (const Failure &failure) {
            result.error = failure.error;
        }

        // The model changes the old rows in place only while it holds them alone.
        previous = {};

        return result;
    }));

    emit started();
}

} // namespace Cutefish::IO
