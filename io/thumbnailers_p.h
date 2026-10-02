// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QHash>
#include <QImage>
#include <QList>
#include <QMutex>
#include <QSize>
#include <QStringList>

/**
 * The external thumbnailers installed as `thumbnailers/*.thumbnailer` in the
 * XDG data directories, as GNOME and Xfce use them. Entries in earlier
 * directories win, and installing or removing one is picked up within seconds.
 */
class Thumbnailers
{
public:
    static Thumbnailers *self();

    bool contains(const QString &mimeType);

    /** Runs the thumbnailer for @p mimeType on @p path; null on failure or timeout. */
    QImage generate(const QString &path, const QString &mimeType, const QSize &size);

private:
    QStringList command(const QString &mimeType);
    void refreshLocked();

    QMutex m_mutex;
    QElapsedTimer m_checked;
    QList<std::pair<QString, QDateTime>> m_directories;
    QHash<QString, QStringList> m_commands;
};
