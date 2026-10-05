#include "../networkclient.h"
#include "../secretagent.h"
#include "fakenetworkmanager.h"
#include "fakesecrets.h"

#include <QDBusPendingCallWatcher>
#include <QSignalSpy>
#include <QTest>

#include <memory>

static const QString agentPath = QStringLiteral("/org/freedesktop/NetworkManager/SecretAgent");
static const QString agentInterface = QStringLiteral("org.freedesktop.NetworkManager.SecretAgent");
static const QString connectionPath = QStringLiteral("/org/freedesktop/NetworkManager/Settings/7");
static const QString uuid = QStringLiteral("5e3c1b64-3b43-4a2c-9d53-0d6c1a1f4f10");
static const QString wirelessSecurity = QStringLiteral("802-11-wireless-security");

// NMSecretAgentGetSecretsFlags
static constexpr uint allowInteraction = 0x1;
static constexpr uint requestNew = 0x2;
static constexpr uint userRequested = 0x4;
static constexpr uint noErrors = 0x40000000;

static NMVariantMapMap wpaConnection(uint pskFlags = 0)
{
    NMVariantMapMap connection;
    connection[QStringLiteral("connection")] = {{QStringLiteral("id"), QStringLiteral("Cafe")},
                                                {QStringLiteral("uuid"), uuid}};
    connection[wirelessSecurity] = {{QStringLiteral("key-mgmt"), QStringLiteral("wpa-psk")},
                                    {QStringLiteral("psk-flags"), pskFlags}};
    return connection;
}

static NMVariantMapMap vpnConnection(const QString &service, const QMap<QString, QString> &data)
{
    NMVariantMapMap connection;
    connection[QStringLiteral("connection")] = {{QStringLiteral("id"), QStringLiteral("Office")},
                                                {QStringLiteral("uuid"), uuid}};
    connection[QStringLiteral("vpn")] = {{QStringLiteral("service-type"), service},
                                         {QStringLiteral("data"), QVariant::fromValue(data)}};
    return connection;
}

static const QString openvpn = QStringLiteral("org.freedesktop.NetworkManager.openvpn");
static const QMap<QString, QString> passwordData {{QStringLiteral("connection-type"), QStringLiteral("password")},
                                                  {QStringLiteral("password-flags"), QStringLiteral("1")}};

class SecretAgentTest : public QObject
{
    Q_OBJECT

private slots:
    void init()
    {
        // Separate connections: NetworkManager's objects fill /org/freedesktop.
        keyring = std::make_unique<QDBusConnection>(QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("fake-secrets")));
        server = std::make_unique<QDBusConnection>(QDBusConnection::connectToBus(
                QDBusConnection::SessionBus, QStringLiteral("fake-services")));
        QVERIFY(keyring->isConnected());
        QVERIFY(server->isConnected());
        secrets = std::make_unique<FakeSecretService>();
        networkManager = std::make_unique<FakeNetworkManager>(*server);
        QVERIFY(keyring->registerVirtualObject(QStringLiteral("/org/freedesktop/secrets"),
                                               secrets.get(), QDBusConnection::SubPath));
        QVERIFY(server->registerVirtualObject(QStringLiteral("/org/freedesktop"), networkManager.get(),
                                              QDBusConnection::SubPath));
        QVERIFY(keyring->registerService(QStringLiteral("org.freedesktop.secrets")));
        QVERIFY(server->registerService(QStringLiteral("org.freedesktop.NetworkManager")));
        QTRY_VERIFY(NetworkClient::instance()->ready());

