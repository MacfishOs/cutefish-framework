#include "wirelessitemsettings.h"
#include "networkclient.h"

#include <QDebug>

using namespace NetworkDBus;

namespace {

const QString connectionSetting = QStringLiteral("connection");

} // namespace

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
    m_client->call(m_path, connectionInterface, QStringLiteral("Update"), {QVariant::fromValue(settings)}, this,
                   [](const QDBusMessage &reply) {
                       if (reply.type() == QDBusMessage::ErrorMessage)
                           qWarning() << "Cannot update the network:" << reply.errorMessage();
                   },
                   2 * 60 * 1000);
}

void WirelessItemSettings::refresh()
{
    if (!m_client->readable(m_path))
        return;

    const QString name = m_client->setting(m_path, connectionSetting, QStringLiteral("id")).toString();
    const QVariant autoConnect = m_client->setting(m_path, connectionSetting, QStringLiteral("autoconnect"));
    const bool available = m_client->setting(m_path, connectionSetting, QStringLiteral("permissions")).toStringList().isEmpty();

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
