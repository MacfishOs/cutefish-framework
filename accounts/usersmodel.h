#ifndef CUTEFISH_USERSMODEL_H
#define CUTEFISH_USERSMODEL_H

#include <QtCore/QAbstractListModel>
#include <QtDBus/QDBusConnection>

namespace QtAccountsService {

class UserAccount;
class UsersModelPrivate;

class UsersModel : public QAbstractListModel
{
    Q_OBJECT
    Q_DECLARE_PRIVATE(UsersModel)
public:
    enum Roles {
        UserAccountRole = Qt::UserRole + 1,
        UserIdRole,
        AccountTypeRole,
        LockedRole,
        AutomaticLoginRole,
        LoginFrequencyRole,
        LoginTimeRole,
        PasswordModeRole,
        PasswordHintRole,
        LocalAccount,
        SystemAccount,
        UserNameRole,
        RealNameRole,
        DisplayNameRole,
        HomeDirectoryRole,
        ShellRole,
        IconFileNameRole,
        EmailRole,
        LanguageRole,
        LocationRole,
        XSessionRole
    };
    Q_ENUM(Roles)

    explicit UsersModel(QObject *parent = nullptr);
    explicit UsersModel(const QDBusConnection &bus, QObject *parent = nullptr);
    ~UsersModel() override;

    QHash<int, QByteArray> roleNames() const override;
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    UserAccount *userAccount(const QModelIndex &index) const;

private:
    UsersModelPrivate *const d_ptr;

private Q_SLOTS:
    void _q_userAdded(UserAccount *account);
    void _q_userDeleted(qlonglong uid);
};

}

#endif
