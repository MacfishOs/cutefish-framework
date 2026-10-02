#pragma once

#include <QObject>
#include <QVariant>
#include <QVector>

struct spa_dict;
struct spa_pod;

namespace CutefishAudio {

class Backend;

class AudioObject : public QObject
{
    Q_OBJECT
    Q_PROPERTY(quint32 index READ index CONSTANT)
    Q_PROPERTY(QString name READ name NOTIFY changed)
    Q_PROPERTY(QString description READ description NOTIFY changed)
    Q_PROPERTY(QString displayName READ displayName NOTIFY changed)
    Q_PROPERTY(QString iconName READ iconName NOTIFY changed)
    Q_PROPERTY(QVariantMap properties READ properties NOTIFY changed)
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY changed)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY changed)
    Q_PROPERTY(bool hasVolume READ hasVolume NOTIFY changed)
    Q_PROPERTY(bool volumeWritable READ volumeWritable NOTIFY changed)
    Q_PROPERTY(QVariantList channelVolumes READ channelVolumes WRITE setChannelVolumes NOTIFY changed)
    Q_PROPERTY(QStringList channels READ channels NOTIFY changed)
    Q_PROPERTY(bool default READ isDefault WRITE setDefault NOTIFY changed)
    Q_PROPERTY(int state READ state NOTIFY changed)
    Q_PROPERTY(bool virtualDevice READ isVirtualDevice NOTIFY changed)
    Q_PROPERTY(quint32 cardIndex READ cardIndex NOTIFY changed)
    Q_PROPERTY(QString bus READ bus NOTIFY changed)
    Q_PROPERTY(QVariantList ports READ ports NOTIFY changed)
    Q_PROPERTY(int activePortIndex READ activePortIndex WRITE setActivePortIndex NOTIFY changed)
    Q_PROPERTY(QVariantList profiles READ profiles NOTIFY changed)
    Q_PROPERTY(int activeProfileIndex READ activeProfileIndex WRITE setActiveProfileIndex NOTIFY changed)
    Q_PROPERTY(quint32 deviceIndex READ deviceIndex WRITE setDeviceIndex NOTIFY changed)
    Q_PROPERTY(bool corked READ isCorked NOTIFY changed)

public:
    enum Kind { Sink, Source, Playback, Capture, Card };
    Q_ENUM(Kind)
    enum State { UnknownState, RunningState, IdleState, SuspendedState };
    Q_ENUM(State)
    enum Availability { Unknown, Available, Unavailable };
    Q_ENUM(Availability)

    static constexpr quint32 InvalidId = 0xffffffff;
    static constexpr double NormalVolume = 1.0;
    static constexpr double MaximalVolume = 1.5;

    AudioObject(Backend *backend, quint32 id, Kind kind);
    quint32 index() const { return m_id; }
    Kind kind() const { return m_kind; }
    QString name() const;
    QString description() const;
    QString displayName() const;
    QString iconName() const;
    QVariantMap properties() const { return m_properties; }
    double volume() const;
    bool muted() const { return m_muted; }
    bool hasVolume() const { return !m_volumes.isEmpty(); }
    bool volumeWritable() const { return m_writable && hasVolume(); }
    QVariantList channelVolumes() const;
    QStringList channels() const;
    bool isDefault() const;
    int state() const { return m_state; }
    bool isVirtualDevice() const;
    quint32 cardIndex() const;
    QString bus() const;
    QVariantList ports() const;
    int activePortIndex() const;
    QVariantList profiles() const { return m_profiles; }
    int activeProfileIndex() const;
    quint32 deviceIndex() const { return m_deviceId; }
    bool isCorked() const { return m_state != RunningState; }

    void setVolume(double volume);
    void setMuted(bool muted);
    void setChannelVolumes(const QVariantList &volumes);
    void setDefault(bool enable);
    void setActivePortIndex(int index);
    void setActiveProfileIndex(int index);
    void setDeviceIndex(quint32 id);
    Q_INVOKABLE void setChannelVolume(int channel, double volume);
    Q_INVOKABLE void switchStreams();

    static double fromLinear(float volume);
    static float toLinear(double volume);

Q_SIGNALS:
    void changed();

private:
    friend class Backend;
    friend class AudioTest;
    void updateProperties(const spa_dict *properties);
    void updateVolume(const spa_pod *param);
    void updateProfile(const spa_pod *param, bool current);
    // Returns the device of a current route, -1 otherwise.
    int updateRoute(const spa_pod *param, bool current);
    int profileDevice() const;
    const AudioObject *cardObject() const;

    Backend *m_backend;
    quint32 m_id;
    Kind m_kind;
    QVariantMap m_properties;
    QVector<float> m_volumes;
    QVector<quint32> m_channels;
    bool m_channelVolume = false;
    bool m_muted = false;
    bool m_writable = false;
    int m_state = UnknownState;
    quint32 m_deviceId = InvalidId;
    QVariantList m_profiles;
    QVariantList m_routes;
    QMap<int, int> m_activeRoutes;
    QMap<int, QByteArray> m_routeProperties;
    int m_profile = -1;
};

}
