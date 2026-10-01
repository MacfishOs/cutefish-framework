#ifndef CUTEFISH_NETWORK_SECRETAGENT_H
#define CUTEFISH_NETWORK_SECRETAGENT_H

#include "networkdbustypes.h"

#include <QDBusMessage>
#include <QList>
#include <QPointer>
#include <functional>

#include <QtQml/qqmlregistration.h>

class Handler;
class NetworkClient;
class SecretAgentService;
class SecretStore;

// The session's NetworkManager secret agent: answers from the keyring, and asks the
// user through the window bound to it when NetworkManager allows interaction.
class CUTEFISH_NETWORK_EXPORT NetworkSecretAgent : public QObject
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

    struct Request {
        quint64 id = 0;
        QDBusMessage message;
        QString connectionPath;
        QString settingName;
        QString connectionName;
        QString uuid;
        QString identity;
        QString key;
        uint secretFlags = 0;
        Kind kind = WpaPassword;
        bool interactive = false;
        bool retry = false;
        bool noErrors = false;
        // A network asked about before anything is added, see ConnectWireless().
        QString devicePath;
        QString accessPointPath;
        QString deviceActive;
    };

    void getSecrets(const NMVariantMapMap &connection, const QString &connectionPath,
                    const QString &settingName, uint flags, const QDBusMessage &message);
    void cancelGetSecrets(const QString &connectionPath, const QString &settingName);
    void saveSecrets(const NMVariantMapMap &connection, const QDBusMessage &message);
    void deleteSecrets(const NMVariantMapMap &connection, const QDBusMessage &message);
    bool fromNetworkManager(const QDBusMessage &message) const;

    void registerAgent(int attempt = 0);
    void reply(const Request &request, const NMVariantMapMap &secrets);
    void fail(const Request &request, const QString &error, const QString &text);
    void ask(const Request &request);
    void finish();
    void dismiss(const std::function<bool(const Request &)> &matches);
    void dismissStalePrompts();

    // Owned by the application, which may tear it down first.
    QPointer<NetworkClient> m_client;
    SecretAgentService *m_service;
    SecretStore *m_store;
    Handler *m_handler;
    QList<Request> m_lookups;
    QList<Request> m_requests;
    quint64 m_nextId = 0;
    quint64 m_registration = 0;
    bool m_registered = false;
};

#endif // CUTEFISH_NETWORK_SECRETAGENT_H
