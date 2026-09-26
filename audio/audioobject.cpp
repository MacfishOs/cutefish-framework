#include "audioobject.h"
#include "backend.h"

#include <algorithm>
#include <cmath>

#include <spa/debug/types.h>
#include <spa/param/audio/raw-types.h>
#include <spa/param/profile.h>
#include <spa/param/props.h>
#include <spa/param/route.h>
#include <spa/pod/parser.h>

namespace CutefishAudio {
namespace {

template<typename T>
QVector<T> arrayValues(const spa_pod *pod, quint32 type)
{
    if (!spa_pod_is_array(pod) || SPA_POD_ARRAY_VALUE_TYPE(pod) != type
        || SPA_POD_ARRAY_VALUE_SIZE(pod) != sizeof(T)) {
        return {};
    }
    const auto *values = static_cast<const T *>(SPA_POD_ARRAY_VALUES(pod));
    const auto count = std::min<quint32>(SPA_POD_ARRAY_N_VALUES(pod), SPA_AUDIO_MAX_CHANNELS);
    return QVector<T>(values, values + count);
}

int availability(quint32 value)
{
    if (value == SPA_PARAM_AVAILABILITY_yes)
        return AudioObject::Available;
    if (value == SPA_PARAM_AVAILABILITY_no)
        return AudioObject::Unavailable;
    return AudioObject::Unknown;
}

void updateChoice(QVariantList &list, const QVariantMap &choice)
{
    for (QVariant &item : list) {
        if (item.toMap().value("index") == choice.value("index")) {
            item = choice;
            return;
        }
    }
    list.append(choice);
}

}

AudioObject::AudioObject(Backend *backend, quint32 id, Kind kind)
    : QObject(backend)
    , m_backend(backend)
    , m_id(id)
    , m_kind(kind)
{
}

QString AudioObject::name() const
{
    return m_properties.value(m_kind == Card ? "device.name" : "node.name").toString();
}

QString AudioObject::description() const
{
    const QStringList keys = m_kind == Card
        ? QStringList {"device.description", "device.nick", "device.name"}
        : QStringList {"node.description", "node.nick", "media.name", "application.name", "node.name"};
    for (const QString &key : keys) {
        const QString value = m_properties.value(key).toString();
        if (!value.isEmpty())
            return value;
    }
    return name();
}

// A built-in card's node is named after the controller ("Ryzen HD Audio
// Controller Analog Stereo"); its active port says what it is ("Speakers").
QString AudioObject::displayName() const
{
    const QString cardBus = bus();
    if ((m_kind == Sink || m_kind == Source) && !isVirtualDevice()
        && cardBus != "usb" && cardBus != "bluetooth") {
        const QVariantList routes = ports();
        const int active = activePortIndex();
        if (active >= 0 && active < routes.size()) {
            const QString port = routes.at(active).toMap().value("description").toString();
            if (!port.isEmpty())
                return port;
        }
    }
    return description();
}

QString AudioObject::iconName() const
{
    return m_properties.value("device.icon-name",
        m_properties.value("application.icon-name",
            m_kind == Source || m_kind == Capture ? "audio-input-microphone" : "audio-card")).toString();
}

double AudioObject::fromLinear(float volume)
{
    return std::isfinite(volume) ? std::cbrt(std::max(0.0f, volume)) : 0.0;
}

float AudioObject::toLinear(double volume)
{
    if (!std::isfinite(volume))
        return 0.0f;
    const double bounded = std::clamp(volume, 0.0, MaximalVolume);
    return static_cast<float>(bounded * bounded * bounded);
}

double AudioObject::volume() const
{
    return m_volumes.isEmpty() ? 0.0 : fromLinear(*std::max_element(m_volumes.begin(), m_volumes.end()));
}

QVariantList AudioObject::channelVolumes() const
{
    QVariantList values;
    for (float volume : m_volumes)
        values.append(fromLinear(volume));
    return values;
}

QStringList AudioObject::channels() const
{
    QStringList names;
    for (quint32 channel : m_channels) {
        const char *name = spa_debug_type_find_short_name(spa_type_audio_channel, channel);
        names.append(name ? QString::fromUtf8(name) : QStringLiteral("UNKNOWN"));
    }
    return names;
}

bool AudioObject::isDefault() const
{
    return m_backend && m_backend->defaultNode(m_kind) == this;
}

bool AudioObject::isVirtualDevice() const
{
    return m_properties.value("node.virtual").toString() == "true"
        || !m_properties.contains("device.id");
}

quint32 AudioObject::cardIndex() const
{
    bool ok = false;
    const quint32 id = m_properties.value("device.id").toUInt(&ok);
    return ok ? id : InvalidId;
}

// The card's bus, such as "pci", "usb" or "bluetooth"; empty for virtual
// nodes and for SoC codecs, which have no bus.
QString AudioObject::bus() const
{
    const AudioObject *card = m_backend ? m_backend->object(cardIndex()) : nullptr;
    return card ? card->m_properties.value("device.bus").toString() : QString();
}

int AudioObject::profileDevice() const
{
    bool ok = false;
    const int id = m_properties.value("card.profile.device").toInt(&ok);
    return ok ? id : -1;
}

QVariantList AudioObject::ports() const
{
    const AudioObject *card = m_backend ? m_backend->object(cardIndex()) : nullptr;
    if (!card || (m_kind != Sink && m_kind != Source))
        return {};
    QVariantList result;
    for (const QVariant &value : card->m_routes) {
        const QVariantMap route = value.toMap();
        const QVariantList devices = route.value("devices").toList();
        const QVariantList profiles = route.value("profiles").toList();
        if (route.value("direction").toUInt() != (m_kind == Sink ? SPA_DIRECTION_OUTPUT : SPA_DIRECTION_INPUT))
            continue;
        if (!devices.isEmpty() && !devices.contains(profileDevice()))
            continue;
        if (!profiles.isEmpty() && !profiles.contains(card->m_profile))
            continue;
        result.append(route);
    }
    return result;
}

int AudioObject::activePortIndex() const
{
    const AudioObject *card = m_backend ? m_backend->object(cardIndex()) : nullptr;
    if (!card)
        return -1;
    const int active = card->m_activeRoutes.value(profileDevice(), -1);
    const QVariantList routes = ports();
    for (int i = 0; i < routes.size(); ++i) {
        if (routes.at(i).toMap().value("index").toInt() == active)
            return i;
    }
    return -1;
}

int AudioObject::activeProfileIndex() const
{
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles.at(i).toMap().value("index").toInt() == m_profile)
            return i;
    }
    return -1;
}

