#include "activeconnection.h"
#include "networkclient.h"
#include "wifiutils.h"

using namespace NetworkDBus;

ActiveConnection::ActiveConnection(QObject *parent)
    : QObject(parent)
    , m_client(NetworkClient::instance())
{
    connect(m_client, &NetworkClient::changed, this, &ActiveConnection::update);
    update();
}

void ActiveConnection::update()
{
    // The Wi-Fi connection that is up, the primary one if there are several; a hotspot
    // is not a network this machine is on.
    const QString primary = NetworkClient::path(m_client->manager(QStringLiteral("PrimaryConnection")));
    QString chosen;
    QString device;
    for (const QString &active : m_client->activeConnections()) {
        const auto property = [&](const char *name) {
            return m_client->property(active, activeInterface, QLatin1String(name));
        };
        const QStringList devices = property("Devices").toStringList();
        if (property("Type").toString() != QLatin1String("802-11-wireless")
            || property("State").toUInt() != ActiveActivated || devices.isEmpty()
            || m_client->property(devices.first(), wirelessInterface, QStringLiteral("Mode")).toUInt() == ModeAp)
            continue;
        if (chosen.isEmpty() || active == primary) {
            chosen = active;
            device = devices.first();
        }
    }

    QString name;
    QString icon;
    if (!chosen.isEmpty()) {
        const QString accessPoint = NetworkClient::path(
                m_client->property(device, wirelessInterface, QStringLiteral("ActiveAccessPoint")));
        const QByteArray ssid = m_client->property(accessPoint, accessPointInterface, QStringLiteral("Ssid")).toByteArray();
        // A hidden network's access point has no name of its own; its profile does.
        name = WifiUtils::ssidHidden(ssid)
                ? m_client->property(chosen, activeInterface, QStringLiteral("Id")).toString()
                : WifiUtils::ssidLabel(ssid);
        icon = WifiUtils::strengthIcon(
                m_client->property(accessPoint, accessPointInterface, QStringLiteral("Strength")).toInt());
    }

    if (name != m_wirelessName) {
        m_wirelessName = name;
        Q_EMIT wirelessNameChanged();
    }
    if (icon != m_wirelessIcon) {
        m_wirelessIcon = icon;
        Q_EMIT wirelessIconChanged();
    }
}
