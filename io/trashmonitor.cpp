// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "trashmonitor.h"

#include "async.h"
#include "trashdirectory_p.h"

#include <QDirIterator>
#include <QFileInfo>

namespace Cutefish::IO {

TrashMonitor::TrashMonitor(QObject *parent)
    : QObject(parent)
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(40);
    m_retry.setInterval(2000);

    connect(&m_watch, &FileWatcher::changed, &m_debounce, qOverload<>(&QTimer::start));
    connect(&m_debounce, &QTimer::timeout, this, &TrashMonitor::refresh);
    connect(&m_retry, &QTimer::timeout, this, &TrashMonitor::refresh);

    refresh();
}

void TrashMonitor::refresh()
{
    if (m_future) {
        m_pending = true;
        return;
    }

    m_future = new QFutureWatcher<State>(this);

    connect(m_future, &QFutureWatcher<State>::finished, this, [this] {
        const State state = m_future->result();
        m_future->deleteLater();
        m_future = nullptr;

        // Mounts and unmounts add and remove volumes' trash, so a folder found can be
        // gone by now.
        QStringList paths;
        for (const auto &path : state.paths) {
            if (QFileInfo(path).isDir())
                paths.append(path);
        }

        if (m_watch.watch(paths, state.entries, true))
            m_retry.stop();
        else
            m_retry.start();

        if (state.empty != m_empty) {
            m_empty = state.empty;
            emit emptyChanged();
        }

        if (m_pending) {
            m_pending = false;
            refresh();
        }
    });

    // Looking for the trash folders reads the mount table and stats each mount.
    m_future->setFuture(Internal::runAsync([] {
        State state;
        Internal::trashWatches(&state.paths, &state.entries);

        for (const auto &trash : Internal::trashDirectories()) {
            const QDirIterator files(trash.path + "/files",
                                     QDir::AllEntries | QDir::Hidden | QDir::System
                                             | QDir::NoDotAndDotDot);
            if (files.hasNext()) {
                state.empty = false;
                break;
            }
        }

        return state;
    }));
}

} // namespace Cutefish::IO
