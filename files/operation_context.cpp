// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "cutefish/files/operation_context.h"

#include <cerrno>

namespace cutefish::files {

void OperationContext::checkpoint(const std::string& path) const
{
    if (canceled.load() || (cancelFlag && cancelFlag->load()))
        throw Failure(ECANCELED, path);

    if (currentItem && !path.empty())
        currentItem(path);
}

void OperationContext::advance(uint64_t amount)
{
    bytes += amount;

    if (progress)
        progress(bytes, files);
}

} // namespace cutefish::files
