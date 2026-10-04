// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QThreadPool>

#include <utility>

namespace Cutefish::IO {

// Drops value on a pool thread: freeing a listing of 50,000 items or its URL index
// takes the GUI thread tens of milliseconds. value must own nothing tied to the
// calling thread, such as QObjects or pixmaps.
template <typename T> void releaseInBackground(T value)
{
    QThreadPool::globalInstance()->start([value = std::move(value)]() mutable {
        value = T();
    });
}

} // namespace Cutefish::IO
