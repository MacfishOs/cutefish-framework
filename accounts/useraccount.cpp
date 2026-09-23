#include "useraccount.h"
#include "useraccount_p.h"

#include <QtCore/QFile>
#include <QtDBus/QDBusInterface>
#include <QtDBus/QDBusPendingCallWatcher>
#include <QtDBus/QDBusPendingReply>

#include <cerrno>
#include <pwd.h>
#include <unistd.h>

namespace QtAccountsService {

namespace {

const QString serviceName = QStringLiteral("org.freedesktop.Accounts");
const QString userInterface = QStringLiteral("org.freedesktop.Accounts.User");
const QString userPathPrefix = QStringLiteral("/org/freedesktop/Accounts/User");

}

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
    d_ptr->values.insert(QStringLiteral("Uid"), path.mid(userPathPrefix.size()).toLongLong());
    d_ptr->values.insert(QStringLiteral("AccountType"), 0);
    d_ptr->values.insert(QStringLiteral("Locked"), false);
    d_ptr->values.insert(QStringLiteral("AutomaticLogin"), false);
    d_ptr->values.insert(QStringLiteral("PasswordMode"), static_cast<int>(NonePasswordMode));
    d_ptr->service = new OrgFreedesktopAccountsUserInterface(serviceName, path,
                                                              d_ptr->bus, this);
    d_ptr->service->setInteractiveAuthorizationAllowed(true);
    d_ptr->service->setTimeout(120000);
    connect(d_ptr->service, &OrgFreedesktopAccountsUserInterface::Changed,
            this, &UserAccount::handleAccountChanged);
    notifyAll();
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

void UserAccount::handleAccountChanged()
{
    refresh();
}

void UserAccount::refresh()
{
    QDBusInterface properties(serviceName, d_ptr->objectPath,
                              QStringLiteral("org.freedesktop.DBus.Properties"), d_ptr->bus);
    QDBusPendingReply<QVariantMap> reply = properties.call(QStringLiteral("GetAll"), userInterface);
    reply.waitForFinished();
    QVariantMap updated;
    if (reply.isError()) {
        // Keep property notifications working with services that only support Get.
        updated = {
            {QStringLiteral("Uid"), d_ptr->service->uid()},
            {QStringLiteral("AccountType"), d_ptr->service->accountType()},
            {QStringLiteral("Locked"), d_ptr->service->locked()},
            {QStringLiteral("AutomaticLogin"), d_ptr->service->automaticLogin()},
            {QStringLiteral("LoginFrequency"), d_ptr->service->loginFrequency()},
            {QStringLiteral("LoginTime"), d_ptr->service->loginTime()},
            {QStringLiteral("PasswordMode"), d_ptr->service->passwordMode()},
            {QStringLiteral("PasswordHint"), d_ptr->service->passwordHint()},
            {QStringLiteral("LocalAccount"), d_ptr->service->localAccount()},
            {QStringLiteral("SystemAccount"), d_ptr->service->systemAccount()},
            {QStringLiteral("UserName"), d_ptr->service->userName()},
            {QStringLiteral("RealName"), d_ptr->service->realName()},
            {QStringLiteral("HomeDirectory"), d_ptr->service->homeDirectory()},
            {QStringLiteral("Shell"), d_ptr->service->shell()},
            {QStringLiteral("IconFile"), d_ptr->service->iconFile()},
            {QStringLiteral("Email"), d_ptr->service->email()},
            {QStringLiteral("Language"), d_ptr->service->language()},
            {QStringLiteral("Location"), d_ptr->service->location()},
            {QStringLiteral("XSession"), d_ptr->service->xSession()}
        };
    } else {
        updated = reply.value();
    }
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

    bool changed = false;
    bool displayNameChanged = false;
    for (const Change &entry : changes) {
        const QString key = QString::fromLatin1(entry.property);
        if (previous.value(key) == updated.value(key))
            continue;
        changed = true;
        Q_EMIT (this->*entry.signal)();
        if (key == QStringLiteral("Uid"))
            Q_EMIT groupIdChanged();
        if (key == QStringLiteral("UserName") || key == QStringLiteral("RealName"))
            displayNameChanged = true;
    }
    if (displayNameChanged)
        Q_EMIT this->displayNameChanged();
    if (changed)
        Q_EMIT accountChanged();
}

void UserAccount::watch(const QDBusPendingCall &call)
{
    auto *watcher = new QDBusPendingCallWatcher(call, this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this](QDBusPendingCallWatcher *finished) {
                QDBusPendingReply<> reply = *finished;
                finished->deleteLater();
                if (reply.isError()) {
                    qWarning("AccountsService update failed: %s",
                             reply.error().message().toUtf8().constData());
                    refresh();
                    return;
                }
                refresh();
                Q_EMIT accountChanged();
            });
}

void UserAccount::notePendingChange(const char *property, const QVariant &value,
                                    void (UserAccount::*notify)(), bool displayName)
{
    d_ptr->values.insert(QString::fromLatin1(property), value);
    Q_EMIT (this->*notify)();
    if (displayName)
        Q_EMIT displayNameChanged();
}

