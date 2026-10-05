// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnailers_p.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMimeDatabase>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUrl>

#include <signal.h>
#include <unistd.h>

// Decoders of untrusted media can hang; a stuck one must not hold a pool thread for good.
static constexpr int timeoutMs = 10000;
static constexpr qint64 refreshIntervalMs = 3000;

static QString canonicalName(const QString &mimeType)
{
    const QMimeType type = QMimeDatabase().mimeTypeForName(mimeType);
    return type.isValid() ? type.name() : mimeType;
}

static QStringList thumbnailerDirectories()
{
    QStringList directories;
    for (const QString &data : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        directories.append(data + QStringLiteral("/thumbnailers"));

    return directories;
}

static bool isRunnable(const QString &program)
{
    if (program.isEmpty())
        return false;

    if (QDir::isAbsolutePath(program))
        return QFileInfo(program).isExecutable();

    return !QStandardPaths::findExecutable(program).isEmpty();
}

namespace {

struct Entry
{
    QStringList command;
    QStringList mimeTypes;
};

} // namespace

// The key file subset thumbnailers use: one group, plain keys, no localized values.
static Entry parseEntry(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return {};

    bool inGroup = false;
    QString exec;
    QString tryExec;
    QString mimeTypes;
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        if (line.isEmpty() || line.startsWith(u'#'))
            continue;

        if (line.startsWith(u'[')) {
            inGroup = line == QStringLiteral("[Thumbnailer Entry]");
            continue;
        }

        const qsizetype equals = line.indexOf(u'=');
        if (!inGroup || equals <= 0)
            continue;

        const QString key = line.left(equals).trimmed();
        const QString value = line.mid(equals + 1).trimmed();
        if (key == QStringLiteral("Exec"))
            exec = value;
        else if (key == QStringLiteral("TryExec"))
            tryExec = value;
        else if (key == QStringLiteral("MimeType"))
            mimeTypes = value;
    }

    const QStringList command = QProcess::splitCommand(exec);
    if (command.isEmpty() || !isRunnable(command.first())
        || (!tryExec.isEmpty() && !isRunnable(tryExec)))
        return {};

    return {command, mimeTypes.split(u';', Qt::SkipEmptyParts)};
}

static QStringList expand(const QStringList &command, const QString &input, const QString &output,
                          int size)
{
    QStringList arguments;
    for (const QString &argument : command) {
        QString expanded;
        for (qsizetype i = 0; i < argument.size(); ++i) {
            if (argument.at(i) != u'%' || i + 1 == argument.size()) {
                expanded.append(argument.at(i));
                continue;
            }

            const QChar code = argument.at(++i);
            if (code == u'i')
                expanded.append(input);
            else if (code == u'u')
                expanded.append(QString::fromLatin1(QUrl::fromLocalFile(input).toEncoded()));
            else if (code == u'o')
                expanded.append(output);
            else if (code == u's')
                expanded.append(QString::number(size));
            else if (code == u'%')
                expanded.append(u'%');
        }
        arguments.append(expanded);
    }

    return arguments;
}

Thumbnailers *Thumbnailers::self()
{
    static Thumbnailers thumbnailers;
    return &thumbnailers;
}

bool Thumbnailers::contains(const QString &mimeType)
{
    return !command(mimeType).isEmpty();
}

QStringList Thumbnailers::command(const QString &mimeType)
{
    const QString name = canonicalName(mimeType);

    QMutexLocker locker(&m_mutex);
    if (!m_checked.isValid() || m_checked.elapsed() >= refreshIntervalMs)
        refreshLocked();

    return m_commands.value(name);
}

void Thumbnailers::refreshLocked()
{
    m_checked.start();

    QList<std::pair<QString, QDateTime>> directories;
    for (const QString &directory : thumbnailerDirectories())
        directories.append({directory, QFileInfo(directory).lastModified()});

    // Package installs add or remove files, which touches the directory.
    if (directories == m_directories)
        return;

    m_directories = directories;
    m_commands.clear();

    QSet<QString> seen;
    for (const auto &[directory, modified] : std::as_const(m_directories)) {
        const QStringList names = QDir(directory).entryList({QStringLiteral("*.thumbnailer")},
                                                            QDir::Files, QDir::Name);
        for (const QString &name : names) {
            // A file in an earlier directory hides the one of the same name after it.
            if (seen.contains(name))
                continue;
            seen.insert(name);

            const Entry entry = parseEntry(directory + u'/' + name);
            for (const QString &mimeType : entry.mimeTypes) {
                const QString canonical = canonicalName(mimeType);
                if (!m_commands.contains(canonical))
                    m_commands.insert(canonical, entry.command);
            }
        }
    }
}

QImage Thumbnailers::generate(const QString &path, const QString &mimeType, const QSize &size)
{
    const QStringList command = this->command(mimeType);
    if (command.isEmpty())
        return {};

    QTemporaryDir directory;
    if (!directory.isValid())
        return {};

    const QString output = directory.filePath(QStringLiteral("thumbnail.png"));
    QStringList arguments =
            expand(command, QFileInfo(path).absoluteFilePath(), output, qMax(size.width(), size.height()));

    QProcess process;
    process.setProgram(arguments.takeFirst());
    process.setArguments(arguments);
    process.setWorkingDirectory(directory.path());
    process.setStandardInputFile(QProcess::nullDevice());
    process.setStandardOutputFile(QProcess::nullDevice());
    process.setStandardErrorFile(QProcess::nullDevice());
    // Its own process group, so a timeout also stops what a wrapper script started.
    process.setChildProcessModifier([] { ::setpgid(0, 0); });
    process.start();
    if (!process.waitForStarted())
        return {};

    if (!process.waitForFinished(timeoutMs)) {
        ::kill(-pid_t(process.processId()), SIGKILL);
        process.waitForFinished();
        return {};
    }

    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
        return {};

    QImageReader reader(output);
    const QSize outputSize = reader.size();
    if (!outputSize.isValid())
        return {};

    // Thumbnailers may write a larger image than asked for.
    if (outputSize.width() > size.width() || outputSize.height() > size.height())
        reader.setScaledSize(outputSize.scaled(size, Qt::KeepAspectRatio));

    return reader.read();
}
