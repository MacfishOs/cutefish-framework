#pragma once

#include "error.h"
#include "fileitem.h"
#include "filewatcher.h"
#include "operationcontext.h"

#include <QFutureWatcher>
#include <QMutex>
#include <QTimer>
#include <memory>

namespace Cutefish::IO {

class DirectoryLister : public QObject
{
    Q_OBJECT

public:
    explicit DirectoryLister(QObject *parent = nullptr);
    ~DirectoryLister() override;

    QUrl url() const
    {
        return m_url;
    }

    bool isListing() const
    {
        return m_future != nullptr;
    }

    FileItem rootItem() const
    {
        return m_rootItem;
    }

    const FileItemList &items() const
    {
        return m_items;
    }

    void setShowHiddenFiles(bool show)
    {
        m_hidden = show;
    }

    void setAutoUpdate(bool enabled)
    {
        m_autoUpdate = enabled;
    }

    // The copied callback runs on workers, for several items at once, and must not
    // access GUI objects.
    void setItemPreparer(std::function<void(FileItem &)> preparer)
    {
        m_itemPreparer = std::move(preparer);
    }

    void openUrl(const QUrl &url);

    // Lists the files at any depth under root whose names contain every word of
    // text, in place of the directory's own, which an empty text lists again.
    // The first search hands found files on while it runs; it keeps no watch.
    void search(const QUrl &root, const QString &text);

    bool isSearching() const
    {
        return !m_searchWords.isEmpty();
    }

    void updateDirectory(const QUrl &url);

    void emitChanges()
    {
        refresh();
    }

    void stop();

signals:
    void started();
    void completed();
    void canceled();
    void error(const QString &message);
    void itemsChanged();
    void directoryChanged();

private:
    void refresh();
    void watchDirectory();
    void reportError(const QString &message);
    void cancelListing();
    void takeFound();

    struct Listing
    {
        FileItemList items;
        FileItem rootItem;
        QUrl url;
        Error error;
        QString typeDatabase;
        bool typesSettled = false;
    };

    // What a running search found since the last look.
    struct Found
    {
        QMutex mutex;
        FileItemList items;
    };

    QUrl m_url;
    FileItem m_rootItem;
    FileItemList m_items;
    bool m_hidden = false;
    bool m_autoUpdate = true;
    bool m_pending = false;
    bool m_listed = false;
    QString m_lastError;
    QString m_typeDatabase;
    bool m_typesSettled = false;
    quint64 m_generation = 0;
    FileWatcher m_watch;
    QTimer m_debounce;
    QTimer m_retry;
    QFutureWatcher<Listing> *m_future = nullptr;
    std::shared_ptr<OperationContext> m_context;
    std::function<void(FileItem &)> m_itemPreparer;
    QUrl m_searchRoot;
    QStringList m_searchWords;
    std::shared_ptr<Found> m_found;
    QTimer m_foundTimer;
};

} // namespace Cutefish::IO
