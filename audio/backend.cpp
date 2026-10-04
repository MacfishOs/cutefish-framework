#include "backend.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSocketNotifier>

#include <cerrno>
#include <cstring>

#include <spa/param/profile.h>
#include <spa/param/audio/raw.h>
#include <spa/param/props.h>
#include <spa/param/route.h>
#include <spa/pod/builder.h>

namespace CutefishAudio {

const pw_core_events Backend::coreEvents = [] {
    pw_core_events events {};
    events.version = PW_VERSION_CORE_EVENTS;
    events.info = [](void *data, const pw_core_info *) {
        auto *self = static_cast<Backend *>(data);
        if (!self->m_connected) {
            self->m_connected = true;
            Q_EMIT self->connectedChanged();
        }
    };
    events.error = [](void *data, quint32 id, int, int result, const char *message) {
        auto *self = static_cast<Backend *>(data);
        Q_EMIT self->operationFailed(QString::fromUtf8(message));
        if (id == PW_ID_CORE && (result == -EPIPE || result == -ECONNRESET)) {
            if (!self->m_failurePending) {
                self->m_failurePending = true;
                QMetaObject::invokeMethod(self, &Backend::fail, Qt::QueuedConnection);
            }
        }
    };
    return events;
}();

const pw_registry_events Backend::registryEvents = [] {
    pw_registry_events events {};
    events.version = PW_VERSION_REGISTRY_EVENTS;
    events.global = [](void *data, quint32 id, quint32 permissions, const char *type,
                       quint32 version, const spa_dict *properties) {
        static_cast<Backend *>(data)->addGlobal(id, permissions, type, version, properties);
    };
    events.global_remove = [](void *data, quint32 id) {
        static_cast<Backend *>(data)->removeGlobal(id);
    };
    return events;
}();

const pw_node_events Backend::nodeEvents = [] {
    pw_node_events events {};
    events.version = PW_VERSION_NODE_EVENTS;
    events.info = [](void *data, const pw_node_info *info) {
        auto *binding = static_cast<Binding *>(data);
        AudioObject *object = binding->object;
        object->updateProperties(info->props);
        if (info->change_mask & PW_NODE_CHANGE_MASK_STATE) {
            switch (info->state) {
            case PW_NODE_STATE_RUNNING:
                object->m_state = AudioObject::RunningState;
                break;
            case PW_NODE_STATE_IDLE:
                object->m_state = AudioObject::IdleState;
                break;
            case PW_NODE_STATE_SUSPENDED:
                object->m_state = AudioObject::SuspendedState;
                break;
            default:
                object->m_state = AudioObject::UnknownState;
            }
        }
        if (info->change_mask & PW_NODE_CHANGE_MASK_PARAMS) {
            object->m_writable = false;
            for (quint32 i = 0; i < info->n_params; ++i) {
                if (info->params[i].id == SPA_PARAM_Props) {
                    object->m_writable = (info->params[i].flags & SPA_PARAM_INFO_WRITE)
                        && (binding->permissions & PW_PERM_W);
                }
            }
        }
        Q_EMIT object->changed();
        binding->backend->updateDefaults();
    };
    events.param = [](void *data, int, quint32 id, quint32, quint32, const spa_pod *param) {
        auto *binding = static_cast<Binding *>(data);
        // The card's cached route still holds the previous volume at this
        // point; applying it here would flash the old value.
        if (id == SPA_PARAM_Props)
            binding->object->updateVolume(param);
    };
    return events;
}();

const pw_device_events Backend::deviceEvents = [] {
    pw_device_events events {};
    events.version = PW_VERSION_DEVICE_EVENTS;
    events.info = [](void *data, const pw_device_info *info) {
        auto *binding = static_cast<Binding *>(data);
        binding->object->updateProperties(info->props);
        if (!(info->change_mask & PW_DEVICE_CHANGE_MASK_PARAMS))
            return;
        for (quint32 i = 0; i < info->n_params; ++i) {
            const auto &param = info->params[i];
            if (binding->paramFlags.contains(param.id)
                && binding->paramFlags.value(param.id) == param.flags)
                continue;
            binding->paramFlags.insert(param.id, param.flags);
            if (!(param.flags & SPA_PARAM_INFO_READ))
                continue;
            if (param.id == SPA_PARAM_EnumProfile)
                binding->object->m_profiles.clear();
            else if (param.id == SPA_PARAM_EnumRoute)
                binding->object->m_routes.clear();
            else if (param.id != SPA_PARAM_Route && param.id != SPA_PARAM_Profile)
                continue;
            pw_device_enum_params(reinterpret_cast<pw_device *>(binding->proxy),
                                  ++binding->enumSequence, param.id, 0, UINT32_MAX, nullptr);
        }
        Q_EMIT binding->object->changed();
    };
    events.param = [](void *data, int, quint32 id, quint32, quint32, const spa_pod *param) {
        auto *binding = static_cast<Binding *>(data);
        if (!param)
            return;
        if (id == SPA_PARAM_EnumProfile || id == SPA_PARAM_Profile)
            binding->object->updateProfile(param, id == SPA_PARAM_Profile);
        else if (id == SPA_PARAM_EnumRoute || id == SPA_PARAM_Route) {
            const int device = binding->object->updateRoute(param, id == SPA_PARAM_Route);
            if (device < 0)
                return;
            // Only that device's nodes: the card's other routes may still hold
            // the previous volume of a node that has already changed.
            for (AudioObject *node : binding->backend->objects()) {
                if (node->kind() != AudioObject::Card && node->cardIndex() == binding->object->index()
                    && node->profileDevice() == device)
                    binding->backend->updateRouteVolume(node);
            }
        }
    };
    return events;
}();

const pw_metadata_events Backend::metadataEvents = [] {
    pw_metadata_events events {};
    events.version = PW_VERSION_METADATA_EVENTS;
    events.property = [](void *data, quint32 subject, const char *key, const char *, const char *value) {
        auto *self = static_cast<Backend *>(data);
        if (subject != PW_ID_CORE)
            return 0;
        const QString name = value ? QJsonDocument::fromJson(value).object().value("name").toString() : QString();
        if (!key || std::strcmp(key, "default.audio.sink") == 0)
            self->m_defaultSink = name;
        if (!key || std::strcmp(key, "default.audio.source") == 0)
            self->m_defaultSource = name;
        self->updateDefaults();
        return 0;
    };
    return events;
}();

Backend::Backend(QObject *parent)
    : QObject(parent)
{
    pw_init(nullptr, nullptr);
    m_retry.setSingleShot(true);
    m_retry.setInterval(1000);
    connect(&m_retry, &QTimer::timeout, this, &Backend::connectToDaemon);
    QTimer::singleShot(0, this, &Backend::connectToDaemon);
}

Backend::~Backend()
{
    m_retry.stop();
    disconnectFromDaemon();
    releaseContext();
    pw_deinit();
}

Backend *Backend::instance()
{
    static Backend *backend = new Backend(QCoreApplication::instance());
    return backend;
}

QList<AudioObject *> Backend::objects() const
{
    QList<AudioObject *> result;
    for (const Binding *binding : m_bindings)
        result.append(binding->object);
    return result;
}

AudioObject *Backend::object(quint32 id) const
{
    const Binding *binding = m_bindings.value(id);
    return binding ? binding->object : nullptr;
}

AudioObject *Backend::defaultNode(AudioObject::Kind kind) const
{
    if (kind != AudioObject::Sink && kind != AudioObject::Source)
        return nullptr;
    const QString name = kind == AudioObject::Sink ? m_defaultSink : m_defaultSource;
    for (const Binding *binding : m_bindings) {
        if (binding->object->kind() == kind && binding->object->name() == name)
            return binding->object;
    }
    return nullptr;
}

void Backend::connectToDaemon()
{
    if (m_core)
        return;
    m_failurePending = false;
    // A context loads its modules and configuration when created; kept across
    // attempts, a retry while the daemon is away costs only the connect.
    if (!m_loop) {
        m_loop = pw_loop_new(nullptr);
        if (m_loop)
            pw_loop_enter(m_loop);
    }
    if (m_loop && !m_context)
        m_context = pw_context_new(m_loop, nullptr, 0);
    if (m_context) {
        m_core = pw_context_connect(m_context,
            pw_properties_new(PW_KEY_APP_NAME, "Cutefish Audio",
                              PW_KEY_APP_ID, "org.cutefish.Audio", nullptr), 0);
    }
    if (!m_core) {
        fail();
        return;
    }
    pw_core_add_listener(m_core, &m_coreListener, &coreEvents, this);
    m_registry = pw_core_get_registry(m_core, PW_VERSION_REGISTRY, 0);
    if (!m_registry) {
        fail();
        return;
    }
    pw_registry_add_listener(m_registry, &m_registryListener, &registryEvents, this);
    m_notifier = new QSocketNotifier(pw_loop_get_fd(m_loop), QSocketNotifier::Read, this);
    connect(m_notifier, &QSocketNotifier::activated, this, &Backend::iterate);
}

void Backend::iterate()
{
    if (!m_loop || m_iterating)
        return;
    m_iterating = true;
    const int result = pw_loop_iterate(m_loop, 0);
    m_iterating = false;
    if (result < 0 && result != -EINTR) {
        // A loop that failed is not reused.
        disconnectFromDaemon();
        releaseContext();
        m_retry.start();
    }
}

void Backend::fail()
{
    disconnectFromDaemon();
    m_retry.start();
}

void Backend::disconnectFromDaemon()
{
    Q_EMIT disconnecting();
    delete m_notifier;
    m_notifier = nullptr;
    const auto ids = m_bindings.keys();
    for (quint32 id : ids)
        removeGlobal(id);
    releaseMetadata();
    if (m_registry) {
        spa_hook_remove(&m_registryListener);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(m_registry));
        m_registry = nullptr;
    }
    if (m_core) {
        spa_hook_remove(&m_coreListener);
        pw_core_disconnect(m_core);
        m_core = nullptr;
    }
    m_links.clear();
    m_defaultSink.clear();
    m_defaultSource.clear();
    if (m_connected) {
        m_connected = false;
        Q_EMIT connectedChanged();
    }
    updateDefaults();
}

