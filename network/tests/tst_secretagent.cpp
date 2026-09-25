#include "../secretagent.h"
#include "fakesecrets.h"

#include <QDBusPendingCallWatcher>
#include <QSignalSpy>
#include <QTest>

#include <memory>

namespace {

const QString agentPath = QStringLiteral("/org/freedesktop/NetworkManager/SecretAgent");
const QString agentInterface = QStringLiteral("org.freedesktop.NetworkManager.SecretAgent");
const QString connectionPath = QStringLiteral("/org/freedesktop/NetworkManager/Settings/7");
const QString uuid = QStringLiteral("5e3c1b64-3b43-4a2c-9d53-0d6c1a1f4f10");
const QString wirelessSecurity = QStringLiteral("802-11-wireless-security");

// SecretAgent::GetSecretsFlag
constexpr uint allowInteraction = 0x1;
constexpr uint requestNew = 0x2;

NMVariantMapMap wpaConnection(uint pskFlags = 0)
{
    NMVariantMapMap connection;
    connection[QStringLiteral("connection")] = {{QStringLiteral("id"), QStringLiteral("Cafe")},
                                                {QStringLiteral("uuid"), uuid}};
    connection[wirelessSecurity] = {{QStringLiteral("key-mgmt"), QStringLiteral("wpa-psk")},
                                    {QStringLiteral("psk-flags"), pskFlags}};
    return connection;
}

} // namespace

class SecretAgentTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        server = std::make_unique<QDBusConnection>(QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("fake-services")));
        QVERIFY(server->isConnected());
        secrets = std::make_unique<FakeSecretService>();
        networkManager = std::make_unique<FakeAgentManager>();
        QVERIFY(server->registerVirtualObject(QStringLiteral("/org/freedesktop/secrets"),
                                              secrets.get(), QDBusConnection::SubPath));
        QVERIFY(server->registerVirtualObject(
                QStringLiteral("/org/freedesktop/NetworkManager/AgentManager"),
                networkManager.get()));
        QVERIFY(server->registerService(QStringLiteral("org.freedesktop.secrets")));
        QVERIFY(server->registerService(QStringLiteral("org.freedesktop.NetworkManager")));

        agent = std::make_unique<NetworkSecretAgent>();
    }

    void cleanup()
    {
        agent.reset();
        server->unregisterService(QStringLiteral("org.freedesktop.secrets"));
        server->unregisterService(QStringLiteral("org.freedesktop.NetworkManager"));
        server->unregisterObject(QStringLiteral("/org/freedesktop/secrets"),
                                 QDBusConnection::UnregisterTree);
        server->unregisterObject(QStringLiteral("/org/freedesktop/NetworkManager/AgentManager"));
        networkManager.reset();
        secrets.reset();
        server.reset();
        QDBusConnection::disconnectFromBus(QStringLiteral("fake-services"));
    }

    void promptsWhenNothingIsStored()
    {
        QDBusPendingCallWatcher reply(getSecrets(wpaConnection(), wirelessSecurity, allowInteraction));
        QTRY_VERIFY(agent->pending());
        QCOMPARE(agent->connectionName(), QStringLiteral("Cafe"));
        QCOMPARE(agent->kind(), NetworkSecretAgent::WpaPassword);
        QVERIFY(!agent->retry());

        agent->respond(QStringLiteral("correct horse"));
        QTRY_VERIFY(reply.isFinished());
        QVERIFY(!reply.isError());
        QCOMPARE(secretsOf(reply).value(wirelessSecurity).value(QStringLiteral("psk")).toString(),
                 QStringLiteral("correct horse"));
        QVERIFY(!agent->pending());
    }

    void answersFromTheKeyring()
    {
        saveSecrets(QStringLiteral("stored password"));
        QTRY_COMPARE(secrets->items.size(), 1);
        const auto item = secrets->items.first();
        QCOMPARE(item.attributes.value(QStringLiteral("connection-uuid")), uuid);
        QCOMPARE(item.attributes.value(QStringLiteral("setting-name")), wirelessSecurity);
        QCOMPARE(item.attributes.value(QStringLiteral("setting-key")), QStringLiteral("psk"));
        QCOMPARE(item.attributes.value(QStringLiteral("xdg:schema")),
                 QStringLiteral("org.freedesktop.NetworkManager.Connection"));

        // No interaction allowed: what the keyring holds is the whole answer.
        QDBusPendingCallWatcher reply(getSecrets(wpaConnection(1), wirelessSecurity, 0));
        QTRY_VERIFY(reply.isFinished());
        QVERIFY(!reply.isError());
        QCOMPARE(secretsOf(reply).value(wirelessSecurity).value(QStringLiteral("psk")).toString(),
                 QStringLiteral("stored password"));
        QVERIFY(!agent->pending());
        QTRY_COMPARE(secrets->closedSessions, 2);
    }

    void savesOnlyAgentOwnedSecrets()
    {
        auto connection = wpaConnection(0);
        connection[wirelessSecurity][QStringLiteral("psk")] = QStringLiteral("system owned");
        call(QStringLiteral("SaveSecrets"),
             {QVariant::fromValue(connection), QVariant::fromValue(QDBusObjectPath(connectionPath))});
        QTest::qWait(100);
        QVERIFY(secrets->items.isEmpty());
    }

    void rejectedPasswordPromptsAgain()
    {
        saveSecrets(QStringLiteral("wrong password"));
        QTRY_COMPARE(secrets->items.size(), 1);

        QDBusPendingCallWatcher reply(
                getSecrets(wpaConnection(1), wirelessSecurity, allowInteraction | requestNew));
        QTRY_VERIFY(agent->pending());
        QVERIFY(agent->retry());

        agent->cancel();
        QTRY_VERIFY(reply.isFinished());
        QCOMPARE(reply.error().name(),
                 QStringLiteral("org.freedesktop.NetworkManager.SecretAgent.UserCanceled"));
    }

    void nothingToOfferWithoutInteraction()
    {
        QDBusPendingCallWatcher reply(getSecrets(wpaConnection(1), wirelessSecurity, 0));
        QTRY_VERIFY(reply.isFinished());
        QCOMPARE(reply.error().name(),
                 QStringLiteral("org.freedesktop.NetworkManager.SecretAgent.NoSecrets"));
        QVERIFY(!agent->pending());
    }

    void networkManagerCancels()
    {
        QDBusPendingCallWatcher reply(getSecrets(wpaConnection(), wirelessSecurity, allowInteraction));
        QTRY_VERIFY(agent->pending());

        call(QStringLiteral("CancelGetSecrets"),
             {QVariant::fromValue(QDBusObjectPath(connectionPath)), wirelessSecurity});
        QTRY_VERIFY(reply.isFinished());
        QCOMPARE(reply.error().name(),
                 QStringLiteral("org.freedesktop.NetworkManager.SecretAgent.AgentCanceled"));
        QVERIFY(!agent->pending());
    }

    void requestsQueue()
    {
        auto wep = wpaConnection();
        wep[QStringLiteral("connection")][QStringLiteral("id")] = QStringLiteral("Old router");
        wep[QStringLiteral("connection")][QStringLiteral("uuid")] = QStringLiteral("other");
        wep[wirelessSecurity] = {{QStringLiteral("key-mgmt"), QStringLiteral("none")},
                                 {QStringLiteral("wep-tx-keyidx"), 2u}};

        QDBusPendingCallWatcher first(getSecrets(wpaConnection(), wirelessSecurity, allowInteraction));
        QTRY_VERIFY(agent->pending());
        QDBusPendingCallWatcher second(getSecrets(wep, wirelessSecurity, allowInteraction));
        QTest::qWait(100);
        QCOMPARE(agent->connectionName(), QStringLiteral("Cafe"));

        QSignalSpy changed(agent.get(), &NetworkSecretAgent::requestChanged);
        agent->respond(QStringLiteral("first password"));
        QTRY_VERIFY(first.isFinished());
        QCOMPARE(changed.size(), 1);
        QVERIFY(agent->pending());
        QCOMPARE(agent->connectionName(), QStringLiteral("Old router"));
        QCOMPARE(agent->kind(), NetworkSecretAgent::WepKey);

        agent->respond(QStringLiteral("abcde"));
        QTRY_VERIFY(second.isFinished());
        QCOMPARE(secretsOf(second).value(wirelessSecurity).value(QStringLiteral("wep-key2")).toString(),
                 QStringLiteral("abcde"));
        QVERIFY(!agent->pending());
    }

    void enterpriseNeedsAnIdentity()
    {
        auto connection = wpaConnection();
        connection[QStringLiteral("802-1x")] = {
                {QStringLiteral("eap"), QStringList {QStringLiteral("peap")}}};

        QDBusPendingCallWatcher anonymous(
                getSecrets(connection, QStringLiteral("802-1x"), allowInteraction));
        QTRY_VERIFY(anonymous.isFinished());
        QCOMPARE(anonymous.error().name(),
                 QStringLiteral("org.freedesktop.NetworkManager.SecretAgent.NoSecrets"));

        connection[QStringLiteral("802-1x")][QStringLiteral("identity")] = QStringLiteral("alice");
        QDBusPendingCallWatcher named(getSecrets(connection, QStringLiteral("802-1x"), allowInteraction));
        QTRY_VERIFY(agent->pending());
        QCOMPARE(agent->kind(), NetworkSecretAgent::EapPassword);
        QCOMPARE(agent->identity(), QStringLiteral("alice"));
        agent->respond(QStringLiteral("hunter2"));
        QTRY_VERIFY(named.isFinished());
        QCOMPARE(secretsOf(named).value(QStringLiteral("802-1x")).value(QStringLiteral("password")).toString(),
                 QStringLiteral("hunter2"));
    }

    void asksBeforeANetworkIsAdded()
    {
        auto connect = [this] {
            auto message = QDBusMessage::createMethodCall(
                    QStringLiteral("com.cutefish.SecretAgent"), QStringLiteral("/SecretAgent"),
                    QStringLiteral("com.cutefish.SecretAgent"), QStringLiteral("ConnectWireless"));
            message << QStringLiteral("/org/freedesktop/NetworkManager/Devices/3")
                    << QStringLiteral("/org/freedesktop/NetworkManager/AccessPoint/9")
                    << QStringLiteral("Old router") << true;
            return server->asyncCall(message);
        };

        QDBusPendingCallWatcher first(connect());
        QTRY_VERIFY(first.isFinished());
        QVERIFY(!first.isError());
        QVERIFY(agent->pending());
        QCOMPARE(agent->connectionName(), QStringLiteral("Old router"));
        QCOMPARE(agent->kind(), NetworkSecretAgent::WepKey);
        QVERIFY(!agent->retry());

        // A second click on the same network does not queue a second prompt.
        QDBusPendingCallWatcher second(connect());
        QTRY_VERIFY(second.isFinished());
        QSignalSpy changed(agent.get(), &NetworkSecretAgent::requestChanged);
        agent->cancel();
        QCOMPARE(changed.size(), 1);
        QVERIFY(!agent->pending());
    }

    void anotherNetworkTakesThePromptOver()
    {
        auto connect = [this](const QString &accessPoint, const QString &name) {
            auto message = QDBusMessage::createMethodCall(
                    QStringLiteral("com.cutefish.SecretAgent"), QStringLiteral("/SecretAgent"),
                    QStringLiteral("com.cutefish.SecretAgent"), QStringLiteral("ConnectWireless"));
            message << QStringLiteral("/org/freedesktop/NetworkManager/Devices/3") << accessPoint
                    << name << false;
            return server->asyncCall(message);
        };

        // NetworkManager is still waiting on the password of the network being left.
        QDBusPendingCallWatcher leaving(getSecrets(wpaConnection(), wirelessSecurity,
                                                   allowInteraction | requestNew));
        QTRY_VERIFY(agent->pending());
        QCOMPARE(agent->connectionName(), QStringLiteral("Cafe"));

        QDBusPendingCallWatcher first(connect(QStringLiteral("/org/freedesktop/NetworkManager/AccessPoint/1"),
                                              QStringLiteral("Library")));
        QTRY_COMPARE(agent->connectionName(), QStringLiteral("Library"));
        QTRY_VERIFY(leaving.isFinished());
        QCOMPARE(leaving.error().name(),
                 QStringLiteral("org.freedesktop.NetworkManager.SecretAgent.UserCanceled"));

        QDBusPendingCallWatcher second(connect(QStringLiteral("/org/freedesktop/NetworkManager/AccessPoint/2"),
                                               QStringLiteral("Station")));
        QTRY_COMPARE(agent->connectionName(), QStringLiteral("Station"));

        // Only the network picked last is waiting.
        agent->cancel();
        QVERIFY(!agent->pending());
    }

    void deletesWithTheConnection()
    {
        saveSecrets(QStringLiteral("stored password"));
        QTRY_COMPARE(secrets->items.size(), 1);

        call(QStringLiteral("DeleteSecrets"),
             {QVariant::fromValue(wpaConnection(1)), QVariant::fromValue(QDBusObjectPath(connectionPath))});
        QTRY_VERIFY(secrets->items.isEmpty());
    }

