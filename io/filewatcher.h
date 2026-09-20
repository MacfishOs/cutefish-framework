#pragma once
#include <QObject>
#include <QSocketNotifier>

namespace Cutefish::IO {
class FileWatcher : public QObject
{
    Q_OBJECT
public:
    explicit FileWatcher(QObject *parent = nullptr);
    ~FileWatcher() override;
    bool watch(const QString &path);
    void clear();
signals:
    void changed();
    void error(const QString &message);

private:
    int m_fd = -1;
    QList<int> m_watches;
    int m_parentWatch = -1;
    QByteArray m_parentEntry;
    QSocketNotifier *m_notifier = nullptr;
};

} // namespace Cutefish::IO
