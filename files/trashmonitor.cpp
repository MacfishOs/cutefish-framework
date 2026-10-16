// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "files/trashmonitor.h"

#include <chrono>
#include <dirent.h>
#include <sys/stat.h>

#include "trashdirectory_p.h"

namespace cutefish::files {

using namespace std::chrono_literals;

static bool hasEntries(const std::string& path)
{
    DIR* dir = ::opendir(path.c_str());
    if (!dir)
        return false;

    bool found = false;
    while (const dirent* entry = ::readdir(dir)) {
        const std::string_view name = entry->d_name;
        if (name != "." && name != "..") {
            found = true;
            break;
        }
    }
    ::closedir(dir);
    return found;
}

TrashMonitor::TrashMonitor()
{
    m_debounce.setSingleShot(true);
    m_debounce.setInterval(40ms);
    m_retry.setInterval(2000ms);

    m_watchChanged = m_watch.changed.connect([this] {
        m_debounce.start();
    });
    m_debounce.onTimeout = [this] {
        refresh();
    };
    m_retry.onTimeout = [this] {
        refresh();
    };

    refresh();
}

void TrashMonitor::refresh()
{
    if (m_job.isPending()) {
        m_pending = true;
        return;
    }

    // Looking for the trash folders reads the mount table and stats each mount.
    m_job = base::ThreadPool::global().run(
        [] {
            State state;
            Internal::trashWatches(&state.paths, &state.entries);

            for (const auto& trash : Internal::trashDirectories()) {
                if (hasEntries(trash.path + "/files")) {
                    state.empty = false;
                    break;
                }
            }

            return state;
        },
        [this](State state) {
            // Mounts and unmounts add and remove volumes' trash, so a folder found can be
            // gone by now.
            std::vector<std::string> paths;
            for (const auto& path : state.paths) {
                struct stat st;
                if (::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode))
                    paths.push_back(path);
            }

            if (m_watch.watch(paths, state.entries, true))
                m_retry.stop();
            else
                m_retry.start();

            if (state.empty != m_empty) {
                m_empty = state.empty;
                emptyChanged.emit();
            }

            if (m_pending) {
                m_pending = false;
                refresh();
            }
        });
}

} // namespace cutefish::files
