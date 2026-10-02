#include "usersmodel.h"
#include "usersmodel_p.h"

#include <QtGui/QPixmap>

namespace QtAccountsService {

UsersModelPrivate::UsersModelPrivate(UsersModel *model, const QDBusConnection &bus)
    : manager(new AccountsManager(bus, model))
{
}

UsersModel::UsersModel(QObject *parent)
    : UsersModel(QDBusConnection::systemBus(), parent)
{
}

UsersModel::UsersModel(const QDBusConnection &bus, QObject *parent)
    : QAbstractListModel(parent)
    , d_ptr(new UsersModelPrivate(this, bus))
{
    connect(d_ptr->manager, &AccountsManager::userAdded,
            this, &UsersModel::_q_userAdded);
    connect(d_ptr->manager, &AccountsManager::userDeleted,
            this, &UsersModel::_q_userDeleted);
    connect(d_ptr->manager, &AccountsManager::listCachedUsersFinished, this,
            [this](const UserAccountList &users) {
                for (UserAccount *user : users)
                    _q_userAdded(user);
            });
    d_ptr->manager->listCachedUsers();
}

void UsersModel::_q_userAdded(UserAccount *account)
{
    if (!account)
        return;
    for (const UserRow &row : d_ptr->rows) {
        if (row.account == account)
            return;
    }

    const qlonglong uid = account->userId();
    const int position = d_ptr->rows.size();
    beginInsertRows(QModelIndex(), position, position);
    d_ptr->rows.append({account, uid});
    endInsertRows();

    connect(account, &UserAccount::accountChanged, this, [this, account]() {
        for (int row = 0; row < d_ptr->rows.size(); ++row) {
            if (d_ptr->rows.at(row).account == account) {
                Q_EMIT dataChanged(index(row), index(row));
                return;
            }
        }
    });
    connect(account, &QObject::destroyed, this, [this, uid]() {
        for (int row = 0; row < d_ptr->rows.size(); ++row) {
            const UserRow &entry = d_ptr->rows.at(row);
            if (entry.uid == uid && entry.account.isNull()) {
                beginRemoveRows(QModelIndex(), row, row);
                d_ptr->rows.removeAt(row);
                endRemoveRows();
                return;
            }
        }
    });
}

void UsersModel::_q_userDeleted(qlonglong uid)
{
    for (int row = d_ptr->rows.size() - 1; row >= 0; --row) {
        if (d_ptr->rows.at(row).uid == uid) {
            beginRemoveRows(QModelIndex(), row, row);
            d_ptr->rows.removeAt(row);
            endRemoveRows();
        }
    }
}

UsersModel::~UsersModel()
{
    for (const UserRow &row : d_ptr->rows) {
        if (row.account)
            disconnect(row.account, nullptr, this, nullptr);
    }
    delete d_ptr;
}

QHash<int, QByteArray> UsersModel::roleNames() const
{
    QHash<int, QByteArray> roles = QAbstractItemModel::roleNames();
    const QHash<int, QByteArray> accountRoles = {
        {UserAccountRole, "userAccount"},
        {UserIdRole, "userId"},
        {AccountTypeRole, "accountType"},
        {LockedRole, "locked"},
        {AutomaticLoginRole, "automaticLogin"},
        {LoginFrequencyRole, "loginFrequency"},
        {LoginTimeRole, "loginTime"},
        {PasswordModeRole, "passwordMode"},
        {PasswordHintRole, "passwordHint"},
        {LocalAccount, "localAccount"},
        {SystemAccount, "systemAccount"},
        {UserNameRole, "userName"},
        {RealNameRole, "realName"},
        {DisplayNameRole, "displayNameRole"},
        {HomeDirectoryRole, "homeDirectory"},
        {ShellRole, "shell"},
        {IconFileNameRole, "iconFileName"},
        {EmailRole, "email"},
        {LanguageRole, "language"},
        {LocationRole, "location"},
        {XSessionRole, "xsession"}
    };
    for (auto it = accountRoles.cbegin(); it != accountRoles.cend(); ++it)
        roles.insert(it.key(), it.value());
    return roles;
}

int UsersModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : d_ptr->rows.size();
}

UserAccount *UsersModel::userAccount(const QModelIndex &index) const
{
    if (!index.isValid() || index.model() != this || index.column() != 0
            || index.row() < 0 || index.row() >= d_ptr->rows.size())
        return nullptr;
    return d_ptr->rows.at(index.row()).account;
}

QVariant UsersModel::data(const QModelIndex &index, int role) const
{
    UserAccount *account = userAccount(index);
    if (!account)
        return {};

    switch (role) {
    case Qt::DisplayRole:
    case DisplayNameRole:
        return account->displayName();
    case Qt::DecorationRole:
        return QPixmap(account->iconFileName());
    case UserAccountRole:
        return QVariant::fromValue(account);
    case UserIdRole:
        return account->userId();
    case AccountTypeRole:
        return account->accountType();
    case LockedRole:
        return account->isLocked();
    case AutomaticLoginRole:
        return account->automaticLogin();
    case LoginFrequencyRole:
        return account->loginFrequency();
    case LoginTimeRole:
        return account->loginTime();
    case PasswordModeRole:
        return account->passwordMode();
    case PasswordHintRole:
        return account->passwordHint();
    case LocalAccount:
        return account->isLocalAccount();
    case SystemAccount:
        return account->isSystemAccount();
    case UserNameRole:
        return account->userName();
    case RealNameRole:
        return account->realName();
    case HomeDirectoryRole:
        return account->homeDirectory();
    case ShellRole:
        return account->shell();
    case IconFileNameRole:
        return account->iconFileName();
    case EmailRole:
        return account->email();
    case LanguageRole:
        return account->language();
    case LocationRole:
        return account->location();
    case XSessionRole:
        return account->xsession();
    default:
        return {};
    }
}

bool UsersModel::setData(const QModelIndex &index, const QVariant &value, int role)
{
    UserAccount *account = userAccount(index);
    if (!account)
        return false;

    switch (role) {
    case UserNameRole:
        account->setUserName(value.toString());
        return true;
    case RealNameRole:
        account->setRealName(value.toString());
        return true;
    case IconFileNameRole:
        account->setIconFileName(value.toString());
        return true;
    case AccountTypeRole:
        account->setAccountType(static_cast<UserAccount::AccountType>(value.toInt()));
        return true;
    case LanguageRole:
        account->setLanguage(value.toString());
        return true;
    default:
        return false;
    }
}

}

#include "moc_usersmodel.cpp"
