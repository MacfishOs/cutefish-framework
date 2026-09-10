# Cutefish::IO

Filesystem infrastructure for Linux desktop applications, written in C++20 with
QtCore integration. It provides local file operations, asynchronous tasks,
directory models, filesystem watching, and trash management.

```cmake
find_package(CutefishIO REQUIRED)
target_link_libraries(app PRIVATE Cutefish::IO)
```

The module can also be added with `add_subdirectory(io)` when building from the
framework source tree.

## Structure

| Component | Responsibility |
| --- | --- |
| `error` | Filesystem errors with an errno code, path, and message |
| `filepath` | Byte-valued paths and lexical path operations |
| `filesystemurl` | Local and trash URL validation and resolution |
| `fileitem` | File metadata, MIME information, and display formatting |
| `fileenumerator` | Directory enumeration and recursive size calculation |
| `operationcontext` | Cancellation and callbacks used by synchronous operations |
| `localfilesystem` | Copy, move, link, delete, and directory creation |
| `trash` | Trash metadata, restore, and empty-trash operations |
| `filewatcher` | Linux inotify integration |
| `directorylister` | Asynchronous directory snapshots and refresh scheduling |
| `directorymodel` | Qt item model with stable indexes for surviving entries |
| `task` and `taskprogress` | Requests, execution, progress snapshots, and task control |

`filesystem.h` and `directory.h` are convenience umbrella headers. Applications
can include individual component headers to keep their dependencies explicit.
`posix_p.h` contains shared descriptor helpers; `async.h` dispatches worker jobs.
These two headers are private implementation details.

Synchronous filesystem operations do not require an event loop. Recursive
operations use directory descriptors and do not traverse symbolic links.
Asynchronous tasks run on a thread pool and deliver signals on their owning
thread. Submit requests and access task/controller objects from that thread.
Controller-owned tasks start on the next event-loop iteration and are deleted
after their result signal. Copy the outcome in the result handler when it must
be retained.

Directory watching coalesces events and rescans on queue overflow. Parent watches
allow recovery when a displayed directory is renamed, deleted, or recreated.
When a watch cannot be established, listing uses a polling fallback. Stale
snapshots from previous locations are discarded.

## Progress and cancellation

`Task::progressInfo` exposes the preparation state, current path, completed and
total bytes/items, and elapsed execution time. Preparation calculates totals
without modifying files. When a source cannot be inspected in advance, the task
continues with unknown totals and lets the actual operation report any error.
Preparation is cancellable, including while scanning recursive directories.

The file manager uses these snapshots for its operation dialog: an operation and
destination heading, current filename, progress bar, formatted size or item
counts, and an estimated remaining time. Time estimates are shown only after
work has begun; preparation and finalization have separate messages. Conflicts
and cancellation retain their own states instead of appearing as stalled copies.
The existing `progress(bytes, files)` signal remains available for consumers such
as the selected-file size display.

Cancellation is cooperative between copy chunks and recursive entries. Copies
stage a sibling before publication, so canceling an unpublished copy removes its
staging data and preserves an existing destination. Multi-source operations and
directory merges can commit some entries before cancellation or failure.
Permanent deletion does not restore entries already removed.

## Copy, move, and conflicts

Files use exclusive creation, bounded reads/writes, permission and timestamp
preservation, and `fsync`. Sparse extents are retained where supported.
`renameat2(RENAME_NOREPLACE)` prevents accidental conflict overwrites. Conflict
policies are fail, skip, keep both, replace, and ask. Replacing directories merges
their contents while retaining unrelated destination entries.

An ask-policy task waits for `resolveConflict()` or `cancel()`. Without a conflict
handler, it fails rather than waiting indefinitely. Synchronous callers can
supply a resolver through `OperationContext`.

Moves attempt a rename first. Cross-filesystem moves publish a complete copy
before removing the source. Cancellation is deferred during that final removal;
a removal error can leave both copies or a partly removed source. Operations do
not provide transactional rollback.

Clipboard operations recognize Cutefish and interoperable cut-selection MIME
markers. Drops support copying, moving, and symbolic links.

## Trash

Trash uses the standard local data directory's `Trash/files` and `Trash/info`
layout. Metadata contains the percent-encoded original absolute path and deletion
date. Exclusive reservations avoid name collisions, and an advisory lock
serializes trash mutations across Cutefish processes. Restore applies the chosen
conflict policy. Empty trash removes payloads and metadata, including orphaned
metadata. Successful trash operations are recorded for in-process undo.

Current scope:

- Supported URL schemes are local `file:` and home `trash:`.
- Per-mount `.Trash/$uid` and `.Trash-$uid` stores are not aggregated. New trash
  operations use the home store, with a copy/remove fallback across filesystems.
- Restore requires the original parent directory to exist.
- Copy preserves ordinary permission bits and access/modification timestamps;
  ownership, ACLs, extended attributes, and hard-link topology are not cloned.
- The Qt URL/model boundary assumes filenames representable in Qt's filesystem
  encoding. Arbitrary invalid UTF-8 names cannot reliably round-trip through it.
- Undo is not persistent. There is no trash crash-recovery journal, transactional
  rollback, or recursive inotify tree subscription. Watching covers the displayed
  directory. Operations are not a security boundary against concurrent malicious
  changes to ancestor paths or mount points.

## Design references

Chromium revision: `7acd40ddefff49c9d36144999aafdbb674dbd07a`.

Chromium separates paths, file handles, enumeration, utilities, and platform
watchers. The following files informed the component boundaries and POSIX
behavior:

- `base/files/file_path.h` and `file_path.cc`: byte-valued paths and lexical operations.
- `base/files/file.h` and `file_posix.cc`: descriptor ownership, open flags, and large offsets.
- `base/files/file_enumerator_posix.cc`: enumeration and explicit symlink treatment.
- `base/files/file_util_posix.cc`: recursive deletion, exclusive copies, and move fallbacks.
- `base/files/file_path_watcher.h` and `file_path_watcher_inotify.cc`: watch invalidation and overflow handling.

Cutefish's task/controller API is designed around Qt's event loop and signal
model. It does not claim behavioral parity with Chromium's Ash I/O tasks.

## Building and testing

From the framework source root:

```sh
cmake -S io -B /tmp/cutefish-io-module-build -DBUILD_TESTING=ON
cmake --build /tmp/cutefish-io-module-build -j6
ctest --test-dir /tmp/cutefish-io-module-build --output-on-failure
```

The test harness uses QtTest and QtGui for model testing. Run permission tests
as an ordinary user. To exercise real cross-filesystem moves, provide a writable
second filesystem:

```sh
CUTEFISH_IO_CROSS_DEVICE_ROOT=/dev/shm \
  ctest --test-dir /tmp/cutefish-io-module-build --output-on-failure
```

Tests cover metadata, recursive operations, conflicts, cancellation, permissions,
symlinks, large sparse files, trash, model refresh, task lifetime, and progress.
File-manager integration tests additionally exercise FolderModel actions and
operation-dialog presentation.
