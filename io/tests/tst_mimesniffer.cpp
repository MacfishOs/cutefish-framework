// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mimesniffer_p.h"

#include <QMimeDatabase>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

using Cutefish::IO::Internal::MimeSniffer;

namespace {

// moc cannot read a raw string literal holding quotes.
const QByteArray UserPackage =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<mime-info xmlns=\"http://www.freedesktop.org/standards/shared-mime-info\">\n"
        "  <mime-type type=\"text/x-matlab\"><glob-deleteall/><glob pattern=\"*.mlab\"/></mime-type>\n"
        "  <mime-type type=\"text/x-makefile\"><glob-deleteall/></mime-type>\n"
        "  <mime-type type=\"text/troff\"><glob-deleteall/></mime-type>\n"
        "  <mime-type type=\"video/mp2t\"><glob-deleteall/><glob pattern=\"*.m2ts\"/></mime-type>\n"
        "  <mime-type type=\"application/x-cutefish-test\">\n"
        "    <glob pattern=\"*.ts\"/>\n"
        "    <magic priority=\"90\"><match type=\"string\" value=\"CUTEFISH\" offset=\"0:64\"/></magic>\n"
        "  </mime-type>\n"
        "</mime-info>\n";

} // namespace

// A user MIME directory, before the system one, that deletes system globs from
// the literal list, the suffix tree and the complex globs, and adds its own.
class MimeSnifferTest : public QObject
{
    Q_OBJECT

    QTemporaryDir m_data;

private slots:
    void initTestCase()
    {
        const QString update = QStandardPaths::findExecutable(QStringLiteral("update-mime-database"));
        if (update.isEmpty())
            QSKIP("update-mime-database is not installed");

        QVERIFY(m_data.isValid());
        QVERIFY(QDir().mkpath(m_data.filePath("mime/packages")));
        QFile package(m_data.filePath("mime/packages/test.xml"));
        QVERIFY(package.open(QIODevice::WriteOnly));
        QCOMPARE(package.write(UserPackage), UserPackage.size());
        package.close();

        QCOMPARE(QProcess::execute(update, {m_data.filePath("mime")}), 0);
        // Before the first QMimeDatabase use, which fixes its directories.
        qputenv("XDG_DATA_HOME", m_data.path().toUtf8());
    }

    void deletedGlobsMatchQt()
    {
        QTemporaryDir dir;
        const QList<QPair<QString, QByteArray>> files = {
                {"Makefile", "all:\n"},
                {"GNUmakefile", "all:\n"},
                {"function.m", "function y = f(x)\nend\n"},
                {"script.mlab", "x = 1\n"},
                {"page.1", "plain words\n"},
                {"libsample.so.6", "\x7f" "ELF"},
                {"video.ts", "xxCUTEFISHxx"},
                {"strings.ts", "<?xml version=\"1.0\"?>\n<TS version=\"2.1\"></TS>\n"},
                {"clip.m2ts", QByteArray(1, 0x47) + QByteArray(187, 0)},
                {"marked", "xxCUTEFISHxx"},
                {"photo.jpg", "\xff\xd8\xff\xe0"},
        };

        const QMimeDatabase database;
        for (const auto &[name, data] : files) {
            QFile file(dir.filePath(name));
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(data), data.size());
            file.close();

            QCOMPARE(name + u' ' + MimeSniffer::self()->mimeTypeForFile(file.fileName()).name(),
                     name + u' ' + database.mimeTypeForFile(file.fileName()).name());
        }

        // The deletions took effect: Qt no longer takes the type from these names.
        QVERIFY(database.mimeTypeForFile(dir.filePath("Makefile")).name()
                != QLatin1String("text/x-makefile"));
        QCOMPARE(database.mimeTypeForFile(dir.filePath("marked")).name(),
                 QStringLiteral("application/x-cutefish-test"));
    }
};

QTEST_GUILESS_MAIN(MimeSnifferTest)
#include "tst_mimesniffer.moc"