void Backend::releaseContext()
{
    if (m_context) {
        pw_context_destroy(m_context);
        m_context = nullptr;
    }
    if (m_loop) {
        pw_loop_leave(m_loop);
        pw_loop_destroy(m_loop);
        m_loop = nullptr;
    }
}

void Backend::releaseMetadata()
{
    if (m_metadata) {
        spa_hook_remove(&m_metadataListener);
        pw_proxy_destroy(reinterpret_cast<pw_proxy *>(m_metadata));
        m_metadata = nullptr;
    }
    m_metadataId = AudioObject::InvalidId;
    m_metadataPermissions = 0;
}

void Backend::addGlobal(quint32 id, quint32 permissions, const char *type,
                        quint32 version, const spa_dict *properties)
{
    if (!properties)
        return;
    if (std::strcmp(type, PW_TYPE_INTERFACE_Metadata) == 0) {
        const char *name = spa_dict_lookup(properties, PW_KEY_METADATA_NAME);
        if (m_metadata || !name || std::strcmp(name, "default") != 0)
            return;
        m_metadata = static_cast<pw_metadata *>(pw_registry_bind(m_registry, id, type,
            std::min(version, quint32(PW_VERSION_METADATA)), 0));
        if (m_metadata) {
            m_metadataId = id;
            m_metadataPermissions = permissions;
            pw_metadata_add_listener(m_metadata, &m_metadataListener, &metadataEvents, this);
        }
        return;
    }
    if (std::strcmp(type, PW_TYPE_INTERFACE_Link) == 0) {
        const char *output = spa_dict_lookup(properties, PW_KEY_LINK_OUTPUT_NODE);
        const char *input = spa_dict_lookup(properties, PW_KEY_LINK_INPUT_NODE);
        bool outputOk = false;
        bool inputOk = false;
        const quint32 outputId = QString::fromUtf8(output).toUInt(&outputOk);
        const quint32 inputId = QString::fromUtf8(input).toUInt(&inputOk);
        if (outputOk && inputOk) {
            m_links.insert(id, {outputId, inputId});
            updateLinks();
        }
        return;
    }
    const char *mediaClass = spa_dict_lookup(properties, PW_KEY_MEDIA_CLASS);
    if (!mediaClass || m_bindings.contains(id))
        return;
    const char *nodeName = spa_dict_lookup(properties, PW_KEY_NODE_NAME);
    if (nodeName && QString::fromUtf8(nodeName).startsWith("cutefish.internal."))
        return;
    // WirePlumber's "Dummy Output", which stands in while no real sink exists.
    if (nodeName && std::strcmp(nodeName, "auto_null") == 0)
        return;
    AudioObject::Kind kind;
    quint32 bindVersion;
    if (std::strcmp(type, PW_TYPE_INTERFACE_Node) == 0) {
        if (std::strcmp(mediaClass, "Audio/Sink") == 0)
            kind = AudioObject::Sink;
        else if (std::strcmp(mediaClass, "Audio/Source") == 0 || std::strcmp(mediaClass, "Audio/Source/Virtual") == 0)
            kind = AudioObject::Source;
        else if (std::strcmp(mediaClass, "Stream/Output/Audio") == 0)
            kind = AudioObject::Playback;
        else if (std::strcmp(mediaClass, "Stream/Input/Audio") == 0)
            kind = AudioObject::Capture;
        else
            return;
        bindVersion = PW_VERSION_NODE;
    } else if (std::strcmp(type, PW_TYPE_INTERFACE_Device) == 0 && std::strcmp(mediaClass, "Audio/Device") == 0) {
        kind = AudioObject::Card;
        bindVersion = PW_VERSION_DEVICE;
    } else {
        return;
    }
    auto *proxy = static_cast<pw_proxy *>(pw_registry_bind(m_registry, id, type, std::min(version, bindVersion), 0));
    if (!proxy)
        return;
    auto *audioObject = new AudioObject(this, id, kind);
    audioObject->updateProperties(properties);
    auto *binding = new Binding {this, audioObject, proxy, {}, permissions, 0, {}};
    m_bindings.insert(id, binding);
    if (kind == AudioObject::Card) {
        pw_device_add_listener(reinterpret_cast<pw_device *>(proxy), &binding->listener, &deviceEvents, binding);
        connect(audioObject, &AudioObject::changed, this, [this, id] {
            for (AudioObject *node : objects()) {
                if (node->kind() != AudioObject::Card && node->cardIndex() == id)
                    Q_EMIT node->changed();
            }
        });
    } else {
        pw_node_add_listener(reinterpret_cast<pw_node *>(proxy), &binding->listener, &nodeEvents, binding);
        quint32 params[] = {SPA_PARAM_Props};
        pw_node_subscribe_params(reinterpret_cast<pw_node *>(proxy), params, 1);
    }
    Q_EMIT objectAdded(audioObject);
    updateDefaults();
    updateLinks();
}