void AudioObject::setVolume(double volume)
{
    if (!m_backend || !volumeWritable() || !std::isfinite(volume))
        return;
    const float value = toLinear(volume);
    const float maximum = *std::max_element(m_volumes.begin(), m_volumes.end());
    QVector<float> volumes = m_volumes;
    for (float &channel : volumes)
        channel = maximum > 0.0f ? channel / maximum * value : value;
    m_backend->setVolume(this, volumes);
}

void AudioObject::setMuted(bool muted)
{
    if (m_backend && m_writable)
        m_backend->setMute(this, muted);
}

void AudioObject::setChannelVolumes(const QVariantList &values)
{
    if (!m_backend || !volumeWritable() || values.size() != m_volumes.size())
        return;
    QVector<float> volumes;
    for (const QVariant &value : values) {
        bool ok = false;
        const double volume = value.toDouble(&ok);
        if (!ok || !std::isfinite(volume))
            return;
        volumes.append(toLinear(volume));
    }
    m_backend->setVolume(this, volumes);
}

void AudioObject::setChannelVolume(int channel, double volume)
{
    if (channel < 0 || channel >= m_volumes.size() || !std::isfinite(volume))
        return;
    QVariantList values = channelVolumes();
    values[channel] = volume;
    setChannelVolumes(values);
}

void AudioObject::setDefault(bool enable)
{
    if (m_backend && (m_kind == Sink || m_kind == Source))
        m_backend->setDefault(this, enable);
}

void AudioObject::setActivePortIndex(int index)
{
    const QVariantList routes = ports();
    if (m_backend && index >= 0 && index < routes.size()
        && routes.at(index).toMap().value("availability").toInt() != Unavailable) {
        m_backend->setRoute(this, routes.at(index).toMap().value("index").toInt());
    }
}

void AudioObject::setActiveProfileIndex(int index)
{
    if (m_backend && m_kind == Card && index >= 0 && index < m_profiles.size()
        && m_profiles.at(index).toMap().value("availability").toInt() != Unavailable) {
        m_backend->setProfile(this, m_profiles.at(index).toMap().value("index").toInt());
    }
}

void AudioObject::setDeviceIndex(quint32 id)
{
    if (m_backend && (m_kind == Playback || m_kind == Capture))
        m_backend->moveStream(this, id);
}

void AudioObject::switchStreams()
{
    if (!m_backend || (m_kind != Sink && m_kind != Source))
        return;
    for (AudioObject *object : m_backend->objects()) {
        if (object->kind() == (m_kind == Sink ? Playback : Capture))
            object->setDeviceIndex(m_id);
    }
}

void AudioObject::updateProperties(const spa_dict *properties)
{
    if (!properties)
        return;
    const spa_dict_item *item;
    spa_dict_for_each(item, properties) {
        const QString key = QString::fromUtf8(item->key);
        if (item->value)
            m_properties.insert(key, QString::fromUtf8(item->value));
        else
            m_properties.remove(key);
    }
    Q_EMIT changed();
}

