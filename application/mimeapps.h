// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

// The user's mimeapps.list and xdg-terminals.list, edited line by line so comments and
// entries this code does not touch survive.
namespace MimeApps {
// The user's files under $XDG_CONFIG_HOME, which every choice is written to.
QString userListPath();
QString userTerminalsListPath();

QStringList associations(const QString &path, const QString &group, const QString &mimeType);
bool setDefault(const QString &path, const QString &mimeType, const QString &desktopId);

// xdg-terminals.list of the xdg-terminal-exec spec: desktop entry IDs, most preferred first.
QString defaultTerminal(const QString &path);
bool setDefaultTerminal(const QString &path, const QString &desktopId);
} // namespace MimeApps
