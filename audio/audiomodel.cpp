#include "audiomodel.h"
#include "backend.h"

#include <QMetaProperty>

#include <cctype>

namespace CutefishAudio {
namespace {
constexpr int ObjectRole = Qt::UserRole + 1;
constexpr int SortByDefaultRole = ObjectRole + 1;
}

AudioModel::AudioModel(AudioObject::Kind kind, Backend *backend, QObject *parent)
    : QAbstractListModel(parent)
    , m_backend(backend)
    , m_kind(kind)
{
    m_roles.insert(ObjectRole, "AudioObject");
    m_roles.insert(SortByDefaultRole, "SortByDefault");
    const QMetaObject &meta = AudioObject::staticMetaObject;
    for (int i = meta.propertyOffset(); i < meta.propertyCount(); ++i) {
        QByteArray name = meta.property(i).name();
        name[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(name[0])));
        const int role = SortByDefaultRole + 1 + i;
        m_roles.insert(role, name);
        m_properties.insert(role, i);
    }
    connect(m_backend, &Backend::objectAdded, this, &AudioModel::addObject);
    connect(m_backend, &Backend::objectRemoved, this, &AudioModel::removeObject);
    connect(m_backend, &Backend::defaultsChanged, this, &AudioModel::defaultNodeChanged);
    for (AudioObject *object : m_backend->objects())
        addObject(object);
}

int AudioModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_objects.size();
}

QVariant AudioModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.model() != this || index.column() != 0
        || index.row() < 0 || index.row() >= m_objects.size())
        return {};
    AudioObject *object = m_objects.at(index.row());
    if (role == Qt::DisplayRole)
        return object->description();
    if (role == ObjectRole)
        return QVariant::fromValue(object);
    if (role == SortByDefaultRole)
        return object->isDefault() ? 1 : 0;
    const int property = m_properties.value(role, -1);
    return property < 0 ? QVariant() : object->metaObject()->property(property).read(object);
}

bool AudioModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    if (!index.isValid() || index.model() != this || index.column() != 0
        || index.row() < 0 || index.row() >= m_objects.size())
        return false;
    const int propertyIndex = m_properties.value(role, -1);
    if (propertyIndex < 0)
        return false;
    const QMetaProperty property = AudioObject::staticMetaObject.property(propertyIndex);
    if (!property.isWritable())
        return false;
    return property.write(m_objects.at(index.row()), value);
}

Qt::ItemFlags AudioModel::flags(const QModelIndex &index) const
{
    return index.isValid() ? QAbstractListModel::flags(index) | Qt::ItemIsEditable : Qt::NoItemFlags;
}

QHash<int, QByteArray> AudioModel::roleNames() const
{
    return m_roles;
}

int AudioModel::role(const QByteArray &name) const
{
    return m_roles.key(name, -1);
}

AudioObject *AudioModel::defaultNode() const
{
    return m_backend->defaultNode(m_kind);
}

void AudioModel::addObject(AudioObject *object)
{
    if (object->kind() != m_kind)
        return;
    const int row = m_objects.size();
    beginInsertRows({}, row, row);
    m_objects.append(object);
    connect(object, &AudioObject::changed, this, [this, object] {
        const int row = m_objects.indexOf(object);
        if (row >= 0)
            Q_EMIT dataChanged(index(row, 0), index(row, 0));
    });
    endInsertRows();
    Q_EMIT countChanged();
}

void AudioModel::removeObject(AudioObject *object)
{
    const int row = m_objects.indexOf(object);
    if (row < 0)
        return;
    beginRemoveRows({}, row, row);
    m_objects.removeAt(row);
    disconnect(object, nullptr, this, nullptr);
    endRemoveRows();
    Q_EMIT countChanged();
}

SinkModel::SinkModel(QObject *parent)
    : AudioModel(AudioObject::Sink, Backend::instance(), parent)
{
}

SourceModel::SourceModel(QObject *parent)
    : AudioModel(AudioObject::Source, Backend::instance(), parent)
{
}

SinkInputModel::SinkInputModel(QObject *parent)
    : AudioModel(AudioObject::Playback, Backend::instance(), parent)
{
}

SourceOutputModel::SourceOutputModel(QObject *parent)
    : AudioModel(AudioObject::Capture, Backend::instance(), parent)
{
}

CardModel::CardModel(QObject *parent)
    : AudioModel(AudioObject::Card, Backend::instance(), parent)
{
}

}
