// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "deviceusage.h"
#include "pipewirecapture.h"
#include "videodevicewatch.h"

#include <QFile>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/video/format-utils.h>

#include <unistd.h>

using namespace CutefishPrivacy;

// A capture stream of this process, on a connection of its own.
class TestStream
{
public:
    TestStream(pw_thread_loop *loop, pw_core *core, const char *name, bool video, const char *target,
               std::initializer_list<std::pair<const char *, const char *>> extra = {})
        : m_loop(loop)
    {
        pw_thread_loop_lock(m_loop);
        auto *properties = pw_properties_new(PW_KEY_NODE_NAME, name, PW_KEY_APP_NAME, name,
            PW_KEY_MEDIA_TYPE, video ? "Video" : "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
            PW_KEY_TARGET_OBJECT, target, nullptr);
        for (const auto &[key, value] : extra)
            pw_properties_set(properties, key, value);
        m_stream = pw_stream_new(core, name, properties);
        pw_stream_add_listener(m_stream, &m_listener, &events, this);
        uint8_t buffer[512];
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        const spa_pod *params[1];
        if (video) {
            params[0] = static_cast<const spa_pod *>(spa_pod_builder_add_object(&builder,
                SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat,
                SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
                SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw)));
        } else {
            spa_audio_info_raw format {};
            format.format = SPA_AUDIO_FORMAT_F32;
            format.rate = 48000;
            format.channels = 2;
            format.position[0] = SPA_AUDIO_CHANNEL_FL;
            format.position[1] = SPA_AUDIO_CHANNEL_FR;
            params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &format);
        }
        pw_stream_connect(m_stream, PW_DIRECTION_INPUT, PW_ID_ANY,
            static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1);
        pw_thread_loop_unlock(m_loop);
    }

    ~TestStream()
    {
        pw_thread_loop_lock(m_loop);
        spa_hook_remove(&m_listener);
        pw_stream_destroy(m_stream);
        pw_thread_loop_unlock(m_loop);
    }

    void setActive(bool active)
    {
        pw_thread_loop_lock(m_loop);
        pw_stream_set_active(m_stream, active);
        pw_thread_loop_unlock(m_loop);
    }

private:
    pw_thread_loop *m_loop;
    pw_stream *m_stream = nullptr;
    spa_hook m_listener {};

    inline static const pw_stream_events events = [] {
        pw_stream_events events {};
        events.version = PW_VERSION_STREAM_EVENTS;
        events.process = [](void *data) {
            auto *self = static_cast<TestStream *>(data);
            if (pw_buffer *buffer = pw_stream_dequeue_buffer(self->m_stream))
                pw_stream_queue_buffer(self->m_stream, buffer);
        };
        return events;
    }();
};

class PrivacyTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void directOpen();
    void briefOpenIsIgnored();
    void deviceHotplug();
    void microphone();
    void systemAudio();
    void camera();
    void ignoredStreams();
    void pausedStream();
    void merged();
    void cleanupTestCase();

private:
    bool hasUse(const DeviceUses &uses, DeviceUse::Kind kind, const QString &name) const;

    QTemporaryDir m_runtime;
    QTemporaryDir m_devices;
    QProcess m_server;
    QProcess m_policy;
    pw_thread_loop *m_loop = nullptr;
    pw_context *m_context = nullptr;
    pw_core *m_core = nullptr;
    std::unique_ptr<PipeWireCapture> m_capture;
};

void PrivacyTest::initTestCase()
{
    QVERIFY(m_runtime.isValid());
    QVERIFY(m_devices.isValid());
    const QByteArray path = m_runtime.path().toUtf8();
    qputenv("XDG_RUNTIME_DIR", path);
    qputenv("PIPEWIRE_RUNTIME_DIR", path);
    qputenv("PIPEWIRE_REMOTE", "cutefish-privacy-test");
    qputenv("XDG_CONFIG_HOME", path + "/config");
    qputenv("XDG_STATE_HOME", path + "/state");
    qputenv("XDG_CACHE_HOME", path + "/cache");
    qunsetenv("PIPEWIRE_CONFIG_NAME");
    qunsetenv("PIPEWIRE_CONFIG_DIR");
    qunsetenv("WIREPLUMBER_CONFIG_DIR");

    m_server.setProcessChannelMode(QProcess::ForwardedChannels);
    m_server.start(PIPEWIRE_EXECUTABLE, {"-c", PRIVACY_TEST_CONFIG});
    QVERIFY2(m_server.waitForStarted(), qPrintable(m_server.errorString()));
    QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(m_runtime.path() + "/cutefish-privacy-test"), 5000);
    m_policy.setProcessChannelMode(QProcess::ForwardedChannels);
    m_policy.start(WIREPLUMBER_EXECUTABLE, {"--profile=policy"});
    QVERIFY2(m_policy.waitForStarted(), qPrintable(m_policy.errorString()));

    pw_init(nullptr, nullptr);
    m_loop = pw_thread_loop_new("privacy-test", nullptr);
    QVERIFY(m_loop);
    m_context = pw_context_new(pw_thread_loop_get_loop(m_loop), nullptr, 0);
    QVERIFY(m_context);
    QVERIFY(pw_thread_loop_start(m_loop) == 0);
    pw_thread_loop_lock(m_loop);
    m_core = pw_context_connect(m_context, nullptr, 0);
    pw_thread_loop_unlock(m_loop);
    QVERIFY(m_core);

    m_capture = std::make_unique<PipeWireCapture>();
    QTRY_VERIFY_WITH_TIMEOUT(m_capture->isConnected(), 10000);
}

