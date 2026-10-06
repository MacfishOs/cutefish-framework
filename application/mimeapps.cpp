// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mimeapps.h"
#include "applicationregistry.h"
#include "desktopentry.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLockFile>
#include <QMimeDatabase>
#include <QMimeType>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>

static QStringList readEntries(const QStringList &lines, const QString &group, const QString &key)
{
    QString current;
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (trimmed.startsWith('[') && trimmed.endsWith(']'))
            current = trimmed.mid(1, trimmed.size() - 2);
        else if (current == group && !trimmed.startsWith('#')) {
            const auto equals = line.indexOf('=');
            if (equals >= 0 && line.left(equals).trimmed() == key)
                return line.mid(equals + 1).trimmed().split(';', Qt::SkipEmptyParts);
        }
    }
    return {};
}

static void writeEntry(QStringList &lines, const QString &group, const QString &key,
                       const QStringList &values)
{
    const QString entry = key + '=' + values.join(';') + ';';
    QString current;
    int insertion = -1;
    for (int i = 0; i < lines.size(); ++i) {
        const QString trimmed = lines.at(i).trimmed();
        if (trimmed.startsWith('[') && trimmed.endsWith(']')) {
            if (current == group)
                break;
            current = trimmed.mid(1, trimmed.size() - 2);
            if (current == group)
                insertion = i + 1;
        } else if (current == group) {
            const auto equals = lines.at(i).indexOf('=');
            if (equals >= 0 && lines.at(i).left(equals).trimmed() == key) {
                lines.removeAt(i--);
            } else {
                insertion = i + 1;
            }
        }
    }
    if (insertion < 0) {
        lines.append('[' + group + ']');
        insertion = lines.size();
    }
    lines.insert(insertion, entry);
}

// An entry may carry a "+" (include) or "-" (exclude) prefix and an ":action" suffix.
static QString terminalId(const QString &line)
{
    QString entry = line.trimmed();
    if (entry.startsWith('+') || entry.startsWith('-'))
        entry.remove(0, 1);
    return entry.section(':', 0, 0);
}

// Hidden entries only mask a system entry; programs that need a terminal are not
// offered for opening files.
static bool opensFiles(const DesktopEntry *entry)
{
    return !entry->hidden() && !entry->terminal();
}

static DesktopEntry *entryForDesktopId(QString desktopId)
{
    if (desktopId.endsWith(QLatin1String(".desktop")))
        desktopId.chop(QLatin1String(".desktop").size());

    DesktopEntry *entry = ApplicationRegistry::instance()->byId(desktopId);
    return entry && opensFiles(entry) ? entry : nullptr;
}

static bool writeLines(const QString &path, const QStringList &lines)
{
    QSaveFile output(path);
    if (!output.open(QIODevice::WriteOnly))
        return false;
    const auto data = (lines.join('\n') + '\n').toUtf8();
    return output.write(data) == data.size() && output.commit();
}

QString MimeApps::userListPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QLatin1String("/mimeapps.list");
}

QString MimeApps::userTerminalsListPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QLatin1String("/xdg-terminals.list");
}

QStringList MimeApps::associations(const QString &path, const QString &group,
                                   const QString &mimeType)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return readEntries(QString::fromUtf8(file.readAll()).split('\n'), group, mimeType);
}

bool MimeApps::setDefault(const QString &path, const QString &mimeType, const QString &desktopId)
{
    if (mimeType.isEmpty() || mimeType.contains('\n') || mimeType.contains('\r')
        || mimeType.contains('=') || desktopId.isEmpty() || desktopId.contains('\n')
        || desktopId.contains('\r') || desktopId.contains(';'))
        return false;
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QLockFile lock(path + ".lock");
    if (!lock.tryLock())
        return false;
    QFile input(path);
    QStringList lines;
    if (input.exists()) {
        if (!input.open(QIODevice::ReadOnly))
            return false;
        lines = QString::fromUtf8(input.readAll()).split('\n');
        input.close();
    }
    auto added = readEntries(lines, "Added Associations", mimeType);
    added.removeAll(desktopId);
    added.prepend(desktopId);
    writeEntry(lines, "Default Applications", mimeType, {desktopId});
    writeEntry(lines, "Added Associations", mimeType, added);
    auto removed = readEntries(lines, "Removed Associations", mimeType);
    if (removed.removeAll(desktopId))
        writeEntry(lines, "Removed Associations", mimeType, removed);
    return writeLines(path, lines);
}

