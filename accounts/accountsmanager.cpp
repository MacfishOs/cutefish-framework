#include "accountsmanager.h"
#include "accountsmanager_p.h"

#include <QtCore/QDebug>
#include <QtDBus/QDBusPendingCallWatcher>

namespace QtAccountsService {

static const QString userPathPrefix = QStringLiteral("/org/freedesktop/Accounts/User");

AccountsManagerPrivate::AccountsManagerPrivate(AccountsManager *manager,
                                               const QDBusConnection &bus)
    : owner(manager)
    , service(new OrgFreedesktopAccountsInterface(
              QStringLiteral("org.freedesktop.Accounts"),
              QStringLiteral("/org/freedesktop/Accounts"), bus, manager))
{
    service->setInteractiveAuthorizationAllowed(true);
    service->setTimeout(120000);
}

UserAccount *AccountsManagerPrivate::account(const QString &path, quint64 requestSerial,
                                             const QString &name)
{
    if (path.isEmpty() || deletionSerials.value(path) > requestSerial)
        return nullptr;

    UserAccount *user = accounts.value(path);
    if (!user) {
        user = new UserAccount(path, service->connection(), owner);
        accounts.insert(path, user);
        const auto updateName = [this, user]() {
            const QString path = pathFor(user);
            if (!path.isEmpty())
                names.insert(path, user->userName());
        };
        QObject::connect(user, &UserAccount::userNameChanged, owner, updateName);
        QObject::connect(user, &UserAccount::accountChanged, owner, updateName);
        QObject::connect(user, &UserAccount::userIdChanged, owner,
                         [this, user]() {
                             const QString previousPath = pathFor(user);
                             if (previousPath.isEmpty())
                                 return;

                             const QString nextPath = userPathPrefix + QString::number(user->userId());
                             if (nextPath == previousPath)
                                 return;
                             const QString previousName = names.take(previousPath);
                             accounts.remove(previousPath);
                             remove(nextPath);
                             accounts.insert(nextPath, user);
                             const QString userName = user->userName();
                             names.insert(nextPath, userName.isEmpty() ? previousName : userName);
                         });
    }
    const QString currentName = user->userName();
    if (!currentName.isEmpty())
        names.insert(path, currentName);
    else if (!name.isEmpty())
        names.insert(path, name);
    return user;
}

UserAccountList AccountsManagerPrivate::accountsFor(const QList<QDBusObjectPath> &paths,
                                                    quint64 requestSerial)
{
    UserAccountList result;
    for (const QDBusObjectPath &path : paths) {
        if (UserAccount *user = account(path.path(), requestSerial))
            result.append(user);
    }
    return result;
}

QString AccountsManagerPrivate::pathFor(UserAccount *user) const
{
    for (auto it = accounts.cbegin(); it != accounts.cend(); ++it) {
        if (it.value() == user)
            return it.key();
    }
    return {};
}

void AccountsManagerPrivate::remove(const QString &path)
{
    UserAccount *user = accounts.take(path);
    names.remove(path);
    if (user)
        user->deleteLater();
}

static constexpr int kMaxDeletionSerials = 64;

void AccountsManagerPrivate::removedByService(const QString &path)
{
    deletionSerials.insert(path, ++serial);
    // Only replies still in flight can be older than a deletion; the hash
    // needs no more history than that.
    while (deletionSerials.size() > kMaxDeletionSerials) {
        auto oldest = deletionSerials.begin();
        for (auto it = deletionSerials.begin(); it != deletionSerials.end(); ++it) {
            if (it.value() < oldest.value())
                oldest = it;
        }
        deletionSerials.erase(oldest);
    }
    UserAccount *user = accounts.value(path);
    bool valid = false;
    const qlonglong uid = path.startsWith(userPathPrefix)
            ? path.mid(userPathPrefix.size()).toLongLong(&valid)
            : 0;
    const qlonglong reportedUid = valid ? uid : (user ? user->userId() : -1);
    remove(path);
    if (reportedUid >= 0)
        Q_EMIT owner->userDeleted(reportedUid);
}

void AccountsManagerPrivate::addedByService(const QString &path)
{
    if (UserAccount *user = account(path, ++serial))
        Q_EMIT owner->userAdded(user);
}

quint64 AccountsManagerPrivate::nextRequest(const QString &name)
{
    const quint64 number = requests.value(name) + 1;
    requests.insert(name, number);
    return number;
}

bool AccountsManagerPrivate::isLatestRequest(const QString &name, quint64 number) const
{
    return requests.value(name) == number;
}

AccountsManager::AccountsManager(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , d_ptr(new AccountsManagerPrivate(this, bus))
{
    qRegisterMetaType<UserAccount::AccountType>();
    qRegisterMetaType<UserAccount *>();
    qRegisterMetaType<UserAccountList>();

    connect(d_ptr->service, &OrgFreedesktopAccountsInterface::UserAdded,
            this, &AccountsManager::_q_userAdded);
    connect(d_ptr->service, &OrgFreedesktopAccountsInterface::UserDeleted,
            this, &AccountsManager::_q_userDeleted);
}

AccountsManager::~AccountsManager()
{
    delete d_ptr;
}

void AccountsManager::_q_userAdded(const QDBusObjectPath &path)
{
    d_ptr->addedByService(path.path());
}

void AccountsManager::_q_userDeleted(const QDBusObjectPath &path)
{
    d_ptr->removedByService(path.path());
}

void AccountsManager::cacheUser(const QString &userName)
{
    const quint64 request = d_ptr->nextRequest(userName);
    const quint64 started = d_ptr->serial;
    auto *watcher = new QDBusPendingCallWatcher(d_ptr->service->CacheUser(userName), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, userName, request, started](QDBusPendingCallWatcher *finished) {
                QDBusPendingReply<QDBusObjectPath> reply = *finished;
                finished->deleteLater();
                if (reply.isError()) {
                    qWarning() << "CacheUser failed:" << reply.error().message();
                    return;
                }
                if (!d_ptr->isLatestRequest(userName, request))
                    return;
                if (d_ptr->account(reply.value().path(), started, userName))
                    Q_EMIT userCached(userName);
            });
}

void AccountsManager::uncacheUser(const QString &userName)
{
    const quint64 request = d_ptr->nextRequest(userName);
    auto *watcher = new QDBusPendingCallWatcher(d_ptr->service->UncacheUser(userName), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, userName, request](QDBusPendingCallWatcher *finished) {
                QDBusPendingReply<> reply = *finished;
                finished->deleteLater();
                if (reply.isError()) {
                    qWarning() << "UncacheUser failed:" << reply.error().message();
                    return;
                }
                if (!d_ptr->isLatestRequest(userName, request))
                    return;

                QStringList paths;
                for (auto it = d_ptr->names.cbegin(); it != d_ptr->names.cend(); ++it) {
                    if (it.value() == userName)
                        paths.append(it.key());
                }
                for (const QString &path : paths)
                    d_ptr->remove(path);
                Q_EMIT userUncached(userName);
            });
}

