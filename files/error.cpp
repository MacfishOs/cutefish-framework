// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files/error.h"

#include <cstring>

namespace cutefish::files {

Failure::Failure(int code, const std::string& path)
    : std::runtime_error(std::strerror(code))
    , error{code, path, std::strerror(code)}
{
}

} // namespace cutefish::files
