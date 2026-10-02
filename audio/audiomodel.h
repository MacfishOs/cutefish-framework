#pragma once

#include "audioobject.h"

#include <QAbstractListModel>

namespace CutefishAudio {

class Backend;

class AudioModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
    Q_PROPERTY(CutefishAudio::AudioObject *defaultSink READ defaultNode NOTIFY defaultNodeChanged)
    Q_PROPERTY(CutefishAudio::AudioObject *preferredSink READ defaultNode NOTIFY defaultNodeChanged)
    Q_PROPERTY(CutefishAudio::AudioObject *defaultSource READ defaultNode NOTIFY defaultNodeChanged)

public:
    AudioModel(AudioObject::Kind kind, Backend *backend, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QHash<int, QByteArray> roleNames() const override;
    Q_INVOKABLE int role(const QByteArray &name) const;
    AudioObject *defaultNode() const;

Q_SIGNALS:
    void countChanged();
    void defaultNodeChanged();

private:
    AudioObject *objectAt(const QModelIndex &index) const;
    void addObject(AudioObject *object);
    void removeObject(AudioObject *object);

    Backend *m_backend;
    AudioObject::Kind m_kind;
    QList<AudioObject *> m_objects;
    QHash<int, QByteArray> m_roles;
    QHash<int, int> m_properties;
};

class SinkModel : public AudioModel
{
    Q_OBJECT
public:
    explicit SinkModel(QObject *parent = nullptr);
};

class SourceModel : public AudioModel
{
    Q_OBJECT
public:
    explicit SourceModel(QObject *parent = nullptr);
};

}