void PrivacyTest::cleanupTestCase()
{
    m_capture.reset();
    if (m_loop) {
        pw_thread_loop_lock(m_loop);
        if (m_core)
            pw_core_disconnect(m_core);
        pw_thread_loop_unlock(m_loop);
        pw_thread_loop_stop(m_loop);
        if (m_context)
            pw_context_destroy(m_context);
        pw_thread_loop_destroy(m_loop);
    }
    for (QProcess *process : {&m_policy, &m_server}) {
        process->terminate();
        if (!process->waitForFinished(3000))
            process->kill();
    }
}

bool PrivacyTest::hasUse(const DeviceUses &uses, DeviceUse::Kind kind, const QString &name) const
{
    for (const DeviceUse &use : uses) {
        if (use.kind == kind && use.name == name && use.pid == quint32(getpid()))
            return true;
    }
    return false;
}

static QString ownName()
{
    QFile comm(QStringLiteral("/proc/self/comm"));
    comm.open(QIODevice::ReadOnly);
    return QString::fromUtf8(comm.readAll()).trimmed();
}

void PrivacyTest::directOpen()
{
    QFile device(m_devices.filePath("video0"));
    QVERIFY(device.open(QIODevice::WriteOnly));
    device.close();
    QFile other(m_devices.filePath("videoX"));
    QVERIFY(other.open(QIODevice::WriteOnly));

    VideoDeviceWatch watch(m_devices.path());
    QVERIFY(watch.uses().isEmpty());

    // Held open before the watch began.
    QVERIFY(device.open(QIODevice::ReadOnly));
    VideoDeviceWatch late(m_devices.path());
    QVERIFY(hasUse(late.uses(), DeviceUse::Camera, ownName()));

    QTRY_VERIFY_WITH_TIMEOUT(hasUse(watch.uses(), DeviceUse::Camera, ownName()), 5000);
    QCOMPARE(watch.uses().size(), 1);
    device.close();
    QTRY_VERIFY_WITH_TIMEOUT(watch.uses().isEmpty(), 5000);
}

void PrivacyTest::briefOpenIsIgnored()
{
    QFile device(m_devices.filePath("video0"));
    VideoDeviceWatch watch(m_devices.path());
    QSignalSpy spy(&watch, &VideoDeviceWatch::usesChanged);
    QVERIFY(device.open(QIODevice::ReadOnly));
    QTest::qWait(VideoDeviceWatch::SettleDelay / 4);
    device.close();
    QTest::qWait(VideoDeviceWatch::SettleDelay * 2);
    QCOMPARE(spy.count(), 0);
}

void PrivacyTest::deviceHotplug()
{
    VideoDeviceWatch watch(m_devices.path());
    QFile device(m_devices.filePath("video7"));
    QVERIFY(device.open(QIODevice::WriteOnly));
    device.close();
    QTest::qWait(VideoDeviceWatch::SettleDelay * 2);
    QVERIFY(device.open(QIODevice::ReadOnly));
    QTRY_VERIFY_WITH_TIMEOUT(hasUse(watch.uses(), DeviceUse::Camera, ownName()), 5000);
    device.close();
    QVERIFY(device.remove());
    QTRY_VERIFY_WITH_TIMEOUT(watch.uses().isEmpty(), 5000);
}

void PrivacyTest::microphone()
{
    {
        TestStream stream(m_loop, m_core, "test-recorder", false, "test-microphone");
        QTRY_VERIFY_WITH_TIMEOUT(hasUse(m_capture->uses(), DeviceUse::Microphone, "test-recorder"), 10000);
        QCOMPARE(m_capture->uses().size(), 1);
    }
    QTRY_VERIFY_WITH_TIMEOUT(m_capture->uses().isEmpty(), 10000);
}

