// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "files/fileitem.h"
#include "files/operationcontext.h"

namespace cutefish::files {

class FileEnumerator
{
public:
    // The items of a folder, or of every trash for trash:/, not yet prepared.
    static FileItemList list(const Url& url, bool hidden = true, OperationContext* context = nullptr);
    // The prepared files at any depth under url, nearest first, whose names contain
    // every word, ignoring case. Folders that cannot be read are skipped; links and
    // kernel file systems are not followed. found sees each file before it is added.
    static FileItemList search(const Url& url, const std::vector<std::string>& words, bool hidden,
                               OperationContext* context = nullptr,
                               const std::function<void(FileItem&)>& found = {});
    static uint64_t size(const std::string& path, OperationContext& context);
};

} // namespace cutefish::files
