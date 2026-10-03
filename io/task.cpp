#include "task.h"
#include "async.h"
#include "fileitem.h"
#include "posix_p.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QLocale>
#include <QMetaMethod>
#include <QTimer>

#include <cerrno>
#include <linux/magic.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/vfs.h>

namespace Cutefish::IO {

namespace {

// A copy within a filesystem that shares extents may take no space at all, so it is not counted.
bool takesSpace(const QString &source, const QString &directory, bool move)
{
    const auto sourcePath = Internal::native(source);
    const auto directoryPath = Internal::native(directory);

    struct stat from, to;
    struct statfs fromFs, toFs;
    if (::lstat(sourcePath, &from) < 0 || ::stat(directoryPath, &to) < 0
        || ::statfs(sourcePath, &fromFs) < 0 || ::statfs(directoryPath, &toFs) < 0)
        return false;

    if (move && from.st_dev == to.st_dev)
        return false;

    const auto type = toFs.f_type;
    return fromFs.f_type != type
            || (type != BTRFS_SUPER_MAGIC && type != XFS_SUPER_MAGIC
                && type != BCACHEFS_SUPER_MAGIC);
}

TaskResult execute(const TaskRequest &request, OperationContext &ctx,
                   const std::function<void(const TaskProgress &, bool)> &report)
{
    TaskResult result;
    TaskProgress info;
    QElapsedTimer elapsed;
    qint64 pausedMs = 0;
    QList<QPair<quint64, quint64>> weights;
    quint64 creditedBytes = 0, creditedItems = 0;
    quint64 sourceBytes = 0, sourceItems = 0;
    quint64 required = 0;
    const auto legacyProgress = ctx.progress;
    const auto conflictResolver = ctx.conflict;

    auto update = [&](bool force = false) {
        info.elapsedMs = elapsed.isValid() ? elapsed.elapsed() - pausedMs : 0;
        report(info, force);
    };

    ctx.conflict = [&](const QString &source, const QString &destination) {
        const auto before = elapsed.isValid() ? elapsed.elapsed() : 0;
        const auto policy = conflictResolver ? conflictResolver(source, destination)
                                             : ConflictPolicy::Fail;
        if (elapsed.isValid())
            pausedMs += elapsed.elapsed() - before;

        return policy;
    };

    ctx.currentItem = [&](const QString &path) {
        info.currentPath = path;
        update();
    };

    ctx.progress = [&](quint64 bytes, quint64 files) {
        info.processedBytes = info.totalsKnown ? creditedBytes + bytes - sourceBytes : bytes;
        info.processedItems = info.totalsKnown ? creditedItems + files - sourceItems : files;
        update();

        if (legacyProgress)
            legacyProgress(bytes, files);
    };

    try {
        ctx.checkpoint();

        if (request.operation == Operation::Move || request.operation == Operation::Rename
            || request.operation == Operation::Trash || request.operation == Operation::Delete) {
            for (const auto &url : request.sources) {
                const bool allowedHomeMove = request.operation == Operation::Move
                        && canMoveStandardFolderTo(url, request.destination);
                if (isStandardFolder(url) && !allowedHomeMove)
                    throw Failure(EPERM, url.toLocalFile());
            }
        }

        auto sources = request.sources;
        if (request.operation == Operation::EmptyTrash) {
            for (const auto &item : LocalFileSystem::enumerate(QUrl("trash:/")))
                sources.append(item.url());
        }

        info.totalsKnown = request.operation != Operation::Size;

        if (request.operation != Operation::Size) {
            for (const auto &url : sources) {
                OperationContext scan;
                scan.cancelFlag = &ctx.canceled;
                scan.currentItem = ctx.currentItem;

                if (request.operation == Operation::Rename || request.operation == Operation::Link)
                    scan.files = 1;
                else {
                    try {
                        const auto source = FileSystemUrl(url).localPath();
                        LocalFileSystem::directorySize(source, scan);

                        if ((request.operation == Operation::Copy
                             || request.operation == Operation::Move)
                            && takesSpace(source, FileSystemUrl(request.destination).localPath(),
                                          request.operation == Operation::Move))
                            required += scan.allocated;
                    } catch (const Failure &failure) {
                        if (failure.error.code == ECANCELED)
                            throw;
                        info.totalsKnown = false;
                    }
                }

                weights.append({scan.bytes, scan.files});
                info.totalBytes += scan.bytes;
                info.totalItems += scan.files;
            }
        }

        if (info.totalsKnown && required) {
            // A lower bound: block rounding and metadata are not counted, so a copy can still
            // run out, but one that cannot fit fails before it writes anything.
            const auto destination = FileSystemUrl(request.destination).localPath();
            struct statvfs fs;
            if (::statvfs(Internal::native(destination), &fs) == 0
                && required > quint64(fs.f_bavail) * fs.f_frsize)
                throw Failure(ENOSPC, destination);
        }

        if (request.operation == Operation::Mkdir)
            info.totalItems = 1;

        info.preparing = false;
        elapsed.start();
        update(true);

        if (request.operation == Operation::Mkdir) {
            LocalFileSystem::mkdir(FileSystemUrl(request.destination).localPath());
            result.outputs.append(request.destination);
        } else if (request.operation == Operation::EmptyTrash) {
            LocalFileSystem::emptyTrash(ctx);
        } else {
            int sourceIndex = 0;

            for (const auto &url : request.sources) {
                sourceBytes = ctx.bytes;
                sourceItems = ctx.files;
                ctx.checkpoint(FileSystemUrl(url).localPath());

                const auto source = FileSystemUrl(url).localPath();
                QUrl output;

                switch (request.operation) {
                case Operation::Copy:
                case Operation::Move:
                case Operation::Link:
                case Operation::Rename: {
                    const auto destination = FileSystemUrl(request.destination).localPath();
                    const auto target = request.operation == Operation::Rename
                            ? destination
                            : QDir(destination).filePath(url.fileName());
                    const auto path = request.operation == Operation::Link
                            ? LocalFileSystem::link(source, target, request.conflict, ctx)
                            : request.operation == Operation::Copy
                            ? LocalFileSystem::copy(source, target, request.conflict, ctx)
                            : LocalFileSystem::move(source, target, request.conflict, ctx);
                    if (!path.isEmpty())
                        output = QUrl::fromLocalFile(path);
                    break;
                }
                case Operation::Delete:
                    LocalFileSystem::remove(url, ctx);
                    break;
                case Operation::Trash:
                    output = LocalFileSystem::trash(url, ctx);
                    break;
                case Operation::Restore:
                    output = LocalFileSystem::restore(url, request.conflict, ctx);
                    break;
                case Operation::Size:
                    LocalFileSystem::directorySize(source, ctx);
                    break;
                default:
                    break;
                }

                result.completedSources.append(url);
                if (!output.isEmpty())
                    result.outputs.append(output);

                if (info.totalsKnown && sourceIndex < weights.size()) {
                    creditedBytes += weights[sourceIndex].first;
                    creditedItems += weights[sourceIndex].second;
                    info.processedBytes = creditedBytes;
                    info.processedItems = creditedItems;
                }

                ++sourceIndex;
                update(true);
            }
        }
    } catch (const Failure &failure) {
        result.error = failure.error;
    } catch (const std::exception &exception) {
        result.error = {EIO, {}, QString::fromLocal8Bit(exception.what())};
    }

    result.bytes = ctx.bytes;
    result.files = ctx.files;

    if (!result.error && info.totalsKnown) {
        info.processedBytes = info.totalBytes;
        info.processedItems = info.totalItems;
    }

    update(true);
    ctx.currentItem = {};
    ctx.progress = legacyProgress;
    ctx.conflict = conflictResolver;
    return result;
}

} // namespace

Task::Task(TaskRequest request, QObject *parent)
    : QObject(parent)
    , m_request(std::move(request))
    , m_context(std::make_shared<OperationContext>())
{
    connect(&m_watcher, &QFutureWatcher<TaskResult>::finished, this, [this] {
        m_result = m_watcher.result();
        m_state = m_result.error.code == ECANCELED ? State::Canceled
                : m_result.error                   ? State::Failed
                                                   : State::Succeeded;

        emit progress(m_result.bytes, m_result.files);
        emit stateChanged(m_state);
        emit result(this);
    });
}

Task::~Task()
{
    cancel();
    m_watcher.waitForFinished();
}

void Task::cancel()
{
    {
        std::lock_guard lock(m_conflictMutex);
        m_context->canceled.store(true);
    }

    m_conflictCondition.notify_all();
}

void Task::resolveConflict(int policy, bool applyToAll)
{
    if (policy < int(ConflictPolicy::Fail) || policy >= int(ConflictPolicy::Ask))
        return;

    {
        std::lock_guard lock(m_conflictMutex);
        m_resolution = ConflictPolicy(policy);
        m_resolved = true;
        if (applyToAll)
            m_resolutionForAll = m_resolution;
    }

    m_conflictCondition.notify_all();
}

void Task::start()
{
    if (m_state != State::Queued)
        return;

    m_state = State::Running;
    emit stateChanged(m_state);

    m_context->progress = [this, timer = QElapsedTimer()](quint64 bytes, quint64 files) mutable {
        if (timer.isValid() && timer.elapsed() < 40)
            return;

        timer.start();
        QMetaObject::invokeMethod(
                this,
                [this, bytes, files] {
                    emit progress(bytes, files);
                },
                Qt::QueuedConnection);
    };

    m_context->conflict = [this](const QString &source, const QString &destination) {
        std::unique_lock lock(m_conflictMutex);
        if (m_resolutionForAll)
            return *m_resolutionForAll;

        m_resolved = false;
        QMetaObject::invokeMethod(
                this,
                [this, source, destination] {
                    if (isSignalConnected(QMetaMethod::fromSignal(&Task::conflictRequested)))
                        emit conflictRequested(source, destination);
                    else
                        resolveConflict(int(ConflictPolicy::Fail));
                },
                Qt::QueuedConnection);

        QThreadPool::globalInstance()->releaseThread();
        m_conflictCondition.wait(lock, [this] {
            return m_resolved || m_context->canceled.load();
        });
        QThreadPool::globalInstance()->reserveThread();

        m_context->checkpoint(destination);
        return m_resolution;
    };

    m_watcher.setFuture(Internal::runAsync([this, request = m_request, ctx = m_context] {
        auto report = [this, timer = QElapsedTimer()](const TaskProgress &info,
                                                      bool force) mutable {
            if (!force && timer.isValid() && timer.elapsed() < 80)
                return;

            timer.start();
            QMetaObject::invokeMethod(
                    this,
                    [this, info] {
                        m_progressInfo = info;
                        emit progressInfoChanged();
                    },
                    Qt::QueuedConnection);
        };

        return execute(request, *ctx, report);
    }));
}

QString Task::formattedSize(quint64 bytes) const
{
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeSIFormat);
}

