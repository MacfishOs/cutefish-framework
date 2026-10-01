#include "secretstore.h"

#include <QTimer>
#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusVariant>
#include <QDebug>

#include <memory>

namespace {

const QString service = QStringLiteral("org.freedesktop.secrets");
const QString servicePath = QStringLiteral("/org/freedesktop/secrets");
const QString serviceInterface = QStringLiteral("org.freedesktop.Secret.Service");
const QString collectionInterface = QStringLiteral("org.freedesktop.Secret.Collection");
const QString itemInterface = QStringLiteral("org.freedesktop.Secret.Item");
const QString promptInterface = QStringLiteral("org.freedesktop.Secret.Prompt");
const QString sessionInterface = QStringLiteral("org.freedesktop.Secret.Session");
const QString propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString noPrompt = QStringLiteral("/");

using Attributes = QMap<QString, QString>;

struct Secret {
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};

QDBusArgument &operator<<(QDBusArgument &argument, const Secret &secret)
{
    argument.beginStructure();
    argument << secret.session << secret.parameters << secret.value << secret.contentType;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, Secret &secret)
{
    argument.beginStructure();
    argument >> secret.session >> secret.parameters >> secret.value >> secret.contentType;
    argument.endStructure();
    return argument;
}

} // namespace

Q_DECLARE_METATYPE(Secret)

// Completed arrives on the prompt object after the user answered it.
constexpr int kPromptTimeoutMs = 10 * 60 * 1000;

class PromptWatcher : public QObject
{
    Q_OBJECT

public:
    PromptWatcher(std::function<void(bool, const QVariant &)> done, QObject *parent)
        : QObject(parent)
        , m_done(std::move(done))
    {
    }

public Q_SLOTS:
    void completed(bool dismissed, const QDBusVariant &result)
    {
        finish(dismissed, result.variant());
    }

public:
    void finish(bool dismissed, const QVariant &result)
    {
        if (!m_done)
            return;
        auto done = std::move(m_done);
        m_done = nullptr;
        done(dismissed, result);
        deleteLater();
    }

private:
    std::function<void(bool, const QVariant &)> m_done;
};

SecretStore::SecretStore(QObject *parent)
    : QObject(parent)
{
    qDBusRegisterMetaType<Secret>();
    qDBusRegisterMetaType<Attributes>();
}

void SecretStore::load(const QString &uuid, const QString &settingName,
                       std::function<void(const QVariantMap &)> done)
{
    openSession([this, uuid, settingName, done](const QString &session) {
        if (session.isEmpty()) {
            done({});
            return;
        }

        search({{QStringLiteral("connection-uuid"), uuid},
                {QStringLiteral("setting-name"), settingName}},
               [this, session, done](const Paths &items) {
                   if (items.isEmpty()) {
                       closeSession(session);
                       done({});
                       return;
                   }

                   struct Pending {
                       QVariantMap secrets;
                       int calls = 0;
                   };
                   auto pending = std::make_shared<Pending>();
                   pending->calls = items.size();

                   auto settle = [this, session, done, pending] {
                       if (--pending->calls > 0)
                           return;
                       closeSession(session);
                       done(pending->secrets);
                   };

                   for (const QDBusObjectPath &item : items) {
                       call(item.path(), propertiesInterface, QStringLiteral("Get"),
                            {itemInterface, QStringLiteral("Attributes")},
                            [this, item, session, pending, settle](const QDBusMessage &reply) {
                                const QString key =
                                        qdbus_cast<Attributes>(
                                                reply.arguments().value(0).value<QDBusVariant>().variant())
                                                .value(QStringLiteral("setting-key"));
                                if (key.isEmpty()) {
                                    settle();
                                    return;
                                }

                                call(item.path(), itemInterface, QStringLiteral("GetSecret"),
                                     {QVariant::fromValue(QDBusObjectPath(session))},
                                     [key, pending, settle](const QDBusMessage &reply) {
                                         if (reply.type() == QDBusMessage::ReplyMessage) {
                                             const auto secret = qdbus_cast<Secret>(reply.arguments().value(0));
                                             pending->secrets.insert(key, QString::fromUtf8(secret.value));
                                         }
                                         settle();
                                     });
                            });
                   }
               });
    });
}

