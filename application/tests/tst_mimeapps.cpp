// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mimeapps.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class MimeAppsTest : public QObject
{
    Q_OBJECT

private slots:
    void userListsLiveInTheConfigHome()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        qputenv("XDG_CONFIG_HOME", dir.path().toUtf8());
        QCOMPARE(MimeApps::userListPath(), dir.filePath("mimeapps.list"));
        QCOMPARE(MimeApps::userTerminalsListPath(), dir.filePath("xdg-terminals.list"));
    }

    void preservesAssociations()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("mimeapps.list");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# Keep this comment\n[Default Applications]\nimage/png=image.desktop;\n"
                   "[Added Associations]\ntext/plain=old.desktop;new.desktop;\n"
                   "[Removed Associations]\ntext/plain=new.desktop;blocked.desktop;\n");
        file.close();
        QVERIFY(MimeApps::setDefault(path, "text/plain", "new.desktop"));
        QVERIFY(MimeApps::setDefault(path, "text/plain", "new.desktop"));
        QCOMPARE(MimeApps::associations(path, "Default Applications", "text/plain"),
                 QStringList {"new.desktop"});
        QCOMPARE(MimeApps::associations(path, "Default Applications", "image/png"),
                 QStringList {"image.desktop"});
        QCOMPARE(MimeApps::associations(path, "Added Associations", "text/plain"),
                 QStringList({"new.desktop", "old.desktop"}));
        QCOMPARE(MimeApps::associations(path, "Removed Associations", "text/plain"),
                 QStringList {"blocked.desktop"});
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto contents = file.readAll();
        QVERIFY(contents.contains("# Keep this comment"));
        QVERIFY(contents.contains("text/plain=new.desktop;"));
        QVERIFY(!contents.contains("text%2Fplain"));
        QVERIFY(!MimeApps::setDefault(path, "text/plain", "bad;entry.desktop"));
        QVERIFY(!MimeApps::setDefault(dir.path(), "text/plain", "new.desktop"));
    }

    void terminalListPrependsTheDefault()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("xdg-terminals.list");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("# Keep this comment\n-blocked.desktop\nfirst.desktop\n+new.desktop:new-window\n");
        file.close();
        QCOMPARE(MimeApps::defaultTerminal(path), QStringLiteral("first.desktop"));
        QVERIFY(MimeApps::setDefaultTerminal(path, "new.desktop"));
        QCOMPARE(MimeApps::defaultTerminal(path), QStringLiteral("new.desktop"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("new.desktop\n# Keep this comment\n-blocked.desktop\n"
                                            "first.desktop\n"));
        QVERIFY(!MimeApps::setDefaultTerminal(path, "bad\nentry.desktop"));
    }
};

QTEST_GUILESS_MAIN(MimeAppsTest)
#include "tst_mimeapps.moc"
