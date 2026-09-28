#include "audiomodel.h"
#include "backend.h"
#include "soundplayer.h"
#include "volumemonitor.h"
#include "fakeaudiodevice.h"
#include "teststream.h"

#include <QAbstractItemModelTester>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

#include <spa/param/audio/raw.h>
#include <spa/param/profile.h>
#include <spa/param/props.h>
#include <spa/param/route.h>
#include <spa/pod/builder.h>

namespace CutefishAudio {

class AudioTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void volumeScale();
    void parseProperties();
    void parseProfilesAndRoutes();
    void discoveryAndModels();
    void volumeAndMute();
    void externalChanges();
    void defaultDevices();
    void hotplug();
    void hardwareRoutes();
    void streamRouting();
    void feedbackAndMonitor();
    void qmlModelsAndControls();
    void settingsMultipleDevices();
    void reconnect();
    void cleanupTestCase();
    void cleanup() { m_failed |= QTest::currentTestFailed(); }

private:
    void startServers();
    void stopServers();
    void activateDevices();
    AudioObject *find(const QString &name) const;
    QByteArray command(const QString &program, const QStringList &args);
    QTemporaryDir m_runtime;
    QProcess m_server;
    QProcess m_policy;
    Backend *m_backend = nullptr;
    std::unique_ptr<QQmlEngine> m_engine;
    std::vector<std::unique_ptr<TestStream>> m_drivers;
    bool m_failed = false;
};