QString Task::operationName() const
{
    switch (m_request.operation) {
    case Operation::Copy:
        return QStringLiteral("copy");
    case Operation::Move:
        return QStringLiteral("move");
    case Operation::Link:
        return QStringLiteral("link");
    case Operation::Delete:
        return QStringLiteral("delete");
    case Operation::Mkdir:
        return QStringLiteral("mkdir");
    case Operation::Rename:
        return QStringLiteral("rename");
    case Operation::Trash:
        return QStringLiteral("trash");
    case Operation::Restore:
        return QStringLiteral("restore");
    case Operation::EmptyTrash:
        return QStringLiteral("emptyTrash");
    case Operation::Size:
        return QStringLiteral("size");
    }

    return {};
}

QString Task::destinationName() const
{
    const auto url = m_request.destination.adjusted(QUrl::StripTrailingSlash);
    return url.fileName().isEmpty() ? url.path() : url.fileName();
}

TaskController *TaskController::instance()
{
    static auto *controller = new TaskController(QCoreApplication::instance());
    return controller;
}

TaskController::~TaskController()
{
    for (auto *task : findChildren<Task *>(QString(), Qt::FindDirectChildrenOnly))
        task->cancel();
}

Task *TaskController::submit(const TaskRequest &request)
{
    return submit(request, false);
}

