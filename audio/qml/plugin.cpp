#include "plugin.h"

#include "audiomodel.h"
#include "backend.h"
#include "volumemonitor.h"

#include <QQmlEngine>

void Plugin::registerTypes(const char *uri)
{
    using namespace CutefishAudio;

    qmlRegisterType<SinkModel>(uri, 1, 0, "SinkModel");
    qmlRegisterType<SourceModel>(uri, 1, 0, "SourceModel");
    qmlRegisterType<VolumeMonitor>(uri, 1, 0, "VolumeMonitor");
    qmlRegisterUncreatableType<AudioObject>(uri, 1, 0, "AudioObject", QStringLiteral("Provided by audio models"));
    qmlRegisterSingletonType<Backend>(uri, 1, 0, "AudioService", [](QQmlEngine *, QJSEngine *) {
        Backend *backend = Backend::instance();
        QQmlEngine::setObjectOwnership(backend, QQmlEngine::CppOwnership);
        return backend;
    });
    qmlRegisterSingletonType(uri, 1, 0, "PipeWire", [](QQmlEngine *, QJSEngine *engine) {
        QJSValue object = engine->newObject();
        object.setProperty(QStringLiteral("NormalVolume"), AudioObject::NormalVolume);
        object.setProperty(QStringLiteral("MinimalVolume"), 0.0);
        object.setProperty(QStringLiteral("MaximalVolume"), AudioObject::MaximalVolume);
        return object;
    });
}
