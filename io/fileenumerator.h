#pragma once
#include "fileitem.h"
#include "operationcontext.h"

namespace Cutefish::IO {
class FileEnumerator
{
public:
    static FileItemList list(const QUrl &url, bool hidden = true,
                             OperationContext *context = nullptr);
    static quint64 size(const QString &path, OperationContext &context);
};
} // namespace Cutefish::IO
