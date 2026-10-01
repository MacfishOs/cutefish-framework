#include "windowhints.h"

#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTest>

class WindowHintsTest : public QObject
{
    Q_OBJECT

private slots:
    void cppAccessBeforeQmlRegistration()
    {
        QWindow window;
        auto *hints = Cutefish::WindowHints::forWindow(&window);
        QVERIFY(hints);
        QCOMPARE(Cutefish::WindowHints::forWindow(&window), hints);
        QVERIFY(!hints->skipTaskbar());
        QSignalSpy changed(hints, &Cutefish::WindowHints::skipTaskbarChanged);
        hints->setSkipTaskbar(true);
        hints->setSkipTaskbar(true);
        QCOMPARE(changed.count(), 1);
        if (qEnvironmentVariableIsSet("CUTEFISH_TEST_WINDOW_HINTS"))
            QVERIFY(hints->available());
        QVERIFY(!hints->alwaysOnTop());
        QSignalSpy onTopChanged(hints, &Cutefish::WindowHints::alwaysOnTopChanged);
        hints->setAlwaysOnTop(true);
        hints->setAlwaysOnTop(true);
        QCOMPARE(onTopChanged.count(), 1);
        QVERIFY(!hints->resizeAroundCenter());
        QSignalSpy anchorChanged(hints, &Cutefish::WindowHints::resizeAroundCenterChanged);
        hints->setResizeAroundCenter(true);
        hints->setResizeAroundCenter(true);
        QCOMPARE(anchorChanged.count(), 1);
        window.create();
        window.destroy();
        window.create();
        QVERIFY(hints->skipTaskbar());
        QVERIFY(hints->alwaysOnTop());
        QVERIFY(hints->resizeAroundCenter());
    }

    void qmlAndCppShareHintsAcrossSurfaceRecreation()
    {
        QQmlEngine engine;
        engine.addImportPath(APPLICATION_QML_PATH);
        QQmlComponent component(&engine);
        component.setData(R"(
            import QtQuick
            import Cutefish.Application
            Window {
                width: 100
                height: 80
                WindowHints.skipTaskbar: true
                WindowHints.alwaysOnTop: true
                WindowHints.resizeAroundCenter: true
                property bool skipped: WindowHints.skipTaskbar
                property bool onTop: WindowHints.alwaysOnTop
            }
        )", QUrl());
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *window = qobject_cast<QQuickWindow *>(object.data());
        QVERIFY(window);
        auto *hints = Cutefish::WindowHints::forWindow(window);
        QVERIFY(hints->skipTaskbar());
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        window->hide();
        window->destroy();
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(hints->skipTaskbar());
        QVERIFY(hints->alwaysOnTop());
        QVERIFY(hints->resizeAroundCenter());
        hints->setSkipTaskbar(false);
        QCOMPARE(object->property("skipped").toBool(), false);
        hints->setAlwaysOnTop(false);
        QCOMPARE(object->property("onTop").toBool(), false);
    }
};

QTEST_MAIN(WindowHintsTest)

#include "tst_windowhints.moc"
