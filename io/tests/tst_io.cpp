#include "directory.h"
#include "task.h"
#include <QAbstractItemModelTester>
#include <QCryptographicHash>
#include <QPersistentModelIndex>
#include <QPixmap>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

using namespace Cutefish::IO;

namespace {
void writeFile(const QString &path, const QByteArray &data = "contents")
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        throw Failure(EIO, path);
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw Failure(EIO, path);
    return file.readAll();
}

template <class F> int errorCode(F action)
{
    try {
        action();
    } catch (const Failure &failure) {
        return failure.error.code;
    }
    return 0;
}

QUrl local(const QString &path)
{
    return QUrl::fromLocalFile(path);
}
} // namespace

class IOTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_data;
private slots:

    void initTestCase()
    {
        // The model tester probes GUI decoration types even for a Core-only model.
        qRegisterMetaType<QPixmap>();
        QVERIFY(m_data.isValid());
        qputenv("XDG_DATA_HOME", m_data.path().toUtf8());
        QCOMPARE(FileSystemUrl::trashRoot(), m_data.path() + "/Trash");
    }

    void cleanup()
    {
        OperationContext ctx;
        LocalFileSystem::emptyTrash(ctx);
    }

    void pathsAndUrls()
    {
        QCOMPARE(FilePath("/one/two").parent().bytes(), QByteArray("/one"));
        QCOMPARE(FilePath("/one").child("a% b").bytes(), QByteArray("/one/a% b"));
        QCOMPARE(FilePath(QByteArray("/a/\xff", 4)).bytes(), QByteArray("/a/\xff", 4));
        QCOMPARE(errorCode([] {
                     FilePath(QByteArray("x\0y", 3));
                 }),
                 EINVAL);
        QCOMPARE(errorCode([] {
                     FilePath("/a").child("../x");
                 }),
                 EINVAL);
        QCOMPARE(errorCode([] {
                     FileSystemUrl(QUrl("https://example.com/file"));
                 }),
                 ENOTSUP);
        QCOMPARE(errorCode([] {
                     FileSystemUrl(QUrl("file://remote/tmp/a"));
                 }),
                 ENOTSUP);
        QCOMPARE(errorCode([] {
                     FileSystemUrl(QUrl("trash:/../a"));
                 }),
                 EINVAL);
        QCOMPARE(errorCode([] {
                     FileSystemUrl(QUrl("trash:/%2E%2E/a"));
                 }),
                 EINVAL);
        QCOMPARE(FileSystemUrl(local("/tmp/a % # é")).localPath(), QString("/tmp/a % # é"));
        QCOMPARE(upUrl(local("/tmp/a")), local("/tmp"));
    }

    void enumerationAndMetadata()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("file.txt"), "hello");
        writeFile(dir.filePath(".hidden"));
        QVERIFY(QDir(dir.path()).mkdir("sub"));
        QVERIFY(::symlink("missing", QFile::encodeName(dir.filePath("broken"))) == 0);
        const auto items = LocalFileSystem::enumerate(local(dir.path()));
        QCOMPARE(items.size(), 4);
        QCOMPARE(LocalFileSystem::enumerate(local(dir.path()), false).size(), 3);
        FileItem item(local(dir.filePath("file.txt")));
        QCOMPARE(item.size(), 5);
        QVERIFY(item.isFile());
        QVERIFY(item.time(FileItem::ModificationTime).isValid());
        QCOMPARE(item.mimetype(), QString("text/plain"));
        QVERIFY(FileItem(local(dir.filePath("broken"))).isLink());
        QCOMPARE(errorCode([&] {
                     LocalFileSystem::enumerate(local(dir.filePath("missing")));
                 }),
                 ENOENT);
    }

    void copyRecursiveSymlinksAndPermissions()
    {
        QTemporaryDir dir;
        const auto source = dir.filePath("source");
        QVERIFY(QDir().mkpath(source + "/a/b"));
        writeFile(source + "/a/b/file", QByteArray("hello\0world", 11));
        QVERIFY(::chmod(QFile::encodeName(source + "/a/b/file"), 0751) == 0);
        QVERIFY(::symlink("..", QFile::encodeName(source + "/a/loop")) == 0);
        QVERIFY(::symlink("absent", QFile::encodeName(source + "/broken")) == 0);
        QVERIFY(::symlink("/etc/passwd", QFile::encodeName(source + "/external")) == 0);
        OperationContext ctx;
        const auto destination = dir.filePath("target");
        QCOMPARE(LocalFileSystem::copy(source, destination, ConflictPolicy::Fail, ctx),
                 destination);
        QCOMPARE(readFile(destination + "/a/b/file"), readFile(source + "/a/b/file"));
        struct stat st;
        QCOMPARE(::lstat(QFile::encodeName(destination + "/a/b/file"), &st), 0);
        QCOMPARE(st.st_mode & 0777, mode_t(0751));
        QCOMPARE(::lstat(QFile::encodeName(destination + "/broken"), &st), 0);
        QVERIFY(S_ISLNK(st.st_mode));
        QVERIFY(QFileInfo(destination + "/a/loop").isSymbolicLink());
        LocalFileSystem::remove(destination, ctx);
        QVERIFY(!QFileInfo::exists(destination));
        QVERIFY(QFileInfo::exists("/etc/passwd"));
        QVERIFY(QFileInfo::exists(source + "/a/b/file"));
    }

    void conflicts()
    {
        QTemporaryDir dir;
        auto source = dir.filePath("source.txt"), target = dir.filePath("target.txt");
        writeFile(source, "new");
        writeFile(target, "old");
        OperationContext ctx;
        QCOMPARE(errorCode([&] {
                     LocalFileSystem::copy(source, target, ConflictPolicy::Fail, ctx);
                 }),
                 EEXIST);
        QCOMPARE(readFile(target), QByteArray("old"));
        QVERIFY(LocalFileSystem::copy(source, target, ConflictPolicy::Skip, ctx).isEmpty());
        QCOMPARE(LocalFileSystem::copy(source, target, ConflictPolicy::KeepBoth, ctx),
                 dir.filePath("target (1).txt"));
        QCOMPARE(readFile(dir.filePath("target (1).txt")), QByteArray("new"));
        QCOMPARE(LocalFileSystem::copy(source, target, ConflictPolicy::Replace, ctx), target);
        QCOMPARE(readFile(target), QByteArray("new"));
        QVERIFY(!QDir(dir.path())
                         .entryList({".cutefish-*"}, QDir::AllEntries | QDir::Hidden)
                         .size());
    }

    void directoryConflictPreservesDestination()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath("source/sub")));
        QVERIFY(QDir().mkpath(dir.filePath("target/sub")));
        writeFile(dir.filePath("target/sub/keep"));
        OperationContext ctx;
        writeFile(dir.filePath("source/sub/new"));
        LocalFileSystem::copy(dir.filePath("source"), dir.filePath("target"),
                              ConflictPolicy::Replace, ctx);
        QVERIFY(QFileInfo::exists(dir.filePath("target/sub/new")));
        QVERIFY(QFileInfo::exists(dir.filePath("target/sub/keep")));
    }

    void selfAndDescendantProtection()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("file"));
        QVERIFY(QDir(dir.path()).mkdir("sub"));
        QVERIFY(::symlink(QFile::encodeName(dir.path()), QFile::encodeName(dir.filePath("alias")))
                == 0);
        OperationContext ctx;
        QCOMPARE(errorCode([&] {
                     LocalFileSystem::copy(dir.filePath("file"), dir.filePath("file"),
                                           ConflictPolicy::Replace, ctx);
                 }),
                 EINVAL);
        QCOMPARE(errorCode([&] {
                     LocalFileSystem::copy(dir.path(), dir.filePath("sub/copy"),
                                           ConflictPolicy::Fail, ctx);
                 }),
                 EINVAL);
        QCOMPARE(errorCode([&] {
                     LocalFileSystem::move(dir.path(), dir.filePath("alias/copy"),
                                           ConflictPolicy::Fail, ctx);
                 }),
                 EINVAL);
        QCOMPARE(errorCode([&] {
                     LocalFileSystem::remove("/", ctx);
                 }),
                 EINVAL);
    }

    void destinationSymlinkIsNotFollowed()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("source"), "new");
        writeFile(dir.filePath("victim"), "unchanged");
        QVERIFY(::symlink("victim", QFile::encodeName(dir.filePath("target"))) == 0);
        OperationContext ctx;
        LocalFileSystem::copy(dir.filePath("source"), dir.filePath("target"),
                              ConflictPolicy::Replace, ctx);
        QCOMPARE(readFile(dir.filePath("victim")), QByteArray("unchanged"));
        QCOMPARE(readFile(dir.filePath("target")), QByteArray("new"));
        QVERIFY(!QFileInfo(dir.filePath("target")).isSymbolicLink());
    }

    void cancellationCleansStaging()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("source"), QByteArray(8 * 1024 * 1024, 'x'));
        writeFile(dir.filePath("target"), "original");
        OperationContext ctx;
        ctx.progress = [&](quint64 bytes, quint64) {
            if (bytes >= 1024 * 1024)
                ctx.canceled = true;
        };
        QCOMPARE(errorCode([&] {
                     LocalFileSystem::copy(dir.filePath("source"), dir.filePath("target"),
                                           ConflictPolicy::Replace, ctx);
                 }),
                 ECANCELED);
        QCOMPARE(readFile(dir.filePath("target")), QByteArray("original"));
        QCOMPARE(QFileInfo(dir.filePath("source")).size(), 8 * 1024 * 1024);
        QVERIFY(QDir(dir.path())
                        .entryList({".cutefish-*"}, QDir::AllEntries | QDir::Hidden)
                        .isEmpty());
    }

    void cancelBeforeMutation()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("source"));
        OperationContext ctx;
        ctx.canceled = true;
        QCOMPARE(errorCode([&] {
                     LocalFileSystem::move(dir.filePath("source"), dir.filePath("target"),
                                           ConflictPolicy::Fail, ctx);
                 }),
                 ECANCELED);
        QCOMPARE(errorCode([&] {
                     LocalFileSystem::remove(dir.filePath("source"), ctx);
                 }),
                 ECANCELED);
        QVERIFY(QFileInfo::exists(dir.filePath("source")));
    }

    void sizeDoesNotFollowSymlinks()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkdir("sub"));
        writeFile(dir.filePath("sub/file"), QByteArray(50, 'x'));
        QVERIFY(::symlink("..", QFile::encodeName(dir.filePath("sub/loop"))) == 0);
        OperationContext ctx;
        QCOMPARE(LocalFileSystem::directorySize(dir.path(), ctx), quint64(52));
        QCOMPARE(ctx.bytes, quint64(52));
        ctx.canceled = true;
        QCOMPARE(errorCode([&] {
                     LocalFileSystem::directorySize(dir.path(), ctx);
                 }),
                 ECANCELED);
    }

    void sparseLargeFile()
    {
        QTemporaryDir dir;
        QFile source(dir.filePath("source"));
        QVERIFY(source.open(QIODevice::WriteOnly));
        constexpr qint64 length = (qint64(5) << 30) + 17;
        QVERIFY(source.resize(length));
        QVERIFY(source.seek(length - 4));
        QCOMPARE(source.write("tail"), 4);
        source.close();
        OperationContext ctx;
        LocalFileSystem::copy(source.fileName(), dir.filePath("target"), ConflictPolicy::Fail, ctx);
        QCOMPARE(QFileInfo(dir.filePath("target")).size(), length);
        QCOMPARE(ctx.bytes, quint64(length));
        QFile target(dir.filePath("target"));
        QVERIFY(target.open(QIODevice::ReadOnly));
        QVERIFY(target.seek(length - 4));
        QCOMPARE(target.read(4), QByteArray("tail"));
        QVERIFY(target.seek(0));
        QCOMPARE(target.read(4), QByteArray(4, '\0'));
    }

    void specialFilesRejected()
    {
        QTemporaryDir dir;
        QVERIFY(::mkfifo(QFile::encodeName(dir.filePath("fifo")), 0600) == 0);
        OperationContext ctx;
        QCOMPARE(errorCode([&] {
                     LocalFileSystem::copy(dir.filePath("fifo"), dir.filePath("target"),
                                           ConflictPolicy::Fail, ctx);
                 }),
                 ENOTSUP);
        QVERIFY(!QFileInfo::exists(dir.filePath("target")));
    }

    void permissionErrors()
    {
        if (::geteuid() == 0)
            QSKIP("Permission checks require an unprivileged user");
        QTemporaryDir dir;
        writeFile(dir.filePath("source"));
        QVERIFY(QDir(dir.path()).mkdir("blocked"));
        QVERIFY(::chmod(QFile::encodeName(dir.filePath("blocked")), 0500) == 0);
        OperationContext ctx;
        QCOMPARE(errorCode([&] {
                     LocalFileSystem::copy(dir.filePath("source"), dir.filePath("blocked/target"),
                                           ConflictPolicy::Fail, ctx);
                 }),
                 EACCES);
        QVERIFY(::chmod(QFile::encodeName(dir.filePath("source")), 0000) == 0);
        QCOMPARE(errorCode([&] {
                     LocalFileSystem::copy(dir.filePath("source"), dir.filePath("target"),
                                           ConflictPolicy::Fail, ctx);
                 }),
                 EACCES);
        QVERIFY(::chmod(QFile::encodeName(dir.filePath("blocked")), 0700) == 0);
        QVERIFY(::chmod(QFile::encodeName(dir.filePath("source")), 0600) == 0);
    }

    void trashRestoreAndConflicts()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("é % #\n.txt");
        writeFile(path, "original");
        OperationContext ctx;
        const auto trashed = LocalFileSystem::trash(local(path), ctx);
        QVERIFY(!QFileInfo::exists(path));
        QCOMPARE(readFile(FileSystemUrl(trashed).localPath()), QByteArray("original"));
        writeFile(path, "replacement");
        QCOMPARE(errorCode([&] {
                     LocalFileSystem::restore(trashed, ConflictPolicy::Fail, ctx);
                 }),
                 EEXIST);
        QVERIFY(QFileInfo::exists(FileSystemUrl(trashed).localPath()));
        QCOMPARE(LocalFileSystem::restore(trashed, ConflictPolicy::Replace, ctx), local(path));
        QCOMPARE(readFile(path), QByteArray("original"));
        QVERIFY(LocalFileSystem::enumerate(QUrl("trash:/")).isEmpty());
        QVERIFY(!QFileInfo::exists(FileSystemUrl::trashRoot() + "/info/" + trashed.fileName()
                                   + ".trashinfo"));
    }

    void trashRecursiveDuplicateAndEmpty()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath("tree/sub")));
        writeFile(dir.filePath("tree/sub/file"));
        OperationContext ctx;
        const auto first = LocalFileSystem::trash(local(dir.filePath("tree")), ctx);
        QVERIFY(QDir().mkpath(dir.filePath("tree/sub")));
        const auto second = LocalFileSystem::trash(local(dir.filePath("tree")), ctx);
        QVERIFY(first != second);
        QCOMPARE(LocalFileSystem::enumerate(QUrl("trash:/")).size(), 2);
        LocalFileSystem::emptyTrash(ctx);
        QVERIFY(LocalFileSystem::enumerate(QUrl("trash:/")).isEmpty());
        QVERIFY(QDir(FileSystemUrl::trashRoot() + "/info").entryList(QDir::Files).isEmpty());
    }

    void longTrashNameAndOrphanMetadata()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath(QString(250, 'a'));
        writeFile(path);
        OperationContext ctx;
        const auto trashed = LocalFileSystem::trash(local(path), ctx);
        QCOMPARE(LocalFileSystem::restore(trashed, ConflictPolicy::Fail, ctx), local(path));
        writeFile(FileSystemUrl::trashRoot() + "/info/orphan.trashinfo",
                  "[Trash Info]\nPath=/missing\n");
        LocalFileSystem::emptyTrash(ctx);
        QVERIFY(QDir(FileSystemUrl::trashRoot() + "/info").entryList(QDir::Files).isEmpty());
    }

    void malformedTrashInfo()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("file"));
        OperationContext ctx;
        const auto url = LocalFileSystem::trash(local(dir.filePath("file")), ctx);
        writeFile(FileSystemUrl::trashRoot() + "/info/" + url.fileName() + ".trashinfo",
                  "[Trash Info]\nPath=relative\n");
        QCOMPARE(errorCode([&] {
                     LocalFileSystem::restore(url, ConflictPolicy::Fail, ctx);
                 }),
                 EINVAL);
        QVERIFY(QFileInfo::exists(FileSystemUrl(url).localPath()));
    }

    void watcherAndStableModel()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("first"));
        DirectoryLister lister;
        DirectoryModel model;
        model.setDirLister(&lister);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy listed(&lister, &DirectoryLister::completed);
        lister.openUrl(local(dir.path()));
        QTRY_COMPARE(model.rowCount(), 1);
        QPersistentModelIndex first(model.indexForUrl(local(dir.filePath("first"))));
        writeFile(dir.filePath("second"));
        QTRY_COMPARE(model.rowCount(), 2);
        QVERIFY(first.isValid());
        QCOMPARE(model.itemForIndex(first).name(), QString("first"));
        writeFile(dir.filePath("first"), "modified contents");
        QTRY_COMPARE(model.itemForIndex(first).size(), quint64(17));
        QVERIFY(QFile::rename(dir.filePath("second"), dir.filePath("renamed")));
        QTRY_VERIFY(model.indexForUrl(local(dir.filePath("renamed"))).isValid());
        QVERIFY(QFile::remove(dir.filePath("renamed")));
        QTRY_COMPARE(model.rowCount(), 1);
        writeFile(dir.filePath(".hidden"));
        lister.setShowHiddenFiles(true);
        lister.emitChanges();
        QTRY_COMPARE(model.rowCount(), 2);
        QVERIFY(listed.size() >= 4);
    }

    void watcherIgnoresUnrelatedSiblings()
    {
        QTemporaryDir directory;
        const auto watched = directory.filePath("watched");
        QVERIFY(QDir().mkpath(watched));

        FileWatcher watcher;
        QSignalSpy changed(&watcher, &FileWatcher::changed);
        QVERIFY(watcher.watch(watched));

        writeFile(directory.filePath("unrelated"));
        QTest::qWait(80);
        QCOMPARE(changed.count(), 0);

        writeFile(watched + "/inside");
        QTRY_COMPARE(changed.count(), 1);
    }

    void watcherDirectoryRecreation()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("watched");
        QVERIFY(QDir().mkpath(path));
        DirectoryLister lister;
        DirectoryModel model;
        model.setDirLister(&lister);
        lister.openUrl(local(path));
        QTest::qWait(80);
        QVERIFY(QDir().rmdir(path));
        QTest::qWait(80);
        QVERIFY(QDir().mkpath(path));
        writeFile(path + "/new");
        QTRY_COMPARE_WITH_TIMEOUT(model.rowCount(), 1, 5000);
        QCOMPARE(model.itemForIndex(model.index(0, 0)).name(), QString("new"));
    }

    void staleListingAndDestruction()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath("a")));
        QVERIFY(QDir().mkpath(dir.filePath("b")));
        for (int i = 0; i < 100; ++i)
            writeFile(dir.filePath("a/" + QString::number(i)));
        writeFile(dir.filePath("b/only"));
        DirectoryLister lister;
        DirectoryModel model;
        model.setDirLister(&lister);
        lister.openUrl(local(dir.filePath("a")));
        lister.openUrl(local(dir.filePath("b")));
        QTRY_COMPARE(model.rowCount(), 1);
        QCOMPARE(model.itemForIndex(model.index(0, 0)).name(), QString("only"));
        auto *pending = new DirectoryLister;
        pending->openUrl(local(dir.filePath("a")));
        delete pending;
    }

    void asynchronousTasksAndPaste()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath("target")));
        writeFile(dir.filePath("source"), "payload");
        QMimeData mime;
        mime.setUrls({local(dir.filePath("source"))});
        auto *copyTask = paste(&mime, local(dir.filePath("target")));
        QSignalSpy copied(copyTask, &Task::result);
        QTRY_COMPARE(copied.size(), 1);
        QCOMPARE(readFile(dir.filePath("target/source")), QByteArray("payload"));
        QVERIFY(QFileInfo::exists(dir.filePath("source")));
        mime.setData("application/x-cutefish-cutselection", "1");
        auto *moveTask = paste(&mime, local(dir.filePath("target")));
        QSignalSpy moved(moveTask, &Task::result);
        QTRY_COMPARE(moved.size(), 1);
        QVERIFY(!QFileInfo::exists(dir.filePath("source")));
        QCOMPARE(readFile(dir.filePath("target/source (1)")), QByteArray("payload"));
        Task rename({Operation::Rename,
                     {local(dir.filePath("target/source (1)"))},
                     local(dir.filePath("renamed"))});
        QSignalSpy renamed(&rename, &Task::result);
        rename.start();
        rename.start();
        QTRY_COMPARE(renamed.size(), 1);
        QCOMPARE(rename.state(), Task::State::Succeeded);
        Task remove({Operation::Delete, {local(dir.filePath("target"))}});
        QSignalSpy removed(&remove, &Task::result);
        remove.start();
        QTRY_COMPARE(removed.size(), 1);
        QVERIFY(!QFileInfo::exists(dir.filePath("target")));
    }

    void asynchronousConflictAndCancel()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath("target")));
        writeFile(dir.filePath("source"), "new");
        writeFile(dir.filePath("target/source"), "old");
        Task task({Operation::Copy,
                   {local(dir.filePath("source"))},
                   local(dir.filePath("target")),
                   ConflictPolicy::Ask});
        QSignalSpy conflict(&task, &Task::conflictRequested), finished(&task, &Task::result);
        task.start();
        QTRY_COMPARE(conflict.size(), 1);
        task.resolveConflict(int(ConflictPolicy::KeepBoth));
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(task.state(), Task::State::Succeeded);
        QCOMPARE(readFile(dir.filePath("target/source (1)")), QByteArray("new"));
        Task canceled(task.request());
        QSignalSpy waiting(&canceled, &Task::conflictRequested), done(&canceled, &Task::result);
        canceled.start();
        QTRY_COMPARE(waiting.size(), 1);
        canceled.cancel();
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(canceled.state(), Task::State::Canceled);
        QCOMPARE(readFile(dir.filePath("target/source")), QByteArray("old"));
    }

    void controllerTrashUndo()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("file"));
        TaskController controller;
        QSignalSpy finished(&controller, &TaskController::taskFinished);
        controller.submit({Operation::Trash, {local(dir.filePath("file"))}});
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(controller.isUndoAvailable());
        QVERIFY(controller.undo());
        QTRY_COMPARE(finished.size(), 2);
        QVERIFY(QFileInfo::exists(dir.filePath("file")));
        QVERIFY(!controller.isUndoAvailable());
    }

    void crossDeviceMove()
    {
        const auto root = qEnvironmentVariable("CUTEFISH_IO_CROSS_DEVICE_ROOT");
        if (root.isEmpty())
            QSKIP("Set CUTEFISH_IO_CROSS_DEVICE_ROOT to a writable different filesystem");
        QTemporaryDir source;
        QTemporaryDir target(root + "/cutefish-io-XXXXXX");
        QVERIFY(target.isValid());
        struct stat from, to;
        QCOMPARE(::stat(QFile::encodeName(source.path()), &from), 0);
        QCOMPARE(::stat(QFile::encodeName(target.path()), &to), 0);
        QVERIFY(from.st_dev != to.st_dev);
        QVERIFY(QDir().mkpath(source.filePath("tree/sub")));
        writeFile(source.filePath("tree/sub/file"), QByteArray(4 * 1024 * 1024, 'x'));
        QVERIFY(::symlink("sub/file", QFile::encodeName(source.filePath("tree/link"))) == 0);
        OperationContext ctx;
        LocalFileSystem::move(source.filePath("tree"), target.filePath("tree"),
                              ConflictPolicy::Fail, ctx);
        QVERIFY(!QFileInfo::exists(source.filePath("tree")));
        QCOMPARE(QFileInfo(target.filePath("tree/sub/file")).size(), 4 * 1024 * 1024);
        QVERIFY(QFileInfo(target.filePath("tree/link")).isSymbolicLink());
        OperationContext cancel;
        cancel.progress = [&](quint64 bytes, quint64) {
            if (bytes)
                cancel.canceled = true;
        };
        QCOMPARE(errorCode([&] {
                     LocalFileSystem::move(target.filePath("tree"), source.filePath("tree"),
                                           ConflictPolicy::Fail, cancel);
                 }),
                 ECANCELED);
        QVERIFY(QFileInfo::exists(target.filePath("tree/sub/file")));
        QVERIFY(!QFileInfo::exists(source.filePath("tree")));
    }

    void progressIncludesTotalsAndCurrentPath()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath("source/nested")));
        QVERIFY(QDir().mkpath(dir.filePath("target")));
        writeFile(dir.filePath("source/nested/one"), QByteArray(4 * 1024 * 1024, 'x'));
        writeFile(dir.filePath("two"), QByteArray(2 * 1024 * 1024, 'y'));
        Task task({Operation::Copy,
                   {local(dir.filePath("source")), local(dir.filePath("two"))},
                   local(dir.filePath("target"))});
        QList<TaskProgress> snapshots;
        connect(&task, &Task::progressInfoChanged, this, [&] {
            QCOMPARE(QThread::currentThread(), task.thread());
            snapshots.append(task.progressInfo());
        });
        QSignalSpy done(&task, &Task::result);
        task.start();
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(task.state(), Task::State::Succeeded);
        QVERIFY(!snapshots.isEmpty());
        QVERIFY(snapshots.first().preparing);
        QVERIFY(!snapshots.first().currentPath.isEmpty());
        const auto last = task.progressInfo();
        QVERIFY(!last.preparing);
        QVERIFY(last.totalsKnown);
        QCOMPARE(last.totalBytes, quint64(6 * 1024 * 1024));
        QCOMPARE(last.processedBytes, last.totalBytes);
        QCOMPARE(last.totalItems, quint64(4));
        QCOMPARE(last.processedItems, last.totalItems);
        quint64 previous = 0;
        for (const auto &snapshot : snapshots) {
            if (snapshot.preparing)
                continue;
            QVERIFY(snapshot.processedBytes >= previous);
            QVERIFY(snapshot.processedBytes <= snapshot.totalBytes);
            previous = snapshot.processedBytes;
        }
        QCOMPARE(task.operationName(), QString("copy"));
        QCOMPARE(task.destinationName(), QString("target"));
    }

    void preparationSharesCancellation()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath("nested")));
        writeFile(dir.filePath("nested/file"));
        OperationContext task;
        OperationContext scan;
        scan.cancelFlag = &task.canceled;
        scan.currentItem = [&](const QString &) {
            task.canceled = true;
        };
        QCOMPARE(errorCode([&] {
                     FileEnumerator::size(dir.path(), scan);
                 }),
                 ECANCELED);
        QVERIFY(QFileInfo::exists(dir.filePath("nested/file")));
    }

    void taskDestructionWhileWaiting()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("source"));
        writeFile(dir.filePath("target"));
        auto *task = new Task({Operation::Rename,
                               {local(dir.filePath("source"))},
                               local(dir.filePath("target")),
                               ConflictPolicy::Ask});
        QSignalSpy conflict(task, &Task::conflictRequested);
        task->start();
        QTRY_COMPARE(conflict.size(), 1);
        delete task;
        QVERIFY(QFileInfo::exists(dir.filePath("source")));
    }

    void asynchronousErrorsAndMkdir()
    {
        QTemporaryDir dir;
        Task mkdirTask({Operation::Mkdir, {}, local(dir.filePath("folder"))});
        QSignalSpy made(&mkdirTask, &Task::result);
        mkdirTask.start();
        QTRY_COMPARE(made.size(), 1);
        QCOMPARE(mkdirTask.state(), Task::State::Succeeded);
        Task unsupported(
                {Operation::Copy, {QUrl("https://example.invalid/file")}, local(dir.path())});
        QSignalSpy failed(&unsupported, &Task::result);
        unsupported.start();
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(unsupported.error(), ENOTSUP);
        Task canceled({Operation::Mkdir, {}, local(dir.filePath("canceled"))});
        QSignalSpy done(&canceled, &Task::result);
        canceled.cancel();
        canceled.start();
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(canceled.state(), Task::State::Canceled);
        QVERIFY(!QFileInfo::exists(dir.filePath("canceled")));
    }

    void linkDrop()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("file"));
        QVERIFY(QDir().mkpath(dir.filePath("target")));
        QMimeData mime;
        mime.setUrls({local(dir.filePath("file"))});
        QSignalSpy finished(TaskController::instance(), &TaskController::taskFinished);
        drop(&mime, local(dir.filePath("target")), Qt::LinkAction);
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(QFileInfo(dir.filePath("target/file")).isSymbolicLink());
        QVERIFY(QFileInfo::exists(dir.filePath("file")));
    }
};
QTEST_GUILESS_MAIN(IOTest)
#include "tst_io.moc"