void Backend::removeGlobal(quint32 id)
{
    if (id == m_metadataId && m_metadata) {
        releaseMetadata();
        m_defaultSink.clear();
        m_defaultSource.clear();
        updateDefaults();
    }
    if (Binding *binding = m_bindings.take(id)) {
        spa_hook_remove(&binding->listener);
        pw_proxy_destroy(binding->proxy);
        updateDefaults();
        Q_EMIT objectRemoved(binding->object);
        // QML bindings can still be evaluating the removed object during row removal.
        binding->object->m_backend = nullptr;
        binding->object->deleteLater();
        delete binding;
        for (AudioObject *node : objects()) {
            if (node->cardIndex() == id)
                Q_EMIT node->changed();
        }
    }
    m_links.remove(id);
    updateLinks();
}

void Backend::updateDefaults()
{
    Q_EMIT defaultsChanged();
    for (AudioObject *object : objects()) {
        if (object->kind() == AudioObject::Sink || object->kind() == AudioObject::Source)
            Q_EMIT object->changed();
    }
}

void Backend::updateLinks()
{
    for (AudioObject *stream : objects()) {
        if (stream->kind() != AudioObject::Playback && stream->kind() != AudioObject::Capture)
            continue;
        quint32 target = AudioObject::InvalidId;
        for (const auto &link : m_links) {
            const bool playback = stream->kind() == AudioObject::Playback;
            if ((playback ? link.first : link.second) != stream->index())
                continue;
            AudioObject *node = object(playback ? link.second : link.first);
            if (node && node->kind() == (playback ? AudioObject::Sink : AudioObject::Source)) {
                target = node->index();
                break;
            }
        }
        if (stream->m_deviceId != target) {
            stream->m_deviceId = target;
            Q_EMIT stream->changed();
        }
    }
}

