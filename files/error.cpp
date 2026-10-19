// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: BSD-3-Clause

#include "cutefish/files/error.h"

#include <cstring>

namespace cutefish::files {

Failure::Failure(int code, const std::string& path)
    : std::runtime_error(std::strerror(code))
    , error{code, path, std::strerror(code)}
{
}

Failure::Failure(int code, const std::string& path, const std::string& message)
    : std::runtime_error(message)
    , error{code, path, message}
{
}

} // namespace cutefish::files
