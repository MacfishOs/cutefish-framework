#pragma once

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QDBusVirtualObject>
#include <QMap>

using Attributes = QMap<QString, QString>;

// The freedesktop Secret Service as gnome-keyring answers it: one unlocked
// default collection and plain sessions.
class FakeSecretService : public QDBusVirtualObject
{
public:
    struct Item {
        Attributes attributes;
        QByteArray value;
    };

    FakeSecretService() { qDBusRegisterMetaType<Attributes>(); }

    QString introspect(const QString &) const override { return {}; }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        const auto args = message.arguments();
        const QString member = message.member();
        QDBusMessage reply = message.createReply();

        if (member == QStringLiteral("OpenSession")) {
            reply << QVariant::fromValue(QDBusVariant(QString()))
                  << QVariant::fromValue(QDBusObjectPath(sessionPath));
        } else if (member == QStringLiteral("Close")) {
            ++closedSessions;
        } else if (member == QStringLiteral("ReadAlias")) {
            reply << QVariant::fromValue(QDBusObjectPath(collectionPath));
        } else if (member == QStringLiteral("SearchItems")) {
            const auto wanted = qdbus_cast<Attributes>(args.value(0));
            QList<QDBusObjectPath> found;
            for (auto it = items.cbegin(); it != items.cend(); ++it) {
                if (matches(it->attributes, wanted))
                    found.append(QDBusObjectPath(it.key()));
            }
            reply << QVariant::fromValue(found) << QVariant::fromValue(QList<QDBusObjectPath>());
        } else if (member == QStringLiteral("Unlock")) {
            reply << args.value(0) << QVariant::fromValue(QDBusObjectPath(QStringLiteral("/")));
        } else if (member == QStringLiteral("CreateItem")) {
            const auto properties = qdbus_cast<QVariantMap>(args.value(0));
            Item item;
            item.attributes = qdbus_cast<Attributes>(
                    properties.value(QStringLiteral("org.freedesktop.Secret.Item.Attributes")));
            const auto secret = args.value(1).value<QDBusArgument>();
            QDBusObjectPath session;
            QByteArray parameters;
            QString contentType;
            secret.beginStructure();
            secret >> session >> parameters >> item.value >> contentType;
            secret.endStructure();

            QString path;
            for (auto it = items.cbegin(); it != items.cend(); ++it) {
                if (it->attributes == item.attributes)
                    path = it.key();
            }
            if (path.isEmpty())
                path = collectionPath + QStringLiteral("/%1").arg(++lastItem);
            items.insert(path, item);
            reply << QVariant::fromValue(QDBusObjectPath(path))
                  << QVariant::fromValue(QDBusObjectPath(QStringLiteral("/")));
        } else if (member == QStringLiteral("GetSecret")) {
            QDBusArgument secret;
            secret.beginStructure();
            secret << QDBusObjectPath(sessionPath) << QByteArray()
                   << items.value(message.path()).value << QStringLiteral("text/plain");
            secret.endStructure();
            reply << QVariant::fromValue(secret);
        } else if (member == QStringLiteral("Get")) {
            reply << QVariant::fromValue(
                    QDBusVariant(QVariant::fromValue(items.value(message.path()).attributes)));
        } else if (member == QStringLiteral("Delete")) {
            items.remove(message.path());
            reply << QVariant::fromValue(QDBusObjectPath(QStringLiteral("/")));
        } else {
            return false;
        }

        connection.send(reply);
        return true;
    }

    static bool matches(const Attributes &attributes, const Attributes &wanted)
    {
        for (auto it = wanted.cbegin(); it != wanted.cend(); ++it) {
            if (attributes.value(it.key()) != it.value())
                return false;
        }
        return true;
    }

    const QString sessionPath = QStringLiteral("/org/freedesktop/secrets/session/s1");
    const QString collectionPath = QStringLiteral("/org/freedesktop/secrets/collection/login");
    QMap<QString, Item> items;
    int lastItem = 0;
    int closedSessions = 0;
};

// Just enough of NetworkManager for NetworkManagerQt to register its agent.
class FakeAgentManager : public QDBusVirtualObject
{
public:
    QString introspect(const QString &) const override { return {}; }

    bool handleMessage(const QDBusMessage &message, const QDBusConnection &connection) override
    {
        if (message.interface() != QStringLiteral("org.freedesktop.NetworkManager.AgentManager"))
            return false;
        connection.send(message.createReply());
        return true;
    }
};
