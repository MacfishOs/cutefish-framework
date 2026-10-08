// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "mimeapps.h"
#include "applicationregistry.h"
#include "desktopentry.h"

#include <QFile>
#include <QMimeDatabase>
#include <QTemporaryDir>
#include <QtTest>

static void writeList(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(contents);
}

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

        // The desktop's own list in the config home comes first, the plain one after it,
        // and the system's lists follow.
        const QStringList paths = MimeApps::listPaths();
        QVERIFY(paths.first().startsWith(dir.path()));
        QVERIFY(paths.first().endsWith("-mimeapps.list"));
        const int user = paths.indexOf(dir.filePath("mimeapps.list"));
        QVERIFY(user > 0);
        QVERIFY(paths.size() > user + 1);
        QVERIFY(!paths.at(user + 1).startsWith(dir.path()));
    }

    void earlierListsOverrideLaterOnes()
    {
        QTemporaryDir dir;
        const QString user = dir.filePath("user.list");
        const QString system = dir.filePath("system.list");
        writeList(user, "[Default Applications]\ntext/plain=gone.desktop;\n"
                        "[Removed Associations]\ntext/plain=system-editor.desktop;\n");
        writeList(system, "[Default Applications]\ntext/plain=system-editor.desktop;\n"
                          "[Added Associations]\ntext/plain=viewer.desktop;system-editor.desktop;\n");

        auto *registry = ApplicationRegistry::instance();
        QTRY_VERIFY(!registry->loading());
        DesktopEntryData systemEditor;
        DesktopEntryData viewer;
        DesktopEntryData otherEditor;
        QVERIFY(DesktopEntry::parse(
                "system-editor", "/applications/system-editor.desktop",
                "[Desktop Entry]\nType=Application\nName=System Editor\nExec=system-editor %F\n"
                "MimeType=text/plain;\n", &systemEditor));
        QVERIFY(DesktopEntry::parse(
                "viewer", "/applications/viewer.desktop",
                "[Desktop Entry]\nType=Application\nName=Viewer\nExec=viewer %F\n", &viewer));
        QVERIFY(DesktopEntry::parse(
                "other-editor", "/applications/other-editor.desktop",
                "[Desktop Entry]\nType=Application\nName=Other Editor\nExec=other-editor %F\n"
                "MimeType=text/plain;\n", &otherEditor));
        registry->applyScan({systemEditor, viewer, otherEditor});

        const QMimeType text = QMimeDatabase().mimeTypeForName("text/plain");
        const QStringList chain {user, system};
        // The user's default is not installed, and the user removed the system's default.
        QVERIFY(MimeApps::chosenApplication(chain, text).isEmpty());
        // The system's addition comes before the entries that list the type; the removed
        // application is out even though the system list adds it and it lists the type.
        QCOMPARE(MimeApps::applications(text, chain),
                 QStringList({"/applications/viewer.desktop", "/applications/other-editor.desktop"}));
        QCOMPARE(MimeApps::defaultApplication(chain, text), "/applications/viewer.desktop");
        QCOMPARE(MimeApps::chosenApplication({system}, text), "/applications/system-editor.desktop");
        QCOMPARE(MimeApps::applications(text, {}),
                 QStringList({"/applications/system-editor.desktop",
                              "/applications/other-editor.desktop"}));
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
