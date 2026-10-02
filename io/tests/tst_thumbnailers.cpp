// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "thumbnailcache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

#include <unistd.h>

namespace {

void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(data), data.size());
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

QString localUrl(const QString &path)
{
    return QUrl::fromLocalFile(path).toString();
}

} // namespace

class ThumbnailersTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_tools;
    QTemporaryDir m_files;
    QString m_thumbnailers;
    QString m_cache;

    void installThumbnailer(const QString &name, const QByteArray &body)
    {
        writeFile(m_thumbnailers + u'/' + name, "[Thumbnailer Entry]\n" + body);
    }

private slots:
    void initTestCase()
    {
        QStandardPaths::setTestModeEnabled(true);
        QVERIFY(m_tools.isValid());
        QVERIFY(m_files.isValid());

        m_thumbnailers = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
                + QStringLiteral("/thumbnailers");
        m_cache = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation)
                + QStringLiteral("/thumbnails");
        QDir(m_thumbnailers).removeRecursively();
        QDir(m_cache).removeRecursively();
        QVERIFY(QDir().mkpath(m_thumbnailers));

        QImage source(300, 150, QImage::Format_RGB32);
        source.fill(Qt::red);
        QVERIFY(source.save(m_tools.filePath("source.png")));

        // Records its arguments and writes the source image to the last one.
        const QString script = m_tools.filePath("thumbnail.sh");
        writeFile(script, "#!/bin/sh\nprintf '%s\\n' \"$@\" > " + m_tools.filePath("arguments").toUtf8()
                          + "\nfor last; do :; done\ncp " + m_tools.filePath("source.png").toUtf8()
                          + " \"$last\"\n");
        QVERIFY(QFile::setPermissions(script, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner));

        installThumbnailer("record.thumbnailer",
                           "TryExec=" + script.toUtf8() + "\nExec=" + script.toUtf8()
                                   + " -s %s %u literal%% %o\nMimeType=text/plain;\n");
        installThumbnailer("fail.thumbnailer", "Exec=false %i %o\nMimeType=text/csv;\n");
        installThumbnailer("hang.thumbnailer", "Exec=sh -c \"sleep 63\" sh %i %o\nMimeType=text/x-csrc;\n");
        installThumbnailer("missing.thumbnailer",
                           "TryExec=/nonexistent/thumbnailer\nExec=/nonexistent/thumbnailer %i %o\n"
                           "MimeType=application/json;\n");
    }

    void cleanupTestCase()
    {
        QDir(m_thumbnailers).removeRecursively();
        QDir(m_cache).removeRecursively();
    }

    void runsInstalledThumbnailer()
    {
        QVERIFY(ThumbnailCache::self()->canThumbnail(QStringLiteral("text/plain")));

        const QString note = m_files.filePath("a note.txt");
        writeFile(note, "text");
        const Thumbnail thumbnail = ThumbnailCache::self()->requestThumbnail(localUrl(note), QSize(64, 64));

        // The normal flavour is 128 pixels; the larger output is scaled into it.
        QCOMPARE(thumbnail.image.size(), QSize(128, 64));
        QVERIFY(thumbnail.path.startsWith(m_cache + QStringLiteral("/normal/")));
        QVERIFY(QFileInfo::exists(thumbnail.path));

        const QStringList arguments =
                QString::fromUtf8(readFile(m_tools.filePath("arguments"))).split(u'\n', Qt::SkipEmptyParts);
        QCOMPARE(arguments.size(), 5);
        QCOMPARE(arguments.at(0), QStringLiteral("-s"));
        QCOMPARE(arguments.at(1), QStringLiteral("128"));
        QCOMPARE(arguments.at(2), QString::fromLatin1(QUrl::fromLocalFile(note).toEncoded()));
        QCOMPARE(arguments.at(3), QStringLiteral("literal%"));
        QVERIFY(arguments.at(4).endsWith(QStringLiteral("/thumbnail.png")));
        QVERIFY(!arguments.at(4).startsWith(m_cache));

        // A second request reads the cache instead of running the thumbnailer again.
        QFile::remove(m_tools.filePath("arguments"));
        const Thumbnail cached = ThumbnailCache::self()->requestThumbnail(localUrl(note), QSize(64, 64));
        QCOMPARE(cached.path, thumbnail.path);
        QVERIFY(!QFileInfo::exists(m_tools.filePath("arguments")));
    }

    void imagesStayInProcess()
    {
        QVERIFY(ThumbnailCache::self()->canThumbnail(QStringLiteral("image/png")));

        const QString image = m_files.filePath("image.png");
        QImage source(40, 20, QImage::Format_RGB32);
        source.fill(Qt::blue);
        QVERIFY(source.save(image));

        QFile::remove(m_tools.filePath("arguments"));
        const Thumbnail thumbnail = ThumbnailCache::self()->requestThumbnail(localUrl(image), QSize(64, 64));
        QCOMPARE(thumbnail.image.size(), QSize(128, 64));
        QVERIFY(!QFileInfo::exists(m_tools.filePath("arguments")));
    }

    void failureIsRemembered()
    {
        QVERIFY(ThumbnailCache::self()->canThumbnail(QStringLiteral("text/csv")));

        const QString table = m_files.filePath("table.csv");
        writeFile(table, "a,b\n");
        const Thumbnail thumbnail = ThumbnailCache::self()->requestThumbnail(localUrl(table), QSize(64, 64));
        QVERIFY(thumbnail.image.isNull());

        const QString name = QString::fromLatin1(
                QCryptographicHash::hash(QUrl::fromLocalFile(table).toEncoded(), QCryptographicHash::Md5)
                        .toHex());
        QVERIFY(QFileInfo::exists(m_cache + QStringLiteral("/fail/cutefish/") + name + QStringLiteral(".png")));
    }

    void hangingThumbnailerIsStopped()
    {
        const QString script = m_files.filePath("source.c");
        writeFile(script, "int x;\n");

        QElapsedTimer timer;
        timer.start();
        const Thumbnail thumbnail = ThumbnailCache::self()->requestThumbnail(localUrl(script), QSize(64, 64));
        QVERIFY(thumbnail.image.isNull());
        QVERIFY(timer.elapsed() >= 9000);
        QVERIFY(timer.elapsed() < 15000);

        // The child of the wrapper shell goes with it.
        QTRY_COMPARE(QProcess::execute(QStringLiteral("pgrep"),
                                       {QStringLiteral("-u"), QString::number(::getuid()),
                                        QStringLiteral("-fx"), QStringLiteral("sleep 63")}),
                     1);
    }

    void skipsMissingPrograms()
    {
        QVERIFY(!ThumbnailCache::self()->canThumbnail(QStringLiteral("application/json")));
        QVERIFY(!ThumbnailCache::self()->canThumbnail(QStringLiteral("application/x-cutefish-unknown")));
    }

    void picksUpNewThumbnailers()
    {
        QVERIFY(!ThumbnailCache::self()->canThumbnail(QStringLiteral("text/markdown")));
        installThumbnailer("markdown.thumbnailer", "Exec=true %i %o\nMimeType=text/markdown;\n");
        QTRY_VERIFY_WITH_TIMEOUT(ThumbnailCache::self()->canThumbnail(QStringLiteral("text/markdown")),
                                 6000);
    }
};

QTEST_GUILESS_MAIN(ThumbnailersTest)
#include "tst_thumbnailers.moc"
