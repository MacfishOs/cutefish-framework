#include "volumemonitor.h"
#include "backend.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include <spa/param/audio/format-utils.h>

namespace CutefishAudio {

const pw_stream_events VolumeMonitor::streamEvents = [] {
    pw_stream_events events {};
    events.version = PW_VERSION_STREAM_EVENTS;
    events.process = [](void *data) { static_cast<VolumeMonitor *>(data)->process(); };
    events.state_changed = [](void *data, pw_stream_state, pw_stream_state state, const char *) {
        auto *self = static_cast<VolumeMonitor *>(data);
        const bool available = state == PW_STREAM_STATE_STREAMING;
        if (self->m_available != available) {
            self->m_available = available;
            Q_EMIT self->availableChanged();
        }
        if (!available && self->m_volume != 0.0) {
            self->m_volume = 0.0;
            Q_EMIT self->volumeChanged();
        }
    };
    return events;
}();

VolumeMonitor::VolumeMonitor(QObject *parent)
    : QObject(parent)
{
    connect(Backend::instance(), &Backend::disconnecting, this, [this] { setTarget(nullptr); });
    connect(Backend::instance(), &Backend::objectRemoved, this, [this](AudioObject *object) {
        if (m_target == object)
            setTarget(nullptr);
    });
}

VolumeMonitor::~VolumeMonitor()
{
    stop();
}

void VolumeMonitor::setTarget(AudioObject *target)
{
    if (m_target == target)
        return;
    stop();
    m_target = target;
    Q_EMIT targetChanged();
    Backend *backend = Backend::instance();
    if (!target || !backend->isConnected() || backend->object(target->index()) != target
        || target->kind() == AudioObject::Card)
        return;
    const QByteArray serial = target->properties().value(PW_KEY_OBJECT_SERIAL).toString().toUtf8();
    if (serial.isEmpty())
        return;
    const bool sink = target->kind() == AudioObject::Sink;
    // A passive link never wakes its peer: right for a sink, which plays anyway,
    // but a microphone nobody else records from would stay suspended.
    auto *properties = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Capture",
        PW_KEY_NODE_NAME, "cutefish.internal.monitor",
        PW_KEY_MEDIA_ROLE, "DSP", PW_KEY_TARGET_OBJECT, serial.constData(),
        "stream.capture.sink", sink ? "true" : "false",
        "node.passive", sink ? "true" : "false", "node.dont-fallback", "true", "node.linger", "true", nullptr);
    m_stream = pw_stream_new(backend->core(), "Cutefish volume monitor", properties);
    if (!m_stream)
        return;
    pw_stream_add_listener(m_stream, &m_listener, &streamEvents, this);
    spa_audio_info_raw format {};
    format.format = SPA_AUDIO_FORMAT_F32;
    format.rate = 48000;
    format.channels = 1;
    format.position[0] = SPA_AUDIO_CHANNEL_MONO;
    uint8_t buffer[512];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_pod *params[] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &format)};
    if (pw_stream_connect(m_stream, PW_DIRECTION_INPUT, PW_ID_ANY,
            static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1) < 0)
        stop();
}

void VolumeMonitor::process()
{
    double peak = 0.0;
    quint32 frames = 0;
    pw_buffer *buffer;
    while ((buffer = pw_stream_dequeue_buffer(m_stream))) {
        const spa_buffer *spaBuffer = buffer->buffer;
        if (spaBuffer->n_datas > 0) {
            const spa_data &data = spaBuffer->datas[0];
            if (data.data && data.chunk && data.chunk->offset <= data.maxsize) {
                const quint32 size = std::min(data.chunk->size, data.maxsize - data.chunk->offset);
                const auto *samples = static_cast<const uint8_t *>(data.data) + data.chunk->offset;
                frames += size / sizeof(float);
                for (quint32 offset = 0; offset + sizeof(float) <= size; offset += sizeof(float)) {
                    float value;
                    std::memcpy(&value, samples + offset, sizeof(value));
                    if (std::isfinite(value))
                        peak = std::max(peak, double(std::abs(value)));
                }
            }
        }
        pw_stream_queue_buffer(m_stream, buffer);
    }
    if (frames == 0)
        return;
    // Rise at once but fall at 30 dB/s, as a level meter does; raw per-buffer
    // peaks jump by tens of dB and make the meter flicker.
    const double fall = std::pow(10.0, -30.0 / 20.0 * frames / 48000.0);
    const double volume = std::clamp(std::max(peak, m_volume * fall), 0.0, 1.0);
    if (m_volume != volume) {
        m_volume = volume;
        Q_EMIT volumeChanged();
    }
}

void VolumeMonitor::stop()
{
    if (m_stream) {
        spa_hook_remove(&m_listener);
        pw_stream_destroy(m_stream);
        m_stream = nullptr;
    }
    if (m_available) {
        m_available = false;
        Q_EMIT availableChanged();
    }
    if (m_volume != 0.0) {
        m_volume = 0.0;
        Q_EMIT volumeChanged();
    }
}

}
