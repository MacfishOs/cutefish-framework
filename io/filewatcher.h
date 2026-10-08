#pragma once

#include <QHash>
#include <QObject>
#include <QSocketNotifier>
#include <QStringList>

namespace Cutefish::IO {

class FileWatcher : public QObject
{
    Q_OBJECT

public:
    explicit FileWatcher(QObject *parent = nullptr);
    ~FileWatcher() override;

    bool watch(const QString &path)
    {
        return watch(QStringList {path});
    }

    // Watches each folder of paths, and the entry naming it in its parent so that one
    // created, replaced or removed is noticed; in each folder of entries, only those
    // entries; with mounts, the mount table. Returns whether every folder of paths is.
    bool watch(const QStringList &paths, const QHash<QString, QList<QByteArray>> &entries = {},
               bool mounts = false);
    void clear();

signals:
    void changed();
    void error(const QString &message);

private:
    // The entries of a watched folder that count, or every one.
    struct Filter
    {
        bool all = false;
        QList<QByteArray> names;
    };

    int m_fd = -1;
    QStringList m_paths;
    QHash<QString, QList<QByteArray>> m_entries;
    bool m_mounts = false;
    // Cleared once a watch is removed or its directory moves or goes away.
    bool m_valid = false;
    QHash<int, Filter> m_watches;
    QSocketNotifier *m_notifier = nullptr;
    int m_mountTable = -1;
    QSocketNotifier *m_mountNotifier = nullptr;
};

} // namespace Cutefish::IO
