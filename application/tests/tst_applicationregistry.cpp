#include "applicationregistry.h"

#include <QTemporaryDir>
#include <QTest>

class ApplicationRegistryTest : public QObject
{
    Q_OBJECT

private slots:

    void resolvesNonMenuWindows()
    {
        QTemporaryDir dataRoot;
        QVERIFY(dataRoot.isValid());
        qputenv("XDG_DATA_HOME", dataRoot.path().toUtf8());
        qputenv("XDG_DATA_DIRS", dataRoot.path().toUtf8());

        ApplicationRegistry registry;
        QTRY_VERIFY(!registry.loading());

        DesktopEntryData launcher;
        QVERIFY(DesktopEntry::parse(
                "fcitx5-configtool", "/applications/fcitx5-configtool.desktop",
                "[Desktop Entry]\nType=Application\nName=Fcitx 5 Configuration\n"
                "Exec=/usr/bin/fcitx5-configtool\nIcon=fcitx\n",
                &launcher));
        DesktopEntryData config;
        QVERIFY(DesktopEntry::parse(
                "org.fcitx.fcitx5-config-qt", "/applications/org.fcitx.fcitx5-config-qt.desktop",
                "[Desktop Entry]\nType=Application\nName=Fcitx 5 Configuration\n"
                "Exec=fcitx5-config-qt\nIcon=fcitx\nNoDisplay=true\n",
                &config));
        registry.applyScan({launcher, config});

        DesktopEntry *entry = registry.byId(config.id);
        QVERIFY(entry);
        QCOMPARE(registry.applicationEntries().size(), 1);
        QCOMPARE(registry.applicationEntries().first()->id(), launcher.id);
        QCOMPARE(registry.resolveWindow(config.id, {}, {}, {}), entry);
        QCOMPARE(registry.resolveWindow({}, {}, "/usr/bin/fcitx5-config-qt", "fcitx5-config-qt"),
                 entry);

        config.noDisplay = false;
        config.onlyShowIn = {QStringLiteral("UnavailableTestDesktop")};
        registry.applyScan({launcher, config});
        QCOMPARE(registry.resolveWindow(config.id, {}, {}, {}), entry);

        config.hidden = true;
        registry.applyScan({launcher, config});
        QVERIFY(!registry.resolveWindow(config.id, {}, {}, {}));
        QVERIFY(!registry.resolveWindow({}, {}, "/usr/bin/fcitx5-config-qt", "fcitx5-config-qt"));
        QCOMPARE(registry.resolveWindow(launcher.id, {}, {}, {}), registry.byId(launcher.id));
    }

    void prefersMainApplicationForSharedExecutable()
    {
        QTemporaryDir dataRoot;
        QVERIFY(dataRoot.isValid());
        qputenv("XDG_DATA_HOME", dataRoot.path().toUtf8());
        qputenv("XDG_DATA_DIRS", dataRoot.path().toUtf8());

        ApplicationRegistry registry;
        QTRY_VERIFY(!registry.loading());

        DesktopEntryData disks;
        QVERIFY(DesktopEntry::parse(
                "org.gnome.DiskUtility", "/applications/org.gnome.DiskUtility.desktop",
                "[Desktop Entry]\nType=Application\nName=Disks\nExec=gnome-disks\n",
                &disks));
        DesktopEntryData writer;
        QVERIFY(DesktopEntry::parse(
                "gnome-disk-image-writer", "/applications/gnome-disk-image-writer.desktop",
                "[Desktop Entry]\nType=Application\nName=Disk Image Writer\n"
                "Exec=gnome-disks --restore-disk-image %U\nNoDisplay=true\n"
                "StartupWMClass=DiskImageWriter\n",
                &writer));

        for (bool helperFirst : {true, false}) {
            registry.applyScan(helperFirst ? QList<DesktopEntryData>{writer, disks}
                                           : QList<DesktopEntryData>{disks, writer});

            QCOMPARE(registry.applicationEntries().size(), 1);
            QCOMPARE(registry.resolveWindow({}, {}, "/usr/bin/gnome-disks", "gnome-disks"),
                     registry.byId(disks.id));
            QCOMPARE(registry.resolveWindow({}, {}, "gnome-disks", "gnome-disks"),
                     registry.byId(disks.id));
            QCOMPARE(registry.resolveWindow(writer.id, {}, "/usr/bin/gnome-disks", "gnome-disks"),
                     registry.byId(writer.id));
            QCOMPARE(registry.resolveWindow({}, "DiskImageWriter", {}, "gnome-disks"),
                     registry.byId(writer.id));
            QVERIFY(!registry.resolveWindow({}, {}, {}, {}));
        }
    }

    void prefersEntryForCurrentDesktop()
    {
        QTemporaryDir dataRoot;
        QVERIFY(dataRoot.isValid());
        qputenv("XDG_DATA_HOME", dataRoot.path().toUtf8());
        qputenv("XDG_DATA_DIRS", dataRoot.path().toUtf8());
        qputenv("XDG_CURRENT_DESKTOP", "Cutefish");

        ApplicationRegistry registry;
        QTRY_VERIFY(!registry.loading());

        DesktopEntryData monitor;
        QVERIFY(DesktopEntry::parse(
                "org.gnome.SystemMonitor", "/applications/org.gnome.SystemMonitor.desktop",
                "[Desktop Entry]\nType=Application\nName=System Monitor\n"
                "Exec=gnome-system-monitor\nStartupWMClass=gnome-system-monitor\n",
                &monitor));
        DesktopEntryData kdeMonitor;
        QVERIFY(DesktopEntry::parse(
                "gnome-system-monitor-kde", "/applications/gnome-system-monitor-kde.desktop",
                "[Desktop Entry]\nType=Application\nName=GNOME System Monitor\n"
                "Exec=gnome-system-monitor\nStartupWMClass=gnome-system-monitor\n"
                "OnlyShowIn=KDE;\n",
                &kdeMonitor));

        for (bool otherDesktopFirst : {true, false}) {
            registry.applyScan(otherDesktopFirst
                                       ? QList<DesktopEntryData>{kdeMonitor, monitor}
                                       : QList<DesktopEntryData>{monitor, kdeMonitor});

            // Dialogs without a GtkApplication report the program name as app_id.
            QCOMPARE(registry.resolveWindow("gnome-system-monitor", "gnome-system-monitor",
                                            "/usr/bin/gnome-system-monitor",
                                            "gnome-system-monitor"),
                     registry.byId(monitor.id));
            QCOMPARE(registry.resolveWindow(kdeMonitor.id, {}, {}, {}),
                     registry.byId(kdeMonitor.id));
        }
    }

