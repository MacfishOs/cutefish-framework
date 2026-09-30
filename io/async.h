#pragma once

#include <QFuture>
#include <QPromise>
#include <QThreadPool>
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

} // namespace Cutefish::IO::Internal
