#include "useraccount.h"
#include "useraccount_p.h"

#include <QtCore/QFile>
#include <QtDBus/QDBusMessage>
#include <QtDBus/QDBusPendingCallWatcher>
#include <QtDBus/QDBusPendingReply>

#include <cerrno>
#include <pwd.h>
#include <unistd.h>

namespace QtAccountsService {

static const QString serviceName = QStringLiteral("org.freedesktop.Accounts");
static const QString userInterface = QStringLiteral("org.freedesktop.Accounts.User");
static const QString userPathPrefix = QStringLiteral("/org/freedesktop/Accounts/User");

UserAccountPrivate::UserAccountPrivate(const QDBusConnection &connection)
    : bus(connection)
{
}

UserAccount::UserAccount(const QDBusConnection &bus, QObject *parent)
    : UserAccount(userPathPrefix + QString::number(getuid()), bus, parent)
{
}

UserAccount::UserAccount(const QString &path, const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , d_ptr(new UserAccountPrivate(bus))
{
    setObjectPath(path);
}

UserAccount::~UserAccount()
{
    delete d_ptr;
}

void UserAccount::setObjectPath(const QString &path)
{
    if (d_ptr->objectPath == path)
        return;

    delete d_ptr->service;
    d_ptr->objectPath = path;
    d_ptr->values.clear();
    d_ptr->pendingValues.clear();
    d_ptr->values.insert(QStringLiteral("Uid"), path.mid(userPathPrefix.size()).toLongLong());
    d_ptr->service = new OrgFreedesktopAccountsUserInterface(serviceName, path,
                                                              d_ptr->bus, this);
    d_ptr->service->setInteractiveAuthorizationAllowed(true);
    d_ptr->service->setTimeout(120000);
    connect(d_ptr->service, &OrgFreedesktopAccountsUserInterface::Changed,
            this, &UserAccount::refresh);
    refresh();
    notifyAll();
}

QVariant UserAccount::value(const char *property) const
{
    return d_ptr->values.value(QString::fromLatin1(property));
}

void UserAccount::notifyAll()
{
    Q_EMIT userIdChanged();
    Q_EMIT groupIdChanged();
    Q_EMIT accountTypeChanged();
    Q_EMIT lockedChanged();
    Q_EMIT automaticLoginChanged();
    Q_EMIT passwordModeChanged();
    Q_EMIT userNameChanged();
    Q_EMIT realNameChanged();
    Q_EMIT displayNameChanged();
    Q_EMIT homeDirectoryChanged();
    Q_EMIT shellChanged();
    Q_EMIT iconFileNameChanged();
    Q_EMIT emailChanged();
    Q_EMIT languageChanged();
    Q_EMIT locationChanged();
    Q_EMIT xsessionChanged();
}

// Reads are never answered from the bus on the caller's thread: a getter
// returns the cached value and this fills the cache when the reply lands.
void UserAccount::refresh()
{
    const quint64 serial = ++d_ptr->refreshSerial;
    QDBusMessage message = QDBusMessage::createMethodCall(
            serviceName, d_ptr->objectPath, QStringLiteral("org.freedesktop.DBus.Properties"),
            QStringLiteral("GetAll"));
    message << userInterface;
    auto *watcher = new QDBusPendingCallWatcher(d_ptr->bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, serial](QDBusPendingCallWatcher *finished) {
                QDBusPendingReply<QVariantMap> reply = *finished;
                finished->deleteLater();
                if (serial != d_ptr->refreshSerial)
                    return;
                if (reply.isError()) {
                    qWarning("AccountsService read failed: %s",
                             reply.error().message().toUtf8().constData());
                    return;
                }
                applyValues(reply.value());
            });
}

void UserAccount::applyValues(const QVariantMap &reply)
{
    QVariantMap updated = reply;
    for (auto it = d_ptr->pendingValues.cbegin(); it != d_ptr->pendingValues.cend(); ++it)
        updated.insert(it.key(), it.value());

    const QVariantMap previous = d_ptr->values;
    d_ptr->values = updated;

    struct Change {
        const char *property;
        void (UserAccount::*signal)();
    };
    static const Change changes[] = {
        {"Uid", &UserAccount::userIdChanged},
        {"AccountType", &UserAccount::accountTypeChanged},
        {"Locked", &UserAccount::lockedChanged},
        {"AutomaticLogin", &UserAccount::automaticLoginChanged},
        {"PasswordMode", &UserAccount::passwordModeChanged},
        {"UserName", &UserAccount::userNameChanged},
        {"RealName", &UserAccount::realNameChanged},
        {"HomeDirectory", &UserAccount::homeDirectoryChanged},
        {"Shell", &UserAccount::shellChanged},
        {"IconFile", &UserAccount::iconFileNameChanged},
        {"Email", &UserAccount::emailChanged},
        {"Language", &UserAccount::languageChanged},
        {"Location", &UserAccount::locationChanged},
        {"XSession", &UserAccount::xsessionChanged}
    };

    bool changed = previous != updated;
    bool nameChanged = false;
    for (const Change &entry : changes) {
        const QString key = QString::fromLatin1(entry.property);
        if (previous.value(key) == updated.value(key))
            continue;
        Q_EMIT (this->*entry.signal)();
        if (key == QStringLiteral("Uid"))
            Q_EMIT groupIdChanged();
        if (key == QStringLiteral("UserName") || key == QStringLiteral("RealName"))
            nameChanged = true;
    }
    if (nameChanged)
        Q_EMIT displayNameChanged();
    if (changed)
        Q_EMIT accountChanged();
}

void UserAccount::watch(const QDBusPendingCall &call, void (UserAccount::*notify)(),
                        bool displayName)
{
    ++d_ptr->pendingSets;
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, notify, displayName](QDBusPendingCallWatcher *finished) {
                QDBusPendingReply<> reply = *finished;
                finished->deleteLater();
                if (--d_ptr->pendingSets == 0)
                    d_ptr->pendingValues.clear();
                if (reply.isError()) {
                    qWarning("AccountsService update failed: %s",
                             reply.error().message().toUtf8().constData());
                    refresh();
                    return;
                }
                // notePendingChange() already recorded the value, so refresh() stays quiet
                // unless the service settled on something else.
                refresh();
                if (notify)
                    Q_EMIT (this->*notify)();
                if (displayName)
                    Q_EMIT displayNameChanged();
                Q_EMIT accountChanged();
            });
}

