#ifndef CUTEFISH_USERACCOUNT_P_H
#define CUTEFISH_USERACCOUNT_P_H

#include "user_interface.h"

#include <QtCore/QVariantMap>

namespace QtAccountsService {

class UserAccountPrivate
{
public:
    explicit UserAccountPrivate(const QDBusConnection &bus);

    QDBusConnection bus;
    OrgFreedesktopAccountsUserInterface *service = nullptr;
    QString objectPath;
    QVariantMap values;
};

}

#endif