void AccountsManager::listCachedUsers()
{
    const quint64 started = d_ptr->serial;
    auto *watcher = new QDBusPendingCallWatcher(d_ptr->service->ListCachedUsers(), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, started](QDBusPendingCallWatcher *finished) {
                QDBusPendingReply<QList<QDBusObjectPath>> reply = *finished;
                finished->deleteLater();
                if (reply.isError()) {
                    qWarning() << "ListCachedUsers failed:" << reply.error().message();
                    return;
                }

                Q_EMIT listCachedUsersFinished(d_ptr->accountsFor(reply.value(), started));
            });
}

UserAccountList AccountsManager::listCachedUsersSync()
{
    const quint64 started = d_ptr->serial;
    QDBusPendingReply<QList<QDBusObjectPath>> reply = d_ptr->service->ListCachedUsers();
    reply.waitForFinished();
    if (reply.isError()) {
        qWarning() << "ListCachedUsers failed:" << reply.error().message();
        return {};
    }

    return d_ptr->accountsFor(reply.value(), started);
}

UserAccount *AccountsManager::cachedUser(const QString &userName) const
{
    for (auto it = d_ptr->names.cbegin(); it != d_ptr->names.cend(); ++it) {
        if (it.value() == userName) {
            if (UserAccount *user = d_ptr->accounts.value(it.key()))
                return user;
        }
    }
    return nullptr;
}

UserAccount *AccountsManager::findUserById(qlonglong uid)
{
    const quint64 started = d_ptr->serial;
    QDBusPendingReply<QDBusObjectPath> reply = d_ptr->service->FindUserById(uid);
    reply.waitForFinished();
    if (reply.isError()) {
        qWarning() << "FindUserById failed:" << reply.error().message();
        return nullptr;
    }
    return d_ptr->account(reply.value().path(), started);
}

UserAccount *AccountsManager::findUserByName(const QString &userName)
{
    const quint64 started = d_ptr->serial;
    QDBusPendingReply<QDBusObjectPath> reply = d_ptr->service->FindUserByName(userName);
    reply.waitForFinished();
    if (reply.isError()) {
        qWarning() << "FindUserByName failed:" << reply.error().message();
        return nullptr;
    }
    return d_ptr->account(reply.value().path(), started, userName);
}

void AccountsManager::createUser(const QString &userName, const QString &fullName,
                                 UserAccount::AccountType accountType)
{
    const quint64 started = d_ptr->serial;
    auto *watcher = new QDBusPendingCallWatcher(
            d_ptr->service->CreateUser(userName, fullName, static_cast<int>(accountType)), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, userName, started](QDBusPendingCallWatcher *finished) {
                QDBusPendingReply<QDBusObjectPath> reply = *finished;
                finished->deleteLater();
                if (reply.isError()) {
                    Q_EMIT userCreationFailed(reply.error().message());
                    return;
                }
                if (UserAccount *user = d_ptr->account(reply.value().path(), started, userName))
                    Q_EMIT userCreated(user);
                else
                    Q_EMIT userCreationFailed(QStringLiteral("Created user was removed"));
            });
}

// Deleting asks polkit, which may take a dialog's worth of time: never wait here.
void AccountsManager::deleteUser(qlonglong uid, bool removeFiles)
{
    auto *watcher = new QDBusPendingCallWatcher(d_ptr->service->DeleteUser(uid, removeFiles), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *finished) {
                QDBusPendingReply<> reply = *finished;
                finished->deleteLater();
                if (reply.isError()) {
                    qWarning() << "DeleteUser failed:" << reply.error().message();
                    Q_EMIT userDeletionFailed(reply.error().message());
                }
            });
}

}

#include "moc_accountsmanager.cpp"
