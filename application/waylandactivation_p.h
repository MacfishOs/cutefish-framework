// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CUTEFISH_WAYLANDACTIVATION_P_H
#define CUTEFISH_WAYLANDACTIVATION_P_H

#include <QString>

namespace Cutefish {

namespace WaylandActivation {

// An xdg-activation token for whatever this process is about to start, taken
// from its focused window and the latest user input. Empty when there is none.
QString requestToken();

} // namespace WaylandActivation

} // namespace Cutefish

#endif
