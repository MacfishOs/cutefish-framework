#pragma once
#include "fileitem.h"
#include "filesystemurl.h"
#include "operationcontext.h"

namespace Cutefish::IO {
class LocalFileSystem
{
public:
    static FileItemList enumerate(const QUrl &url, bool hidden = true);
    static quint64 directorySize(const QString &path, OperationContext &context);
    static QString copy(const QString &source, const QString &destination, ConflictPolicy policy,
                        OperationContext &context);
    static QString link(const QString &source, const QString &destination, ConflictPolicy policy,
                        OperationContext &context);
    static QString move(const QString &source, const QString &destination, ConflictPolicy policy,
                        OperationContext &context);
    static void remove(const QString &path, OperationContext &context);
    static void remove(const QUrl &url, OperationContext &context);
    static void mkdir(const QString &path);
    static QUrl trash(const QUrl &url, OperationContext &context);
    static QUrl restore(const QUrl &url, ConflictPolicy policy, OperationContext &context);
    static void emptyTrash(OperationContext &context);
};

} // namespace Cutefish::IO
