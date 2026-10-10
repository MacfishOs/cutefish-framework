#ifndef CUTEFISH_USERACCOUNT_H
#define CUTEFISH_USERACCOUNT_H

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariant>
#include <QtDBus/QDBusConnection>
#include <QtDBus/QDBusPendingCall>

namespace QtAccountsService {

class AccountsManagerPrivate;
class UserAccountPrivate;

class UserAccount : public QObject
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(UserAccount)
    Q_PROPERTY(qlonglong userId READ userId WRITE setUserId NOTIFY userIdChanged)
    Q_PROPERTY(qlonglong groupId READ groupId NOTIFY groupIdChanged)
    Q_PROPERTY(AccountType accountType READ accountType WRITE setAccountType NOTIFY accountTypeChanged)
    Q_PROPERTY(bool locked READ isLocked WRITE setLocked NOTIFY lockedChanged)
    Q_PROPERTY(bool automaticLogin READ automaticLogin WRITE setAutomaticLogin NOTIFY automaticLoginChanged)
    Q_PROPERTY(qlonglong loginFrequency READ loginFrequency NOTIFY accountChanged)
    Q_PROPERTY(qlonglong loginTime READ loginTime NOTIFY accountChanged)
    Q_PROPERTY(PasswordMode passwordMode READ passwordMode WRITE setPasswordMode NOTIFY passwordModeChanged)
    Q_PROPERTY(QString passwordHint READ passwordHint NOTIFY accountChanged)
    Q_PROPERTY(bool localAccount READ isLocalAccount NOTIFY accountChanged)
    Q_PROPERTY(bool systemAccount READ isSystemAccount NOTIFY accountChanged)
    Q_PROPERTY(QString userName READ userName WRITE setUserName NOTIFY userNameChanged)
    Q_PROPERTY(QString realName READ realName WRITE setRealName NOTIFY realNameChanged)
    Q_PROPERTY(QString displayName READ displayName NOTIFY displayNameChanged)
    Q_PROPERTY(QString homeDirectory READ homeDirectory WRITE setHomeDirectory NOTIFY homeDirectoryChanged)
    Q_PROPERTY(QString shell READ shell WRITE setShell NOTIFY shellChanged)
    Q_PROPERTY(QString iconFileName READ iconFileName WRITE setIconFileName NOTIFY iconFileNameChanged)
    Q_PROPERTY(QString email READ email WRITE setEmail NOTIFY emailChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString location READ location WRITE setLocation NOTIFY locationChanged)
    Q_PROPERTY(QString xsession READ xsession WRITE setXSession NOTIFY xsessionChanged)

public:
    enum AccountType { StandardAccountType = 0, AdministratorAccountType };
    Q_ENUM(AccountType)

    enum PasswordMode { RegularPasswordMode = 0, SetAtLoginPasswordMode, NonePasswordMode };
    Q_ENUM(PasswordMode)

    explicit UserAccount(const QDBusConnection &bus = QDBusConnection::systemBus(),
                         QObject *parent = nullptr);
    ~UserAccount() override;

    qlonglong userId() const;
    void setUserId(qlonglong uid);
    qlonglong groupId() const;
    AccountType accountType() const;
    void setAccountType(AccountType type);
    bool isLocked() const;
    void setLocked(bool locked);
    bool automaticLogin() const;
    void setAutomaticLogin(bool enabled);
    qlonglong loginFrequency() const;
    qlonglong loginTime() const;
    PasswordMode passwordMode() const;
    void setPasswordMode(PasswordMode mode);
    QString passwordHint() const;
    bool isLocalAccount() const;
    bool isSystemAccount() const;
    QString userName() const;
    void setUserName(const QString &name);
    QString realName() const;
    void setRealName(const QString &name);
    QString displayName() const;
    QString homeDirectory() const;
    void setHomeDirectory(const QString &path);
    QString shell() const;
    void setShell(const QString &path);
    QString iconFileName() const;
    void setIconFileName(const QString &path);
    QString email() const;
    void setEmail(const QString &address);
    QString language() const;
    void setLanguage(const QString &locale);
    QString location() const;
    void setLocation(const QString &place);
    QString xsession() const;
    void setXSession(const QString &session);
    Q_INVOKABLE void setPassword(const QString &password, const QString &hint = QString());
    Q_INVOKABLE void setPasswordHint(const QString &hint);

Q_SIGNALS:
    void accountChanged();
    void userIdChanged();
    void groupIdChanged();
    void accountTypeChanged();
    void lockedChanged();
    void automaticLoginChanged();
    void passwordModeChanged();
    void userNameChanged();
    void realNameChanged();
    void displayNameChanged();
    void homeDirectoryChanged();
    void shellChanged();
    void iconFileNameChanged();
    void emailChanged();
    void languageChanged();
    void locationChanged();
    void xsessionChanged();

private:
    friend class AccountsManagerPrivate;

    UserAccount(const QString &objectPath, const QDBusConnection &bus, QObject *parent);
    void setObjectPath(const QString &path);
    void notifyAll();
    void refresh();
    void watch(const QDBusPendingCall &call, void (UserAccount::*notify)() = nullptr,
               bool displayName = false);
    void notePendingChange(const char *property, const QVariant &value,
                           void (UserAccount::*notify)(), bool displayName = false);
    void applyValues(const QVariantMap &reply);
    QVariant value(const char *property) const;

    UserAccountPrivate *const d_ptr;
};

using UserAccountList = QList<UserAccount *>;

}

#endif
