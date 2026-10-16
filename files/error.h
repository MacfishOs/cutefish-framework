// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <stdexcept>
#include <string>

namespace cutefish::files {

struct Error
{
    // An errno value; 0 for none.
    int code = 0;
    std::string path;
    std::string message;

    explicit operator bool() const { return code != 0; }
};

class Failure : public std::runtime_error
{
public:
    Failure(int code, const std::string& path);

    Error error;
};

} // namespace cutefish::files