void SecretStore::save(const QString &uuid, const QString &connectionName,
                       const QString &settingName, const QString &key, const QString &value,
                       std::function<void()> done)
{
    const auto finish = [done](const QString &session, SecretStore *store) {
        if (!session.isEmpty())
            store->closeSession(session);
        if (done)
            done();
    };

    openSession([=, this](const QString &session) {
        if (session.isEmpty()) {
            finish(session, this);
            return;
        }

        call(servicePath, serviceInterface, QStringLiteral("ReadAlias"), {QStringLiteral("default")},
             [=, this](const QDBusMessage &reply) {
                 const auto collection = reply.arguments().value(0).value<QDBusObjectPath>();
                 if (reply.type() != QDBusMessage::ReplyMessage || collection.path() == noPrompt) {
                     qWarning() << "Secret Service has no default collection to store" << key << "in";
                     finish(session, this);
                     return;
                 }

                 unlock({collection}, [=, this](const Paths &unlocked) {
                     if (unlocked.isEmpty()) {
                         finish(session, this);
                         return;
                     }

                     const Attributes attributes {
                         {QStringLiteral("xdg:schema"),
                          QStringLiteral("org.freedesktop.NetworkManager.Connection")},
                         {QStringLiteral("connection-uuid"), uuid},
                         {QStringLiteral("setting-name"), settingName},
                         {QStringLiteral("setting-key"), key},
                     };
                     const QVariantMap properties {
                         {QStringLiteral("org.freedesktop.Secret.Item.Label"),
                          QStringLiteral("Network secret for %1/%2/%3")
                                  .arg(connectionName, settingName, key)},
                         {QStringLiteral("org.freedesktop.Secret.Item.Attributes"),
                          QVariant::fromValue(attributes)},
                     };
                     const Secret secret {QDBusObjectPath(session), {}, value.toUtf8(),
                                          QStringLiteral("text/plain")};

                     call(collection.path(), collectionInterface, QStringLiteral("CreateItem"),
                          {properties, QVariant::fromValue(secret), true},
                          [=, this](const QDBusMessage &reply) {
                              const auto promptPath =
                                      reply.arguments().value(1).value<QDBusObjectPath>();
                              if (reply.type() == QDBusMessage::ReplyMessage
                                  && promptPath.path() != noPrompt)
                                  prompt(promptPath, [=, this](bool, const QVariant &) {
                                      finish(session, this);
                                  });
                              else
                                  finish(session, this);
                          });
                 });
             });
    });
}

void SecretStore::replace(const QString &uuid, const QString &connectionName,
                          const QList<Entry> &entries, std::function<void()> done)
{
    remove(uuid, [=, this] {
        if (entries.isEmpty()) {
            if (done)
                done();
            return;
        }

        auto pending = std::make_shared<int>(entries.size());
        for (const Entry &entry : entries) {
            save(uuid, connectionName, entry.setting, entry.key, entry.value, [pending, done] {
                if (--*pending == 0 && done)
                    done();
            });
        }
    });
}

void SecretStore::remove(const QString &uuid, std::function<void()> done)
{
    search({{QStringLiteral("connection-uuid"), uuid}}, [this, done](const Paths &items) {
        if (items.isEmpty()) {
            if (done)
                done();
            return;
        }

        auto pending = std::make_shared<int>(items.size());
        const auto settle = [pending, done] {
            if (--*pending == 0 && done)
                done();
        };
        for (const QDBusObjectPath &item : items) {
            call(item.path(), itemInterface, QStringLiteral("Delete"), {},
                 [this, settle](const QDBusMessage &reply) {
                     const auto promptPath = reply.arguments().value(0).value<QDBusObjectPath>();
                     if (reply.type() == QDBusMessage::ReplyMessage
                         && promptPath.path() != noPrompt)
                         prompt(promptPath, [settle](bool, const QVariant &) { settle(); });
                     else
                         settle();
                 });
        }
    });
}

