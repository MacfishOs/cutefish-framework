#pragma once

#include "audioobject.h"

#include <QPointer>
#include <QTimer>

#include <pipewire/pipewire.h>

namespace CutefishAudio {

class Backend;

class SoundPlayer : public QObject
{
    Q_OBJECT
public:
    explicit SoundPlayer(Backend *backend, QObject *parent = nullptr);
    ~SoundPlayer() override;
    void play(AudioObject *sink, const QString &channel = {});
    void stop();

Q_SIGNALS:
    void finished();
    void failed(const QString &message);

private:
    static const pw_stream_events streamEvents;
    void process();

    Backend *m_backend;
    pw_stream *m_stream = nullptr;
    spa_hook m_listener {};
    QTimer m_timeout;
    quint32 m_frame = 0;
    quint32 m_frames = 0;
    quint32 m_channels = 0;
    int m_channel = -1;
    bool m_draining = false;
    quint64 m_generation = 0;
};

class VolumeFeedback : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool valid READ isValid NOTIFY validChanged)
public:
    explicit VolumeFeedback(QObject *parent = nullptr);
    bool isValid() const;
    Q_INVOKABLE void play(quint32 sinkIndex);

Q_SIGNALS:
    void validChanged();

private:
    SoundPlayer m_player;
};

class SpeakerTest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(CutefishAudio::AudioObject *sink READ sink WRITE setSink NOTIFY sinkChanged)
public:
    explicit SpeakerTest(QObject *parent = nullptr);
    AudioObject *sink() const { return m_sink; }
    void setSink(AudioObject *sink);
    Q_INVOKABLE void testChannel(const QString &channel);

Q_SIGNALS:
    void sinkChanged();

private:
    QPointer<AudioObject> m_sink;
    SoundPlayer m_player;
};

}
