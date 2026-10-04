#pragma once

#include "audioobject.h"

#include <QMap>
#include <QTimer>

#include <pipewire/pipewire.h>
#include <pipewire/extensions/metadata.h>

class QSocketNotifier;

namespace CutefishAudio {

class Backend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool connected READ isConnected NOTIFY connectedChanged)

public:
    explicit Backend(QObject *parent = nullptr);
    ~Backend() override;
    static Backend *instance();

    bool isConnected() const { return m_connected; }
    QList<AudioObject *> objects() const;
    AudioObject *object(quint32 id) const;
    AudioObject *defaultNode(AudioObject::Kind kind) const;
    pw_core *core() const { return m_core; }

    void setVolume(AudioObject *object, const QVector<float> &volumes);
    void setMute(AudioObject *object, bool mute);
    void setDefault(AudioObject *object, bool enable);
    void setProfile(AudioObject *object, int profile);
    void setRoute(AudioObject *object, int route);
    void moveStream(AudioObject *object, quint32 target);

Q_SIGNALS:
    void objectAdded(CutefishAudio::AudioObject *object);
    void objectRemoved(CutefishAudio::AudioObject *object);
    void defaultsChanged();
    void connectedChanged();
    void disconnecting();
    void operationFailed(const QString &message);

private:
    struct Binding {
        Backend *backend = nullptr;
        AudioObject *object = nullptr;
        pw_proxy *proxy = nullptr;
        spa_hook listener {};
        quint32 permissions = 0;
        int enumSequence = 0;
        QMap<quint32, quint32> paramFlags;
    };

    void connectToDaemon();
    void disconnectFromDaemon();
    void releaseContext();
    void releaseMetadata();
    void fail();
    void iterate();
    void addGlobal(quint32 id, quint32 permissions, const char *type,
                   quint32 version, const spa_dict *properties);
    void removeGlobal(quint32 id);
    void updateDefaults();
    void updateLinks();
    void updateRouteVolume(AudioObject *object);
    void setNodeParam(AudioObject *object, const spa_pod *param);
    void checkResult(int result, const char *operation);

    static const pw_core_events coreEvents;
    static const pw_registry_events registryEvents;
    static const pw_node_events nodeEvents;
    static const pw_device_events deviceEvents;
    static const pw_metadata_events metadataEvents;

    pw_loop *m_loop = nullptr;
    pw_context *m_context = nullptr;
    pw_core *m_core = nullptr;
    pw_registry *m_registry = nullptr;
    pw_metadata *m_metadata = nullptr;
    spa_hook m_coreListener {};
    spa_hook m_registryListener {};
    spa_hook m_metadataListener {};
    quint32 m_metadataId = AudioObject::InvalidId;
    quint32 m_metadataPermissions = 0;
    QSocketNotifier *m_notifier = nullptr;
    QTimer m_retry;
    QMap<quint32, Binding *> m_bindings;
    QMap<quint32, QPair<quint32, quint32>> m_links;
    QString m_defaultSink;
    QString m_defaultSource;
    bool m_connected = false;
    bool m_iterating = false;
    bool m_failurePending = false;
};

}