void UserAccount::notePendingChange(const char *property, const QVariant &value,
                                    void (UserAccount::*notify)(), bool displayName)
{
    d_ptr->pendingValues.insert(QString::fromLatin1(property), value);
    d_ptr->values.insert(QString::fromLatin1(property), value);
    Q_EMIT (this->*notify)();
    if (displayName)
        Q_EMIT displayNameChanged();
}

qlonglong UserAccount::userId() const
{
    return value("Uid").toLongLong();
}

void UserAccount::setUserId(qlonglong uid)
{
    if (userId() == uid)
        return;
    setObjectPath(userPathPrefix + QString::number(uid));
}

qlonglong UserAccount::groupId() const
{
    QByteArray storage(16384, '\0');
    struct passwd data;
    struct passwd *result = nullptr;
    for (;;) {
        const int error = getpwuid_r(static_cast<uid_t>(userId()), &data,
                                     storage.data(), storage.size(), &result);
        if (error == 0)
            return result ? result->pw_gid : 0;
        if (error != ERANGE || storage.size() >= 1024 * 1024)
            return 0;
        storage.resize(storage.size() * 2);
    }
}

UserAccount::AccountType UserAccount::accountType() const
{
    return static_cast<AccountType>(value("AccountType").toInt());
}

void UserAccount::setAccountType(AccountType type)
{
    if (accountType() == type)
        return;
    watch(d_ptr->service->SetAccountType(static_cast<int>(type)), &UserAccount::accountTypeChanged);
    notePendingChange("AccountType", static_cast<int>(type), &UserAccount::accountTypeChanged);
}

bool UserAccount::isLocked() const
{
    return value("Locked").toBool();
}

void UserAccount::setLocked(bool locked)
{
    if (isLocked() == locked)
        return;
    watch(d_ptr->service->SetLocked(locked), &UserAccount::lockedChanged);
    notePendingChange("Locked", locked, &UserAccount::lockedChanged);
}

bool UserAccount::automaticLogin() const
{
    return value("AutomaticLogin").toBool();
}

void UserAccount::setAutomaticLogin(bool enabled)
{
    if (automaticLogin() == enabled)
        return;
    watch(d_ptr->service->SetAutomaticLogin(enabled), &UserAccount::automaticLoginChanged);
    notePendingChange("AutomaticLogin", enabled, &UserAccount::automaticLoginChanged);
}

qlonglong UserAccount::loginFrequency() const
{
    return value("LoginFrequency").toLongLong();
}