// A fresh operation forks the history: what was undone before it can no
// longer be redone. Undo and redo themselves keep the other stack.
Task *TaskController::submit(const TaskRequest &request, bool keepRedo)
{
    if (!keepRedo && !m_redo.isEmpty()) {
        m_redo.clear();
        emit historyChanged();
    }

    auto *task = new Task(request, this);

    connect(task, &Task::result, this, [this, request](Task *task) {
        const auto &result = task->outcome();

        if (request.operation == Operation::Trash && !result.outputs.isEmpty()) {
            m_undo.append({Operation::Restore, result.outputs});
            while (m_undo.size() > kMaxHistory)
                m_undo.removeFirst();
            emit historyChanged();
        }

        emit taskFinished(task);
        task->deleteLater();
    });

    emit taskAdded(task);
    QTimer::singleShot(0, task, &Task::start);
    return task;
}

Task *TaskController::undo()
{
    if (m_undo.isEmpty())
        return nullptr;

    const auto request = m_undo.takeLast();
    emit historyChanged();
    auto *task = submit(request, true);

    connect(task, &Task::result, this, [this, request](Task *done) {
        const auto &result = done->outcome();
        if (!result.outputs.isEmpty())
            m_redo.append({Operation::Trash, result.outputs});

        if (done->error()) {
            auto remaining = request;
            for (const auto &source : result.completedSources)
                remaining.sources.removeAll(source);
            if (!remaining.sources.isEmpty())
                m_undo.append(remaining);
        }

        emit historyChanged();
    });

    return task;
}

