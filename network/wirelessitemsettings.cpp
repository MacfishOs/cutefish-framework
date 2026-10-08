#include "wirelessitemsettings.h"
#include "networkclient.h"

#include <QDebug>

using namespace NetworkDBus;

static const QString connectionSetting = QStringLiteral("connection");

WirelessItemSettings::WirelessItemSettings(QObject *parent)
    : QObject(parent)
    , m_client(NetworkClient::instance())
{
    connect(m_client, &NetworkClient::changed, this, &WirelessItemSettings::refresh);
}

void WirelessItemSettings::setPath(const QString &path)
{
    if (m_path == path)
        return;
    m_path = path;
    m_pending.clear();
    Q_EMIT pathChanged();
    refresh();
}

QString WirelessItemSettings::name() const
{
    return m_name;
}

void WirelessItemSettings::setName(const QString &name)
{
    if (name == m_name || !m_client->readable(m_path))
        return;
    m_name = name;
    update(QStringLiteral("id"), name);
    Q_EMIT nameChanged();
}

bool WirelessItemSettings::autoConnect() const
{
    return m_autoConnect;
}

void WirelessItemSettings::setAutoConnect(bool value)
{
    if (value == m_autoConnect || !m_client->readable(m_path))
        return;
    m_autoConnect = value;
    update(QStringLiteral("autoconnect"), value);
    Q_EMIT autoConnectChanged();
}

bool WirelessItemSettings::availableToOtherUsers() const
{
    return m_availableToOtherUsers;
}

void WirelessItemSettings::setAvailableToOtherUsers(bool value)
{
    if (value == m_availableToOtherUsers || !m_client->readable(m_path))
        return;
    m_availableToOtherUsers = value;
    update(QStringLiteral("permissions"),
           value ? QStringList() : QStringList {NetworkClient::userPermission()});
    Q_EMIT availableToOtherUsersChanged();
}

void WirelessItemSettings::update(const QString &key, const QVariant &value)
{
    // Without secrets in it, an update keeps the ones NetworkManager and the keyring hold.
    NMVariantMapMap settings = m_client->settings(m_path);
    settings[connectionSetting].insert(key, value);
    m_pending.insert(key, value);
    const QString path = m_path;
    m_client->call(m_path, connectionInterface, QStringLiteral("Update"), {QVariant::fromValue(settings)}, this,
                   [this, path, key, value](const QDBusMessage &reply) {
                       if (reply.type() != QDBusMessage::ErrorMessage)
                           return;
                       qWarning() << "Cannot update the network:" << reply.errorMessage();
                       // The profile keeps its old value, so show that again.
                       if (path == m_path && m_pending.value(key) == value) {
                           m_pending.remove(key);
                           refresh();
                       }
                   },
                   2 * 60 * 1000);
}

// The cache is re-read after NetworkManager reports the profile updated, which
// comes after any unrelated change. Until then a written value is the truth.
QVariant WirelessItemSettings::cached(const QString &key, const QVariant &defaultValue)
{
    const QVariant value = m_client->setting(m_path, connectionSetting, key);
    const auto pending = m_pending.constFind(key);
    if (pending == m_pending.constEnd())
        return value;
    // NetworkManager leaves out a value that is the default.
    if ((value.isValid() ? value : defaultValue) != pending.value())
        return pending.value();
    m_pending.erase(pending);
    return value;
}

void WirelessItemSettings::refresh()
{
    if (!m_client->readable(m_path))
        return;

    const QString name = cached(QStringLiteral("id")).toString();
    const QVariant autoConnect = cached(QStringLiteral("autoconnect"), true);
    const bool available = cached(QStringLiteral("permissions"), QStringList()).toStringList().isEmpty();

    if (name != m_name) {
        m_name = name;
        Q_EMIT nameChanged();
    }
    // NetworkManager leaves out a value that is the default.
    const bool automatic = !autoConnect.isValid() || autoConnect.toBool();
    if (automatic != m_autoConnect) {
        m_autoConnect = automatic;
        Q_EMIT autoConnectChanged();
    }
    if (available != m_availableToOtherUsers) {
        m_availableToOtherUsers = available;
        Q_EMIT availableToOtherUsersChanged();
    }
}