void AudioTest::initTestCase()
{
    QVERIFY(m_runtime.isValid());
    const QByteArray path = m_runtime.path().toUtf8();
    qputenv("XDG_RUNTIME_DIR", path);
    qputenv("PIPEWIRE_RUNTIME_DIR", path);
    qputenv("PIPEWIRE_REMOTE", "cutefish-audio-test");
    qputenv("XDG_CONFIG_HOME", path + "/config");
    qputenv("XDG_STATE_HOME", path + "/state");
    qputenv("XDG_CACHE_HOME", path + "/cache");
    qunsetenv("PIPEWIRE_CONFIG_NAME");
    qunsetenv("PIPEWIRE_CONFIG_DIR");
    qunsetenv("WIREPLUMBER_CONFIG_DIR");
    m_backend = Backend::instance();
    connect(m_backend, &Backend::disconnecting, this, [this] { m_drivers.clear(); });
    connect(m_backend, &Backend::operationFailed, this, [](const QString &message) { qWarning() << message; });
    QTest::qWait(50);
    QVERIFY(!m_backend->isConnected());
    startServers();
    QTRY_VERIFY_WITH_TIMEOUT(m_backend->isConnected(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(find("test-output-a") && find("test-output-b") && find("test-input"), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(m_backend->defaultNode(AudioObject::Sink), 10000);
    activateDevices();
    // The settings application registers this module from C++; the page
    // under test only needs its Metrics singleton.
    qmlRegisterSingletonType(QUrl::fromLocalFile(QStringLiteral(
                                     AUDIO_SOURCE_ROOT "/settings/src/qml/Metrics.qml")),
                             "Cutefish.Settings", 1, 0, "Metrics");
    qmlProtectModule("Cutefish.Settings", 1);
    m_engine = std::make_unique<QQmlEngine>();
    m_engine->addImportPath(AUDIO_IMPORT_PATH);
}

void AudioTest::startServers()
{
    m_server.setProcessChannelMode(QProcess::MergedChannels);
    m_server.start(PIPEWIRE_EXECUTABLE, {"-c", AUDIO_TEST_CONFIG});
    QVERIFY2(m_server.waitForStarted(), qPrintable(m_server.errorString()));
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(m_runtime.path() + "/cutefish-audio-test"), 5000);
    m_policy.setProcessChannelMode(QProcess::MergedChannels);
    m_policy.start(WIREPLUMBER_EXECUTABLE, {"--profile=policy"});
    QVERIFY2(m_policy.waitForStarted(), qPrintable(m_policy.errorString()));
}

void AudioTest::stopServers()
{
    m_drivers.clear();
    for (QProcess *process : {&m_policy, &m_server}) {
        if (process->state() != QProcess::NotRunning) {
            process->terminate();
            if (!process->waitForFinished(3000)) {
                process->kill();
                process->waitForFinished(3000);
            }
        }
    }
}

void AudioTest::activateDevices()
{
    for (const QString &name : {QString("test-output-a"), QString("test-output-b"), QString("test-input")}) {
        QTRY_VERIFY_WITH_TIMEOUT(find(name), 10000);
        AudioObject *node = find(name);
        auto stream = std::make_unique<TestStream>(m_backend->core(), "cutefish.internal.test-" + name.toUtf8(),
            node->properties().value(PW_KEY_OBJECT_SERIAL).toString().toUtf8(), node->kind() == AudioObject::Source);
        QVERIFY(stream->connected);
        QTRY_VERIFY_WITH_TIMEOUT(stream->streaming, 5000);
        m_drivers.push_back(std::move(stream));
    }
}

AudioObject *AudioTest::find(const QString &name) const
{
    for (AudioObject *object : m_backend->objects()) {
        if (object->name() == name)
            return object;
    }
    return nullptr;
}

QByteArray AudioTest::command(const QString &program, const QStringList &args)
{
    QProcess process;
    process.setProcessChannelMode(QProcess::MergedChannels);
    process.start(program, args);
    if (!process.waitForFinished(5000) || process.exitCode() != 0) {
        QTest::qFail(qPrintable(QString::fromUtf8(process.readAll())), __FILE__, __LINE__);
        return {};
    }
    return process.readAll();
}

void AudioTest::volumeScale()
{
    QCOMPARE(AudioObject::fromLinear(0.125f), 0.5);
    QCOMPARE(AudioObject::toLinear(0.5), 0.125f);
    QCOMPARE(AudioObject::toLinear(-1.0), 0.0f);
    QCOMPARE(AudioObject::toLinear(100.0), 3.375f);
    QCOMPARE(AudioObject::toLinear(std::numeric_limits<double>::infinity()), 0.0f);
    QCOMPARE(AudioObject::fromLinear(std::numeric_limits<float>::quiet_NaN()), 0.0);
    for (double value : {0.0, 0.01, 0.25, 0.75, 1.0, 1.5})
        QVERIFY(std::abs(AudioObject::fromLinear(AudioObject::toLinear(value)) - value) < 0.000001);
}

void AudioTest::parseProperties()
{
    AudioObject object(nullptr, 42, AudioObject::Sink);
    QVERIFY(!object.hasVolume());
    QCOMPARE(object.activePortIndex(), -1);
    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const float volumes[] = {0.125f, 1.0f};
    const quint32 channels[] = {SPA_AUDIO_CHANNEL_FL, SPA_AUDIO_CHANNEL_FR};
    const auto *param = static_cast<spa_pod *>(spa_pod_builder_add_object(&builder,
        SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
        SPA_PROP_channelVolumes, SPA_POD_Array(sizeof(float), SPA_TYPE_Float, 2, volumes),
        SPA_PROP_channelMap, SPA_POD_Array(sizeof(quint32), SPA_TYPE_Id, 2, channels),
        SPA_PROP_mute, SPA_POD_Bool(true)));
    object.updateVolume(param);
    QVERIFY(object.hasVolume());
    QVERIFY(object.muted());
    QCOMPARE(object.volume(), 1.0);
    QCOMPARE(object.channelVolumes(), QVariantList({0.5, 1.0}));
    QCOMPARE(object.channels(), QStringList({"FL", "FR"}));
    object.setChannelVolume(-1, 0.5);
    object.setChannelVolume(2, 0.5);
    object.updateVolume(nullptr);
    QCOMPARE(object.volume(), 1.0);

    const spa_dict_item items[] = {{"node.name", "test"}, {"node.description", "Test speaker"}};
    const spa_dict properties = SPA_DICT_INIT(items, 2);
    object.updateProperties(&properties);
    QCOMPARE(object.description(), QString("Test speaker"));
    QCOMPARE(object.displayName(), QString("Test speaker"));
    QCOMPARE(object.cardIndex(), AudioObject::InvalidId);
}

void AudioTest::parseProfilesAndRoutes()
{
    AudioObject card(nullptr, 99, AudioObject::Card);
    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const auto *profile = static_cast<spa_pod *>(spa_pod_builder_add_object(&builder,
        SPA_TYPE_OBJECT_ParamProfile, SPA_PARAM_EnumProfile,
        SPA_PARAM_PROFILE_index, SPA_POD_Int(7), SPA_PARAM_PROFILE_name, SPA_POD_String("stereo"),
        SPA_PARAM_PROFILE_description, SPA_POD_String("Stereo"),
        SPA_PARAM_PROFILE_available, SPA_POD_Id(SPA_PARAM_AVAILABILITY_yes)));
    card.updateProfile(profile, false);
    card.updateProfile(profile, false);
    card.updateProfile(profile, true);
    QCOMPARE(card.profiles().size(), 1);
    QCOMPARE(card.activeProfileIndex(), 0);
    QCOMPARE(card.profiles().first().toMap().value("availability").toInt(), int(AudioObject::Available));
    builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const int devices[] = {3};
    const int profiles[] = {7};
    const auto *route = static_cast<spa_pod *>(spa_pod_builder_add_object(&builder,
        SPA_TYPE_OBJECT_ParamRoute, SPA_PARAM_EnumRoute,
        SPA_PARAM_ROUTE_index, SPA_POD_Int(9), SPA_PARAM_ROUTE_device, SPA_POD_Int(3),
        SPA_PARAM_ROUTE_direction, SPA_POD_Id(SPA_DIRECTION_OUTPUT),
        SPA_PARAM_ROUTE_name, SPA_POD_String("headphones"),
        SPA_PARAM_ROUTE_devices, SPA_POD_Array(sizeof(int), SPA_TYPE_Int, 1, devices),
        SPA_PARAM_ROUTE_profiles, SPA_POD_Array(sizeof(int), SPA_TYPE_Int, 1, profiles),
        SPA_PARAM_ROUTE_available, SPA_POD_Id(SPA_PARAM_AVAILABILITY_no)));
    card.updateRoute(route, false);
    card.updateRoute(route, true);
    QCOMPARE(card.m_routes.size(), 1);
    QCOMPARE(card.m_activeRoutes.value(3), 9);
    QCOMPARE(card.m_routes.first().toMap().value("availability").toInt(), int(AudioObject::Unavailable));
}

void AudioTest::discoveryAndModels()
{
    AudioModel sinks(AudioObject::Sink, m_backend);
    AudioModel sources(AudioObject::Source, m_backend);
    QAbstractItemModelTester tester(&sinks, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QCOMPARE(sinks.rowCount(), 2);
    QCOMPARE(sources.rowCount(), 1);
    QCOMPARE(sinks.rowCount(sinks.index(0, 0)), 0);
    QVERIFY(sinks.role("AudioObject") > Qt::UserRole);
    QVERIFY(sinks.role("Volume") > Qt::UserRole);
    QVERIFY(sinks.role("Description") > Qt::UserRole);
    QCOMPARE(sinks.role("MissingRole"), -1);
    QVERIFY(!sinks.setData({}, 0.5, sinks.role("Volume")));
    QVERIFY(!sinks.setData(sinks.index(0, 0), "bad", sinks.role("Name")));
    QVERIFY(sinks.data(sinks.index(0, 0), sinks.role("AudioObject")).value<AudioObject *>());
    QCOMPARE(sources.data(sources.index(0, 0), sources.role("Name")).toString(), QString("test-input"));
}

void AudioTest::volumeAndMute()
{
    AudioObject *sink = find("test-output-a");
    QTRY_VERIFY(sink->volumeWritable());
    sink->setVolume(0.42);
    QTRY_VERIFY(std::abs(sink->volume() - 0.42) < 0.0001);
    sink->setMuted(true);
    QTRY_VERIFY(sink->muted());
    sink->setMuted(false);
    QTRY_VERIFY(!sink->muted());
    sink->setChannelVolumes({0.25, 0.5});
    QTRY_VERIFY(std::abs(sink->channelVolumes().at(0).toDouble() - 0.25) < 0.0001);
    sink->setVolume(1.0);
    QTRY_VERIFY(std::abs(sink->volume() - 1.0) < 0.0001);
    QVERIFY(std::abs(sink->channelVolumes().at(0).toDouble() - 0.5) < 0.0001);
    sink->setVolume(std::numeric_limits<double>::quiet_NaN());
    sink->setChannelVolumes({0.1});
    sink->setChannelVolume(-1, 0.0);
    QTest::qWait(30);
    QVERIFY(std::abs(sink->volume() - 1.0) < 0.0001);
    sink->setVolume(-1.0);
    QTRY_COMPARE(sink->volume(), 0.0);
    sink->setVolume(5.0);
    QTRY_VERIFY(std::abs(sink->volume() - 1.5) < 0.0001);
    sink->setVolume(0.5);
    QTRY_VERIFY(std::abs(sink->volume() - 0.5) < 0.0001);
    AudioObject *source = find("test-input");
    QTRY_VERIFY(source->volumeWritable());
    source->setMuted(true);
    QTRY_VERIFY(source->muted());
    source->setMuted(false);
    QTRY_VERIFY(!source->muted());
}

void AudioTest::externalChanges()
{
    AudioObject *sink = find("test-output-a");
    command(PW_CLI_EXECUTABLE, {"set-param", QString::number(sink->index()), "Props",
                              "{ channelVolumes: [ 0.064 0.064 ], mute: true }"});
    QTRY_VERIFY(std::abs(sink->volume() - 0.4) < 0.0001);
    QTRY_VERIFY(sink->muted());
    sink->setMuted(false);
    QTRY_VERIFY(!sink->muted());
}

void AudioTest::defaultDevices()
{
    AudioObject *first = find("test-output-a");
    AudioObject *second = find("test-output-b");
    second->setDefault(true);
    QTRY_COMPARE(m_backend->defaultNode(AudioObject::Sink), second);
    QVERIFY(second->isDefault());
    QVERIFY(!first->isDefault());
    first->setDefault(true);
    QTRY_COMPARE(m_backend->defaultNode(AudioObject::Sink), first);
    find("test-input")->setDefault(true);
    QTRY_COMPARE(m_backend->defaultNode(AudioObject::Source), find("test-input"));
}

void AudioTest::hotplug()
{
    AudioModel sinks(AudioObject::Sink, m_backend);
    QAbstractItemModelTester tester(&sinks, QAbstractItemModelTester::FailureReportingMode::QtTest);
    auto *properties = pw_properties_new("factory.name", "support.null-audio-sink",
        PW_KEY_NODE_NAME, "hotplug-output", PW_KEY_MEDIA_CLASS, "Audio/Sink",
        "audio.position", "[ FL FR ]", nullptr);
    auto *proxy = static_cast<pw_proxy *>(pw_core_create_object(m_backend->core(), "adapter",
        PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, &properties->dict, 0));
    pw_properties_free(properties);
    QVERIFY(proxy);
    QTRY_COMPARE(sinks.rowCount(), 3);
    QPointer<AudioObject> device = find("hotplug-output");
    QVERIFY(device);
    device->setDefault(true);
    QTRY_COMPARE(m_backend->defaultNode(AudioObject::Sink), device.data());
    pw_proxy_destroy(proxy);
    QTRY_COMPARE(sinks.rowCount(), 2);
    QTRY_VERIFY(device.isNull());
    QTRY_VERIFY(m_backend->defaultNode(AudioObject::Sink));
}

void AudioTest::hardwareRoutes()
{
    FakeAudioDevice device;
    QVERIFY(device.exportTo(m_backend->core()));
    QTRY_VERIFY(find("test-card"));
    AudioObject *card = find("test-card");
    QTRY_COMPARE(card->profiles().size(), 2);
    QTRY_VERIFY(card->activeProfileIndex() >= 0);
    card->setActiveProfileIndex(1);
    QTRY_COMPARE(device.profile, 8);
    QTRY_COMPARE(card->activeProfileIndex(), 1);
    QVERIFY(device.saved);

    const QByteArray cardId = QByteArray::number(card->index());
    auto *properties = pw_properties_new("factory.name", "support.null-audio-sink",
        PW_KEY_NODE_NAME, "test-card-output", PW_KEY_MEDIA_CLASS, "Audio/Sink",
        PW_KEY_DEVICE_ID, cardId.constData(), "card.profile.device", "3",
        "audio.position", "[ FL FR ]", nullptr);
    auto *proxy = static_cast<pw_proxy *>(pw_core_create_object(m_backend->core(), "adapter",
        PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, &properties->dict, 0));
    pw_properties_free(properties);
    QVERIFY(proxy);
    QTRY_VERIFY(find("test-card-output"));
    AudioObject *sink = find("test-card-output");
    QTRY_VERIFY(sink->volumeWritable());
    QTRY_COMPARE(sink->ports().size(), 2);
    sink->setActivePortIndex(1);
    QTRY_COMPARE(device.route, 10);
    QTRY_COMPARE(sink->activePortIndex(), 1);
    QCOMPARE(sink->displayName(), QString("Headphones"));
    sink->setChannelVolumes({0.5, 0.5});
    QTRY_COMPARE(device.volumes[0], 0.125f);
    QTRY_COMPARE(device.volumes[1], 0.125f);
    QTRY_VERIFY(std::abs(sink->volume() - 0.5) < 0.0001);
    QVERIFY(device.saved);
    sink->setVolume(0.25);
    QTRY_COMPARE(device.volumes[0], 0.015625f);
    QTRY_VERIFY(std::abs(sink->volume() - 0.25) < 0.0001);
    sink->setMuted(true);
    QTRY_VERIFY(device.muted);
    QTRY_VERIFY(sink->muted());
    const int writes = device.writes;
    sink->setActivePortIndex(-1);
    card->setActiveProfileIndex(999);
    QTest::qWait(30);
    QCOMPARE(device.writes, writes);

    device.volumes[0] = 0.064f;
    device.volumes[1] = 0.064f;
    device.muted = false;
    device.notify();
    QTRY_VERIFY(std::abs(sink->volume() - 0.4) < 0.0001);
    QTRY_VERIFY(!sink->muted());
    pw_proxy_destroy(proxy);
    QTRY_VERIFY(!find("test-card-output"));
}

void AudioTest::streamRouting()
{
    AudioObject *first = find("test-output-a");
    AudioObject *second = find("test-output-b");
    TestStream playback(m_backend->core(), "test-playback",
        first->properties().value(PW_KEY_OBJECT_SERIAL).toString().toUtf8());
    QVERIFY(playback.connected);
    QTRY_VERIFY(playback.streaming);
    QTRY_VERIFY(find("test-playback"));
    AudioObject *stream = find("test-playback");
    QCOMPARE(stream->kind(), AudioObject::Playback);
    QTRY_COMPARE(stream->deviceIndex(), first->index());
    QTRY_VERIFY(stream->volumeWritable());
    stream->setVolume(0.25);
    QTRY_VERIFY(std::abs(stream->volume() - 0.25) < 0.0001);
    stream->setMuted(true);
    QTRY_VERIFY(stream->muted());
    stream->setDeviceIndex(second->index());
    QTRY_COMPARE(stream->deviceIndex(), second->index());
    stream->setDeviceIndex(find("test-input")->index());
    QTest::qWait(30);
    QCOMPARE(stream->deviceIndex(), second->index());
    first->switchStreams();
    QTRY_COMPARE(stream->deviceIndex(), first->index());

    TestStream recording(m_backend->core(), "test-recording",
        find("test-input")->properties().value(PW_KEY_OBJECT_SERIAL).toString().toUtf8(), true);
    QVERIFY(recording.connected);
    QTRY_VERIFY(recording.streaming);
    QTRY_VERIFY(find("test-recording"));
    QCOMPARE(find("test-recording")->kind(), AudioObject::Capture);
    QTRY_COMPARE(find("test-recording")->deviceIndex(), find("test-input")->index());

    auto *properties = pw_properties_new("factory.name", "audiotestsrc",
        PW_KEY_NODE_NAME, "second-input", PW_KEY_MEDIA_CLASS, "Audio/Source",
        "audio.position", "[ FL FR ]", nullptr);
    auto *proxy = static_cast<pw_proxy *>(pw_core_create_object(m_backend->core(), "adapter",
        PW_TYPE_INTERFACE_Node, PW_VERSION_NODE, &properties->dict, 0));
    pw_properties_free(properties);
    QVERIFY(proxy);
    QTRY_VERIFY(find("second-input"));
    find("second-input")->setDefault(true);
    QTRY_COMPARE(m_backend->defaultNode(AudioObject::Source), find("second-input"));
    find("test-recording")->setDeviceIndex(find("second-input")->index());
    QTRY_COMPARE(find("test-recording")->deviceIndex(), find("second-input")->index());
    find("test-recording")->setDeviceIndex(find("test-input")->index());
    QTRY_COMPARE(find("test-recording")->deviceIndex(), find("test-input")->index());
    pw_proxy_destroy(proxy);
    QTRY_VERIFY(!find("second-input"));
    QTRY_COMPARE(m_backend->defaultNode(AudioObject::Source), find("test-input"));
}

void AudioTest::feedbackAndMonitor()
{
    AudioObject *sink = find("test-output-a");
    sink->setVolume(1.0);
    QTRY_VERIFY(std::abs(sink->volume() - 1.0) < 0.0001);
    VolumeMonitor monitor;
    monitor.setTarget(sink);
    SoundPlayer player(m_backend);
    QSignalSpy finished(&player, &SoundPlayer::finished);
    QSignalSpy failures(&player, &SoundPlayer::failed);
    double peak = 0.0;
    connect(&monitor, &VolumeMonitor::volumeChanged, this, [&] { peak = std::max(peak, monitor.volume()); });
    player.play(sink, "front-left");
    QTRY_VERIFY_WITH_TIMEOUT(monitor.isAvailable(), 2000);
    QTRY_VERIFY_WITH_TIMEOUT(peak > 0.01, 2000);
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, 2000);
    QCOMPARE(failures.count(), 0);
    monitor.setTarget(nullptr);
    QCOMPARE(monitor.volume(), 0.0);
    QVERIFY(!monitor.isAvailable());
    player.play(nullptr);
    player.play(sink, "not-a-channel");
    QCOMPARE(finished.count(), 1);
}

void AudioTest::qmlModelsAndControls()
{
    QQmlEngine &engine = *m_engine;
    QQmlComponent component(&engine);
    component.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import FishUI 1.0 as FishUI
        import Cutefish.Audio 1.0
        Item {
            width: 400
            height: 400
            property alias sinks: sinks
            property alias slider: slider
            SinkModel { id: sinks }
            SourceModel { id: sources }
            AudioObjectFilterModel {
                id: filtered
                sourceModel: sinks
                sortRole: "SortByDefault"
                sortOrder: Qt.DescendingOrder
                filterOutInactiveDevices: true
            }
            FishUI.Slider {
                id: slider
                from: PipeWire.MinimalVolume
                to: PipeWire.NormalVolume
                value: sinks.defaultSink ? sinks.defaultSink.volume : 0
                onMoved: if (sinks.defaultSink) sinks.defaultSink.volume = value
            }
            Repeater {
                model: filtered
                delegate: Text { text: Description + ": " + Math.round(Volume * 100) }
            }
            VolumeFeedback {}
            SpeakerTest {}
            VolumeMonitor {}
        }
    )", QUrl("file:///audio-test.qml"));
    QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 3000);
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> item(component.create());
    QVERIFY2(item, qPrintable(component.errorString()));
    QObject *slider = item->property("slider").value<QObject *>();
    QVERIFY(slider);
    QCOMPARE(slider->property("to").toDouble(), 1.0);
    slider->setProperty("value", 0.63);
    QVERIFY(QMetaObject::invokeMethod(slider, "moved"));
    QTRY_VERIFY(std::abs(m_backend->defaultNode(AudioObject::Sink)->volume() - 0.63) < 0.0001);
}

