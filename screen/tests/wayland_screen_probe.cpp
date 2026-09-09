#include <cstdio>

#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScreen>
#include <QTimer>

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    QTimer report;
    report.setSingleShot(true);
    const auto schedule = [&] {
        report.start(0);
    };
    const auto watch = [&](QScreen *screen) {
        QObject::connect(screen, &QScreen::geometryChanged, &app, schedule);
        schedule();
    };
    QObject::connect(&app, &QGuiApplication::screenAdded, &app, watch);
    QObject::connect(&app, &QGuiApplication::screenRemoved, &app, schedule);
    QObject::connect(&report, &QTimer::timeout, &app, [] {
        QJsonArray screens;
        for (auto *screen : QGuiApplication::screens()) {
            const auto geometry = screen->geometry();
            screens.append(QJsonObject {{"name", screen->name()},
                                        {"x", geometry.x()},
                                        {"y", geometry.y()},
                                        {"width", geometry.width()},
                                        {"height", geometry.height()}});
        }
        const auto data = QJsonDocument(screens).toJson(QJsonDocument::Compact);
        std::puts(data.constData());
        std::fflush(stdout);
    });
    for (auto *screen : QGuiApplication::screens())
        watch(screen);
    return app.exec();
}