QStringList MimeApps::applications(const QMimeType &mimeType)
{
    const QList<DesktopEntry *> entries = ApplicationRegistry::instance()->entries();
    QMimeDatabase mimeDatabase;
    QList<QMimeType> mimeTypes;
    QStringList apps;

    // Without a MIME database every type is invalid, and so is each one's parent.
    if (mimeType.isValid())
        mimeTypes.append(mimeType);

    while (apps.isEmpty() && !mimeTypes.isEmpty()) {
        QStringList names;
        for (const QMimeType &type : std::as_const(mimeTypes))
            names << type.name() << type.aliases();

        QList<DesktopEntry *> matches;
        for (DesktopEntry *entry : entries) {
            if (!opensFiles(entry))
                continue;

            const QStringList entryTypes = entry->mimeTypes();
            if (std::any_of(entryTypes.cbegin(), entryTypes.cend(),
                            [&names](const QString &type) { return names.contains(type); }))
                matches.append(entry);
        }

        // The first one opens files without a default, so keep that the earliest installed.
        std::stable_sort(matches.begin(), matches.end(), [](DesktopEntry *a, DesktopEntry *b) {
            return QFileInfo(a->path()).birthTime() < QFileInfo(b->path()).birthTime();
        });

        QList<DesktopEntry *> added;
        for (DesktopEntry *entry : std::as_const(matches)) {
            // One program installed under two desktop files is offered once.
            const bool duplicate = std::any_of(added.cbegin(), added.cend(),
                                               [entry](const DesktopEntry *other) {
                return other->exec() == entry->exec() && other->name() == entry->name();
            });
            if (duplicate)
                continue;

            added.append(entry);
            apps.append(entry->path());
        }

        QList<QMimeType> parents;
        for (const QMimeType &type : std::as_const(mimeTypes)) {
            for (const QString &name : type.parentMimeTypes()) {
                const QMimeType parent = mimeDatabase.mimeTypeForName(name);
                if (parent.isValid())
                    parents.append(parent);
            }
        }
        mimeTypes = parents;
    }

    return apps;
}

QString MimeApps::chosenApplication(const QString &path, const QMimeType &mimeType)
{
    for (const QString &group :
         {QStringLiteral("Default Applications"), QStringLiteral("Added Associations")}) {
        const auto apps = associations(path, group, mimeType.name());
        for (const QString &app : apps) {
            if (const DesktopEntry *entry = entryForDesktopId(app))
                return entry->path();
        }
    }
    return {};
}

QString MimeApps::defaultApplication(const QString &path, const QMimeType &mimeType)
{
    const QString chosen = chosenApplication(path, mimeType);
    return chosen.isEmpty() ? applications(mimeType).value(0) : chosen;
}

QString MimeApps::defaultTerminal(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    for (const QString &line : QString::fromUtf8(file.readAll()).split('\n')) {
        const QString entry = line.trimmed();
        if (!entry.isEmpty() && !entry.startsWith('#') && !entry.startsWith('-'))
            return terminalId(entry);
    }
    return {};
}

bool MimeApps::setDefaultTerminal(const QString &path, const QString &desktopId)
{
    if (desktopId.isEmpty() || desktopId.contains('\n') || desktopId.contains('\r'))
        return false;
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QLockFile lock(path + ".lock");
    if (!lock.tryLock())
        return false;
    QFile input(path);
    QStringList lines;
    if (input.exists()) {
        if (!input.open(QIODevice::ReadOnly))
            return false;
        lines = QString::fromUtf8(input.readAll()).split('\n', Qt::SkipEmptyParts);
        input.close();
    }
    lines.removeIf([&desktopId](const QString &line) {
        return !line.trimmed().startsWith('#') && terminalId(line) == desktopId;
    });
    lines.prepend(desktopId);
    return writeLines(path, lines);
}