private:
    QDBusPendingCall call(const QString &method, const QVariantList &arguments)
    {
        // The agent lives on the "system" bus, which the test points at the session bus.
        auto message = QDBusMessage::createMethodCall(QDBusConnection::systemBus().baseService(),
                                                      agentPath, agentInterface, method);
        message.setArguments(arguments);
        return server->asyncCall(message);
    }

    QDBusPendingCall getSecrets(const NMVariantMapMap &connection, const QString &setting, uint flags)
    {
        return call(QStringLiteral("GetSecrets"),
                    {QVariant::fromValue(connection), QVariant::fromValue(QDBusObjectPath(connectionPath)),
                     setting, QStringList(), flags});
    }

    void saveSecrets(const QString &psk)
    {
        auto connection = wpaConnection(1);
        connection[wirelessSecurity][QStringLiteral("psk")] = psk;
        QDBusPendingCallWatcher reply(call(QStringLiteral("SaveSecrets"),
                                           {QVariant::fromValue(connection),
                                            QVariant::fromValue(QDBusObjectPath(connectionPath))}));
        QTRY_VERIFY(reply.isFinished());
        QVERIFY(!reply.isError());
    }

    static NMVariantMapMap secretsOf(const QDBusPendingCallWatcher &reply)
    {
        return qdbus_cast<NMVariantMapMap>(reply.reply().arguments().value(0));
    }

    std::unique_ptr<QDBusConnection> server;
    std::unique_ptr<FakeSecretService> secrets;
    std::unique_ptr<FakeAgentManager> networkManager;
    std::unique_ptr<NetworkSecretAgent> agent;
};

int main(int argc, char **argv)
{
    qputenv("DBUS_SYSTEM_BUS_ADDRESS", qgetenv("DBUS_SESSION_BUS_ADDRESS"));
    QCoreApplication app(argc, argv);
    SecretAgentTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "tst_secretagent.moc"
