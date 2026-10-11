// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "deviceuse.h"

#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantMap>

#include <pipewire/pipewire.h>

class QSocketNotifier;

namespace CutefishPrivacy {

/**
 * The streams that capture from a camera, a microphone or a sink's monitor,
 * read from the PipeWire graph. A device PipeWire opens is held by the daemon,
 * so only the graph tells which client the frames go to.
 */
class PipeWireCapture : public QObject
{
    Q_OBJECT

public:
    explicit PipeWireCapture(QObject *parent = nullptr);
    ~PipeWireCapture() override;

    bool isConnected() const { return m_connected; }
    DeviceUses uses() const { return m_uses; }

Q_SIGNALS:
    void usesChanged();
    void connectedChanged();

private:
    struct Proxy {
        PipeWireCapture *owner = nullptr;
        quint32 id = 0;
        bool client = false;
        pw_proxy *proxy = nullptr;
        spa_hook listener {};
        QVariantMap properties;
        bool running = false;
    };

    void connectToDaemon();
    void disconnectFromDaemon();
    void iterate();
    void fail();
    void addGlobal(quint32 id, const char *type, quint32 version, const spa_dict *properties);
    void removeGlobal(quint32 id);
    void scheduleUpdate();
    void update();
    DeviceUse useOf(const Proxy *stream, DeviceUse::Kind kind) const;

    static const pw_core_events coreEvents;
    static const pw_registry_events registryEvents;
    static const pw_node_events nodeEvents;
    static const pw_client_events clientEvents;

    pw_loop *m_loop = nullptr;
    pw_context *m_context = nullptr;
    pw_core *m_core = nullptr;
    pw_registry *m_registry = nullptr;
    spa_hook m_coreListener {};
    spa_hook m_registryListener {};
    QSocketNotifier *m_notifier = nullptr;
    QTimer m_retry;
    QHash<quint32, Proxy *> m_proxies;
    // Link id to its output and input node.
    QHash<quint32, QPair<quint32, quint32>> m_links;
    DeviceUses m_uses;
    bool m_connected = false;
    bool m_iterating = false;
    bool m_updatePending = false;
    bool m_failurePending = false;
};

}
