#ifndef CUTEFISH_USERACCOUNT_P_H
#define CUTEFISH_USERACCOUNT_P_H

#include "user_interface.h"

#include <QtCore/QHash>
#include <QtCore/QVariantMap>

namespace QtAccountsService {

class UserAccountPrivate
{
public:
    explicit UserAccountPrivate(const QDBusConnection &bus);

    QDBusConnection bus;
    OrgFreedesktopAccountsUserInterface *service = nullptr;
    QString objectPath;
    // Every getter reads from here; GetAll replies fill it asynchronously.
    QVariantMap values;
    // Values set but not yet confirmed; a GetAll answered meanwhile still has the old ones.
    QVariantMap pendingValues;
    // Calls still in flight per property in pendingValues.
    QHash<QString, int> pendingCalls;
    quint64 refreshSerial = 0;
};

}

#endif
