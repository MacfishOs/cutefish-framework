// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>
#include <QStringList>

class QMimeType;

// The user's mimeapps.list and xdg-terminals.list, edited line by line so comments and
// entries this code does not touch survive.
namespace MimeApps {
// The user's files under $XDG_CONFIG_HOME, which every choice is written to.
QString userListPath();
QString userTerminalsListPath();
// Every mimeapps.list that applies, the user's first: the config directories, then the
// applications directories, each with the current desktops' own list before the plain one.
QStringList listPaths();

QStringList associations(const QString &path, const QString &group, const QString &mimeType);
bool setDefault(const QString &path, const QString &mimeType, const QString &desktopId);

// Desktop file paths of the installed applications that open the type: those the lists add,
// then those listing it themselves, else its parents a generation at a time, earliest
// installed first. An application a list removes is left out, unless an earlier list adds it.
QStringList applications(const QMimeType &mimeType, const QStringList &paths);
// The installed application the first list naming one chooses for the type, or an empty
// string. An application removed by an earlier list is not chosen by a later one.
QString chosenApplication(const QStringList &paths, const QMimeType &mimeType);
// What opens the type: the choice in the lists, else the first of applications().
QString defaultApplication(const QStringList &paths, const QMimeType &mimeType);

// xdg-terminals.list of the xdg-terminal-exec spec: desktop entry IDs, most preferred first.
QString defaultTerminal(const QString &path);
bool setDefaultTerminal(const QString &path, const QString &desktopId);
} // namespace MimeApps