void AudioObject::updateVolume(const spa_pod *param)
{
    if (!param || !spa_pod_is_object(param))
        return;
    const spa_pod_prop *prop;
    if ((prop = spa_pod_find_prop(param, nullptr, SPA_PROP_channelVolumes))) {
        const auto volumes = arrayValues<float>(&prop->value, SPA_TYPE_Float);
        if (!volumes.isEmpty()) {
            m_volumes = volumes;
            for (float &value : m_volumes)
                value = std::isfinite(value) ? std::max(0.0f, value) : 0.0f;
            m_channelVolume = true;
        }
    }
    if (!m_channelVolume && (prop = spa_pod_find_prop(param, nullptr, SPA_PROP_volume))) {
        float volume = 0.0f;
        if (spa_pod_get_float(&prop->value, &volume) == 0 && std::isfinite(volume))
            m_volumes = {std::max(0.0f, volume)};
    }
    if ((prop = spa_pod_find_prop(param, nullptr, SPA_PROP_mute)))
        spa_pod_get_bool(&prop->value, &m_muted);
    if ((prop = spa_pod_find_prop(param, nullptr, SPA_PROP_channelMap)))
        m_channels = arrayValues<quint32>(&prop->value, SPA_TYPE_Id);
    Q_EMIT changed();
}

void AudioObject::updateProfile(const spa_pod *param, bool current)
{
    int index = -1;
    int priority = 0;
    quint32 available = SPA_PARAM_AVAILABILITY_unknown;
    const char *name = "";
    const char *description = "";
    if (spa_pod_parse_object(param, SPA_TYPE_OBJECT_ParamProfile, nullptr,
            SPA_PARAM_PROFILE_index, SPA_POD_Int(&index),
            SPA_PARAM_PROFILE_name, SPA_POD_OPT_String(&name),
            SPA_PARAM_PROFILE_description, SPA_POD_OPT_String(&description),
            SPA_PARAM_PROFILE_priority, SPA_POD_OPT_Int(&priority),
            SPA_PARAM_PROFILE_available, SPA_POD_OPT_Id(&available)) < 0)
        return;
    if (current) {
        if (m_profile != index) {
            m_activeRoutes.clear();
            m_routeProperties.clear();
        }
        m_profile = index;
    } else {
        updateChoice(m_profiles, {{"index", index}, {"name", QString::fromUtf8(name)},
            {"description", QString::fromUtf8(description)}, {"priority", priority},
            {"availability", availability(available)}});
    }
    Q_EMIT changed();
}

int AudioObject::updateRoute(const spa_pod *param, bool current)
{
    int index = -1;
    int device = -1;
    int priority = 0;
    quint32 direction = SPA_DIRECTION_OUTPUT;
    quint32 available = SPA_PARAM_AVAILABILITY_unknown;
    const char *name = "";
    const char *description = "";
    if (spa_pod_parse_object(param, SPA_TYPE_OBJECT_ParamRoute, nullptr,
            SPA_PARAM_ROUTE_index, SPA_POD_Int(&index),
            SPA_PARAM_ROUTE_device, SPA_POD_OPT_Int(&device),
            SPA_PARAM_ROUTE_direction, SPA_POD_OPT_Id(&direction),
            SPA_PARAM_ROUTE_name, SPA_POD_OPT_String(&name),
            SPA_PARAM_ROUTE_description, SPA_POD_OPT_String(&description),
            SPA_PARAM_ROUTE_priority, SPA_POD_OPT_Int(&priority),
            SPA_PARAM_ROUTE_available, SPA_POD_OPT_Id(&available)) < 0)
        return -1;
    if (current) {
        m_activeRoutes.insert(device, index);
        const spa_pod_prop *props = spa_pod_find_prop(param, nullptr, SPA_PARAM_ROUTE_props);
        if (props && spa_pod_is_object(&props->value)) {
            m_routeProperties.insert(device, QByteArray(reinterpret_cast<const char *>(&props->value),
                                                        SPA_POD_SIZE(&props->value)));
        }
    } else {
        QVariantList devices;
        QVariantList profiles;
        const spa_pod_prop *prop = spa_pod_find_prop(param, nullptr, SPA_PARAM_ROUTE_devices);
        if (prop) {
            for (int value : arrayValues<int>(&prop->value, SPA_TYPE_Int))
                devices.append(value);
        }
        prop = spa_pod_find_prop(param, nullptr, SPA_PARAM_ROUTE_profiles);
        if (prop) {
            for (int value : arrayValues<int>(&prop->value, SPA_TYPE_Int))
                profiles.append(value);
        }
        updateChoice(m_routes, {{"index", index}, {"name", QString::fromUtf8(name)},
            {"description", QString::fromUtf8(description)}, {"priority", priority},
            {"direction", direction}, {"availability", availability(available)},
            {"devices", devices}, {"profiles", profiles}});
    }
    Q_EMIT changed();
    return current ? device : -1;
}

}
