// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "pipewirecapture.h"

#include <QSocketNotifier>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <optional>
#include <tuple>

namespace CutefishPrivacy {

static QVariantMap toMap(const spa_dict *dict)
{
    QVariantMap map;
    if (!dict)
        return map;
    const spa_dict_item *item;
    spa_dict_for_each(item, dict) {
        if (item->key && item->value)
            map.insert(QString::fromUtf8(item->key), QString::fromUtf8(item->value));
    }
    return map;
}

static bool isWatchedClass(const char *mediaClass)
{
    static const char *const classes[] = {"Audio/Source", "Audio/Source/Virtual", "Audio/Sink",
                                          "Video/Source", "Stream/Input/Audio", "Stream/Input/Video"};
    for (const char *watched : classes) {
        if (std::strcmp(mediaClass, watched) == 0)
            return true;
    }
    return false;
}

static bool isCamera(const QVariantMap &properties)
{
    const QString api = properties.value(PW_KEY_DEVICE_API).toString();
    return properties.value(PW_KEY_MEDIA_ROLE).toString() == QLatin1String("Camera")
        || api == QLatin1String("v4l2") || api == QLatin1String("libcamera");
}

const pw_core_events PipeWireCapture::coreEvents = [] {
    pw_core_events events {};
    events.version = PW_VERSION_CORE_EVENTS;
    events.info = [](void *data, const pw_core_info *) {
        auto *self = static_cast<PipeWireCapture *>(data);
        if (!self->m_connected) {
            self->m_connected = true;
            Q_EMIT self->connectedChanged();
        }
    };
    events.error = [](void *data, quint32 id, int, int result, const char *) {
        auto *self = static_cast<PipeWireCapture *>(data);
        if (id == PW_ID_CORE && (result == -EPIPE || result == -ECONNRESET) && !self->m_failurePending) {
            self->m_failurePending = true;
            QMetaObject::invokeMethod(self, &PipeWireCapture::fail, Qt::QueuedConnection);
        }
    };
    return events;
}();

const pw_registry_events PipeWireCapture::registryEvents = [] {
    pw_registry_events events {};
    events.version = PW_VERSION_REGISTRY_EVENTS;
    events.global = [](void *data, quint32 id, quint32, const char *type, quint32 version,
                       const spa_dict *properties) {
        static_cast<PipeWireCapture *>(data)->addGlobal(id, type, version, properties);
    };
    events.global_remove = [](void *data, quint32 id) {
        static_cast<PipeWireCapture *>(data)->removeGlobal(id);
    };
    return events;
}();

const pw_node_events PipeWireCapture::nodeEvents = [] {
    pw_node_events events {};
    events.version = PW_VERSION_NODE_EVENTS;
    events.info = [](void *data, const pw_node_info *info) {
        auto *proxy = static_cast<Proxy *>(data);
        if (info->change_mask & PW_NODE_CHANGE_MASK_PROPS)
            proxy->properties = toMap(info->props);
        if (info->change_mask & PW_NODE_CHANGE_MASK_STATE)
            proxy->running = info->state == PW_NODE_STATE_RUNNING;
        proxy->owner->scheduleUpdate();
    };
    return events;
}();

const pw_client_events PipeWireCapture::clientEvents = [] {
    pw_client_events events {};
    events.version = PW_VERSION_CLIENT_EVENTS;
    events.info = [](void *data, const pw_client_info *info) {
        auto *proxy = static_cast<Proxy *>(data);
        if (info->change_mask & PW_CLIENT_CHANGE_MASK_PROPS) {
            proxy->properties = toMap(info->props);
            proxy->owner->scheduleUpdate();
        }
    };
    return events;
}();

PipeWireCapture::PipeWireCapture(QObject *parent)
    : QObject(parent)
{
    pw_init(nullptr, nullptr);
    m_retry.setSingleShot(true);
    m_retry.setInterval(1000);
    connect(&m_retry, &QTimer::timeout, this, &PipeWireCapture::connectToDaemon);
    connectToDaemon();
}

PipeWireCapture::~PipeWireCapture()
{
    m_retry.stop();
    disconnectFromDaemon();
    if (m_context)
        pw_context_destroy(m_context);
    if (m_loop) {
        pw_loop_leave(m_loop);
        pw_loop_destroy(m_loop);
    }
    pw_deinit();
}

void PipeWireCapture::connectToDaemon()
{
    if (m_core)
        return;
    m_failurePending = false;
    if (!m_loop) {
        m_loop = pw_loop_new(nullptr);
        if (m_loop)
            pw_loop_enter(m_loop);
    }
    if (m_loop && !m_context)
        m_context = pw_context_new(m_loop, nullptr, 0);
    if (m_context) {
        m_core = pw_context_connect(m_context,
            pw_properties_new(PW_KEY_APP_NAME, "Cutefish Privacy",
                              PW_KEY_APP_ID, "org.cutefish.Privacy", nullptr), 0);
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
    connect(m_notifier, &QSocketNotifier::activated, this, &PipeWireCapture::iterate);
}

void PipeWireCapture::disconnectFromDaemon()
{
    delete m_notifier;
    m_notifier = nullptr;
    const auto ids = m_proxies.keys();
    for (quint32 id : ids)
        removeGlobal(id);
    m_links.clear();
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
    if (m_connected) {
        m_connected = false;
        Q_EMIT connectedChanged();
    }
    update();
}

void PipeWireCapture::iterate()
{
    if (!m_loop || m_iterating)
        return;
    m_iterating = true;
    const int result = pw_loop_iterate(m_loop, 0);
    m_iterating = false;
    if (result < 0 && result != -EINTR)
        fail();
}

void PipeWireCapture::fail()
{
    disconnectFromDaemon();
    m_retry.start();
}

void PipeWireCapture::addGlobal(quint32 id, const char *type, quint32 version, const spa_dict *properties)
{
    if (!properties)
        return;
    if (std::strcmp(type, PW_TYPE_INTERFACE_Link) == 0) {
        bool outputOk = false;
        bool inputOk = false;
        const quint32 output = QByteArray(spa_dict_lookup(properties, PW_KEY_LINK_OUTPUT_NODE)).toUInt(&outputOk);
        const quint32 input = QByteArray(spa_dict_lookup(properties, PW_KEY_LINK_INPUT_NODE)).toUInt(&inputOk);
        if (outputOk && inputOk) {
            m_links.insert(id, {output, input});
            scheduleUpdate();
        }
        return;
    }

    const bool client = std::strcmp(type, PW_TYPE_INTERFACE_Client) == 0;
    if (!client) {
        const char *mediaClass = spa_dict_lookup(properties, PW_KEY_MEDIA_CLASS);
        if (std::strcmp(type, PW_TYPE_INTERFACE_Node) != 0 || !mediaClass || !isWatchedClass(mediaClass))
            return;
    }
    if (m_proxies.contains(id))
        return;

    auto *bound = static_cast<pw_proxy *>(pw_registry_bind(m_registry, id, type,
        std::min(version, quint32(client ? PW_VERSION_CLIENT : PW_VERSION_NODE)), 0));
    if (!bound)
        return;
    auto *proxy = new Proxy {this, id, client, bound, {}, toMap(properties), false};
    m_proxies.insert(id, proxy);
    if (client)
        pw_client_add_listener(reinterpret_cast<pw_client *>(bound), &proxy->listener, &clientEvents, proxy);
    else
        pw_node_add_listener(reinterpret_cast<pw_node *>(bound), &proxy->listener, &nodeEvents, proxy);
}

void PipeWireCapture::removeGlobal(quint32 id)
{
    if (Proxy *proxy = m_proxies.take(id)) {
        spa_hook_remove(&proxy->listener);
        pw_proxy_destroy(proxy->proxy);
        delete proxy;
        scheduleUpdate();
    }
    if (m_links.remove(id))
        scheduleUpdate();
}

void PipeWireCapture::scheduleUpdate()
{
    // A stream starting brings its node, links and state in one burst.
    if (m_updatePending)
        return;
    m_updatePending = true;
    QMetaObject::invokeMethod(this, &PipeWireCapture::update, Qt::QueuedConnection);
}

void PipeWireCapture::update()
{
    m_updatePending = false;
    DeviceUses uses;
    for (const Proxy *stream : std::as_const(m_proxies)) {
        if (stream->client || !stream->running)
            continue;
        const QVariantMap &properties = stream->properties;
        if (!properties.value(PW_KEY_MEDIA_CLASS).toString().startsWith(QLatin1String("Stream/Input/")))
            continue;
        // Our own level meters, peak meters, and the loopback and filter
        // plumbing that a real capture stream already stands for.
        if (properties.value(PW_KEY_NODE_NAME).toString().startsWith(QLatin1String("cutefish.internal."))
            || properties.value(PW_KEY_STREAM_MONITOR).toString() == QLatin1String("true")
            || properties.contains(PW_KEY_NODE_LINK_GROUP))
            continue;
        for (const auto &link : std::as_const(m_links)) {
            if (link.second != stream->id)
                continue;
            const Proxy *source = m_proxies.value(link.first);
            if (!source || source->client)
                continue;
            const QString mediaClass = source->properties.value(PW_KEY_MEDIA_CLASS).toString();
            std::optional<DeviceUse::Kind> kind;
            if (mediaClass == QLatin1String("Video/Source") && isCamera(source->properties))
                kind = DeviceUse::Camera;
            else if (mediaClass.startsWith(QLatin1String("Audio/Source")))
                kind = DeviceUse::Microphone;
            else if (mediaClass == QLatin1String("Audio/Sink"))
                kind = DeviceUse::SystemAudio;
            if (!kind)
                continue;
            const DeviceUse use = useOf(stream, *kind);
            if (!uses.contains(use))
                uses.append(use);
        }
    }
    // Hash order would otherwise report a reordering as a change.
    std::sort(uses.begin(), uses.end(), [](const DeviceUse &a, const DeviceUse &b) {
        return std::tie(a.kind, a.pid, a.appId, a.name) < std::tie(b.kind, b.pid, b.appId, b.name);
    });
    if (uses != m_uses) {
        m_uses = uses;
        Q_EMIT usesChanged();
    }
}

DeviceUse PipeWireCapture::useOf(const Proxy *stream, DeviceUse::Kind kind) const
{
    DeviceUse use;
    use.kind = kind;
    const Proxy *client = m_proxies.value(stream->properties.value(PW_KEY_CLIENT_ID).toUInt());
    const QVariantMap clientProperties = client && client->client ? client->properties : QVariantMap();
    // A PulseAudio application's connection is pipewire-pulse's, so its own
    // process id is only the one it reports. Any other reported id may be a
    // sandbox's own, so the socket's peer is trusted instead.
    const bool pulse = clientProperties.value(PW_KEY_CLIENT_API).toString() == QLatin1String("pipewire-pulse");
    use.pid = clientProperties.value(pulse ? PW_KEY_APP_PROCESS_ID : PW_KEY_SEC_PID).toUInt();
    use.appId = clientProperties.value(QStringLiteral("pipewire.access.portal.app_id")).toString();
    use.name = stream->properties.value(PW_KEY_APP_NAME).toString();
    if (use.name.isEmpty())
        use.name = clientProperties.value(PW_KEY_APP_NAME).toString();
    return use;
}

}
