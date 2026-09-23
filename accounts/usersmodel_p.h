#ifndef CUTEFISH_USERSMODEL_P_H
#define CUTEFISH_USERSMODEL_P_H

#include "accountsmanager.h"

#include <QtCore/QPointer>
#include <QtCore/QVector>

namespace QtAccountsService {

struct UserRow
{
    QPointer<UserAccount> account;
    qlonglong uid;
};

class UsersModelPrivate
{
public:
    UsersModelPrivate(UsersModel *owner, const QDBusConnection &bus);

    UsersModel *owner;
    AccountsManager *manager;
    QVector<UserRow> rows;
};

}

#endif