void AudioTest::settingsMultipleDevices()
{
    auto playback = std::make_unique<TestStream>(m_backend->core(), "settings-playback",
        find("test-output-a")->properties().value(PW_KEY_OBJECT_SERIAL).toString().toUtf8());
    QTRY_VERIFY(playback->streaming);
    QTRY_VERIFY(find("settings-playback"));
    QQmlEngine &engine = *m_engine;
    QSignalSpy warnings(&engine, &QQmlEngine::warnings);
    QQmlComponent host(&engine);
    host.setData(R"(
        import QtQuick
        import QtQuick.Controls
        import FishUI 1.0 as FishUI
        FishUI.ApplicationWindow {
            property int headerInset: 40
            width: 820
            height: 780
            visible: true
            header: Item { height: 40 }
            background: Rectangle { color: "#f6f6f6" }
        }
    )", QUrl());
    QScopedPointer<QObject> window(host.create());
    QVERIFY2(window, qPrintable(host.errorString()));
    engine.rootContext()->setContextProperty("rootWindow", window.data());
    QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(AUDIO_SOURCE_ROOT "/settings/src/qml/Sound/Main.qml")));
    QTRY_VERIFY_WITH_TIMEOUT(component.status() != QQmlComponent::Loading, 3000);
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> page(component.create());
    QVERIFY2(page, qPrintable(component.errorString()));
    auto *item = qobject_cast<QQuickItem *>(page.data());
    QVERIFY(item);
    item->setParentItem(qobject_cast<QQuickWindow *>(window.data())->contentItem());
    item->setWidth(820);
    item->setHeight(740);
    AudioObject *first = find("test-output-a");
    AudioObject *second = find("test-output-b");
    AudioObject *source = find("test-input");
    auto visualChild = [&](const QString &name) -> QObject * {
        QList<QQuickItem *> pending {item};
        while (!pending.isEmpty()) {
            QQuickItem *candidate = pending.takeLast();
            if (candidate->objectName() == name)
                return candidate;
            pending.append(candidate->childItems());
        }
        return nullptr;
    };
    auto child = [&](const QString &prefix, AudioObject *device) {
        return visualChild(prefix + QString::number(device->index()));
    };
    QTRY_VERIFY(child("device-", first));
    QTRY_VERIFY(child("device-", second));
    QVERIFY(!child("device-", source));
    QTRY_VERIFY(child("device-", first)->property("width").toDouble() > 700);
    QObject *secondRow = child("device-", second);
    QVERIFY(QMetaObject::invokeMethod(secondRow, "clicked"));
    QTRY_COMPARE(m_backend->defaultNode(AudioObject::Sink), second);
    QTRY_VERIFY(secondRow->property("checked").toBool());
    QVERIFY(!child("device-", first)->property("checked").toBool());
    QTRY_VERIFY(child("volume-", second));
    QVERIFY(!child("volume-", first));

    const double originalVolume = first->volume();
    QObject *slider = child("volume-", second);
    QVERIFY(slider);
    slider->setProperty("value", 0.57);
    QVERIFY(QMetaObject::invokeMethod(slider, "moved"));
    QTRY_VERIFY(std::abs(second->volume() - 0.57) < 0.0001);
    QVERIFY(std::abs(first->volume() - originalVolume) < 0.0001);
    QVERIFY(!visualChild("mute"));
    second->setMuted(true);
    QTRY_COMPARE(slider->property("value").toDouble(), 0.0);
    slider->setProperty("value", 0.4);
    QVERIFY(QMetaObject::invokeMethod(slider, "moved"));
    QTRY_VERIFY(!second->muted());
    QTRY_VERIFY(std::abs(second->volume() - 0.4) < 0.0001);
    slider->setProperty("value", 0.0);
    QVERIFY(QMetaObject::invokeMethod(slider, "moved"));
    QTRY_VERIFY(second->muted());
    QVERIFY(!first->muted());
    QVERIFY(std::abs(first->volume() - originalVolume) < 0.0001);
    visualChild("tabs")->setProperty("currentIndex", 1);
    QTRY_VERIFY(child("device-", source));
    QVERIFY(!child("device-", first));
    QTRY_VERIFY(child("volume-", source));
    AudioObject *stream = find("settings-playback");
    QVERIFY(!child("stream-", stream));
    QVERIFY(!child("volume-", stream));
    QList<QQuickItem *> pending {item};
    while (!pending.isEmpty()) {
        QQuickItem *candidate = pending.takeLast();
        const QString text = candidate->property("text").toString();
        QVERIFY(text != "Applications" && text != "Test sound");
        pending.append(candidate->childItems());
    }
    QTest::qWait(100);
    QCOMPARE(warnings.count(), 0);
    if (qEnvironmentVariableIsSet("CUTEFISH_AUDIO_SCREENSHOT")) {
        const QImage screenshot = qobject_cast<QQuickWindow *>(window.data())->grabWindow();
        QVERIFY(!screenshot.isNull());
        QVERIFY(screenshot.save(qEnvironmentVariable("CUTEFISH_AUDIO_SCREENSHOT")));
    }

    visualChild("tabs")->setProperty("currentIndex", 0);
    const QString firstCardName = "device-" + QString::number(first->index());
    playback.reset();
    QTRY_VERIFY(!find("settings-playback"));
    stopServers();
    QTRY_VERIFY(!m_backend->isConnected());
    QTRY_VERIFY(!visualChild(firstCardName));
    startServers();
    QTRY_VERIFY_WITH_TIMEOUT(find("test-output-a") && find("test-output-b"), 10000);
    activateDevices();
    QTRY_VERIFY_WITH_TIMEOUT(child("device-", find("test-output-a")), 10000);
    QCOMPARE(warnings.count(), 0);
}

