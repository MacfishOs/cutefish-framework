#include "directory.h"
#include "mimesniffer_p.h"
#include "task.h"
#include "trashdirectory_p.h"
#include "trashmonitor.h"

#include <QAbstractItemModelTester>
#include <QCryptographicHash>
#include <QMimeDatabase>
#include <QPersistentModelIndex>
#include <QPixmap>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimeZone>
#include <QtTest>

#include <fcntl.h>
#include <linux/capability.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace Cutefish::IO;

static void writeFile(const QString &path, const QByteArray &data = "contents")
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size())
        throw Failure(EIO, path);
}

static QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw Failure(EIO, path);

    return file.readAll();
}

template <class F> static int errorCode(F action)
{
    try {
        action();
    } catch (const Failure &failure) {
        return failure.error.code;
    }

    return 0;
}

static QUrl local(const QString &path)
{
    return QUrl::fromLocalFile(path);
}

static QString userTrash(const QString &topdir)
{
    return topdir + "/.Trash-" + QString::number(::getuid());
}

static QString trashId(const QString &topdir, bool shared = false)
{
    return (shared ? 's' : 'u') + QString::fromLatin1(QFile::encodeName(topdir).toHex());
}

// Mount lines as the kernel writes them, spaces in mount points escaped, after one
// for /proc.
static void writeMountTable(const QString &table, const QList<QPair<QString, QByteArray>> &mounts)
{
    QByteArray content = "22 1 0:21 / /proc rw,nosuid shared:5 - proc proc rw\n";
    int id = 30;

    for (const auto &[point, type] : mounts) {
        content += QByteArray::number(id++) + " 1 8:1 / "
                + QFile::encodeName(point).replace('\\', "\\134").replace(' ', "\\040")
                + " rw,relatime shared:1 - " + type + " /dev/sda1 rw\n";
    }

    writeFile(table, content);
}

class IOTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_data;
    QString m_mountTable;

