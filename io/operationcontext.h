#pragma once
#include "error.h"
#include <atomic>
#include <functional>

namespace Cutefish::IO {
enum class ConflictPolicy {
    Fail,
    Skip,
    KeepBoth,
    Replace,
    Ask
};

struct OperationContext {
    std::atomic_bool canceled {false};
    const std::atomic_bool *cancelFlag = nullptr;
    std::function<void(const QString &)> currentItem;
    quint64 bytes = 0;
    quint64 files = 0;
    std::function<void(quint64, quint64)> progress;
    std::function<ConflictPolicy(const QString &source, const QString &destination)> conflict;
    void checkpoint(const QString &path = {}) const;
    void advance(quint64 amount);
};

} // namespace Cutefish::IO