void Backend::checkResult(int result, const char *operation)
{
    if (result < 0)
        Q_EMIT operationFailed(QStringLiteral("%1: %2").arg(QString::fromUtf8(operation), QString::fromLocal8Bit(strerror(-result))));
}

void Backend::updateRouteVolume(AudioObject *object)
{
    AudioObject *card = this->object(object->cardIndex());
    if (!card || card->kind() != AudioObject::Card)
        return;
    const QByteArray props = card->m_routeProperties.value(object->profileDevice());
    if (!props.isEmpty())
        object->updateVolume(reinterpret_cast<const spa_pod *>(props.constData()));
}

void Backend::setNodeParam(AudioObject *object, const spa_pod *param)
{
    Binding *binding = m_bindings.value(object->index());
    if (!binding || binding->object != object || !object->m_writable || object->kind() == AudioObject::Card)
        return;
    Binding *card = m_bindings.value(object->cardIndex());
    const int device = object->profileDevice();
    if (card && card->object->m_routeProperties.contains(device)
        && card->object->m_activeRoutes.contains(device)) {
        if (!(card->permissions & PW_PERM_W)) {
            checkResult(-EACCES, "Set device volume");
            return;
        }
        uint8_t buffer[2048];
        spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
        const auto *route = static_cast<spa_pod *>(spa_pod_builder_add_object(&builder,
            SPA_TYPE_OBJECT_ParamRoute, SPA_PARAM_Route,
            SPA_PARAM_ROUTE_index, SPA_POD_Int(card->object->m_activeRoutes.value(device)),
            SPA_PARAM_ROUTE_device, SPA_POD_Int(device),
            SPA_PARAM_ROUTE_props, SPA_POD_Pod(param),
            SPA_PARAM_ROUTE_save, SPA_POD_Bool(true)));
        checkResult(pw_device_set_param(reinterpret_cast<pw_device *>(card->proxy), SPA_PARAM_Route, 0, route), "Set device volume");
        return;
    }
    checkResult(pw_node_set_param(reinterpret_cast<pw_node *>(binding->proxy), SPA_PARAM_Props, 0, param), "Set audio properties");
}

