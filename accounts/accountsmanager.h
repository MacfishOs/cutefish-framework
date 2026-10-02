#ifndef CUTEFISH_ACCOUNTSMANAGER_H
#define CUTEFISH_ACCOUNTSMANAGER_H

#include "useraccount.h"

#include <QtCore/QObject>
#include <QtDBus/QDBusConnection>

QT_FORWARD_DECLARE_CLASS(QDBusObjectPath)

namespace QtAccountsService {

class AccountsManagerPrivate;

class AccountsManager : public QObject
{
    Q_OBJECT
public:
    explicit AccountsManager(const QDBusConnection &bus = QDBusConnection::systemBus(),
                             QObject *parent = nullptr);
    ~AccountsManager() override;

    Q_INVOKABLE void cacheUser(const QString &userName);
    Q_INVOKABLE void uncacheUser(const QString &userName);
    Q_INVOKABLE void listCachedUsers();
    UserAccountList listCachedUsersSync();
    Q_INVOKABLE UserAccount *cachedUser(const QString &userName) const;
    Q_INVOKABLE UserAccount *findUserById(qlonglong uid);
    Q_INVOKABLE UserAccount *findUserByName(const QString &userName);
    Q_INVOKABLE void createUser(const QString &userName, const QString &fullName,
                                UserAccount::AccountType accountType);
    Q_INVOKABLE bool deleteUser(qlonglong uid, bool removeFiles);

Q_SIGNALS:
    void userCreated(QtAccountsService::UserAccount *account);
    void userCreationFailed(const QString &message);
    void userAdded(QtAccountsService::UserAccount *account);
    void userDeleted(qlonglong uid);
    void userCached(const QString &userName);
    void userUncached(const QString &userName);
    void listCachedUsersFinished(const QtAccountsService::UserAccountList &userList);

protected:
    AccountsManagerPrivate *d_ptr;

private:
    Q_DECLARE_PRIVATE(AccountsManager)

private Q_SLOTS:
    void _q_userAdded(const QDBusObjectPath &path);
    void _q_userDeleted(const QDBusObjectPath &path);
};

}

#endif
