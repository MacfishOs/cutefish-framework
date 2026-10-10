#include "accountsmanager.h"
#include "usersmodel.h"

#include <QtCore/QProcess>
#include <QtCore/QTimer>
#include <QtDBus/QDBusAbstractAdaptor>
#include <QtDBus/QDBusConnectionInterface>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusObjectPath>
#include <QtTest/QSignalSpy>
#include <QtTest/QTest>

using namespace QtAccountsService;

static const QString serviceName = QStringLiteral("org.freedesktop.Accounts");
static const QString managerPath = QStringLiteral("/org/freedesktop/Accounts");
static const QStringList userNames = {
    QStringLiteral("alice"), QStringLiteral("bob"), QStringLiteral("carol"),
    QStringLiteral("error"), QStringLiteral("delete-before-cache"),
    QStringLiteral("delete-before-uncache"), QStringLiteral("created")
};

class TestUserAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Accounts.User")
    Q_PROPERTY(QString UserName READ userName)
    Q_PROPERTY(QString RealName READ realName)
    Q_PROPERTY(qulonglong Uid READ uid)
    Q_PROPERTY(QString Email READ email)

public:
    TestUserAdaptor(QObject *parent, const QString &name, qulonglong uid)
        : QDBusAbstractAdaptor(parent)
        , m_name(name)
        , m_uid(uid)
    {
    }

    QString userName() const { return m_name; }
    QString realName() const { return m_realName; }
    qulonglong uid() const { return m_uid; }
    QString email() const { return m_email; }

public Q_SLOTS:
    void SetUserName(const QString &name)
    {
        m_name = name;
        Q_EMIT Changed();
    }

    void SetRealName(const QString &name)
    {
        m_realName = name;
        Q_EMIT Changed();
    }

    void SetRealNameSilently(const QString &name)
    {
        m_realName = name;
    }

    // Answers late, as behind a polkit prompt, and reports an unrelated
    // change while the caller waits.
    void SetEmail(const QString &address, const QDBusMessage &message)
    {
        message.setDelayedReply(true);
        Q_EMIT Changed();
        QTimer::singleShot(300, this, [this, address, message] {
            m_email = address;
            QDBusConnection::sessionBus().send(message.createReply());
            Q_EMIT Changed();
        });
    }

Q_SIGNALS:
    void Changed();

private:
    QString m_name;
    QString m_realName;
    QString m_email;
    qulonglong m_uid;
};

class TestAccountsAdaptor : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Accounts")

public:
    explicit TestAccountsAdaptor(QObject *parent)
        : QDBusAbstractAdaptor(parent)
    {
    }

public Q_SLOTS:
    QList<QDBusObjectPath> ListCachedUsers() const
    {
        return {userPath(QStringLiteral("alice")), userPath(QStringLiteral("bob")),
                userPath(QStringLiteral("carol")), userPath(QStringLiteral("error")),
                userPath(QStringLiteral("delete-before-cache")),
                userPath(QStringLiteral("delete-before-uncache"))};
    }

    QDBusObjectPath FindUserById(qlonglong uid) const
    {
        return QDBusObjectPath(managerPath + QStringLiteral("/User") + QString::number(uid));
    }

    QDBusObjectPath FindUserByName(const QString &name) const
    {
        return userPath(name);
    }

    QDBusObjectPath CreateUser(const QString &name, const QString &, int)
    {
        const QDBusObjectPath path = userPath(name);
        Q_EMIT UserAdded(path);
        return path;
    }

    void DeleteUser(qlonglong uid, bool)
    {
        Q_EMIT UserDeleted(FindUserById(uid));
    }

    QDBusObjectPath CacheUser(const QString &name)
    {
        const auto path = userPath(name);
        if (name == QStringLiteral("delete-before-cache"))
            Q_EMIT UserDeleted(path);
        return path;
    }

    void UncacheUser(const QString &name)
    {
        if (name == QStringLiteral("delete-before-uncache"))
            Q_EMIT UserDeleted(userPath(name));
    }

    void DeleteForTest(const QString &name)
    {
        Q_EMIT UserDeleted(userPath(name));
    }

    void AddForTest(const QString &name)
    {
        Q_EMIT UserAdded(userPath(name));
    }

Q_SIGNALS:
    void UserAdded(const QDBusObjectPath &path);
    void UserDeleted(const QDBusObjectPath &path);

private:
    static QDBusObjectPath userPath(const QString &name)
    {
        return QDBusObjectPath(managerPath + QStringLiteral("/User")
                               + QString::number(1001 + userNames.indexOf(name)));
    }
};

class AccountsManagerTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        startService();
    }

    void cleanupTestCase()
    {
        m_service.terminate();
        m_service.waitForFinished();
    }

    void cacheLookupAndUncache()
    {
        AccountsManager manager(QDBusConnection::sessionBus());
        QSignalSpy cached(&manager, &AccountsManager::userCached);
        QSignalSpy uncached(&manager, &AccountsManager::userUncached);

        QVERIFY(!manager.cachedUser(QStringLiteral("alice")));
        manager.cacheUser(QStringLiteral("alice"));
        QTRY_COMPARE(cached.size(), 1);
        auto *alice = manager.cachedUser(QStringLiteral("alice"));
        QVERIFY(alice);

        manager.cacheUser(QStringLiteral("bob"));
        QTRY_COMPARE(cached.size(), 2);
        auto *bob = manager.cachedUser(QStringLiteral("bob"));
        QVERIFY(bob);
        QCOMPARE(manager.cachedUser(QStringLiteral("alice")), alice);
        QCOMPARE(manager.cachedUser(QStringLiteral("bob")), bob);
        QVERIFY(!manager.cachedUser(QStringLiteral("missing")));

        QPointer<UserAccount> deleted = alice;
        manager.uncacheUser(QStringLiteral("alice"));
        QTRY_COMPARE(uncached.size(), 1);
        QVERIFY(!manager.cachedUser(QStringLiteral("alice")));
        QCOMPARE(manager.cachedUser(QStringLiteral("bob")), bob);
        QTRY_VERIFY(deleted.isNull());

        manager.uncacheUser(QStringLiteral("bob"));
        QTRY_COMPARE(uncached.size(), 2);
        QVERIFY(!manager.cachedUser(QStringLiteral("bob")));
    }

    void removeFirstMiddleLastAndOnly()
    {
        const QStringList names = {QStringLiteral("alice"), QStringLiteral("bob"),
                                   QStringLiteral("carol")};
        for (const QString &removed : names) {
            AccountsManager manager(QDBusConnection::sessionBus());
            QSignalSpy cached(&manager, &AccountsManager::userCached);
            QSignalSpy uncached(&manager, &AccountsManager::userUncached);
            for (const QString &name : names)
                manager.cacheUser(name);
            QTRY_COMPARE(cached.size(), names.size());
            manager.uncacheUser(removed);
            QTRY_COMPARE(uncached.size(), 1);
            QVERIFY(!manager.cachedUser(removed));
            for (const QString &name : names) {
                if (name != removed)
                    QVERIFY(manager.cachedUser(name));
            }
        }
        AccountsManager manager(QDBusConnection::sessionBus());
        QSignalSpy cached(&manager, &AccountsManager::userCached);
        QSignalSpy uncached(&manager, &AccountsManager::userUncached);
        manager.cacheUser(QStringLiteral("alice"));
        QTRY_COMPARE(cached.size(), 1);
        manager.uncacheUser(QStringLiteral("alice"));
        QTRY_COMPARE(uncached.size(), 1);
        QVERIFY(!manager.cachedUser(QStringLiteral("alice")));
    }

    void errorAndDeleteRaces()
    {
        AccountsManager manager(QDBusConnection::sessionBus());
        QSignalSpy cached(&manager, &AccountsManager::userCached);
        QSignalSpy uncached(&manager, &AccountsManager::userUncached);
        QSignalSpy deleted(&manager, &AccountsManager::userDeleted);

        manager.cacheUser(QStringLiteral("error"));
        QTRY_COMPARE(cached.size(), 1);
        QPointer<UserAccount> retained = manager.cachedUser(QStringLiteral("error"));
        QVERIFY(retained);
        m_service.terminate();
        QVERIFY(m_service.waitForFinished());
        manager.uncacheUser(QStringLiteral("error"));
        QTest::qWait(50);
        QCOMPARE(uncached.size(), 0);
        QVERIFY(retained);
        startService();
        QCOMPARE(manager.cachedUser(QStringLiteral("error")), retained.data());

        manager.cacheUser(QStringLiteral("delete-before-cache"));
        QTRY_COMPARE(deleted.size(), 1);
        QTest::qWait(50);
        QCOMPARE(cached.size(), 1);
        QVERIFY(!manager.cachedUser(QStringLiteral("delete-before-cache")));

        manager.cacheUser(QStringLiteral("delete-before-uncache"));
        QTRY_COMPARE(cached.size(), 2);
        manager.uncacheUser(QStringLiteral("delete-before-uncache"));
        QTRY_COMPARE(deleted.size(), 2);
        QTRY_COMPARE(uncached.size(), 1);
        QVERIFY(!manager.cachedUser(QStringLiteral("delete-before-uncache")));
    }

    void deletionRemovesCacheAndAdditionRestoresIt()
    {
        AccountsManager manager(QDBusConnection::sessionBus());
        QSignalSpy cached(&manager, &AccountsManager::userCached);
        QSignalSpy added(&manager, &AccountsManager::userAdded);
        QSignalSpy deleted(&manager, &AccountsManager::userDeleted);
        QDBusInterface service(serviceName, managerPath, serviceName,
                               QDBusConnection::sessionBus());

        manager.cacheUser(QStringLiteral("carol"));
        QTRY_COMPARE(cached.size(), 1);
        QPointer<UserAccount> original = manager.cachedUser(QStringLiteral("carol"));
        QVERIFY(original);

        QVERIFY(service.call(QStringLiteral("DeleteForTest"), QStringLiteral("carol")).type()
                != QDBusMessage::ErrorMessage);
        QTRY_COMPARE(deleted.size(), 1);
        QCOMPARE(deleted.first().first().toLongLong(), 1003);
        QVERIFY(!manager.cachedUser(QStringLiteral("carol")));
        QTRY_VERIFY(original.isNull());

        QVERIFY(service.call(QStringLiteral("AddForTest"), QStringLiteral("carol")).type()
                != QDBusMessage::ErrorMessage);
        QTRY_COMPARE(added.size(), 1);
        // The name is known once the asynchronous property fetch lands.
        QTRY_VERIFY(manager.cachedUser(QStringLiteral("carol")));
    }

    void externallyDestroyedAccountIsNotRetained()
    {
        AccountsManager manager(QDBusConnection::sessionBus());
        QSignalSpy cached(&manager, &AccountsManager::userCached);
        manager.cacheUser(QStringLiteral("bob"));
        QTRY_COMPARE(cached.size(), 1);
        UserAccount *account = manager.cachedUser(QStringLiteral("bob"));
        QVERIFY(account);

        delete account;
        QVERIFY(!manager.cachedUser(QStringLiteral("bob")));

        manager.cacheUser(QStringLiteral("bob"));
        QTRY_COMPARE(cached.size(), 2);
        QVERIFY(manager.cachedUser(QStringLiteral("bob")));
    }

    void modelRemovesDeletedUser()
    {
        UsersModel model(QDBusConnection::sessionBus());
        QCOMPARE(model.roleNames().value(UsersModel::DisplayNameRole),
                 QByteArray("displayNameRole"));
        QTRY_COMPARE(model.rowCount(), 6);

        QDBusInterface service(serviceName, managerPath, serviceName,
                               QDBusConnection::sessionBus());
        QVERIFY(service.call(QStringLiteral("DeleteForTest"), QStringLiteral("alice")).type()
                != QDBusMessage::ErrorMessage);
        QTRY_COMPARE(model.rowCount(), 5);
        for (int row = 0; row < model.rowCount(); ++row)
            QVERIFY(model.data(model.index(row), UsersModel::UserIdRole).toLongLong() != 1001);
    }

    void accountPropertiesFollowServiceChanges()
    {
        AccountsManager manager(QDBusConnection::sessionBus());
        QSignalSpy cached(&manager, &AccountsManager::userCached);
        manager.cacheUser(QStringLiteral("alice"));
        QTRY_COMPARE(cached.size(), 1);

        UserAccount *account = manager.cachedUser(QStringLiteral("alice"));
        QVERIFY(account);
        QSignalSpy realNameChanged(account, &UserAccount::realNameChanged);
        QSignalSpy userNameChanged(account, &UserAccount::userNameChanged);
        account->setRealName(QStringLiteral("Alice Example"));
        QTRY_COMPARE(account->displayName(), QStringLiteral("Alice Example"));
        QTRY_COMPARE(realNameChanged.size(), 1);

        account->setUserName(QStringLiteral("alice-renamed"));
        QTRY_COMPARE(account->userName(), QStringLiteral("alice-renamed"));
        QTRY_COMPARE(userNameChanged.size(), 1);
        QCOMPARE(manager.cachedUser(QStringLiteral("alice-renamed")), account);
        QVERIFY(!manager.cachedUser(QStringLiteral("alice")));
    }

    void changingUserIdRekeysManagerCache()
    {
        AccountsManager manager(QDBusConnection::sessionBus());
        QSignalSpy cached(&manager, &AccountsManager::userCached);
        manager.cacheUser(QStringLiteral("bob"));
        QTRY_COMPARE(cached.size(), 1);
        UserAccount *account = manager.cachedUser(QStringLiteral("bob"));
        QVERIFY(account);

        account->setUserId(1003);
        QCOMPARE(account->userId(), 1003);
        QTRY_COMPARE(manager.cachedUser(QStringLiteral("carol")), account);
        QVERIFY(!manager.cachedUser(QStringLiteral("bob")));
    }

    // Getters answer from the cache without touching the bus; the service's
    // Changed signal is what refreshes it.
    void gettersReadCachedServiceValues()
    {
        AccountsManager manager(QDBusConnection::sessionBus());
        QSignalSpy cached(&manager, &AccountsManager::userCached);
        manager.cacheUser(QStringLiteral("bob"));
        QTRY_COMPARE(cached.size(), 1);
        UserAccount *account = manager.cachedUser(QStringLiteral("bob"));
        QVERIFY(account);
        QCOMPARE(account->userId(), 1002);
        QTRY_COMPARE(account->userName(), QStringLiteral("bob"));

        QDBusInterface user(serviceName, managerPath + QStringLiteral("/User1002"),
                            QStringLiteral("org.freedesktop.Accounts.User"),
                            QDBusConnection::sessionBus());
        QVERIFY(user.call(QStringLiteral("SetRealName"),
                          QStringLiteral("Bob Current")).type() != QDBusMessage::ErrorMessage);
        QTRY_COMPARE(account->realName(), QStringLiteral("Bob Current"));

        QVERIFY(user.call(QStringLiteral("SetRealNameSilently"),
                          QStringLiteral("Bob Silent")).type() != QDBusMessage::ErrorMessage);
        QTest::qWait(50);
        QCOMPARE(account->realName(), QStringLiteral("Bob Current"));
    }

    void aPendingChangeOutlastsReadsAnsweredMeanwhile()
    {
        AccountsManager manager(QDBusConnection::sessionBus());
        QSignalSpy cached(&manager, &AccountsManager::userCached);
        manager.cacheUser(QStringLiteral("carol"));
        QTRY_COMPARE(cached.size(), 1);
        UserAccount *account = manager.cachedUser(QStringLiteral("carol"));
        QVERIFY(account);
        QTRY_COMPARE(account->userName(), QStringLiteral("carol"));

        account->setEmail(QStringLiteral("carol@example.org"));
        QCOMPARE(account->email(), QStringLiteral("carol@example.org"));
        for (int i = 0; i < 6; ++i) {
            QTest::qWait(40);
            QCOMPARE(account->email(), QStringLiteral("carol@example.org"));
        }
        QTest::qWait(400);
        QCOMPARE(account->email(), QStringLiteral("carol@example.org"));
    }

    void synchronousQueriesAndCreateDelete()
    {
        AccountsManager manager(QDBusConnection::sessionBus());
        const UserAccountList users = manager.listCachedUsersSync();
        QCOMPARE(users.size(), 6);
        UserAccount *bob = manager.findUserById(1002);
        QVERIFY(bob);
        QCOMPARE(manager.findUserByName(QStringLiteral("bob")), bob);

        QSignalSpy created(&manager, &AccountsManager::userCreated);
        QSignalSpy deleted(&manager, &AccountsManager::userDeleted);
        manager.createUser(QStringLiteral("created"), QStringLiteral("New User"),
                           UserAccount::StandardAccountType);
        QTRY_COMPARE(created.size(), 1);
        QPointer<UserAccount> account = manager.cachedUser(QStringLiteral("created"));
        QVERIFY(account);
        QCOMPARE(account->userId(), 1007);

        manager.deleteUser(1007, false);
        QTRY_COMPARE(deleted.size(), 1);
        QVERIFY(!manager.cachedUser(QStringLiteral("created")));
        QTRY_VERIFY(account.isNull());
    }

private:
    void startService()
    {
        m_service.setProgram(QCoreApplication::applicationFilePath());
        m_service.setArguments({QStringLiteral("--service")});
        m_service.start();
        QVERIFY(m_service.waitForStarted());
        QTRY_VERIFY(QDBusConnection::sessionBus().interface()->isServiceRegistered(serviceName));
    }

    QProcess m_service;
};

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--service"))) {
        QObject manager;
        TestAccountsAdaptor adaptor(&manager);
        auto bus = QDBusConnection::sessionBus();
        if (!bus.registerService(serviceName)
                || !bus.registerObject(managerPath, &manager, QDBusConnection::ExportAdaptors))
            return 1;

        for (int i = 0; i < userNames.size(); ++i) {
            QObject *user = new QObject(&manager);
            new TestUserAdaptor(user, userNames.at(i), 1001 + i);
            const QString path = managerPath + QStringLiteral("/User") + QString::number(1001 + i);
            if (!bus.registerObject(path, user, QDBusConnection::ExportAdaptors))
                return 1;
        }
        return app.exec();
    }

    AccountsManagerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "accountsmanager_test.moc"