void Backend::setVolume(AudioObject *object, const QVector<float> &volumes)
{
    if (volumes.isEmpty() || volumes.size() > SPA_AUDIO_MAX_CHANNELS)
        return;
    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_pod *param;
    if (object->m_channelVolume) {
        param = static_cast<spa_pod *>(spa_pod_builder_add_object(&builder, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
            SPA_PROP_channelVolumes, SPA_POD_Array(sizeof(float), SPA_TYPE_Float, volumes.size(), volumes.constData())));
    } else {
        param = static_cast<spa_pod *>(spa_pod_builder_add_object(&builder, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
            SPA_PROP_volume, SPA_POD_Float(volumes.first())));
    }
    setNodeParam(object, param);
}

void Backend::setMute(AudioObject *object, bool mute)
{
    uint8_t buffer[128];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const auto *param = static_cast<spa_pod *>(spa_pod_builder_add_object(&builder, SPA_TYPE_OBJECT_Props, SPA_PARAM_Props,
        SPA_PROP_mute, SPA_POD_Bool(mute)));
    setNodeParam(object, param);
}

void Backend::setDefault(AudioObject *object, bool enable)
{
    if (!m_metadata || !(m_metadataPermissions & PW_PERM_W)
        || this->object(object->index()) != object)
        return;
    const char *key = object->kind() == AudioObject::Sink
        ? "default.configured.audio.sink" : "default.configured.audio.source";
    const QByteArray value = QJsonDocument(QJsonObject {{"name", object->name()}}).toJson(QJsonDocument::Compact);
    if (enable || object->isDefault())
        checkResult(pw_metadata_set_property(m_metadata, PW_ID_CORE, key, "Spa:String:JSON", enable ? value.constData() : nullptr), "Set default device");
}

