#include "plugin.h"

#include "audiomodel.h"
#include "backend.h"
#include "model/sortfiltermodel.h"
#include "model/audiofiltermodel.h"
#include "soundplayer.h"
#include "volumemonitor.h"

#include <QQmlEngine>

void Plugin::registerTypes(const char *uri)
{
    using namespace CutefishAudio;

    qmlRegisterType<SortFilterModel>(uri, 1, 0, "SortFilterModel");
    qmlRegisterType<AudioFilterModel>(uri, 1, 0, "AudioObjectFilterModel");
    qmlRegisterType<SinkModel>(uri, 1, 0, "SinkModel");
    qmlRegisterType<SourceModel>(uri, 1, 0, "SourceModel");
    qmlRegisterType<SinkInputModel>(uri, 1, 0, "SinkInputModel");
    qmlRegisterType<SourceOutputModel>(uri, 1, 0, "SourceOutputModel");
    qmlRegisterType<CardModel>(uri, 1, 0, "CardModel");
    qmlRegisterType<VolumeFeedback>(uri, 1, 0, "VolumeFeedback");
    qmlRegisterType<SpeakerTest>(uri, 1, 0, "SpeakerTest");
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