        agent = std::make_unique<NetworkSecretAgent>();
        QTRY_COMPARE(networkManager->count(QStringLiteral("RegisterWithCapabilities")), 1);
    }

    void cleanup()
    {
        agent.reset();
        keyring->unregisterService(QStringLiteral("org.freedesktop.secrets"));
        server->unregisterService(QStringLiteral("org.freedesktop.NetworkManager"));
        keyring->unregisterObject(QStringLiteral("/org/freedesktop/secrets"), QDBusConnection::UnregisterTree);
        server->unregisterObject(QStringLiteral("/org/freedesktop"), QDBusConnection::UnregisterTree);
        QTRY_VERIFY(!NetworkClient::instance()->ready());
        networkManager.reset();
        secrets.reset();
        keyring.reset();
        server.reset();
        QDBusConnection::disconnectFromBus(QStringLiteral("fake-secrets"));
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
        QDBusPendingCallWatcher second(getSecrets(wep, wirelessSecurity, allowInteraction,
                                                  QStringLiteral("/org/freedesktop/NetworkManager/Settings/8")));
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
        addWirelessDevice(QStringLiteral("/org/freedesktop/NetworkManager/Devices/3"),
                          {QStringLiteral("/org/freedesktop/NetworkManager/AccessPoint/9")});

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
        addWirelessDevice(QStringLiteral("/org/freedesktop/NetworkManager/Devices/3"),
                          {QStringLiteral("/org/freedesktop/NetworkManager/AccessPoint/1"),
                           QStringLiteral("/org/freedesktop/NetworkManager/AccessPoint/2")});

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

    void savesWhatTheUserTyped()
    {
        QDBusPendingCallWatcher reply(
                getSecrets(wpaConnection(1), wirelessSecurity, allowInteraction | requestNew));
        QTRY_VERIFY(agent->pending());
        agent->respond(QStringLiteral("new password"));
        QTRY_VERIFY(reply.isFinished());
        QVERIFY(!reply.isError());

        // NetworkManager never hands it back for saving: the next login needs it from the keyring.
        QTRY_COMPARE(secrets->items.size(), 1);
        QCOMPARE(secrets->items.first().value, QByteArray("new password"));
        QCOMPARE(secrets->items.first().attributes.value(QStringLiteral("setting-key")), QStringLiteral("psk"));
    }

    void keepsSystemOwnedSecretsOutOfTheKeyring()
    {
        QDBusPendingCallWatcher reply(getSecrets(wpaConnection(0), wirelessSecurity, allowInteraction));
        QTRY_VERIFY(agent->pending());
        agent->respond(QStringLiteral("shared password"));
        QTRY_VERIFY(reply.isFinished());
        QTest::qWait(100);
        QVERIFY(secrets->items.isEmpty());
    }

    void newerRequestReplacesTheOlder()
    {
        QDBusPendingCallWatcher first(getSecrets(wpaConnection(), wirelessSecurity, allowInteraction));
        QTRY_VERIFY(agent->pending());
        QDBusPendingCallWatcher second(
                getSecrets(wpaConnection(), wirelessSecurity, allowInteraction | requestNew));
        QTRY_VERIFY(first.isFinished());
        QCOMPARE(first.error().name(),
                 QStringLiteral("org.freedesktop.NetworkManager.SecretAgent.AgentCanceled"));
        QTRY_VERIFY(agent->retry());

        agent->respond(QStringLiteral("only once"));
        QTRY_VERIFY(second.isFinished());
        QVERIFY(!second.isError());
        QVERIFY(!agent->pending());
    }

    void savingReplacesWhatWasStored()
    {
        saveSecrets(QStringLiteral("old password"));
        QTRY_COMPARE(secrets->items.size(), 1);

        auto wep = wpaConnection();
        wep[wirelessSecurity] = {{QStringLiteral("key-mgmt"), QStringLiteral("none")},
                                 {QStringLiteral("wep-key0"), QStringLiteral("abcde")},
                                 {QStringLiteral("wep-key-flags"), 1u}};
        QDBusPendingCallWatcher reply(call(QStringLiteral("SaveSecrets"),
                                           {QVariant::fromValue(wep), QVariant::fromValue(QDBusObjectPath(connectionPath))}));
        QTRY_VERIFY(reply.isFinished());
        QVERIFY(!reply.isError());
        QCOMPARE(secrets->items.size(), 1);
        QCOMPARE(secrets->items.first().attributes.value(QStringLiteral("setting-key")), QStringLiteral("wep-key0"));
    }

    void readingWithoutErrorsGetsAnEmptyAnswer()
    {
        QDBusPendingCallWatcher reply(getSecrets(wpaConnection(1), wirelessSecurity, userRequested | noErrors));
        QTRY_VERIFY(reply.isFinished());
        QVERIFY(!reply.isError());
        QVERIFY(secretsOf(reply).value(wirelessSecurity).isEmpty());
    }

    void registersWithEveryNetworkManager()
    {
        server->unregisterService(QStringLiteral("org.freedesktop.NetworkManager"));
        QTRY_VERIFY(!NetworkClient::instance()->ready());
        QVERIFY(server->registerService(QStringLiteral("org.freedesktop.NetworkManager")));
        QTRY_COMPARE(networkManager->count(QStringLiteral("RegisterWithCapabilities")), 2);
        const QDBusMessage registration = networkManager->last(QStringLiteral("RegisterWithCapabilities"));
        QCOMPARE(registration.arguments().value(0).toString(), QStringLiteral("com.cutefish.network"));
    }

    void onlyNetworkManagerMayAsk()
    {
        auto message = QDBusMessage::createMethodCall(QDBusConnection::systemBus().baseService(),
                                                      agentPath, agentInterface, QStringLiteral("GetSecrets"));
        message.setArguments({QVariant::fromValue(wpaConnection()), QVariant::fromValue(QDBusObjectPath(connectionPath)),
                              wirelessSecurity, QStringList(), allowInteraction});
        auto stranger = QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("stranger"));
        QDBusPendingCallWatcher reply(stranger.asyncCall(message));
        QTRY_VERIFY(reply.isFinished());
        QCOMPARE(reply.error().name(),
                 QStringLiteral("org.freedesktop.NetworkManager.SecretAgent.PermissionDenied"));
        QVERIFY(!agent->pending());
        QDBusConnection::disconnectFromBus(QStringLiteral("stranger"));
    }

    void vpnPasswordIsAskedForAndKept()
    {
        QDBusPendingCallWatcher reply(getSecrets(vpnConnection(openvpn, passwordData), QStringLiteral("vpn"),
                                                 allowInteraction));
        QTRY_VERIFY(agent->pending());
        QCOMPARE(agent->kind(), NetworkSecretAgent::VpnSecrets);
        QCOMPARE(agent->connectionName(), QStringLiteral("Office"));
        QCOMPARE(agent->fields().size(), 1);
        QCOMPARE(agent->fields().first().toMap().value(QStringLiteral("key")).toString(), QStringLiteral("password"));

        agent->respondFields({{QStringLiteral("password"), QStringLiteral("s3cret")}});
        QTRY_VERIFY(reply.isFinished());
        QVERIFY(!reply.isError());
        QCOMPARE(vpnSecretsOf(reply).value(QStringLiteral("password")), QStringLiteral("s3cret"));

        QTRY_COMPARE(secrets->items.size(), 1);
        QCOMPARE(secrets->items.first().attributes.value(QStringLiteral("setting-name")), QStringLiteral("vpn"));
        QCOMPARE(secrets->items.first().attributes.value(QStringLiteral("setting-key")), QStringLiteral("password"));
    }

    void vpnAnswersFromTheKeyring()
    {
        auto connection = vpnConnection(openvpn, passwordData);
        connection[QStringLiteral("vpn")][QStringLiteral("secrets")] =
                QVariant::fromValue(QMap<QString, QString> {{QStringLiteral("password"), QStringLiteral("stored")}});
        QDBusPendingCallWatcher saved(call(QStringLiteral("SaveSecrets"),
                                           {QVariant::fromValue(connection), QVariant::fromValue(QDBusObjectPath(connectionPath))}));
        QTRY_VERIFY(saved.isFinished());
        QTRY_COMPARE(secrets->items.size(), 1);

        QDBusPendingCallWatcher reply(getSecrets(vpnConnection(openvpn, passwordData), QStringLiteral("vpn"), allowInteraction));
        QTRY_VERIFY(reply.isFinished());
        QVERIFY(!reply.isError());
        QCOMPARE(vpnSecretsOf(reply).value(QStringLiteral("password")), QStringLiteral("stored"));
        QVERIFY(!agent->pending());
    }

    void vpnWithoutInteractionStillAnswers()
    {
        // An answer without secrets, not an error, has NetworkManager ask again with interaction.
        QDBusPendingCallWatcher reply(getSecrets(vpnConnection(openvpn, passwordData), QStringLiteral("vpn"), 0));
        QTRY_VERIFY(reply.isFinished());
        QVERIFY(!reply.isError());
        QVERIFY(vpnSecretsOf(reply).isEmpty());
    }

