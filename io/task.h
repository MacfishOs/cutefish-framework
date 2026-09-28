#pragma once
#include "filesystem.h"
#include "taskprogress.h"
#include <QFutureWatcher>
#include <QMimeData>
#include <QObject>
#include <QPointer>
#include <condition_variable>
#include <mutex>
#include <optional>

namespace Cutefish::IO {
enum class Operation {
    Copy,
    Move,
    Link,
    Delete,
    Mkdir,
    Rename,
    Trash,
    Restore,
    EmptyTrash,
    Size
};

struct TaskRequest {
    Operation operation = Operation::Copy;
    QList<QUrl> sources;
    QUrl destination;
    ConflictPolicy conflict = ConflictPolicy::Fail;
};

struct TaskResult {
    Error error;
    QList<QUrl> outputs;
    QList<QUrl> completedSources;
    quint64 bytes = 0;
    quint64 files = 0;
};

class Task : public QObject
{
    Q_OBJECT
    Q_PROPERTY(Cutefish::IO::TaskProgress progressInfo READ progressInfo NOTIFY progressInfoChanged)
    Q_PROPERTY(QString operationName READ operationName CONSTANT)
    Q_PROPERTY(int sourceCount READ sourceCount CONSTANT)
    Q_PROPERTY(QString sourceName READ sourceName CONSTANT)
    Q_PROPERTY(QString destinationName READ destinationName CONSTANT)
public:
    enum class State {
        Queued,
        Running,
        Succeeded,
        Failed,
        Canceled
    };
    Q_ENUM(State)
    explicit Task(TaskRequest request, QObject *parent = nullptr);
    ~Task() override;
    void start();
    Q_INVOKABLE void cancel();
    // applyToAll answers every later conflict of this task the same way.
    Q_INVOKABLE void resolveConflict(int policy, bool applyToAll = false);

    State state() const { return m_state; }

    TaskProgress progressInfo() const { return m_progressInfo; }

    QString operationName() const;

    int sourceCount() const { return m_request.sources.size(); }

    QString sourceName() const
    {
        return m_request.sources.isEmpty() ? QString() : m_request.sources.first().fileName();
    }

    QString destinationName() const;
    Q_INVOKABLE QString formattedSize(quint64 bytes) const;

    Q_INVOKABLE bool wasCanceled() const { return m_state == State::Canceled; }

    const TaskResult &outcome() const { return m_result; }

    const TaskRequest &request() const { return m_request; }

    Q_INVOKABLE int error() const { return m_result.error.code; }

    Q_INVOKABLE QString errorString() const
    {
        return m_result.error.path.isEmpty() ? m_result.error.message
                                             : m_result.error.path + ": " + m_result.error.message;
    }

signals:
    void progress(quint64 bytes, quint64 files);
    void progressInfoChanged();
    void conflictRequested(const QString &source, const QString &destination);
    void stateChanged(State state);
    void result(Cutefish::IO::Task *task);

private:
    TaskRequest m_request;
    TaskResult m_result;
    TaskProgress m_progressInfo;
    State m_state = State::Queued;
    std::shared_ptr<OperationContext> m_context;
    QFutureWatcher<TaskResult> m_watcher;
    std::mutex m_conflictMutex;
    std::condition_variable m_conflictCondition;
    ConflictPolicy m_resolution = ConflictPolicy::Fail;
    std::optional<ConflictPolicy> m_resolutionForAll;
    bool m_resolved = false;
};

class TaskController : public QObject
{
    Q_OBJECT
public:
    explicit TaskController(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    static TaskController *instance();
    ~TaskController() override;
    Task *submit(const TaskRequest &request);

    bool isUndoAvailable() const { return !m_undo.isEmpty(); }
    bool isRedoAvailable() const { return !m_redo.isEmpty(); }

    Task *undo();
    Task *redo();
signals:
    void taskAdded(Cutefish::IO::Task *task);
    void taskFinished(Cutefish::IO::Task *task);
    //! isUndoAvailable() or isRedoAvailable() may have changed.
    void historyChanged();

private:
    Task *submit(const TaskRequest &request, bool keepRedo);

    // Trash operations each add an entry; a long session must not keep them all.
    static constexpr int kMaxHistory = 50;
    QList<TaskRequest> m_undo;
    QList<TaskRequest> m_redo;
};

Task *copy(const QList<QUrl> &sources, const QUrl &destination);
Task *move(const QList<QUrl> &sources, const QUrl &destination);
Task *del(const QList<QUrl> &sources);
Task *mkdir(const QUrl &destination);
Task *trash(const QList<QUrl> &sources);
Task *restoreFromTrash(const QList<QUrl> &sources);
Task *emptyTrash();
Task *paste(const QMimeData *data, const QUrl &destination);
Task *drop(const QMimeData *data, const QUrl &destination, Qt::DropAction action);
} // namespace Cutefish::IO
