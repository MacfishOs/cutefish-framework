#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>
#include <QTest>

#include "outputmodel.h"
#include "screen.h"

class ScreenIntegrationTest : public QObject
{
    Q_OBJECT
private slots:

    void multipleDisplaysStayConsistentWithQt()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        qputenv("XDG_RUNTIME_DIR", directory.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
        qputenv("CUTEFISH_DISPLAY_CONFIG_SERVICE", "com.cutefish.DisplayConfig.Test");
        QProcess compositor;
        compositor.start(qEnvironmentVariable("CUTEFISH_TEST_COMPOSITOR"),
                         {"--no-drm", "--no-xwayland", "--headless-outputs", "3", "--socket",
                          "display-test"});
        QVERIFY(compositor.waitForStarted());
        QProcess probe;

        struct Stop {
            QProcess &compositor;
            QProcess &probe;

            ~Stop()
            {
                for (auto *process : {&probe, &compositor}) {
                    process->terminate();
                    if (!process->waitForFinished(3000)) {
                        process->kill();
                        process->waitForFinished();
                    }
                }
            }
        } stop {compositor, probe};

        KScreenScreen screen;
        auto *model = screen.outputModel();
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 3, 5000);
        auto env = QProcessEnvironment::systemEnvironment();
        env.insert("QT_QPA_PLATFORM", "wayland");
        env.insert("WAYLAND_DISPLAY", "display-test");
        env.remove("WAYLAND_SOCKET");
        env.remove("QT_SCALE_FACTOR");
        env.remove("QT_SCREEN_SCALE_FACTORS");
        probe.setProcessEnvironment(env);
        const auto shellProbe = qEnvironmentVariable("CUTEFISH_TEST_SHELL_SCREEN_PROBE");
        probe.start(shellProbe.isEmpty() ? QStringLiteral(SCREEN_PROBE_PATH) : shellProbe);
        QVERIFY(probe.waitForStarted());
        QJsonArray screens;
        const auto count = [&] {
            while (probe.canReadLine())
                screens = QJsonDocument::fromJson(probe.readLine()).array();
            return screens.size();
        };
        QTRY_COMPARE_WITH_TIMEOUT(count(), 3, 5000);
        const QPersistentModelIndex second(model->index(1));
        const auto secondId = model->data(second, DisplayOutputModel::IdRole).toString();
        QVERIFY(!secondId.isEmpty());
        QVERIFY(model->setData(second, 1.25, DisplayOutputModel::ScaleRole));
        QTRY_VERIFY_WITH_TIMEOUT(!screen.busy(), 5000);
        QVERIFY2(screen.error().isEmpty(), qPrintable(screen.error()));
        const auto width = [&] {
            count();
            for (const auto &value : screens) {
                if (value.toObject()["name"] == "Cutefish-2")
                    return value.toObject()["width"].toInt();
            }
            return 0;
        };
        QTRY_COMPARE(width(), 1536);
        QCOMPARE(model->data(second, DisplayOutputModel::IdRole).toString(), secondId);
        QVERIFY(model->setData(model->index(2), false, DisplayOutputModel::EnabledRole));
        QTRY_VERIFY_WITH_TIMEOUT(!screen.busy(), 5000);
        QVERIFY2(screen.error().isEmpty(), qPrintable(screen.error()));
        QTRY_COMPARE(count(), 2);
        QVERIFY(model->setData(model->index(2), true, DisplayOutputModel::EnabledRole));
        QTRY_VERIFY_WITH_TIMEOUT(!screen.busy(), 5000);
        QVERIFY2(screen.error().isEmpty(), qPrintable(screen.error()));
        QTRY_COMPARE(count(), 3);
        QVERIFY(model->setData(model->index(2), 2, DisplayOutputModel::ReplicationSourceIndexRole));
        QTRY_VERIFY_WITH_TIMEOUT(!screen.busy(), 5000);
        QVERIFY2(screen.error().isEmpty(), qPrintable(screen.error()));
        QTRY_COMPARE(count(), 2);
        QTRY_COMPARE(width(), 1536);
        QVERIFY(model->setData(model->index(2), 0, DisplayOutputModel::ReplicationSourceIndexRole));
        QTRY_VERIFY_WITH_TIMEOUT(!screen.busy(), 5000);
        QVERIFY2(screen.error().isEmpty(), qPrintable(screen.error()));
        QTRY_COMPARE(count(), 3);
        QVERIFY(model->setData(second, 2, DisplayOutputModel::RotationRole));
        QTRY_VERIFY_WITH_TIMEOUT(!screen.busy(), 5000);
        QVERIFY2(screen.error().isEmpty(), qPrintable(screen.error()));
        QTRY_COMPARE(width(), 864);
        QVERIFY(model->setData(second, true, DisplayOutputModel::PrimaryRole));
        QTRY_VERIFY_WITH_TIMEOUT(!screen.busy(), 5000);
        QVERIFY2(screen.error().isEmpty(), qPrintable(screen.error()));
        QVERIFY(model->data(second, DisplayOutputModel::PrimaryRole).toBool());
        if (!shellProbe.isEmpty()) {
            const auto primaryName = [&] {
                count();
                for (const auto &value : screens)
                    if (value.toObject().value("primary").toBool())
                        return value.toObject().value("name").toString();
                return QString();
            };
            QTRY_COMPARE(primaryName(), QStringLiteral("Cutefish-2"));
        }
    }

    void clickAppliesAndPersistsWithoutConfirmation()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        qputenv("XDG_RUNTIME_DIR", directory.path().toUtf8());
        qputenv("XDG_CONFIG_HOME", directory.path().toUtf8());
        qputenv("CUTEFISH_DISPLAY_CONFIG_SERVICE", "com.cutefish.DisplayConfig.Test");
        QProcess compositor;
        compositor.start(qEnvironmentVariable("CUTEFISH_TEST_COMPOSITOR"),
                         {"--no-drm", "--no-xwayland", "--socket", "display-test"});
        QVERIFY(compositor.waitForStarted());

        struct Stop {
            QProcess &process;

            ~Stop()
            {
                process.terminate();
                if (!process.waitForFinished(3000)) {
                    process.kill();
                    process.waitForFinished();
                }
            }
        } stop {compositor};

        KScreenScreen screen;
        QTRY_COMPARE_WITH_TIMEOUT(screen.outputModel()->rowCount(), 1, 5000);
        auto *model = screen.outputModel();
        QVERIFY(model->setData(model->index(0), 1.25, DisplayOutputModel::ScaleRole));
        QTRY_VERIFY_WITH_TIMEOUT(!screen.busy(), 5000);
        QVERIFY2(screen.error().isEmpty(), qPrintable(screen.error()));
        QVERIFY(!screen.pendingConfirmation());
        QVERIFY(!screen.dirty());
        QTRY_COMPARE(model->data(model->index(0), DisplayOutputModel::ScaleRole).toDouble(), 1.25);
        DisplayClient observer;
        QTRY_COMPARE(observer.state().logical.size(), 1);
        QCOMPARE(observer.state().logical.first().width, 1536);
        QCOMPARE(observer.state().logical.first().height, 864);
        QCOMPARE(observer.state().physical.size(), 1);
        QVERIFY(observer.state().previewToken.isEmpty());
        QVERIFY(model->setData(model->index(0), 1.5, DisplayOutputModel::ScaleRole));
        QTRY_VERIFY_WITH_TIMEOUT(!screen.busy(), 5000);
        QVERIFY2(screen.error().isEmpty(), qPrintable(screen.error()));
        QTRY_COMPARE(observer.state().logical.first().width, 1280);
        QTRY_COMPARE(observer.state().logical.first().height, 720);
        QVERIFY(model->setData(model->index(0), 2, DisplayOutputModel::ResolutionIndexRole));
        QTRY_VERIFY_WITH_TIMEOUT(!screen.busy(), 5000);
        QVERIFY2(screen.error().isEmpty(), qPrintable(screen.error()));
        QTRY_COMPARE(observer.state().logical.first().width, 854);
        QTRY_COMPARE(observer.state().logical.first().height, 480);
        QCOMPARE(model->data(model->index(0), DisplayOutputModel::SizeRole).toSize(),
                 QSize(1280, 720));
        int currentModes = 0;
        for (const auto &mode : observer.state().physical.first().modes) {
            if (!mode.current)
                continue;
            ++currentModes;
            QCOMPARE(mode.width, 1280);
            QCOMPARE(mode.height, 720);
        }
        QCOMPARE(currentModes, 1);
        compositor.terminate();
        QVERIFY(compositor.waitForFinished(3000));
        QTRY_COMPARE(model->rowCount(), 0);
        compositor.start();
        QVERIFY(compositor.waitForStarted());
        QTRY_COMPARE_WITH_TIMEOUT(model->rowCount(), 1, 5000);
        QCOMPARE(model->data(model->index(0), DisplayOutputModel::ScaleRole).toDouble(), 1.5);
        QCOMPARE(model->data(model->index(0), DisplayOutputModel::SizeRole).toSize(),
                 QSize(1280, 720));
        QVERIFY(!screen.dirty());
        QVERIFY(!screen.pendingConfirmation());
    }
};

QTEST_GUILESS_MAIN(ScreenIntegrationTest)
#include "screen_integration_test.moc"
