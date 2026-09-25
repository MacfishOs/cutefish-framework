#ifndef CUTEFISH_NETWORK_SECRETAGENT_H
#define CUTEFISH_NETWORK_SECRETAGENT_H

#include <QDBusMessage>
#include <QList>
#include <QSet>

#include <functional>
#include <QtQml/qqmlregistration.h>

#include <NetworkManagerQt/GenericTypes>

class Handler;
class SecretAgentService;
class SecretStore;

/**
 * The session's NetworkManager secret agent: NetworkManager asks it for a
 * Wi-Fi password, a WEP key or an 802.1X password whenever an activation
 * lacks one. Agent-owned secrets are kept in the user's keyring; anything the
 * keyring cannot answer comes from whoever shows `pending` to the user.
 *
 * Handler also comes here, over com.cutefish.SecretAgent on the session bus, for
 * the password of a network it is about to add, so that nothing is added or
 * activated before the user has answered.
 *
 * It registers with NetworkManager on construction, and NetworkManager accepts
 * one agent per identifier and user, so only the shell creates one.
 */
class NetworkSecretAgent : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "com.cutefish.SecretAgent")
    QML_NAMED_ELEMENT(SecretAgent)
    Q_PROPERTY(bool pending READ pending NOTIFY requestChanged)
    Q_PROPERTY(QString connectionName READ connectionName NOTIFY requestChanged)
    Q_PROPERTY(Kind kind READ kind NOTIFY requestChanged)
    Q_PROPERTY(QString identity READ identity NOTIFY requestChanged)
    Q_PROPERTY(bool retry READ retry NOTIFY requestChanged)

public:
    enum Kind {
        WpaPassword,
        WepKey,
        LeapPassword,
        EapPassword,
        PrivateKeyPassword,
    };
    Q_ENUM(Kind)

    explicit NetworkSecretAgent(QObject *parent = nullptr);
    ~NetworkSecretAgent() override;

    bool pending() const { return !m_requests.isEmpty(); }

    QString connectionName() const;
    Kind kind() const;
    QString identity() const;
    bool retry() const;

    Q_INVOKABLE void respond(const QString &secret);
    Q_INVOKABLE void cancel();

public Q_SLOTS:
    Q_SCRIPTABLE void ConnectWireless(const QString &devicePath, const QString &accessPointPath,
                                      const QString &name, bool wep);

Q_SIGNALS:
    void requestChanged();

private:
    friend class SecretAgentService;

    // Calls from NetworkManager; message is the GetSecrets call, answered later.
    void getSecrets(const NMVariantMapMap &connection, const QString &connectionPath,
                    const QString &settingName, uint flags, const QDBusMessage &message);
    void cancelGetSecrets(const QString &connectionPath, const QString &settingName);
    void saveSecrets(const NMVariantMapMap &connection);
    void deleteSecrets(const NMVariantMapMap &connection);

    struct Request {
        quint64 id = 0;
        QDBusMessage message;
        QString connectionPath;
        QString settingName;
        QString connectionName;
        QString identity;
        // The key the secret is stored under within settingName.
        QString key;
        Kind kind = WpaPassword;
        bool interactive = false;
        bool retry = false;
        // Asked for by Handler rather than NetworkManager: answering adds the
        // network through accessPointPath instead of replying to message.
        QString devicePath;
        QString accessPointPath;
    };

    void ask(const Request &request);
    void finish();
    // Drops unanswered password-first prompts; NetworkManager cancels its own.
    void dismiss(const std::function<bool(const Request &)> &matches);
    void watchDevice(const QString &devicePath);

    SecretAgentService *m_service;
    SecretStore *m_store;
    Handler *m_handler;
    // Waiting on the keyring, then waiting on the user.
    QList<Request> m_lookups;
    QList<Request> m_requests;
    quint64 m_nextId = 0;
    QSet<QString> m_watchedDevices;
};

#endif // CUTEFISH_NETWORK_SECRETAGENT_H