void PrivacyTest::systemAudio()
{
    {
        TestStream stream(m_loop, m_core, "test-loopback-recorder", false, "test-speaker",
                          {{PW_KEY_STREAM_CAPTURE_SINK, "true"}});
        QTRY_VERIFY_WITH_TIMEOUT(hasUse(m_capture->uses(), DeviceUse::SystemAudio, "test-loopback-recorder"), 10000);
        QCOMPARE(m_capture->uses().size(), 1);
    }
    QTRY_VERIFY_WITH_TIMEOUT(m_capture->uses().isEmpty(), 10000);
}

void PrivacyTest::camera()
{
    {
        TestStream stream(m_loop, m_core, "test-video-call", true, "test-camera");
        QTRY_VERIFY_WITH_TIMEOUT(hasUse(m_capture->uses(), DeviceUse::Camera, "test-video-call"), 10000);
        QCOMPARE(m_capture->uses().size(), 1);
    }
    QTRY_VERIFY_WITH_TIMEOUT(m_capture->uses().isEmpty(), 10000);

    // A screen cast is no camera.
    TestStream screen(m_loop, m_core, "test-screen-viewer", true, "test-screen");
    QTest::qWait(2000);
    QVERIFY(m_capture->uses().isEmpty());
}

void PrivacyTest::ignoredStreams()
{
    TestStream meter(m_loop, m_core, "cutefish.internal.meter", false, "test-microphone");
    TestStream peak(m_loop, m_core, "test-peak", false, "test-microphone", {{PW_KEY_STREAM_MONITOR, "true"}});
    TestStream plumbing(m_loop, m_core, "test-loopback", false, "test-microphone",
                        {{PW_KEY_NODE_LINK_GROUP, "test-loopback-group"}});
    TestStream recorder(m_loop, m_core, "test-recorder", false, "test-microphone");
    QTRY_VERIFY_WITH_TIMEOUT(hasUse(m_capture->uses(), DeviceUse::Microphone, "test-recorder"), 10000);
    QTest::qWait(500);
    QCOMPARE(m_capture->uses().size(), 1);
}

void PrivacyTest::pausedStream()
{
    QTRY_VERIFY_WITH_TIMEOUT(m_capture->uses().isEmpty(), 10000);
    TestStream stream(m_loop, m_core, "test-paused", false, "test-microphone");
    QTRY_VERIFY_WITH_TIMEOUT(hasUse(m_capture->uses(), DeviceUse::Microphone, "test-paused"), 10000);
    stream.setActive(false);
    QTRY_VERIFY_WITH_TIMEOUT(m_capture->uses().isEmpty(), 10000);
    stream.setActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(hasUse(m_capture->uses(), DeviceUse::Microphone, "test-paused"), 10000);
}

void PrivacyTest::merged()
{
    auto *capture = new PipeWireCapture;
    auto *watch = new VideoDeviceWatch(m_devices.path());
    DeviceUsage usage(capture, watch);
    QTRY_VERIFY_WITH_TIMEOUT(capture->isConnected(), 10000);
    QTRY_VERIFY_WITH_TIMEOUT(usage.uses().isEmpty(), 10000);
    QVERIFY(!usage.camera() && !usage.microphone() && !usage.systemAudio());

    TestStream stream(m_loop, m_core, "test-recorder", false, "test-microphone");
    QFile device(m_devices.filePath("video0"));
    QVERIFY(device.open(QIODevice::ReadOnly));
    QTRY_VERIFY_WITH_TIMEOUT(usage.camera() && usage.microphone(), 10000);
    QVERIFY(!usage.systemAudio());
    QCOMPARE(usage.uses().size(), 2);
    const QVariantList list = usage.usesList();
    QCOMPARE(list.size(), 2);
    QCOMPARE(list.first().toMap().value("kind").toInt(), int(DeviceUsage::Camera));
    for (const QVariant &entry : list) {
        const QVariantMap map = entry.toMap();
        QCOMPARE(map.value("pid").toUInt(), quint32(getpid()));
        QVERIFY(map.value("kind").toInt() == DeviceUsage::Camera || map.value("kind").toInt() == DeviceUsage::Microphone);
    }
    device.close();
    QTRY_VERIFY_WITH_TIMEOUT(!usage.camera() && usage.microphone(), 10000);
}

QTEST_GUILESS_MAIN(PrivacyTest)

#include "privacy_test.moc"
