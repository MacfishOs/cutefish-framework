#include "applicationregistry.h"
#include "applicationruntime.h"
#include "cutefishapplication.h"

#include <QCoreApplication>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTemporaryDir>
#include <QTest>

class ApplicationQmlTest : public QObject
{
    Q_OBJECT

private slots:
    void importsWithoutADisplayAndSharesCppInstances()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        qputenv("XDG_DATA_HOME", directory.path().toUtf8());
        qputenv("XDG_DATA_DIRS", directory.path().toUtf8());

        auto *registry = ApplicationRegistry::instance();
        auto *runtime = ApplicationRuntime::instance();
        QTRY_VERIFY(!registry->loading());
        DesktopEntryData entry;
        QVERIFY(DesktopEntry::parse("test-app", "/applications/test-app.desktop",
                                    "[Desktop Entry]\nType=Application\nName=Test\nExec=true\n",
                                    &entry));
        registry->applyScan({entry});
        QPointer<ApplicationRegistry> registryGuard(registry);
        QPointer<ApplicationRuntime> runtimeGuard(runtime);

        for (int i = 0; i < 2; ++i) {
            QQmlEngine engine;
            engine.addImportPath(APPLICATION_QML_PATH);
            QQmlComponent component(&engine);
            component.setData(R"(
                import QtQml
                import Cutefish.Application
                QtObject {
                    property var entries: DesktopEntries
                    property var runtime: AppRuntime
                    property DesktopEntry entry: DesktopEntries.byId("test-app")
                }
            )", QUrl());
            QScopedPointer<QObject> object(component.create());
            QVERIFY2(object, qPrintable(component.errorString()));
            QCOMPARE(object->property("entries").value<QObject *>(), registry);
            QCOMPARE(object->property("runtime").value<QObject *>(), runtime);
            QCOMPARE(object->property("entry").value<DesktopEntry *>(), registry->byId("test-app"));
        }

        QVERIFY(registryGuard);
        QVERIFY(runtimeGuard);
        QVERIFY(!runtime->launchCommand({}));
    }
};

int main(int argc, char **argv)
{
    Cutefish::Application::prepare();
    QCoreApplication app(argc, argv);
    ApplicationQmlTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_applicationqml.moc"
