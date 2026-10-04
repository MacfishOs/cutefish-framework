#pragma once

#include <QFuture>
#include <QMutex>
#include <QPromise>
#include <QSemaphore>
#include <QThread>
#include <QThreadPool>
#include <atomic>
#include <exception>
#include <memory>
#include <type_traits>

namespace Cutefish::IO::Internal {

template <typename Function> auto runAsync(Function function)
{
    using Result = std::invoke_result_t<Function>;

    auto promise = std::make_shared<QPromise<Result>>();
    promise->start();
    auto future = promise->future();

    QThreadPool::globalInstance()->start([promise, function = std::move(function)]() mutable {
        try {
            promise->addResult(function());
        } catch (...) {
            promise->setException(std::current_exception());
        }

        promise->finish();
    });

    return future;
}

// Folders from this size on spread even a refresh over threads.
constexpr qsizetype LargeFolder = 20000;

// Helpers for work split into blocks: half the logical CPUs work as fast as all of
// them for less CPU time, and leave the GUI and the compositor theirs.
inline qsizetype blockHelpers(qsizetype blocks)
{
    return qMax(qsizetype(0), qMin(qsizetype(QThread::idealThreadCount() / 2 - 1), blocks - 1));
}

// Runs body on this thread and on up to helpers pool threads free right now; body
// shares the work out itself and stops once stop is set, after an exception. The
// first exception is rethrown when every run has returned, as the helpers use this
// stack.
template <typename Body> void runOnFreeThreads(qsizetype helpers, Body body)
{
    std::atomic_bool stop = false;
    QMutex failureMutex;
    std::exception_ptr failure;

    const auto run = [&] {
        try {
            body(stop);
        } catch (...) {
            const QMutexLocker locker(&failureMutex);
            if (!failure)
                failure = std::current_exception();
            stop = true;
        }
    };

    QSemaphore finished;
    qsizetype started = 0;

    // Only threads free now: queued helpers could wait behind tasks that wait for them.
    while (started < helpers
           && QThreadPool::globalInstance()->tryStart([&] {
                  run();
                  finished.release();
              }))
        ++started;

    run();
    finished.acquire(started);

    if (failure)
        std::rethrow_exception(failure);
}

} // namespace Cutefish::IO::Internal
