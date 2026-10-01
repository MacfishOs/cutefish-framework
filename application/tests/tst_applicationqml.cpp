#include "cutefishapplication.h"

#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QTest>

class ApplicationQmlTest : public QObject
{
    Q_OBJECT

private slots:
    // Services and other display-less programs import the module too.
    void importsWithoutADisplay()
    {
        QQmlEngine engine;
        engine.addImportPath(APPLICATION_QML_PATH);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQml
            import Cutefish.Application
            QtObject {}
        )", QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
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
