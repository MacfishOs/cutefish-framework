// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

namespace Cutefish::IO::Internal {

// A trash directory, holding files/ and info/. Its items are trash:/<id>/<name>.
struct TrashDirectory
{
    QString id;
    QString path;
    // The mount point of a volume's trash, which relative info paths start from;
    // empty for the home trash.
    QString topdir;
};

TrashDirectory homeTrash();
// $topdir/.Trash/$uid when shared, else $topdir/.Trash-$uid.
TrashDirectory volumeTrash(const QString &topdir, bool shared);

// The trash a URL below trash:/ is in, and in inside the path below its files/;
// throws EINVAL for trash:/ itself or an id that names no trash.
TrashDirectory trashDirectory(const QUrl &url, QString *inside = nullptr);

// False when the mount table lists topdir with a type that holds no trash.
bool volumeTakesTrash(const QString &topdir);

// Creates a volume's trash the way the trash specification asks, or checks the one
// there; returns whether items can be moved into it.
bool prepareVolumeTrash(const TrashDirectory &trash);

// The home trash, whether it exists or not, then each mounted volume's trash that
// does. mountPoints receives the mount points looked in.
QList<TrashDirectory> trashDirectories(QStringList *mountPoints = nullptr);

// What the trash listing watches: the files/ folders that exist, and the entries
// whose appearance would add a trash or a files/ folder.
void trashWatches(QStringList *paths, QHash<QString, QList<QByteArray>> *entries);

struct TrashInfo
{
    // Absolute; empty when the info file names no usable path.
    QString path;
    QDateTime deletionTime;
};

// Reads the info file of the item name at the top of trash; false when it cannot.
bool readTrashInfo(const TrashDirectory &trash, const QString &name, TrashInfo *info);

// Tests list mounts from a file of their own, so they never touch the trash on the
// volumes of the machine they run on.
void setMountTable(const QString &path);

} // namespace Cutefish::IO::Internal