void AudioTest::reconnect()
{
    AudioModel sinks(AudioObject::Sink, m_backend);
    QAbstractItemModelTester tester(&sinks, QAbstractItemModelTester::FailureReportingMode::QtTest);
    QPointer<AudioObject> previous = find("test-output-a");
    VolumeMonitor monitor;
    monitor.setTarget(previous);
    SoundPlayer player(m_backend);
    player.play(previous, "front-right");
    stopServers();
    QTRY_VERIFY_WITH_TIMEOUT(!m_backend->isConnected(), 5000);
    QCOMPARE(sinks.rowCount(), 0);
    QVERIFY(!m_backend->defaultNode(AudioObject::Sink));
    QTRY_VERIFY(previous.isNull());
    QVERIFY(!monitor.target());
    startServers();
    QTRY_VERIFY_WITH_TIMEOUT(m_backend->isConnected(), 10000);
    QTRY_COMPARE_WITH_TIMEOUT(sinks.rowCount(), 2, 10000);
    QTRY_VERIFY_WITH_TIMEOUT(m_backend->defaultNode(AudioObject::Sink), 10000);
    activateDevices();
    AudioObject *sink = find("test-output-a");
    QTRY_VERIFY(sink->volumeWritable());
    sink->setVolume(0.3);
    QTRY_VERIFY(std::abs(sink->volume() - 0.3) < 0.0001);
}

void AudioTest::cleanupTestCase()
{
    stopServers();
    const QByteArray serverLog = m_server.readAll();
    const QByteArray policyLog = m_policy.readAll();
    if (m_failed) {
        qWarning().noquote() << serverLog;
        qWarning().noquote() << policyLog;
    }
}

}

QTEST_MAIN(CutefishAudio::AudioTest)
#include "audio_test.moc"