// Redoing a trash goes through the normal path, which records its undo again.
Task *TaskController::redo()
{
    if (m_redo.isEmpty())
        return nullptr;

    const auto request = m_redo.takeLast();
    emit historyChanged();
    auto *task = submit(request, true);

    connect(task, &Task::result, this, [this, request](Task *done) {
        if (done->error()) {
            auto remaining = request;
            for (const auto &source : done->outcome().completedSources)
                remaining.sources.removeAll(source);

            if (!remaining.sources.isEmpty()) {
                m_redo.append(remaining);
                emit historyChanged();
            }
        }
    });

    return task;
}

Task *copy(const QList<QUrl> &sources, const QUrl &destination)
{
    return TaskController::instance()->submit(
            {Operation::Copy, sources, destination, ConflictPolicy::Ask});
}

Task *move(const QList<QUrl> &sources, const QUrl &destination)
{
    return TaskController::instance()->submit(
            {Operation::Move, sources, destination, ConflictPolicy::Ask});
}

Task *del(const QList<QUrl> &sources)
{
    return TaskController::instance()->submit({Operation::Delete, sources});
}

Task *mkdir(const QUrl &destination)
{
    return TaskController::instance()->submit({Operation::Mkdir, {}, destination});
}

Task *trash(const QList<QUrl> &sources)
{
    return TaskController::instance()->submit({Operation::Trash, sources});
}

Task *restoreFromTrash(const QList<QUrl> &sources)
{
    return TaskController::instance()->submit(
            {Operation::Restore, sources, {}, ConflictPolicy::Ask});
}

Task *emptyTrash()
{
    return TaskController::instance()->submit({Operation::EmptyTrash});
}

Task *paste(const QMimeData *data, const QUrl &destination)
{
    const bool cut = data && data->hasFormat("application/x-cutefish-cutselection");

    return TaskController::instance()->submit({cut ? Operation::Move : Operation::Copy,
                                               data ? data->urls() : QList<QUrl>(), destination,
                                               ConflictPolicy::Ask});
}

Task *drop(const QMimeData *data, const QUrl &destination, Qt::DropAction action)
{
    return TaskController::instance()->submit(
            {action == Qt::LinkAction       ? Operation::Link
                     : action == Qt::MoveAction ? Operation::Move
                                                : Operation::Copy,
             data ? data->urls() : QList<QUrl>(), destination, ConflictPolicy::Ask});
}

} // namespace Cutefish::IO
