#include "operationcontext.h"
#include <cerrno>

namespace Cutefish::IO {
void OperationContext::checkpoint(const QString &path) const
{
    if (canceled.load() || (cancelFlag && cancelFlag->load()))
        throw Failure(ECANCELED, path);
    if (currentItem && !path.isEmpty())
        currentItem(path);
}

void OperationContext::advance(quint64 amount)
{
    bytes += amount;
    if (progress)
        progress(bytes, files);
}

} // namespace Cutefish::IO
