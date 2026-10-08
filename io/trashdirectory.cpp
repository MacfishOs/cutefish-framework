// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "trashdirectory_p.h"

#include "error.h"
#include "filesystemurl.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>

#include <algorithm>
#include <cerrno>
#include <sys/stat.h>
#include <unistd.h>

namespace Cutefish::IO::Internal {

static QString s_mountTable = QStringLiteral("/proc/self/mountinfo");

void setMountTable(const QString &path)
{
    s_mountTable = path;
}

static QString joined(const QString &directory, const QString &name)
{
    return directory.endsWith('/') ? directory + name : directory + '/' + name;
}

static QString userId()
{
    return QString::number(::getuid());
}

TrashDirectory homeTrash()
{
    return {QStringLiteral("home"), FileSystemUrl::trashRoot(), {}};
}

TrashDirectory volumeTrash(const QString &topdir, bool shared)
{
    const QString id =
            (shared ? 's' : 'u') + QString::fromLatin1(QFile::encodeName(topdir).toHex());
    const QString path = shared ? joined(topdir, ".Trash/" + userId())
                                : joined(topdir, ".Trash-" + userId());
    return {id, path, topdir};
}

static TrashDirectory trashForId(const QString &id)
{
    if (id == QLatin1String("home"))
        return homeTrash();

    const QString hex = id.mid(1);
    const bool valid = (id.startsWith('s') || id.startsWith('u')) && !hex.isEmpty()
            && hex.size() % 2 == 0 && std::all_of(hex.cbegin(), hex.cend(), [](QChar c) {
                   return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
               });
    if (!valid)
        throw Failure(EINVAL, id);

    const QByteArray bytes = QByteArray::fromHex(hex.toLatin1());
    const QString topdir = QFile::decodeName(bytes);
    if (bytes.contains('\0') || !topdir.startsWith('/') || QDir::cleanPath(topdir) != topdir
        || topdir.split('/').contains(QLatin1String("..")) || QFile::encodeName(topdir) != bytes)
        throw Failure(EINVAL, id);

    return volumeTrash(topdir, id.startsWith('s'));
}

TrashDirectory trashDirectory(const QUrl &url, QString *inside)
{
    const QString path = url.path().mid(1);
    const qsizetype slash = path.indexOf('/');
    const QString id = path.left(slash);

    if (url.scheme() != QLatin1String("trash") || id.isEmpty())
        throw Failure(EINVAL, url.toString());

    if (inside)
        *inside = slash < 0 ? QString() : path.mid(slash + 1);

    return trashForId(id);
}

// Kernel interfaces, automounts and remote filesystems: looking into an automount
// mounts it, and a hung share would block the listing and its watch.
static bool holdsNoTrash(const QByteArray &type)
{
    static const QSet<QByteArray> types = {
            "proc",     "sysfs",     "devtmpfs",   "devpts",      "cgroup",
            "cgroup2",  "debugfs",   "tracefs",    "securityfs",  "bpf",
            "pstore",   "autofs",    "mqueue",     "hugetlbfs",   "configfs",
            "fusectl",  "efivarfs",  "nsfs",       "binfmt_misc", "rpc_pipefs",
            "fuse.portal", "fuse.gvfsd-fuse",
            "nfs",      "nfs4",      "cifs",       "smb3",        "smbfs",
            "ncpfs",    "afs",       "ceph",       "9p",          "glusterfs",
            "coda",     "davfs",     "fuse.sshfs", "fuse.rclone", "fuse.davfs2",
    };

    return types.contains(type);
}

// Mount points in the table escape space, tab, newline and backslash as octal.
static QString unescapeMountPoint(const QByteArray &field)
{
    QByteArray result;
    result.reserve(field.size());

    const auto octal = [&](qsizetype i) {
        return i < field.size() && field[i] >= '0' && field[i] <= '7';
    };

    for (qsizetype i = 0; i < field.size(); ++i) {
        if (field[i] == '\\' && octal(i + 1) && octal(i + 2) && octal(i + 3)) {
            result.append(char((field[i + 1] - '0') * 64 + (field[i + 2] - '0') * 8
                               + (field[i + 3] - '0')));
            i += 3;
        } else {
            result.append(field[i]);
        }
    }

    return QFile::decodeName(result);
}

// Each mount point with the type mounted there last, which hides any below it.
static QList<QPair<QString, QByteArray>> mounts()
{
    QFile table(s_mountTable);
    if (!table.open(QIODevice::ReadOnly))
        return {};

    QList<QPair<QString, QByteArray>> result;

    for (const QByteArray &line : table.readAll().split('\n')) {
        // ID, parent, device, root, mount point, options, optional fields, "-",
        // type, source, super options.
        const QList<QByteArray> fields = line.split(' ');
        const qsizetype separator = fields.indexOf("-", 6);
        if (fields.size() < 5 || separator < 0 || separator + 1 >= fields.size())
            continue;

        const QString mountPoint = unescapeMountPoint(fields[4]);
        if (!mountPoint.startsWith('/'))
            continue;

        const auto listed = std::find_if(result.begin(), result.end(), [&](const auto &mount) {
            return mount.first == mountPoint;
        });
        if (listed != result.end())
            listed->second = fields[separator + 1];
        else
            result.append({mountPoint, fields[separator + 1]});
    }

    return result;
}

static QStringList mountPoints()
{
    QStringList result;

    for (const auto &[mountPoint, type] : mounts()) {
        if (!holdsNoTrash(type))
            result.append(mountPoint);
    }

    return result;
}

bool volumeTakesTrash(const QString &topdir)
{
    for (const auto &[mountPoint, type] : mounts()) {
        if (mountPoint == topdir)
            return !holdsNoTrash(type);
    }

    return true;
}

static bool isOwnDirectory(const QString &path, struct stat *st)
{
    return ::lstat(QFile::encodeName(path), st) == 0 && S_ISDIR(st->st_mode)
            && st->st_uid == ::getuid();
}

// Per the trash specification, $topdir/.Trash is used only when it is a sticky
// directory and not a link, since every user creates their own folder in it.
static bool isSharedTrashUsable(const QString &topdir)
{
    struct stat st;
    return ::lstat(QFile::encodeName(joined(topdir, ".Trash")), &st) == 0 && S_ISDIR(st.st_mode)
            && (st.st_mode & S_ISVTX);
}

// Creates path as a folder of the user's own, or checks the one there.
static bool ensureOwnDirectory(const QString &path)
{
    struct stat st;
    if (::mkdir(QFile::encodeName(path), 0700) < 0 && errno != EEXIST)
        return false;

    return isOwnDirectory(path, &st);
}

bool prepareVolumeTrash(const TrashDirectory &trash)
{
    if (trash.id.startsWith('s') && !isSharedTrashUsable(trash.topdir))
        return false;

    // A read-only medium keeps the folders it has but takes nothing.
    return ensureOwnDirectory(trash.path) && ensureOwnDirectory(trash.path + "/files")
            && ensureOwnDirectory(trash.path + "/info")
            && ::access(QFile::encodeName(trash.path + "/files"), W_OK) == 0
            && ::access(QFile::encodeName(trash.path + "/info"), W_OK) == 0;
}

QList<TrashDirectory> trashDirectories(QStringList *mountPointsLookedIn)
{
    QList<TrashDirectory> result {homeTrash()};
    QSet<QPair<dev_t, ino_t>> seen;

    struct stat st;
    if (::stat(QFile::encodeName(result.first().path), &st) == 0)
        seen.insert({st.st_dev, st.st_ino});

    const QStringList points = mountPoints();
    if (mountPointsLookedIn)
        *mountPointsLookedIn = points;

    for (const QString &topdir : points) {
        for (const bool shared : {true, false}) {
            if (shared && !isSharedTrashUsable(topdir))
                continue;

            const TrashDirectory trash = volumeTrash(topdir, shared);
            // A volume mounted twice, or bound into another place, has one trash.
            if (isOwnDirectory(trash.path, &st) && !seen.contains({st.st_dev, st.st_ino})) {
                seen.insert({st.st_dev, st.st_ino});
                result.append(trash);
            }
        }
    }

    return result;
}

void trashWatches(QStringList *paths, QHash<QString, QList<QByteArray>> *entries)
{
    QStringList points;

    for (const TrashDirectory &trash : trashDirectories(&points)) {
        const QString files = trash.path + "/files";
        if (QFileInfo(files).isDir())
            paths->append(files);
        else
            (*entries)[trash.path].append("files");

        const QFileInfo directory(trash.path);
        (*entries)[directory.absolutePath()].append(QFile::encodeName(directory.fileName()));
    }

    const QByteArray user = QFile::encodeName(".Trash-" + userId());
    for (const QString &topdir : points) {
        (*entries)[topdir].append({".Trash", user});

        if (QFileInfo(joined(topdir, ".Trash")).isDir())
            (*entries)[joined(topdir, ".Trash")].append(QFile::encodeName(userId()));
    }
}

// Paths are absolute, or in a volume's trash relative to its mount point.
static QString resolvedPath(const TrashDirectory &trash, const QString &path)
{
    if (path.isEmpty() || path.contains(QChar(0)))
        return {};

    if (!QDir::isAbsolutePath(path)) {
        if (trash.topdir.isEmpty() || path.split('/').contains(QLatin1String("..")))
            return {};

        return resolvedPath(trash, QDir::cleanPath(joined(trash.topdir, path)));
    }

    const QString clean = QDir::cleanPath(path);
    return clean == QLatin1String("/") ? QString() : clean;
}

bool readTrashInfo(const TrashDirectory &trash, const QString &name, TrashInfo *info)
{
    QFile file(trash.path + "/info/" + name + ".trashinfo");
    if (!file.open(QIODevice::ReadOnly))
        return false;

    bool group = false;

    while (!file.atEnd()) {
        const auto line = file.readLine(1024 * 1024).trimmed();
        if (line.startsWith('['))
            group = line == "[Trash Info]";
        else if (group && line.startsWith("Path="))
            info->path = resolvedPath(trash, QUrl::fromPercentEncoding(line.mid(5)));
        else if (group && line.startsWith("DeletionDate="))
            info->deletionTime =
                    QDateTime::fromString(QString::fromLatin1(line.mid(13)), Qt::ISODate);
    }

    return true;
}

} // namespace Cutefish::IO::Internal