void SecretStore::call(const QString &path, const QString &interface, const QString &method,
                       const QVariantList &arguments, Reply done)
{
    // Without a session bus, Qt hands back a call that never finishes, which would leave
    // NetworkManager waiting on the agent for good.
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected()) {
        QTimer::singleShot(0, this, [done = std::move(done)] {
            done(QDBusMessage::createError(QDBusError::Disconnected, QStringLiteral("There is no session bus")));
        });
        return;
    }

    auto message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);

    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [done = std::move(done)](QDBusPendingCallWatcher *watcher) {
                const QDBusMessage reply = watcher->reply();
                if (reply.type() == QDBusMessage::ErrorMessage)
                    qWarning() << "Secret Service:" << reply.errorMessage();
                done(reply);
                watcher->deleteLater();
            });
}

void SecretStore::openSession(std::function<void(const QString &)> done)
{
    // "plain" sends the secret unencrypted, which is what libsecret falls back
    // to as well; the session bus never leaves the machine.
    call(servicePath, serviceInterface, QStringLiteral("OpenSession"),
         {QStringLiteral("plain"), QVariant::fromValue(QDBusVariant(QString()))},
         [done](const QDBusMessage &reply) {
             done(reply.type() == QDBusMessage::ReplyMessage
                          ? reply.arguments().value(1).value<QDBusObjectPath>().path()
                          : QString());
         });
}

void SecretStore::closeSession(const QString &session)
{
    call(session, sessionInterface, QStringLiteral("Close"), {}, [](const QDBusMessage &) {});
}

void SecretStore::search(const Attributes &attributes, std::function<void(const Paths &)> done)
{
    call(servicePath, serviceInterface, QStringLiteral("SearchItems"),
         {QVariant::fromValue(attributes)}, [this, done](const QDBusMessage &reply) {
             if (reply.type() != QDBusMessage::ReplyMessage) {
                 done({});
                 return;
             }

             const auto unlocked = qdbus_cast<Paths>(reply.arguments().value(0));
             const auto locked = qdbus_cast<Paths>(reply.arguments().value(1));
             unlock(locked, [unlocked, done](const Paths &paths) {
                 done(unlocked + paths);
             });
         });
}

void SecretStore::unlock(const Paths &objects, std::function<void(const Paths &)> done)
{
    if (objects.isEmpty()) {
        done({});
        return;
    }

    call(servicePath, serviceInterface, QStringLiteral("Unlock"),
         {QVariant::fromValue(objects)}, [this, done](const QDBusMessage &reply) {
             if (reply.type() != QDBusMessage::ReplyMessage) {
                 done({});
                 return;
             }

             const auto unlocked = qdbus_cast<Paths>(reply.arguments().value(0));
             const auto promptPath = reply.arguments().value(1).value<QDBusObjectPath>();
             if (promptPath.path() == noPrompt) {
                 done(unlocked);
                 return;
             }

             prompt(promptPath, [unlocked, done](bool dismissed, const QVariant &result) {
                 done(dismissed ? unlocked : unlocked + qdbus_cast<Paths>(result));
             });
         });
}

void SecretStore::prompt(const QDBusObjectPath &prompt,
                         std::function<void(bool, const QVariant &)> done)
{
    auto *watcher = new PromptWatcher(std::move(done), this);
    QDBusConnection::sessionBus().connect(service, prompt.path(), promptInterface,
                                          QStringLiteral("Completed"), watcher,
                                          SLOT(completed(bool, QDBusVariant)));
    // A prompt the service never completes - it crashed, or the dialog was
    // lost - counts as dismissed instead of keeping the callback chain.
    QTimer::singleShot(kPromptTimeoutMs, watcher, [watcher] { watcher->finish(true, {}); });

    call(prompt.path(), promptInterface, QStringLiteral("Prompt"), {QString()},
         [watcher](const QDBusMessage &reply) {
             if (reply.type() == QDBusMessage::ErrorMessage)
                 watcher->finish(true, {});
         });
}

#include "secretstore.moc"
