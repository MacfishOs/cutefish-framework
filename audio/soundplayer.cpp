#include "soundplayer.h"
#include "backend.h"

#include <algorithm>
#include <cmath>

#include <spa/debug/types.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/audio/raw-types.h>

namespace CutefishAudio {

const pw_stream_events SoundPlayer::streamEvents = [] {
    pw_stream_events events {};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.process = [](void *data) { static_cast<SoundPlayer *>(data)->process(); };
    events.drained = [](void *data) {
        auto *self = static_cast<SoundPlayer *>(data);
        const quint64 generation = self->m_generation;
        QMetaObject::invokeMethod(self, [self, generation] {
            if (self->m_generation == generation)
                self->stop();
        }, Qt::QueuedConnection);
    };
    events.state_changed = [](void *data, pw_stream_state, pw_stream_state state, const char *error) {
        auto *self = static_cast<SoundPlayer *>(data);
        if (state == PW_STREAM_STATE_ERROR) {
            Q_EMIT self->failed(QString::fromUtf8(error));
            const quint64 generation = self->m_generation;
            QMetaObject::invokeMethod(self, [self, generation] {
                if (self->m_generation == generation)
                    self->stop();
            }, Qt::QueuedConnection);
        }
    };
    return events;
}();

SoundPlayer::SoundPlayer(Backend *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, &SoundPlayer::stop);
    connect(m_backend, &Backend::disconnecting, this, &SoundPlayer::stop);
    connect(m_backend, &Backend::objectRemoved, this, [this](AudioObject *object) {
        if (m_stream && object->kind() == AudioObject::Sink) {
            const char *target = pw_properties_get(pw_stream_get_properties(m_stream), PW_KEY_TARGET_OBJECT);
            if (target && object->properties().value(PW_KEY_OBJECT_SERIAL).toString() == QString::fromUtf8(target))
                stop();
        }
    });
}

SoundPlayer::~SoundPlayer()
{
    stop();
}

void SoundPlayer::play(AudioObject *sink, const QString &channel)
{
    stop();
    if (!sink || sink->kind() != AudioObject::Sink || !m_backend->isConnected()
        || m_backend->object(sink->index()) != sink)
        return;
    QString channelName = channel;
    const QMap<QString, QString> names {
        {"front-left", "FL"}, {"front-right", "FR"}, {"front-center", "FC"},
        {"rear-left", "RL"}, {"rear-right", "RR"}, {"rear-center", "RC"},
        {"side-left", "SL"}, {"side-right", "SR"}, {"lfe", "LFE"}, {"mono", "MONO"}
    };
    channelName = names.value(channelName, channelName);
    QStringList channels = sink->channels();
    if (channels.isEmpty())
        channels = {"FL", "FR"};
    m_channel = channel.isEmpty() ? -1 : channels.indexOf(channelName);
    if ((!channel.isEmpty() && m_channel < 0) || channels.size() > SPA_AUDIO_MAX_CHANNELS)
        return;
    m_channels = channels.size();
    m_frame = 0;
    m_frames = channel.isEmpty() ? 5760 : 24000;
    m_draining = false;
    const QByteArray target = sink->properties().value(PW_KEY_OBJECT_SERIAL).toString().toUtf8();
    if (target.isEmpty())
        return;
    auto *properties = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
        PW_KEY_NODE_NAME, "cutefish.internal.feedback",
        PW_KEY_MEDIA_ROLE, "Notification", PW_KEY_MEDIA_NAME, "Cutefish audio feedback",
        PW_KEY_TARGET_OBJECT, target.constData(), PW_KEY_NODE_DONT_RECONNECT, "true",
        "node.dont-fallback", "true", "node.linger", "true", "stream.dont-remix", "true", nullptr);
    m_stream = pw_stream_new(m_backend->core(), "Cutefish audio feedback", properties);
    if (!m_stream)
        return;
    pw_stream_add_listener(m_stream, &m_listener, &streamEvents, this);
    spa_audio_info_raw format {};
    format.format = SPA_AUDIO_FORMAT_F32;
    format.rate = 48000;
    format.channels = m_channels;
    for (quint32 i = 0; i < m_channels; ++i) {
        const QByteArray name = channels.at(i).toUtf8();
        const spa_type_info *type = spa_debug_type_find_short(spa_type_audio_channel, name.constData());
        format.position[i] = type ? type->type : SPA_AUDIO_CHANNEL_UNKNOWN;
    }
    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_pod *params[] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &format)};
    const int result = pw_stream_connect(m_stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
        static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1);
    if (result < 0) {
        Q_EMIT failed(QString::fromLocal8Bit(strerror(-result)));
        stop();
        return;
    }
    m_timeout.start(3000);
}

