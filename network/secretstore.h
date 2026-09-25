#ifndef CUTEFISH_NETWORK_SECRETSTORE_H
#define CUTEFISH_NETWORK_SECRETSTORE_H

#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QObject>
#include <QVariantMap>

#include <functional>

/**
 * Connection secrets kept in the user's keyring through the freedesktop Secret
 * Service (gnome-keyring here). Items carry nm-applet's attributes, so secrets
 * stored by either agent are found by the other.
 */
class SecretStore : public QObject
{
    Q_OBJECT

public:
    explicit SecretStore(QObject *parent = nullptr);

    /** Hands over key → value for one setting of a connection; empty when none are stored. */
    void load(const QString &uuid, const QString &settingName,
              std::function<void(const QVariantMap &)> done);
    void save(const QString &uuid, const QString &connectionName, const QString &settingName,
              const QString &key, const QString &value);
    void remove(const QString &uuid);

private:
    using Reply = std::function<void(const QDBusMessage &)>;
    using Paths = QList<QDBusObjectPath>;

    void call(const QString &path, const QString &interface, const QString &method,
              const QVariantList &arguments, Reply done);
    void openSession(std::function<void(const QString &)> done);
    void closeSession(const QString &session);
    void search(const QMap<QString, QString> &attributes, std::function<void(const Paths &)> done);
    void unlock(const Paths &objects, std::function<void(const Paths &)> done);
    void prompt(const QDBusObjectPath &prompt, std::function<void(bool, const QVariant &)> done);
};

#endif // CUTEFISH_NETWORK_SECRETSTORE_H
