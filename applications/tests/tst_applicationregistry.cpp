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
};

QTEST_GUILESS_MAIN(ApplicationRegistryTest)
#include "tst_applicationregistry.moc"