qlonglong UserAccount::loginTime() const
{
    return value("LoginTime").toLongLong();
}

UserAccount::PasswordMode UserAccount::passwordMode() const
{
    return static_cast<PasswordMode>(value("PasswordMode").toInt());
}

void UserAccount::setPasswordMode(PasswordMode mode)
{
    if (passwordMode() == mode)
        return;
    watch(d_ptr->service->SetPasswordMode(static_cast<int>(mode)),
          &UserAccount::passwordModeChanged);
    notePendingChange("PasswordMode", static_cast<int>(mode), &UserAccount::passwordModeChanged);
}

QString UserAccount::passwordHint() const
{
    return value("PasswordHint").toString();
}

bool UserAccount::isLocalAccount() const
{
    return value("LocalAccount").toBool();
}

bool UserAccount::isSystemAccount() const
{
    return value("SystemAccount").toBool();
}

QString UserAccount::userName() const
{
    return value("UserName").toString();
}

void UserAccount::setUserName(const QString &name)
{
    if (userName() == name)
        return;
    watch(d_ptr->service->SetUserName(name), &UserAccount::userNameChanged, true);
    notePendingChange("UserName", name, &UserAccount::userNameChanged, true);
}

QString UserAccount::realName() const
{
    return value("RealName").toString();
}

void UserAccount::setRealName(const QString &name)
{
    if (realName() == name)
        return;
    watch(d_ptr->service->SetRealName(name), &UserAccount::realNameChanged, true);
    notePendingChange("RealName", name, &UserAccount::realNameChanged, true);
}

QString UserAccount::displayName() const
{
    const QString real = realName();
    return real.isEmpty() ? userName() : real;
}

QString UserAccount::homeDirectory() const
{
    return value("HomeDirectory").toString();
}

void UserAccount::setHomeDirectory(const QString &path)
{
    if (homeDirectory() == path)
        return;
    watch(d_ptr->service->SetHomeDirectory(path), &UserAccount::homeDirectoryChanged);
    notePendingChange("HomeDirectory", path, &UserAccount::homeDirectoryChanged);
}

QString UserAccount::shell() const
{
    return value("Shell").toString();
}

void UserAccount::setShell(const QString &path)
{
    if (shell() == path)
        return;
    watch(d_ptr->service->SetShell(path), &UserAccount::shellChanged);
    notePendingChange("Shell", path, &UserAccount::shellChanged);
}

QString UserAccount::iconFileName() const
{
    const QString path = value("IconFile").toString();
    return QFile::exists(path) ? path : QString();
}

void UserAccount::setIconFileName(const QString &path)
{
    if (iconFileName() == path)
        return;
    watch(d_ptr->service->SetIconFile(path), &UserAccount::iconFileNameChanged);
    notePendingChange("IconFile", path, &UserAccount::iconFileNameChanged);
}

QString UserAccount::email() const
{
    return value("Email").toString();
}

void UserAccount::setEmail(const QString &address)
{
    if (email() == address)
        return;
    watch(d_ptr->service->SetEmail(address), &UserAccount::emailChanged);
    notePendingChange("Email", address, &UserAccount::emailChanged);
}

QString UserAccount::language() const
{
    return value("Language").toString();
}

void UserAccount::setLanguage(const QString &locale)
{
    if (language() == locale)
        return;
    watch(d_ptr->service->SetLanguage(locale), &UserAccount::languageChanged);
    notePendingChange("Language", locale, &UserAccount::languageChanged);
}

QString UserAccount::location() const
{
    return value("Location").toString();
}

void UserAccount::setLocation(const QString &place)
{
    if (location() == place)
        return;
    watch(d_ptr->service->SetLocation(place), &UserAccount::locationChanged);
    notePendingChange("Location", place, &UserAccount::locationChanged);
}

QString UserAccount::xsession() const
{
    return value("XSession").toString();
}

void UserAccount::setXSession(const QString &session)
{
    if (xsession() == session)
        return;
    watch(d_ptr->service->SetXSession(session), &UserAccount::xsessionChanged);
    notePendingChange("XSession", session, &UserAccount::xsessionChanged);
}

void UserAccount::setPassword(const QString &password, const QString &hint)
{
    watch(d_ptr->service->SetPassword(password, hint));
}

void UserAccount::setPasswordHint(const QString &hint)
{
    if (passwordHint() != hint)
        watch(d_ptr->service->SetPasswordHint(hint));
}

}

#include "moc_useraccount.cpp"