private slots:
    void initTestCase()
    {
        // The model tester probes GUI decoration types even for a Core-only model.
        qRegisterMetaType<QPixmap>();

        QVERIFY(m_data.isValid());
        qputenv("XDG_DATA_HOME", m_data.path().toUtf8());
        QCOMPARE(FileSystemUrl::trashRoot(), m_data.path() + "/Trash");

        // No volume of this machine is listed, so no test touches its trash.
        m_mountTable = m_data.filePath("mountinfo");
        writeFile(m_mountTable, {});
        Internal::setMountTable(m_mountTable);
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

        QCOMPARE(errorCode([] { FilePath(QByteArray("x\0y", 3)); }), EINVAL);
        QCOMPARE(errorCode([] { FilePath("/a").child("../x"); }), EINVAL);
        QCOMPARE(errorCode([] { FileSystemUrl(QUrl("https://example.com/file")); }), ENOTSUP);
        QCOMPARE(errorCode([] { FileSystemUrl(QUrl("file://remote/tmp/a")); }), ENOTSUP);
        QCOMPARE(errorCode([] { FileSystemUrl(QUrl("trash:/../a")); }), EINVAL);
        QCOMPARE(errorCode([] { FileSystemUrl(QUrl("trash:/%2E%2E/a")); }), EINVAL);
        QCOMPARE(errorCode([] { FileSystemUrl(QUrl("trash:/a")); }), EINVAL);
        QCOMPARE(errorCode([] { FileSystemUrl(QUrl("trash:/u2F/a")); }), EINVAL);
        QCOMPARE(errorCode([] { FileSystemUrl(QUrl("trash:/u2e2e/a")); }), EINVAL);
        QCOMPARE(errorCode([] { FileSystemUrl(QUrl("trash:/u2f2e2e/a")); }), EINVAL);
        QCOMPARE(errorCode([] { FileSystemUrl(QUrl("trash:/x2f/a")); }), EINVAL);
        QCOMPARE(FileSystemUrl(QUrl("trash:/home/a/b")).localPath(),
                 FileSystemUrl::trashRoot() + "/files/a/b");
        QCOMPARE(FileSystemUrl(QUrl("trash:/" + trashId("/media/a b") + "/c")).localPath(),
                 userTrash("/media/a b") + "/files/c");
        QCOMPARE(FileSystemUrl(QUrl("trash:/" + trashId("/", true) + "/c")).localPath(),
                 "/.Trash/" + QString::number(::getuid()) + "/files/c");

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

        auto moved = dir.filePath("moved.txt");
        writeFile(moved, "moved");
        QCOMPARE(LocalFileSystem::move(moved, target, ConflictPolicy::KeepBoth, ctx),
                 dir.filePath("target (2).txt"));
        QCOMPARE(readFile(dir.filePath("target (2).txt")), QByteArray("moved"));
        QVERIFY(!QFileInfo::exists(moved));

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

        QCOMPARE(errorCode([&] { LocalFileSystem::remove("/", ctx); }), EINVAL);
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
        QCOMPARE(errorCode([&] { LocalFileSystem::directorySize(dir.path(), ctx); }), ECANCELED);
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

    void dataAroundHolesCopiesExactly()
    {
        QTemporaryDir dir;
        QByteArray head(9 * 1024 * 1024 + 3, Qt::Uninitialized);
        QByteArray tail(5 * 1024 * 1024 + 11, Qt::Uninitialized);
        for (qsizetype i = 0; i < head.size(); ++i)
            head[i] = char(i * 7 + i / 4099);
        for (qsizetype i = 0; i < tail.size(); ++i)
            tail[i] = char(i * 13 + i / 8191);

        constexpr qint64 gap = 64 * 1024 * 1024;
        QFile source(dir.filePath("source"));
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write(head), head.size());
        QVERIFY(source.seek(head.size() + gap));
        QCOMPARE(source.write(tail), tail.size());
        source.close();

        const qint64 length = head.size() + gap + tail.size();
        quint64 previous = 0;
        OperationContext ctx;
        ctx.progress = [&](quint64 bytes, quint64) {
            QVERIFY(bytes >= previous);
            previous = bytes;
        };

        LocalFileSystem::copy(source.fileName(), dir.filePath("target"), ConflictPolicy::Fail, ctx);
        QCOMPARE(ctx.bytes, quint64(length));

        const QByteArray copied = readFile(dir.filePath("target"));
        QCOMPARE(copied.size(), length);
        QVERIFY(copied.first(head.size()) == head);
        QVERIFY(copied.sliced(head.size(), gap) == QByteArray(gap, '\0'));
        QVERIFY(copied.last(tail.size()) == tail);

        struct stat st;
        QCOMPARE(::stat(QFile::encodeName(dir.filePath("target")), &st), 0);
        QVERIFY(st.st_blocks * 512 < length - gap / 2);
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

    void removeStopsAtMounts()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath("tree/mount")));
        writeFile(dir.filePath("tree/file"));

        // A private user and mount namespace lets the child mount without privileges.
        const uid_t uid = ::getuid();
        const gid_t gid = ::getgid();
        const pid_t pid = ::fork();
        QVERIFY(pid >= 0);
        if (pid == 0) {
            if (::unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0)
                ::_exit(77);

            // Creating files needs the group mapped as well, which needs setgroups denied first.
            const auto write = [](const char *path, const QByteArray &data) {
                QFile file(QString::fromLatin1(path));
                return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
            };
            if (!write("/proc/self/uid_map", "0 " + QByteArray::number(uid) + " 1")
                || !write("/proc/self/setgroups", "deny")
                || !write("/proc/self/gid_map", "0 " + QByteArray::number(gid) + " 1"))
                ::_exit(77);

            const QByteArray mount = QFile::encodeName(dir.filePath("tree/mount"));
            if (::mount("none", mount, "tmpfs", 0, nullptr) != 0)
                ::_exit(77);

            try {
                writeFile(dir.filePath("tree/mount/kept"));
                OperationContext ctx;
                const int code = errorCode([&] { LocalFileSystem::remove(dir.filePath("tree"), ctx); });
                ::_exit(code == EBUSY && QFileInfo::exists(dir.filePath("tree/mount/kept")) ? 0 : 1);
            } catch (...) {
                ::_exit(2);
            }
        }

        int status = 0;
        QCOMPARE(::waitpid(pid, &status, 0), pid);
        QVERIFY(WIFEXITED(status));
        if (WEXITSTATUS(status) == 77)
            QSKIP("Unprivileged user namespaces are unavailable");
        QCOMPARE(WEXITSTATUS(status), 0);
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

    void restoreRecreatesFolder()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("gone/inner/file");
        QVERIFY(QDir().mkpath(dir.filePath("gone/inner")));
        writeFile(path);

        OperationContext ctx;
        const auto trashed = LocalFileSystem::trash(local(path), ctx);
        QVERIFY(QDir(dir.filePath("gone")).removeRecursively());
        QCOMPARE(LocalFileSystem::restore(trashed, ConflictPolicy::Fail, ctx), local(path));
        QVERIFY(QFileInfo::exists(path));
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

    void volumeTrashListsRestoresAndEmpties()
    {
        QTemporaryDir dir;
        const auto volume = dir.filePath("disk one");
        const auto automount = dir.filePath("auto");
        QVERIFY(QDir().mkpath(userTrash(volume) + "/files"));
        QVERIFY(QDir().mkpath(userTrash(volume) + "/info"));
        QVERIFY(QDir().mkpath(userTrash(automount) + "/files"));
        writeFile(userTrash(automount) + "/files/unseen");
        const auto share = dir.filePath("share");
        QVERIFY(QDir().mkpath(userTrash(share) + "/files"));
        writeFile(userTrash(share) + "/files/remote");
        const auto bound = dir.filePath("bound");
        QVERIFY(::symlink(QFile::encodeName(volume), QFile::encodeName(bound)) == 0);

        // Looking into an automount would mount it, and into a share could hang; a volume
        // reached twice has one trash.
        writeMountTable(m_mountTable,
                        {{volume, "ext4"},
                         {bound, "ext4"},
                         {automount, "autofs"},
                         {share, "nfs"}});
        auto emptyTable = qScopeGuard([this] { writeFile(m_mountTable, {}); });

        writeFile(userTrash(volume) + "/files/old.txt", "old");
        writeFile(userTrash(volume) + "/info/old.txt.trashinfo",
                  "[Trash Info]\nPath=docs/old%20one.txt\nDeletionDate=2026-01-02T03:04:05\n");
        // Left by a tool that wrote no info file.
        writeFile(userTrash(volume) + "/files/orphan");

        writeFile(dir.filePath("local"));
        OperationContext ctx;
        const auto trashed = LocalFileSystem::trash(local(dir.filePath("local")), ctx);
        QCOMPARE(trashed.path(), QString("/home/local"));

        QHash<QString, FileItem> items;
        for (const auto &item : LocalFileSystem::enumerate(QUrl("trash:/")))
            items.insert(item.name(), item);
        QCOMPARE(items.keys().size(), 3);

        const auto old = items.value("old.txt");
        QCOMPARE(old.url().path(), '/' + trashId(volume) + "/old.txt");
        QCOMPARE(old.originalPath(), volume + "/docs/old one.txt");
        QCOMPARE(old.deletionTime(), QDateTime(QDate(2026, 1, 2), QTime(3, 4, 5)));
        QCOMPARE(readFile(old.localPath()), QByteArray("old"));
        QCOMPARE(FileItem(old.url()).originalPath(), old.originalPath());
        QCOMPARE(FileItem(old.url()).deletionTime(), old.deletionTime());

        const auto orphan = items.value("orphan");
        QVERIFY(orphan.originalPath().isEmpty());
        QVERIFY(!orphan.deletionTime().isValid());

        // One trash lists alone, with the same details.
        const auto alone = LocalFileSystem::enumerate(upUrl(orphan.url()));
        QCOMPARE(alone.size(), 2);
        for (const auto &item : alone)
            QCOMPARE(item.originalPath(), items.value(item.name()).originalPath());

        const auto home = items.value("local");
        QCOMPARE(home.url(), trashed);
        QCOMPARE(home.originalPath(), dir.filePath("local"));
        QVERIFY(qAbs(home.deletionTime().secsTo(QDateTime::currentDateTime())) < 60);

        QCOMPARE(errorCode([&] {
                     LocalFileSystem::restore(orphan.url(), ConflictPolicy::Fail, ctx);
                 }),
                 ENOENT);

        QVERIFY(QDir().mkpath(volume + "/docs"));
        QCOMPARE(LocalFileSystem::restore(old.url(), ConflictPolicy::Fail, ctx),
                 local(volume + "/docs/old one.txt"));
        QCOMPARE(readFile(volume + "/docs/old one.txt"), QByteArray("old"));
        QVERIFY(!QFileInfo::exists(userTrash(volume) + "/info/old.txt.trashinfo"));

        LocalFileSystem::emptyTrash(ctx);
        QVERIFY(LocalFileSystem::enumerate(QUrl("trash:/")).isEmpty());
        QVERIFY(QDir(userTrash(volume) + "/files").isEmpty());
        QVERIFY(QDir(FileSystemUrl::trashRoot() + "/info").isEmpty());
        QVERIFY(QFileInfo::exists(userTrash(automount) + "/files/unseen"));
        QVERIFY(QFileInfo::exists(userTrash(share) + "/files/remote"));
    }

    void trashMonitorSeesVolumes()
    {
        QTemporaryDir dir;
        const auto volume = dir.filePath("volume");
        QVERIFY(QDir().mkpath(volume));
        writeMountTable(m_mountTable, {{volume, "ext4"}});
        auto emptyTable = qScopeGuard([this] { writeFile(m_mountTable, {}); });

        writeFile(dir.filePath("local"));
        OperationContext ctx;
        LocalFileSystem::trash(local(dir.filePath("local")), ctx);

        TrashMonitor monitor;
        QTRY_VERIFY(!monitor.isEmpty());

        LocalFileSystem::emptyTrash(ctx);
        QTRY_VERIFY(monitor.isEmpty());

        // The first item trashed on a volume creates its trash.
        QVERIFY(QDir().mkpath(userTrash(volume) + "/files"));
        writeFile(userTrash(volume) + "/files/stick");
        QTRY_VERIFY(!monitor.isEmpty());

        LocalFileSystem::emptyTrash(ctx);
        QTRY_VERIFY(monitor.isEmpty());

        writeFile(userTrash(volume) + "/files/again");
        QTRY_VERIFY(!monitor.isEmpty());
    }

    // A tmpfs in a private user and mount namespace stands in for a second drive.
    void trashOnAnotherFilesystem()
    {
        QTemporaryDir dir;
        const auto volume = dir.filePath("volume");
        QVERIFY(QDir().mkpath(volume));
        const auto table = dir.filePath("mountinfo");

        const uid_t uid = ::getuid();
        const gid_t gid = ::getgid();
        const pid_t pid = ::fork();
        QVERIFY(pid >= 0);
        if (pid == 0) {
            if (::unshare(CLONE_NEWUSER | CLONE_NEWNS) != 0)
                ::_exit(77);

            const auto write = [](const char *path, const QByteArray &data) {
                QFile file(QString::fromLatin1(path));
                return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
            };
            if (!write("/proc/self/uid_map",
                       QByteArray::number(uid) + ' ' + QByteArray::number(uid) + " 1")
                || !write("/proc/self/setgroups", "deny")
                || !write("/proc/self/gid_map",
                          QByteArray::number(gid) + ' ' + QByteArray::number(gid) + " 1"))
                ::_exit(77);

            if (::mount("none", QFile::encodeName(volume), "tmpfs", 0, "mode=0755") != 0)
                ::_exit(77);

            // Without capabilities, permissions hold as they do outside the namespace.
            __user_cap_header_struct header {_LINUX_CAPABILITY_VERSION_3, 0};
            __user_cap_data_struct capabilities[2] = {};
            if (::syscall(SYS_capset, &header, capabilities) != 0)
                ::_exit(76);

            Internal::setMountTable(table);

            // Exits with the number of the first check that fails.
            int step = 0;
            const auto expect = [&](bool passed) {
                ++step;
                if (!passed)
                    ::_exit(step);
            };

            try {
                OperationContext ctx;
                const auto trash = userTrash(volume);
                const auto shared = volume + "/.Trash/" + QString::number(uid);
                const auto mode = [](const QString &path) {
                    struct stat st;
                    return ::stat(QFile::encodeName(path), &st) == 0 ? st.st_mode & 07777 : 0;
                };

                // A remote filesystem takes no trash, so its files go home.
                writeMountTable(table, {{volume, "nfs"}});
                writeFile(volume + "/remote");
                const auto remote = LocalFileSystem::trash(local(volume + "/remote"), ctx);
                expect(remote.path() == "/home/remote");
                expect(!QFileInfo::exists(volume + "/remote") && !QFileInfo::exists(trash));
                expect(LocalFileSystem::restore(remote, ConflictPolicy::Fail, ctx)
                       == local(volume + "/remote"));
                writeMountTable(table, {{volume, "tmpfs"}});

                // A volume that takes no trash sends its files home.
                expect(QDir().mkpath(volume + "/sub"));
                expect(::chmod(QFile::encodeName(volume), 0555) == 0);
                writeFile(volume + "/sub/sent");
                const auto sent = LocalFileSystem::trash(local(volume + "/sub/sent"), ctx);
                expect(sent.path() == "/home/sent");
                expect(!QFileInfo::exists(volume + "/sub/sent") && !QFileInfo::exists(trash));
                expect(::chmod(QFile::encodeName(volume), 0755) == 0);

                writeFile(volume + "/sub/file", "data");
                const auto url = LocalFileSystem::trash(local(volume + "/sub/file"), ctx);
                expect(url.path() == '/' + trashId(volume) + "/file");
                expect(readFile(trash + "/files/file") == "data");
                expect(readFile(trash + "/info/file.trashinfo")
                               .startsWith("[Trash Info]\nPath=sub/file\nDeletionDate="));
                expect(mode(trash) == 0700);

                const auto items = LocalFileSystem::enumerate(QUrl("trash:/"));
                expect(items.size() == 2);
                const auto item = std::find_if(items.cbegin(), items.cend(),
                                               [&](const FileItem &entry) {
                                                   return entry.url() == url;
                                               });
                expect(item != items.cend() && item->originalPath() == volume + "/sub/file"
                       && item->deletionTime().isValid());

                expect(LocalFileSystem::restore(url, ConflictPolicy::Fail, ctx)
                       == local(volume + "/sub/file"));
                expect(readFile(volume + "/sub/file") == "data");
                expect(!QFileInfo::exists(trash + "/info/file.trashinfo"));

                // $topdir/.Trash serves only as a sticky folder that is no link and takes
                // the user's folder.
                expect(QDir().mkdir(volume + "/.Trash"));
                const auto plain = LocalFileSystem::trash(local(volume + "/sub/file"), ctx);
                expect(plain.path() == '/' + trashId(volume) + "/file");

                expect(::chmod(QFile::encodeName(volume + "/.Trash"), 01555) == 0);
                writeFile(volume + "/sub/second");
                const auto unwritable = LocalFileSystem::trash(local(volume + "/sub/second"), ctx);
                expect(unwritable.path() == '/' + trashId(volume) + "/second");

                expect(::chmod(QFile::encodeName(volume + "/.Trash"), 01777) == 0);
                writeFile(volume + "/sub/third");
                const auto sticky = LocalFileSystem::trash(local(volume + "/sub/third"), ctx);
                expect(sticky.path() == '/' + trashId(volume, true) + "/third");
                expect(QFileInfo::exists(shared + "/files/third") && mode(shared) == 0700);

                expect(QFile::rename(volume + "/.Trash", volume + "/.Trash-real"));
                expect(::symlink(".Trash-real", QFile::encodeName(volume + "/.Trash")) == 0);
                writeFile(volume + "/sub/fourth");
                const auto linked = LocalFileSystem::trash(local(volume + "/sub/fourth"), ctx);
                expect(linked.path() == '/' + trashId(volume) + "/fourth");
                expect(::unlink(QFile::encodeName(volume + "/.Trash")) == 0);
                expect(QFile::rename(volume + "/.Trash-real", volume + "/.Trash"));

                // Trashing a trash into itself is refused.
                expect(errorCode([&] {
                           LocalFileSystem::trash(local(trash + "/files/fourth"), ctx);
                       }) == EINVAL);

                expect(LocalFileSystem::enumerate(QUrl("trash:/")).size() == 5);
                LocalFileSystem::emptyTrash(ctx);
                expect(LocalFileSystem::enumerate(QUrl("trash:/")).isEmpty());
                expect(QDir(trash + "/info").isEmpty() && QDir(shared + "/info").isEmpty());
                expect(QDir(FileSystemUrl::trashRoot() + "/files").isEmpty());
            } catch (...) {
                ::_exit(100);
            }

            ::_exit(0);
        }

        int status = 0;
        QCOMPARE(::waitpid(pid, &status, 0), pid);
        QVERIFY(WIFEXITED(status));
        if (WEXITSTATUS(status) == 77)
            QSKIP("Unprivileged user namespaces are unavailable");
        QCOMPARE(WEXITSTATUS(status), 0);
    }

    // A volume's trash made after the listing opened appears in it.
    void trashListingFollowsVolumes()
    {
        QTemporaryDir dir;
        const auto volume = dir.filePath("volume");
        QVERIFY(QDir().mkpath(volume));
        writeMountTable(m_mountTable, {{volume, "ext4"}});
        auto emptyTable = qScopeGuard([this] { writeFile(m_mountTable, {}); });

        DirectoryLister lister;
        DirectoryModel model;
        model.setDirLister(&lister);
        QSignalSpy listed(&lister, &DirectoryLister::completed);
        lister.openUrl(QUrl("trash:/"));
        QTRY_COMPARE(listed.size(), 1);
        QCOMPARE(model.rowCount(), 0);

        QVERIFY(QDir().mkpath(userTrash(volume) + "/files"));
        writeFile(userTrash(volume) + "/files/first");
        QTRY_COMPARE(model.rowCount(), 1);
        QCOMPARE(model.data(model.index(0, DirectoryModel::OriginalPath)).toString(), QString());

        writeFile(userTrash(volume) + "/files/second");
        QTRY_COMPARE(model.rowCount(), 2);

        writeFile(dir.filePath("local"));
        OperationContext ctx;
        LocalFileSystem::trash(local(dir.filePath("local")), ctx);
        QTRY_COMPARE(model.rowCount(), 3);

        const auto row = model.indexForUrl(QUrl("trash:/home/local")).row();
        QCOMPARE(model.data(model.index(row, DirectoryModel::OriginalPath)).toString(),
                 dir.filePath("local"));
        QVERIFY(model.data(model.index(row, DirectoryModel::DeletionTime)).toDateTime().isValid());
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

    void searchFindsNamesAtAnyDepth()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath("Projects/Report Drafts/old"));
        QVERIFY(QDir(dir.path()).mkpath(".cache/report"));
        writeFile(dir.filePath("annual report.txt"));
        writeFile(dir.filePath("Projects/REPORT final.pdf"));
        writeFile(dir.filePath("Projects/Report Drafts/old/report-1.txt"));
        writeFile(dir.filePath("Projects/notes.txt"));
        writeFile(dir.filePath(".cache/report/report.bin"));
        QVERIFY(QFile::link(dir.filePath("Projects"), dir.filePath("Projects/loop")));

        DirectoryLister lister;
        DirectoryModel model;
        model.setDirLister(&lister);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy listed(&lister, &DirectoryLister::completed);

        lister.openUrl(local(dir.filePath("Projects")));
        QTRY_COMPARE(listed.count(), 1);

        const auto names = [&] {
            QStringList result;
            for (const auto &item : lister.items())
                result.append(QDir(dir.path()).relativeFilePath(item.localPath()));
            result.sort();
            return result;
        };

        // Every word, in any case; hidden folders are left out and links not followed.
        lister.search(local(dir.path()), "  report ");
        QVERIFY(lister.isSearching());
        QTRY_COMPARE(listed.count(), 2);
        QCOMPARE(names(), QStringList({"Projects/REPORT final.pdf", "Projects/Report Drafts",
                                       "Projects/Report Drafts/old/report-1.txt",
                                       "annual report.txt"}));
        QCOMPARE(lister.url(), local(dir.filePath("Projects")));
        QCOMPARE(model.rowCount(), 4);

        lister.search(local(dir.path()), "report TXT");
        QTRY_COMPARE(listed.count(), 3);
        QCOMPARE(names(), QStringList({"Projects/Report Drafts/old/report-1.txt",
                                       "annual report.txt"}));

        lister.setShowHiddenFiles(true);
        lister.emitChanges();
        QTRY_COMPARE(listed.count(), 4);
        QCOMPARE(names(), QStringList({"Projects/Report Drafts/old/report-1.txt",
                                       "annual report.txt"}));

        lister.search(local(dir.path()), "report");
        QTRY_COMPARE(listed.count(), 5);
        QVERIFY(names().contains(".cache/report/report.bin"));

        // No text lists the directory again.
        lister.search({}, " ");
        QVERIFY(!lister.isSearching());
        QTRY_COMPARE(listed.count(), 6);
        QCOMPARE(names(), QStringList({"Projects/REPORT final.pdf", "Projects/Report Drafts",
                                       "Projects/loop", "Projects/notes.txt"}));
    }

    void searchStopsWhenAFolderOpens()
    {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath("inner"));
        writeFile(dir.filePath("inner/match"));

        DirectoryLister lister;
        QSignalSpy listed(&lister, &DirectoryLister::completed);
        lister.search(local(dir.path()), "match");
        lister.openUrl(local(dir.filePath("inner")));
        QVERIFY(!lister.isSearching());
        QTRY_COMPARE(listed.count(), 1);
        QCOMPARE(lister.items().size(), 1);
        QCOMPARE(lister.items().constFirst().name(), QString("match"));
    }

    void refreshDetectsChangedContents()
    {
        QTemporaryDir dir;
        // Without an extension the type comes from the contents.
        writeFile(dir.filePath("changed"), "%PDF-1.4\n");
        writeFile(dir.filePath("kept"), "%PDF-1.4\n");

        DirectoryLister lister;
        QSignalSpy listed(&lister, &DirectoryLister::completed);
        lister.openUrl(local(dir.path()));
        QTRY_COMPARE(listed.count(), 1);

        const auto typeOf = [&](const QString &name) {
            for (const auto &item : lister.items()) {
                if (item.name() == name)
                    return item.mimetype();
            }
            return QString();
        };
        QCOMPARE(typeOf("changed"), QString("application/pdf"));
        QCOMPARE(typeOf("kept"), QString("application/pdf"));

        writeFile(dir.filePath("changed"), "#!/bin/sh\necho\n");
        QTRY_COMPARE(typeOf("changed"), QString("application/x-shellscript"));
        QCOMPARE(typeOf("kept"), QString("application/pdf"));

        const auto item = lister.items().constFirst();
        QCOMPARE(item.time(FileItem::ModificationTime, QTimeZone::UTC),
                 item.time(FileItem::ModificationTime));
    }

    // The sniffer answers what QMimeDatabase does, names and contents alike.
    void sniffedTypesMatchQt()
    {
        QTemporaryDir dir;
        const QByteArray elf = readFile(QCoreApplication::applicationFilePath()).left(20000);
        const QByteArray transportStream =
                QByteArray(1, 0x47) + QByteArray(187, 0) + QByteArray(1, 0x47) + QByteArray(187, 0);
        const QList<QPair<QString, QByteArray>> files = {
                {"program", elf},
                {"libsample.so.6", elf},
                {"page.1", "plain words\n"},
                {"script", "#!/bin/sh\necho\n"},
                {"empty", {}},
                {"empty.ts", {}},
                {"strings.ts", "<?xml version=\"1.0\"?>\n<TS version=\"2.1\"></TS>\n"},
                {"video.ts", transportStream},
                {"function.m", "function y = f(x)\nend\n"},
                {"README", "read me\n"},
                {"Makefile", "all:\n"},
                {"PHOTO.JPG", "\xff\xd8\xff\xe0"},
                {"source.C", "int main() {}\n"},
                {"binary", QByteArray("\x00\x01\x02\x03", 4)},
                {"document", "%PDF-1.4\n"},
                {"notes~", "backup\n"},
                {"123.vdr", "recording"},
                {"callgrind.out.1", "events: Ir\n"},
        };

        const QMimeDatabase database;
        for (const auto &[name, data] : files) {
            const QString path = dir.filePath(name);
            writeFile(path, data);
            QCOMPARE(name + u' ' + Internal::MimeSniffer::self()->mimeTypeForFile(path).name(),
                     name + u' ' + database.mimeTypeForFile(path).name());
        }
    }

    // A first listing detects types on several threads.
    void listingDetectsEveryType()
    {
        QTemporaryDir dir;
        for (int i = 0; i < 400; ++i)
            writeFile(dir.filePath(QString::number(i)), i % 2 ? "%PDF-1.4\n" : "#!/bin/sh\n");

        DirectoryLister lister;
        QSignalSpy listed(&lister, &DirectoryLister::completed);
        lister.openUrl(local(dir.path()));
        QTRY_COMPARE(listed.count(), 1);
        QCOMPARE(lister.items().size(), 400);

        for (const auto &item : lister.items()) {
            QCOMPARE(item.name() + u' ' + item.mimetype(),
                     item.name() + (item.name().toInt() % 2 ? " application/pdf"
                                                            : " application/x-shellscript"));
        }
    }

    // The name an item keeps is the one its URL decodes to.
    void listedNamesMatchTheirUrls()
    {
        QTemporaryDir dir;
        const QByteArray base = QFile::encodeName(dir.path());
        const QList<QByteArray> names = {"a%20b", "100%", "c#d", "e?f", "g h", "[x]", "semi;colon",
                                         "back\\slash", "\xc3\xbc", "x\xff", "plus+sign"};
        const auto removeNames = qScopeGuard([&] {
            for (const QByteArray &name : names)
                ::unlink(base + '/' + name);
        });
        for (const QByteArray &name : names)
            QVERIFY(::close(::open(base + '/' + name, O_CREAT | O_WRONLY | O_CLOEXEC, 0644)) == 0);

        DirectoryLister lister;
        QSignalSpy listed(&lister, &DirectoryLister::completed);
        lister.openUrl(local(dir.path()));
        QTRY_COMPARE(listed.count(), 1);
        QCOMPARE(lister.items().size(), names.size());

        for (const auto &item : lister.items()) {
            QCOMPARE(item.name(), item.url().fileName());
            QCOMPARE(QFileInfo(item.localPath()).fileName(), item.name());
        }
    }

    // A folder this large builds its items on several threads.
    void largeListingHasEveryFileOnce()
    {
        QTemporaryDir dir;
        constexpr int Count = 20500;
        for (int i = 0; i < Count; ++i)
            QVERIFY(::close(::open(QFile::encodeName(dir.filePath(QString::number(i))),
                                   O_CREAT | O_WRONLY | O_CLOEXEC, 0644)) == 0);

        DirectoryLister lister;
        QSignalSpy listed(&lister, &DirectoryLister::completed);
        lister.openUrl(local(dir.path()));
        QTRY_COMPARE_WITH_TIMEOUT(listed.count(), 1, 30000);

        QList<bool> seen(Count, false);
        for (const auto &item : lister.items()) {
            const int index = item.name().toInt();
            QVERIFY(!seen[index]);
            seen[index] = true;
            QCOMPARE(item.url(), local(dir.filePath(item.name())));
            QCOMPARE(item.localPath(), dir.filePath(item.name()));
        }
        QVERIFY(!seen.contains(false));
    }

    // Files added or removed in one place leave the other rows where they are.
    void modelFollowsChangesBetweenUnchangedRows()
    {
        QTemporaryDir dir;
        for (int i = 0; i < 60; ++i)
            writeFile(dir.filePath(QString::number(i)));

        DirectoryLister lister;
        DirectoryModel model;
        model.setDirLister(&lister);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy listed(&lister, &DirectoryLister::completed);
        lister.openUrl(local(dir.path()));
        QTRY_COMPARE(listed.count(), 1);

        const auto sortedUrls = [](const FileItemList &items) {
            QStringList urls;
            for (const auto &item : items)
                urls.append(item.url().toString());
            urls.sort();
            return urls;
        };
        const auto modelItems = [&] {
            FileItemList items;
            for (int row = 0; row < model.rowCount(); ++row)
                items.append(model.itemForIndex(model.index(row, 0)));
            return items;
        };

        QPersistentModelIndex kept(model.indexForUrl(local(dir.filePath("30"))));
        QVERIFY(kept.isValid());

        writeFile(dir.filePath("added"));
        QTRY_COMPARE(model.rowCount(), 61);
        QCOMPARE(sortedUrls(modelItems()), sortedUrls(lister.items()));

        QVERIFY(QFile::remove(dir.filePath("10")));
        QTRY_COMPARE(model.rowCount(), 60);
        QCOMPARE(sortedUrls(modelItems()), sortedUrls(lister.items()));

        QVERIFY(kept.isValid());
        QCOMPARE(model.itemForIndex(kept).name(), QString("30"));
        QVERIFY(model.indexForUrl(local(dir.filePath("added"))).isValid());
        QVERIFY(!model.indexForUrl(local(dir.filePath("10"))).isValid());
    }

    // Names that are not UTF-8 can decode to the same text, giving two files one URL.
    void modelKeepsRowsForCollidingNames()
    {
        QTemporaryDir dir;
        const QByteArray base = QFile::encodeName(dir.path());
        const QList<QByteArray> names = {"a\xff", "a\xfe", "b"};
        // QTemporaryDir cannot remove what it cannot name.
        const auto removeNames = qScopeGuard([&] {
            for (const QByteArray &name : names)
                ::unlink(base + '/' + name);
        });
        for (const QByteArray &name : names)
            QVERIFY(::close(::open(base + '/' + name, O_CREAT | O_WRONLY | O_CLOEXEC, 0644)) == 0);

        DirectoryLister lister;
        DirectoryModel model;
        model.setDirLister(&lister);
        QAbstractItemModelTester tester(&model,
                                        QAbstractItemModelTester::FailureReportingMode::QtTest);
        QSignalSpy listed(&lister, &DirectoryLister::completed);
        lister.openUrl(local(dir.path()));
        QTRY_COMPARE(listed.count(), 1);
        QCOMPARE(model.rowCount(), 3);

        for (int round = 0; round < 4; ++round) {
            const int before = listed.count();
            writeFile(dir.filePath(QStringLiteral("new%1").arg(round)));
            QTRY_VERIFY(listed.count() > before);
            QTRY_COMPARE(model.rowCount(), int(lister.items().size()));
            QCOMPARE(model.rowCount(), 4 + round);
        }
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

    // A kept watch must not outlive its directory: one recreated at the path is watched anew.
    void watcherFollowsRecreatedDirectory()
    {
        QTemporaryDir directory;
        const auto watched = directory.filePath("watched");
        QVERIFY(QDir().mkpath(watched));

        FileWatcher watcher;
        QSignalSpy changed(&watcher, &FileWatcher::changed);
        QVERIFY(watcher.watch(watched));

        QVERIFY(QDir(watched).removeRecursively());
        QVERIFY(QDir().mkpath(watched));
        QTRY_VERIFY(changed.count() > 0);
        QVERIFY(watcher.watch(watched));

        QTest::qWait(80);
        changed.clear();
        writeFile(watched + "/inside");
        QTRY_VERIFY(changed.count() > 0);
    }

    void watcherDirectoryRemoval()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("a/b");
        QVERIFY(QDir().mkpath(path));
        writeFile(dir.filePath("sibling"));

        DirectoryLister lister;
        DirectoryModel model;
        model.setDirLister(&lister);
        QSignalSpy listed(&lister, &DirectoryLister::completed);
        QSignalSpy failed(&lister, &DirectoryLister::error);
        lister.openUrl(local(path));
        QTRY_COMPARE(listed.count(), 1);

        QVERIFY(QDir(dir.filePath("a")).removeRecursively());
        QTRY_COMPARE(lister.url(), local(dir.path()));
        QTRY_COMPARE(model.rowCount(), 1);
        QCOMPARE(model.itemForIndex(model.index(0, 0)).name(), QString("sibling"));
        QCOMPARE(failed.count(), 0);
    }

    void missingDirectoryReportsOnce()
    {
        QTemporaryDir dir;
        const auto path = dir.filePath("missing");

        DirectoryLister lister;
        QSignalSpy failed(&lister, &DirectoryLister::error);
        lister.openUrl(local(path));
        QTRY_COMPARE(failed.count(), 1);

        lister.emitChanges();
        QTest::qWait(2500);
        QCOMPARE(failed.count(), 1);
        QCOMPARE(lister.url(), local(path));
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
        QSignalSpy asked(moveTask, &Task::conflictRequested);
        QSignalSpy moved(moveTask, &Task::result);
        QTRY_COMPARE(asked.size(), 1);
        QCOMPARE(asked.first().at(0).toString(), dir.filePath("source"));
        QCOMPARE(asked.first().at(1).toString(), dir.filePath("target/source"));
        moveTask->resolveConflict(int(ConflictPolicy::KeepBoth));
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

    void conflictResolutionAppliesToAll()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath("target")));
        writeFile(dir.filePath("a"), "new a");
        writeFile(dir.filePath("b"), "new b");
        writeFile(dir.filePath("target/a"), "old a");
        writeFile(dir.filePath("target/b"), "old b");

        Task task({Operation::Copy,
                   {local(dir.filePath("a")), local(dir.filePath("b"))},
                   local(dir.filePath("target")),
                   ConflictPolicy::Ask});
        QSignalSpy conflict(&task, &Task::conflictRequested), finished(&task, &Task::result);
        task.start();
        QTRY_COMPARE(conflict.size(), 1);
        task.resolveConflict(int(ConflictPolicy::Replace), true);

        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(conflict.size(), 1);
        QCOMPARE(task.state(), Task::State::Succeeded);
        QCOMPARE(readFile(dir.filePath("target/a")), QByteArray("new a"));
        QCOMPARE(readFile(dir.filePath("target/b")), QByteArray("new b"));
    }

    void pasteIntoOwnFolderDuplicatesWithoutAsking()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("source"), "payload");

        Task task({Operation::Copy,
                   {local(dir.filePath("source"))},
                   local(dir.path()),
                   ConflictPolicy::Ask});
        QSignalSpy conflict(&task, &Task::conflictRequested), finished(&task, &Task::result);
        task.start();
        QTRY_COMPARE(finished.size(), 1);

        QCOMPARE(conflict.size(), 0);
        QCOMPARE(task.state(), Task::State::Succeeded);
        QCOMPARE(readFile(dir.filePath("source (1)")), QByteArray("payload"));
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

    void controllerTrashRedo()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("file"));
        TaskController controller;
        QSignalSpy finished(&controller, &TaskController::taskFinished);

        controller.submit({Operation::Trash, {local(dir.filePath("file"))}});
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(!controller.isRedoAvailable());

        QVERIFY(controller.undo());
        QTRY_COMPARE(finished.size(), 2);
        QVERIFY(controller.isRedoAvailable());

        QVERIFY(controller.redo());
        QTRY_COMPARE(finished.size(), 3);
        QVERIFY(!QFileInfo::exists(dir.filePath("file")));
        QVERIFY(!controller.isRedoAvailable());
        QVERIFY(controller.isUndoAvailable());

        // A new operation after an undo forgets what could be redone.
        QVERIFY(controller.undo());
        QTRY_COMPARE(finished.size(), 4);
        QVERIFY(controller.isRedoAvailable());
        writeFile(dir.filePath("other"));
        controller.submit({Operation::Trash, {local(dir.filePath("other"))}});
        QTRY_COMPARE(finished.size(), 5);
        QVERIFY(!controller.isRedoAvailable());
    }

    void controllerCopyUndo()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath("target")));
        writeFile(dir.filePath("file"), "data");
        TaskController controller;
        QSignalSpy finished(&controller, &TaskController::taskFinished);

        controller.submit({Operation::Copy,
                           {local(dir.filePath("file"))},
                           local(dir.filePath("target"))});
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(controller.undo());
        QTRY_COMPARE(finished.size(), 2);
        QVERIFY(!QFileInfo::exists(dir.filePath("target/file")));
        QVERIFY(QFileInfo::exists(dir.filePath("file")));
        QCOMPARE(LocalFileSystem::enumerate(QUrl("trash:/")).size(), 1);

        // Redo brings the undone copy back from the trash.
        QVERIFY(controller.redo());
        QTRY_COMPARE(finished.size(), 3);
        QCOMPARE(readFile(dir.filePath("target/file")), QByteArray("data"));
        QVERIFY(controller.isUndoAvailable());
        QVERIFY(!controller.isRedoAvailable());
    }

    void controllerMergeHasNoUndo()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath("source/folder")));
        QVERIFY(QDir().mkpath(dir.filePath("target/folder")));
        writeFile(dir.filePath("source/folder/new"), "new");
        writeFile(dir.filePath("target/folder/old"), "old");
        TaskController controller;
        QSignalSpy finished(&controller, &TaskController::taskFinished);

        // Undoing would take the folder's earlier contents along with the merged ones.
        for (const auto operation : {Operation::Copy, Operation::Move}) {
            controller.submit({operation,
                               {local(dir.filePath("source/folder"))},
                               local(dir.filePath("target")),
                               ConflictPolicy::Replace});
            QTRY_VERIFY(!finished.isEmpty());
            finished.clear();
            QCOMPARE(readFile(dir.filePath("target/folder/new")), QByteArray("new"));
            QCOMPARE(readFile(dir.filePath("target/folder/old")), QByteArray("old"));
            QVERIFY(!controller.isUndoAvailable());
        }
    }

    void controllerMoveUndo()
    {
        QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath("a")));
        QVERIFY(QDir().mkpath(dir.filePath("b")));
        QVERIFY(QDir().mkpath(dir.filePath("target")));
        writeFile(dir.filePath("a/one"), "one");
        writeFile(dir.filePath("b/two"), "two");
        writeFile(dir.filePath("target/two"), "old");
        TaskController controller;
        QSignalSpy finished(&controller, &TaskController::taskFinished);

        controller.submit({Operation::Move,
                           {local(dir.filePath("a/one")), local(dir.filePath("b/two"))},
                           local(dir.filePath("target")),
                           ConflictPolicy::KeepBoth});
        QTRY_COMPARE(finished.size(), 1);
        QCOMPARE(finished.first().first().value<Task *>()->errorString(), QString());
        QCOMPARE(readFile(dir.filePath("target/two (1)")), QByteArray("two"));

        // Each item goes back to its own folder under its old name.
        QVERIFY(controller.undo());
        QTRY_COMPARE(finished.size(), 2);
        QCOMPARE(readFile(dir.filePath("a/one")), QByteArray("one"));
        QCOMPARE(readFile(dir.filePath("b/two")), QByteArray("two"));
        QCOMPARE(readFile(dir.filePath("target/two")), QByteArray("old"));
        QVERIFY(!QFileInfo::exists(dir.filePath("target/two (1)")));

        QVERIFY(controller.redo());
        QTRY_COMPARE(finished.size(), 3);
        QCOMPARE(readFile(dir.filePath("target/one")), QByteArray("one"));
        QCOMPARE(readFile(dir.filePath("target/two (1)")), QByteArray("two"));
    }

    void controllerRenameAndMkdirUndo()
    {
        QTemporaryDir dir;
        TaskController controller;
        QSignalSpy finished(&controller, &TaskController::taskFinished);

        controller.submit({Operation::Mkdir, {}, local(dir.filePath("New Folder"))});
        QTRY_COMPARE(finished.size(), 1);
        controller.submit({Operation::Rename,
                           {local(dir.filePath("New Folder"))},
                           local(dir.filePath("Photos"))});
        QTRY_COMPARE(finished.size(), 2);

        QVERIFY(controller.undo());
        QTRY_COMPARE(finished.size(), 3);
        QVERIFY(QFileInfo(dir.filePath("New Folder")).isDir());
        QVERIFY(!QFileInfo::exists(dir.filePath("Photos")));

        QVERIFY(controller.undo());
        QTRY_COMPARE(finished.size(), 4);
        QVERIFY(!QFileInfo::exists(dir.filePath("New Folder")));
        QVERIFY(!controller.isUndoAvailable());

        QVERIFY(controller.redo());
        QTRY_COMPARE(finished.size(), 5);
        QVERIFY(controller.redo());
        QTRY_COMPARE(finished.size(), 6);
        QVERIFY(QFileInfo(dir.filePath("Photos")).isDir());
    }

    void controllerFailedUndoIsDropped()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("file"));
        TaskController controller;
        QSignalSpy finished(&controller, &TaskController::taskFinished);

        controller.submit({Operation::Rename,
                           {local(dir.filePath("file"))},
                           local(dir.filePath("renamed"))});
        QTRY_COMPARE(finished.size(), 1);
        QVERIFY(QFile::remove(dir.filePath("renamed")));

        auto *task = controller.undo();
        QVERIFY(task);
        QSignalSpy result(task, &Task::result);
        QTRY_COMPARE(result.size(), 1);
        QCOMPARE(task->error(), ENOENT);
        QVERIFY(!controller.isUndoAvailable());
        QVERIFY(!controller.isRedoAvailable());
    }

    void controllerEmptyTrashForgetsRestores()
    {
        QTemporaryDir dir;
        writeFile(dir.filePath("file"));
        writeFile(dir.filePath("other"));
        TaskController controller;
        QSignalSpy finished(&controller, &TaskController::taskFinished);

        controller.submit({Operation::Trash, {local(dir.filePath("file"))}});
        QTRY_COMPARE(finished.size(), 1);
        controller.submit({Operation::Rename,
                           {local(dir.filePath("other"))},
                           local(dir.filePath("renamed"))});
        QTRY_COMPARE(finished.size(), 2);
        controller.submit({Operation::EmptyTrash});
        QTRY_COMPARE(finished.size(), 3);

        QVERIFY(controller.undo());
        QTRY_COMPARE(finished.size(), 4);
        QVERIFY(QFileInfo::exists(dir.filePath("other")));
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

    void copyThatCannotFitFailsBeforeWriting()
    {
        QTemporaryDir target(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)
                             + "/cutefish-io-XXXXXX");
        QVERIFY(target.isValid());

        struct statvfs fs;
        QCOMPARE(::statvfs(QFile::encodeName(target.path()), &fs), 0);
        const qint64 available = qint64(fs.f_bavail) * fs.f_frsize;
        if (available > (qint64(8) << 30))
            QSKIP("The runtime directory has too much free space to fill");

        // Kept on the build tree's disk, so the large source costs no memory.
        QTemporaryDir source(QDir::currentPath() + "/cutefish-io-XXXXXX");
        QVERIFY(source.isValid());
        const qint64 length = available + 64 * 1024 * 1024;

        const int full =
                ::open(QFile::encodeName(source.filePath("full")), O_WRONLY | O_CREAT, 0600);
        QVERIFY(full >= 0);
        const int allocated = ::posix_fallocate(full, 0, length);
        ::close(full);
        if (allocated != 0)
            QSKIP("The build tree has no room for a source larger than the target");

        Task copy({Operation::Copy, {local(source.filePath("full"))}, local(target.path())});
        QSignalSpy failed(&copy, &Task::result);
        copy.start();
        QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(copy.error(), ENOSPC);
        QVERIFY(QDir(target.path())
                        .isEmpty(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot));

        QFile sparse(source.filePath("sparse"));
        QVERIFY(sparse.open(QIODevice::WriteOnly));
        QVERIFY(sparse.resize(length));
        QVERIFY(sparse.seek(length - 4));
        QCOMPARE(sparse.write("tail"), 4);
        sparse.close();

        Task fits({Operation::Copy, {local(sparse.fileName())}, local(target.path())});
        QSignalSpy done(&fits, &Task::result);
        fits.start();
        QTRY_COMPARE(done.size(), 1);
        QCOMPARE(fits.state(), Task::State::Succeeded);
        QCOMPARE(QFileInfo(target.filePath("sparse")).size(), length);
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

        QCOMPARE(errorCode([&] { FileEnumerator::size(dir.path(), scan); }), ECANCELED);
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
