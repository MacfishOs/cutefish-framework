#pragma once
#include "error.h"
#include "fileitem.h"
#include "filewatcher.h"
#include "operationcontext.h"
#include <QFutureWatcher>
#include <QTimer>
#include <memory>

namespace Cutefish::IO {
class DirectoryLister : public QObject
{
    Q_OBJECT
public:
    explicit DirectoryLister(QObject *parent = nullptr);
    ~DirectoryLister() override;

    QUrl url() const { return m_url; }

    FileItem rootItem() const { return FileItem(m_url); }

    const FileItemList &items() const { return m_items; }

    void setShowHiddenFiles(bool show) { m_hidden = show; }

    void setAutoUpdate(bool enabled) { m_autoUpdate = enabled; }

    void openUrl(const QUrl &url);
    void updateDirectory(const QUrl &url);

    void emitChanges() { refresh(); }

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
    void cancelListing();

    struct Listing {
        FileItemList items;
        Error error;
    };

    QUrl m_url;
    FileItemList m_items;
    bool m_hidden = false;
    bool m_autoUpdate = true;
    bool m_pending = false;
    quint64 m_generation = 0;
    FileWatcher m_watch;
    QTimer m_debounce;
    QTimer m_retry;
    QFutureWatcher<Listing> *m_future = nullptr;
    std::shared_ptr<OperationContext> m_context;
};

} // namespace Cutefish::IO
