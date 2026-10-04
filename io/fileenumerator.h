#pragma once

#include "fileitem.h"
#include "operationcontext.h"

#include <functional>

namespace Cutefish::IO {

class FileEnumerator
{
public:
    static FileItemList list(const QUrl &url, bool hidden = true,
                             OperationContext *context = nullptr);
    // The prepared files at any depth under url, nearest first, whose names contain
    // every word, ignoring case. Folders that cannot be read are skipped; links and
    // kernel file systems are not followed. found sees each file before it is added.
    static FileItemList search(const QUrl &url, const QStringList &words, bool hidden,
                               OperationContext *context = nullptr,
                               const std::function<void(FileItem &)> &found = {});
    static quint64 size(const QString &path, OperationContext &context);
};

} // namespace Cutefish::IO