private:
    static QMap<QString, QString> vpnSecretsOf(const QDBusPendingCallWatcher &reply)
    {
        return qdbus_cast<QMap<QString, QString>>(secretsOf(reply).value(QStringLiteral("vpn")).value(QStringLiteral("secrets")));
    }

    void addWirelessDevice(const QString &device, const QStringList &accessPoints)
    {
        for (const QString &accessPoint : accessPoints) {
            networkManager->add(accessPoint, {{FakeNM::accessPoint, {{QStringLiteral("Ssid"), QByteArray("Station")}}}});
        }
        networkManager->add(device, {{FakeNM::device, {{QStringLiteral("DeviceType"), 2u},
                                                       {QStringLiteral("ActiveConnection"), FakeNM::path({})}}},
                                     {FakeNM::wireless, {{QStringLiteral("AccessPoints"), FakeNM::paths(accessPoints)}}}});
        QTRY_VERIFY(NetworkClient::instance()->has(device, NetworkDBus::deviceInterface));
    }

    QDBusPendingCall call(const QString &method, const QVariantList &arguments)
    {
        // The agent lives on the "system" bus, which the test points at the session bus, and
        // answers only the connection that owns NetworkManager's name.
        auto message = QDBusMessage::createMethodCall(QDBusConnection::systemBus().baseService(),
                                                      agentPath, agentInterface, method);
        message.setArguments(arguments);
        return server->asyncCall(message);
    }

    QDBusPendingCall getSecrets(const NMVariantMapMap &connection, const QString &setting, uint flags,
                                const QString &path = connectionPath)
    {
        return call(QStringLiteral("GetSecrets"),
                    {QVariant::fromValue(connection), QVariant::fromValue(QDBusObjectPath(path)),
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

    std::unique_ptr<QDBusConnection> keyring;
    std::unique_ptr<QDBusConnection> server;
    std::unique_ptr<FakeSecretService> secrets;
    std::unique_ptr<FakeNetworkManager> networkManager;
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
