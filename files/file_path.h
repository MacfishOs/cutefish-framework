// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <string>

namespace cutefish::files {

// A path as the kernel takes it: bytes, never empty, no NUL.
class FilePath
{
public:
    explicit FilePath(std::string bytes);

    const std::string& bytes() const { return m_bytes; }

    FilePath parent() const;
    FilePath child(const std::string& name) const;

private:
    std::string m_bytes;
};

} // namespace cutefish::files
