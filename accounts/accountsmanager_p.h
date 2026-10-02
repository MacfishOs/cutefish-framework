#ifndef CUTEFISH_ACCOUNTSMANAGER_P_H
#define CUTEFISH_ACCOUNTSMANAGER_P_H

#include "accountsmanager.h"
#include "accounts_interface.h"

#include <QtCore/QHash>
#include <QtCore/QPointer>

namespace QtAccountsService {

class AccountsManagerPrivate
{
public:
    AccountsManagerPrivate(AccountsManager *owner, const QDBusConnection &bus);

    UserAccount *account(const QString &path, quint64 requestSerial,
                         const QString &name = QString());
    UserAccountList accountsFor(const QList<QDBusObjectPath> &paths, quint64 requestSerial);
    QString pathFor(UserAccount *user) const;
    void remove(const QString &path);
    void removedByService(const QString &path);
    void addedByService(const QString &path);
    quint64 nextRequest(const QString &name);
    bool isLatestRequest(const QString &name, quint64 number) const;

    AccountsManager *owner;
    OrgFreedesktopAccountsInterface *service;
    QHash<QString, QPointer<UserAccount>> accounts;
    QHash<QString, QString> names;
    QHash<QString, quint64> deletionSerials;
    QHash<QString, quint64> requests;
    quint64 serial = 0;
};

}

#endif