qlonglong UserAccount::userId() const
{
    return d_ptr->service->uid();
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
    return static_cast<AccountType>(d_ptr->service->accountType());
}

void UserAccount::setAccountType(AccountType type)
{
    if (accountType() == type)
        return;
    watch(d_ptr->service->SetAccountType(static_cast<int>(type)));
    notePendingChange("AccountType", static_cast<int>(type), &UserAccount::accountTypeChanged);
}

bool UserAccount::isLocked() const
{
    return d_ptr->service->locked();
}

void UserAccount::setLocked(bool locked)
{
    if (isLocked() == locked)
        return;
    watch(d_ptr->service->SetLocked(locked));
    notePendingChange("Locked", locked, &UserAccount::lockedChanged);
}

bool UserAccount::automaticLogin() const
{
    return d_ptr->service->automaticLogin();
}

void UserAccount::setAutomaticLogin(bool enabled)
{
    if (automaticLogin() == enabled)
        return;
    watch(d_ptr->service->SetAutomaticLogin(enabled));
    notePendingChange("AutomaticLogin", enabled, &UserAccount::automaticLoginChanged);
}

qlonglong UserAccount::loginFrequency() const
{
    return d_ptr->service->loginFrequency();
}

qlonglong UserAccount::loginTime() const
{
    return d_ptr->service->loginTime();
}

UserAccount::PasswordMode UserAccount::passwordMode() const
{
    return static_cast<PasswordMode>(d_ptr->service->passwordMode());
}

void UserAccount::setPasswordMode(PasswordMode mode)
{
    if (passwordMode() == mode)
        return;
    watch(d_ptr->service->SetPasswordMode(static_cast<int>(mode)));
    notePendingChange("PasswordMode", static_cast<int>(mode), &UserAccount::passwordModeChanged);
}

QString UserAccount::passwordHint() const
{
    return d_ptr->service->passwordHint();
}

bool UserAccount::isLocalAccount() const
{
    return d_ptr->service->localAccount();
}

bool UserAccount::isSystemAccount() const
{
    return d_ptr->service->systemAccount();
}

QString UserAccount::userName() const
{
    return d_ptr->service->userName();
}

void UserAccount::setUserName(const QString &name)
{
    if (userName() == name)
        return;
    watch(d_ptr->service->SetUserName(name));
    notePendingChange("UserName", name, &UserAccount::userNameChanged, true);
}

QString UserAccount::realName() const
{
    return d_ptr->service->realName();
}

void UserAccount::setRealName(const QString &name)
{
    if (realName() == name)
        return;
    watch(d_ptr->service->SetRealName(name));
    notePendingChange("RealName", name, &UserAccount::realNameChanged, true);
}

QString UserAccount::displayName() const
{
    const QString real = realName();
    return real.isEmpty() ? userName() : real;
}

QString UserAccount::homeDirectory() const
{
    return d_ptr->service->homeDirectory();
}

void UserAccount::setHomeDirectory(const QString &path)
{
    if (homeDirectory() == path)
        return;
    watch(d_ptr->service->SetHomeDirectory(path));
    notePendingChange("HomeDirectory", path, &UserAccount::homeDirectoryChanged);
}

QString UserAccount::shell() const
{
    return d_ptr->service->shell();
}

void UserAccount::setShell(const QString &path)
{
    if (shell() == path)
        return;
    watch(d_ptr->service->SetShell(path));
    notePendingChange("Shell", path, &UserAccount::shellChanged);
}

QString UserAccount::iconFileName() const
{
    const QString path = d_ptr->service->iconFile();
    return QFile::exists(path) ? path : QString();
}

void UserAccount::setIconFileName(const QString &path)
{
    if (iconFileName() == path)
        return;
    watch(d_ptr->service->SetIconFile(path));
    notePendingChange("IconFile", path, &UserAccount::iconFileNameChanged);
}

QString UserAccount::email() const
{
    return d_ptr->service->email();
}

void UserAccount::setEmail(const QString &address)
{
    if (email() == address)
        return;
    watch(d_ptr->service->SetEmail(address));
    notePendingChange("Email", address, &UserAccount::emailChanged);
}

QString UserAccount::language() const
{
    return d_ptr->service->language();
}

void UserAccount::setLanguage(const QString &locale)
{
    if (language() == locale)
        return;
    watch(d_ptr->service->SetLanguage(locale));
    notePendingChange("Language", locale, &UserAccount::languageChanged);
}

QString UserAccount::location() const
{
    return d_ptr->service->location();
}

void UserAccount::setLocation(const QString &place)
{
    if (location() == place)
        return;
    watch(d_ptr->service->SetLocation(place));
    notePendingChange("Location", place, &UserAccount::locationChanged);
}

QString UserAccount::xsession() const
{
    return d_ptr->service->xSession();
}

void UserAccount::setXSession(const QString &session)
{
    if (xsession() == session)
        return;
    watch(d_ptr->service->SetXSession(session));
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