void Backend::setProfile(AudioObject *object, int profile)
{
    Binding *binding = m_bindings.value(object->index());
    if (!binding || object->kind() != AudioObject::Card || !(binding->permissions & PW_PERM_W))
        return;
    uint8_t buffer[128];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const auto *param = static_cast<spa_pod *>(spa_pod_builder_add_object(&builder, SPA_TYPE_OBJECT_ParamProfile, SPA_PARAM_Profile,
        SPA_PARAM_PROFILE_index, SPA_POD_Int(profile), SPA_PARAM_PROFILE_save, SPA_POD_Bool(true)));
    checkResult(pw_device_set_param(reinterpret_cast<pw_device *>(binding->proxy), SPA_PARAM_Profile, 0, param), "Set device profile");
}

void Backend::setRoute(AudioObject *object, int route)
{
    Binding *binding = m_bindings.value(object->cardIndex());
    if (!binding || binding->object->kind() != AudioObject::Card || object->profileDevice() < 0
        || !(binding->permissions & PW_PERM_W))
        return;
    uint8_t buffer[256];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const auto *param = static_cast<spa_pod *>(spa_pod_builder_add_object(&builder, SPA_TYPE_OBJECT_ParamRoute, SPA_PARAM_Route,
        SPA_PARAM_ROUTE_index, SPA_POD_Int(route), SPA_PARAM_ROUTE_device, SPA_POD_Int(object->profileDevice()),
        SPA_PARAM_ROUTE_save, SPA_POD_Bool(true)));
    checkResult(pw_device_set_param(reinterpret_cast<pw_device *>(binding->proxy), SPA_PARAM_Route, 0, param), "Set device route");
}

void Backend::moveStream(AudioObject *stream, quint32 target)
{
    AudioObject *node = object(target);
    if (!node || !m_metadata || !(m_metadataPermissions & PW_PERM_W)
        || object(stream->index()) != stream
        || (stream->kind() != AudioObject::Playback && stream->kind() != AudioObject::Capture)
        || node->kind() != (stream->kind() == AudioObject::Playback ? AudioObject::Sink : AudioObject::Source))
        return;
    const QByteArray serial = node->properties().value(PW_KEY_OBJECT_SERIAL).toString().toUtf8();
    if (!serial.isEmpty())
        checkResult(pw_metadata_set_property(m_metadata, stream->index(), "target.object", "Spa:Id", serial.constData()), "Move audio stream");
}

}
