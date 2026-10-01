#include "networkdbustypes.h"

#include <QDBusArgument>
#include <QDBusMetaType>
#include <QDBusVariant>

namespace {

QDBusArgument &operator<<(QDBusArgument &argument, const NMObjects &objects)
{
    argument.beginMap(QMetaType::fromType<QDBusObjectPath>(), QMetaType::fromType<NMVariantMapMap>());
    for (auto it = objects.cbegin(); it != objects.cend(); ++it) {
        argument.beginMapEntry();
        argument << it.key() << it.value();
        argument.endMapEntry();
    }
    argument.endMap();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, NMObjects &objects)
{
    argument.beginMap();
    while (!argument.atEnd()) {
        QDBusObjectPath path;
        NMVariantMapMap interfaces;
        argument.beginMapEntry();
        argument >> path >> interfaces;
        argument.endMapEntry();
        objects.insert(path, interfaces);
    }
    argument.endMap();
    return argument;
}

QVariant read(const QDBusArgument &argument)
{
    switch (argument.currentType()) {
    case QDBusArgument::BasicType:
        return NetworkDBus::plain(argument.asVariant());
    case QDBusArgument::VariantType: {
        QDBusVariant variant;
        argument >> variant;
        return NetworkDBus::plain(variant.variant());
    }
    case QDBusArgument::ArrayType: {
        const QString signature = argument.currentSignature();
        if (signature == QLatin1String("ay")) {
            QByteArray bytes;
            argument >> bytes;
            return bytes;
        }
        if (signature == QLatin1String("as") || signature == QLatin1String("ao")) {
            QStringList strings;
            argument.beginArray();
            while (!argument.atEnd())
                strings << NetworkDBus::plain(argument.asVariant()).toString();
            argument.endArray();
            return strings;
        }
        QVariantList list;
        argument.beginArray();
        while (!argument.atEnd())
            list << read(argument);
        argument.endArray();
        return list;
    }
    case QDBusArgument::MapType: {
        QVariantMap map;
        argument.beginMap();
        while (!argument.atEnd()) {
            argument.beginMapEntry();
            const QString key = NetworkDBus::plain(argument.asVariant()).toString();
            map.insert(key, read(argument));
            argument.endMapEntry();
        }
        argument.endMap();
        return map;
    }
    case QDBusArgument::StructureType: {
        QVariantList fields;
        argument.beginStructure();
        while (!argument.atEnd())
            fields << read(argument);
        argument.endStructure();
        return fields;
    }
    default:
        return {};
    }
}

} // namespace

QVariant NetworkDBus::plain(const QVariant &value)
{
    const QMetaType type = value.metaType();
    if (type == QMetaType::fromType<QDBusArgument>())
        return read(value.value<QDBusArgument>());
    if (type == QMetaType::fromType<QDBusObjectPath>())
        return value.value<QDBusObjectPath>().path();
    if (type == QMetaType::fromType<QDBusVariant>())
        return plain(value.value<QDBusVariant>().variant());
    if (type == QMetaType::fromType<QVariantMap>()) {
        QVariantMap map = value.toMap();
        for (QVariant &entry : map)
            entry = plain(entry);
        return map;
    }
    if (type == QMetaType::fromType<QVariantList>()) {
        QVariantList list = value.toList();
        for (QVariant &entry : list)
            entry = plain(entry);
        return list;
    }
    return value;
}

void registerNetworkDBusTypes()
{
    qDBusRegisterMetaType<NMVariantMapMap>();
    qDBusRegisterMetaType<NMObjects>();
    qDBusRegisterMetaType<QList<QDBusObjectPath>>();
    qDBusRegisterMetaType<QMap<QString, QString>>();
    qDBusRegisterMetaType<QList<QByteArray>>();
}
