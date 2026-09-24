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