    void rejectsLooseMatches()
    {
        QTemporaryDir dataRoot;
        QVERIFY(dataRoot.isValid());
        qputenv("XDG_DATA_HOME", dataRoot.path().toUtf8());
        qputenv("XDG_DATA_DIRS", dataRoot.path().toUtf8());

        ApplicationRegistry registry;
        QTRY_VERIFY(!registry.loading());

        DesktopEntryData vim;
        QVERIFY(DesktopEntry::parse(
                "vim", "/applications/vim.desktop",
                "[Desktop Entry]\nType=Application\nName=Vim\nExec=vim %F\nTerminal=true\n",
                &vim));
        DesktopEntryData python;
        QVERIFY(DesktopEntry::parse(
                "python3.13", "/applications/python3.13.desktop",
                "[Desktop Entry]\nType=Application\nName=Python (v3.13)\n"
                "Exec=/usr/bin/python3.13\nTerminal=true\n",
                &python));
        DesktopEntryData obs;
        QVERIFY(DesktopEntry::parse(
                "com.obsproject.Studio", "/applications/com.obsproject.Studio.desktop",
                "[Desktop Entry]\nType=Application\nName=OBS Studio\nExec=obs\n", &obs));
        DesktopEntryData codeBlocks;
        QVERIFY(DesktopEntry::parse(
                "codeblocks", "/applications/codeblocks.desktop",
                "[Desktop Entry]\nType=Application\nName=Code::Blocks IDE\nExec=codeblocks\n",
                &codeBlocks));
        DesktopEntryData thunderbird;
        QVERIFY(DesktopEntry::parse(
                "thunderbird", "/applications/thunderbird.desktop",
                "[Desktop Entry]\nType=Application\nName=Thunderbird\nExec=thunderbird %u\n",
                &thunderbird));
        DesktopEntryData printers;
        QVERIFY(DesktopEntry::parse(
                "system-config-printer", "/applications/system-config-printer.desktop",
                "[Desktop Entry]\nType=Application\nName=Print Settings\nExec=system-config-printer\n",
                &printers));
        registry.applyScan({vim, python, obs, codeBlocks, thunderbird, printers});

        // Terminal entries own no windows through their process.
        QVERIFY(!registry.resolveWindow("myscript", "myscript", "/usr/bin/python3.13",
                                        "python3.13"));
        QVERIFY(!registry.resolveWindow("nvim-qt", "nvim-qt", "/usr/bin/nvim-qt", "nvim-qt"));
        QVERIFY(!registry.resolveWindow({}, {}, "/usr/bin/vim", "vim"));
        QCOMPARE(registry.resolveWindow("vim", "vim", {}, {}), registry.byId(vim.id));

        // Executable paths match whole components, and prefixes whole words.
        QVERIFY(!registry.resolveWindow("jobsviewer", "jobsviewer", "/usr/libexec/jobsviewer",
                                        "jobsviewer"));
        QVERIFY(!registry.resolveWindow("code", "code", {}, {}));
        QVERIFY(!registry.resolveWindow("c", "c", {}, {}));
        QCOMPARE(registry.resolveWindow("unknown", "unknown", "/usr/lib/thunderbird/thunderbird-bin",
                                        "thunderbird-bin"),
                 registry.byId(thunderbird.id));
        QCOMPARE(registry.resolveWindow({}, {}, "/usr/share/system-config-printer/system-config-printer.py",
                                        "system-config-printer.py"),
                 registry.byId(printers.id));
        QVERIFY(!registry.resolveWindow({}, {}, "/usr/share/system-config-printer/applet.py",
                                        "applet.py"));
        QCOMPARE(registry.resolveWindow("com.obsproject", "com.obsproject", {}, {}),
                 registry.byId(obs.id));
    }

    void localizesByLanguage()
    {
        const QByteArray contents = "[Desktop Entry]\nType=Application\nName=Files\n"
                                    "Name[zh_CN]=文件\nName[sr@latin]=Datoteke\nExec=files\n";
        DesktopEntryData data;

        QVERIFY(DesktopEntry::parse("files", "/applications/files.desktop", contents, &data,
                                    "zh_CN"));
        QCOMPARE(data.name, QStringLiteral("文件"));
        QVERIFY(DesktopEntry::parse("files", "/applications/files.desktop", contents, &data,
                                    "sr_RS@latin"));
        QCOMPARE(data.name, QStringLiteral("Datoteke"));
        QVERIFY(DesktopEntry::parse("files", "/applications/files.desktop", contents, &data,
                                    "de_DE"));
        QCOMPARE(data.name, QStringLiteral("Files"));
    }
};

QTEST_GUILESS_MAIN(ApplicationRegistryTest)
#include "tst_applicationregistry.moc"
