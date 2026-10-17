// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>
#include <string_view>

namespace cutefish::files::internal {

// RFC 1321 MD5 in lower-case hex, which names thumbnail cache entries.
std::string md5Hex(std::string_view data);

} // namespace cutefish::files::internal
