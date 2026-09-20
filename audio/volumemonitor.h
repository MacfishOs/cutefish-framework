#pragma once

#include "audioobject.h"

#include <QPointer>

#include <pipewire/pipewire.h>

namespace CutefishAudio {

class VolumeMonitor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(CutefishAudio::AudioObject *target READ target WRITE setTarget NOTIFY targetChanged)
    Q_PROPERTY(double volume READ volume NOTIFY volumeChanged)
    Q_PROPERTY(bool available READ isAvailable NOTIFY availableChanged)
public:
    explicit VolumeMonitor(QObject *parent = nullptr);
    ~VolumeMonitor() override;
    AudioObject *target() const { return m_target; }
    void setTarget(AudioObject *target);
    double volume() const { return m_volume; }
    bool isAvailable() const { return m_available; }

Q_SIGNALS:
    void targetChanged();
    void volumeChanged();
    void availableChanged();

private:
    static const pw_stream_events streamEvents;
    void stop();
    void process();

    QPointer<AudioObject> m_target;
    pw_stream *m_stream = nullptr;
    spa_hook m_listener {};
    double m_volume = 0.0;
    bool m_available = false;
};

}