void SoundPlayer::process()
{
    pw_buffer *buffer = pw_stream_dequeue_buffer(m_stream);
    if (!buffer)
        return;
    spa_buffer *spaBuffer = buffer->buffer;
    if (spaBuffer->n_datas == 0 || !spaBuffer->datas[0].data || !spaBuffer->datas[0].chunk) {
        pw_stream_queue_buffer(m_stream, buffer);
        return;
    }
    spa_data &data = spaBuffer->datas[0];
    const quint32 stride = sizeof(float) * m_channels;
    const quint32 capacity = data.maxsize / stride;
    const quint32 requested = buffer->requested ? std::min<quint64>(buffer->requested, capacity) : capacity;
    const quint32 frames = std::min(requested, m_frames - m_frame);
    auto *samples = static_cast<float *>(data.data);
    constexpr double pi = 3.14159265358979323846;
    for (quint32 i = 0; i < frames; ++i, ++m_frame) {
        const double envelope = std::min({1.0, m_frame / 480.0, (m_frames - m_frame) / 960.0});
        const float value = static_cast<float>(0.15 * envelope * std::sin(2.0 * pi * 440.0 * m_frame / 48000.0));
        for (quint32 channel = 0; channel < m_channels; ++channel)
            samples[i * m_channels + channel] = m_channel < 0 || int(channel) == m_channel ? value : 0.0f;
    }
    data.chunk->offset = 0;
    data.chunk->stride = stride;
    data.chunk->size = frames * stride;
    buffer->size = frames;
    pw_stream_queue_buffer(m_stream, buffer);
    if (m_frame == m_frames && !m_draining) {
        m_draining = true;
        pw_stream_flush(m_stream, true);
    }
}

void SoundPlayer::stop()
{
    ++m_generation;
    m_timeout.stop();
    if (!m_stream)
        return;
    spa_hook_remove(&m_listener);
    pw_stream_destroy(m_stream);
    m_stream = nullptr;
    Q_EMIT finished();
}

VolumeFeedback::VolumeFeedback(QObject *parent)
    : QObject(parent)
    , m_player(Backend::instance())
{
    connect(Backend::instance(), &Backend::connectedChanged, this, &VolumeFeedback::validChanged);
}

bool VolumeFeedback::isValid() const
{
    return Backend::instance()->isConnected();
}

void VolumeFeedback::play(quint32 sinkIndex)
{
    m_player.play(Backend::instance()->object(sinkIndex));
}

SpeakerTest::SpeakerTest(QObject *parent)
    : QObject(parent)
    , m_player(Backend::instance())
{
    connect(Backend::instance(), &Backend::objectRemoved, this, [this](AudioObject *object) {
        if (m_sink == object)
            setSink(nullptr);
    });
}

void SpeakerTest::setSink(AudioObject *sink)
{
    if (m_sink == sink)
        return;
    m_player.stop();
    m_sink = sink;
    Q_EMIT sinkChanged();
}

void SpeakerTest::testChannel(const QString &channel)
{
    if (!channel.isEmpty())
        m_player.play(m_sink, channel);
}

}
