// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "filewatcher.h"

#include <QFutureWatcher>
#include <QTimer>

namespace Cutefish::IO {

// Whether anything is in the trash: the home trash or that of any mounted volume.
class TrashMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool empty READ isEmpty NOTIFY emptyChanged)

public:
    explicit TrashMonitor(QObject *parent = nullptr);

    bool isEmpty() const
    {
        return m_empty;
    }

signals:
    void emptyChanged();

private:
    void refresh();

    struct State
    {
        bool empty = true;
        QStringList paths;
        QHash<QString, QList<QByteArray>> entries;
    };

    FileWatcher m_watch;
    QTimer m_debounce;
    QTimer m_retry;
    QFutureWatcher<State> *m_future = nullptr;
    bool m_pending = false;
    bool m_empty = true;
};

} // namespace Cutefish::IO
