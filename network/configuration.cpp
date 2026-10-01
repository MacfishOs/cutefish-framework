/*
    Copyright 2017 Jan Grulich <jgrulich@redhat.com>

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) version 3, or any
    later version accepted by the membership of KDE e.V. (or its
    successor approved by the membership of KDE e.V.), which shall
    act as a proxy defined in Section 6 of version 3 of the license.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "configuration.h"
#include "networkclient.h"
#include "wifiutils.h"

#include <QCoreApplication>
#include <QPointer>
#include <QSysInfo>

using namespace NetworkDBus;

namespace {

class HotspotSettings : public QObject
{
    Q_OBJECT

public:
    static HotspotSettings *instance()
    {
        static QPointer<HotspotSettings> settings;
        if (!settings)
            settings = new HotspotSettings(QCoreApplication::instance());
        return settings;
    }

    QString name() const
    {
        if (!m_name.isEmpty())
            return m_name;
        const QString profile = connection();
        if (!profile.isEmpty()) {
            return WifiUtils::ssidLabel(
                    m_client->setting(profile, QStringLiteral("802-11-wireless"), QStringLiteral("ssid")).toByteArray());
        }
        const QString host = QSysInfo::machineHostName();
        return host.isEmpty() || host == QLatin1String("localhost") ? QObject::tr("Hotspot") : host;
    }

    QString password() const
    {
        if (!m_password.isEmpty())
            return m_password;
        return m_profilePassword.isEmpty() ? m_generatedPassword : m_profilePassword;
    }

    void setName(const QString &name)
    {
        if (name == this->name())
            return;
        m_name = name;
        Q_EMIT changed();
    }

    void setPassword(const QString &password)
    {
        if (password == this->password())
            return;
        m_password = password;
        Q_EMIT changed();
    }

Q_SIGNALS:
    void changed();

private:
    explicit HotspotSettings(QObject *parent)
        : QObject(parent)
        , m_client(NetworkClient::instance())
        , m_generatedPassword(WifiUtils::generatedPassword())
    {
        connect(m_client, &NetworkClient::readyChanged, this, &HotspotSettings::load);
        load();
    }

    QString connection() const
    {
        return m_client->hotspotConnection();
    }

    // The profile keeps its password with NetworkManager, which reads it out only on request.
    void load()
    {
        const QString profile = connection();
        if (profile.isEmpty())
            return;

        Q_EMIT changed();
        m_client->call(profile, connectionInterface, QStringLiteral("GetSecrets"),
                       {QStringLiteral("802-11-wireless-security")}, this,
                       [this](const QDBusMessage &reply) {
                           if (reply.type() != QDBusMessage::ReplyMessage)
                               return;
                           const QVariantMap security = NetworkDBus::plain(reply.arguments().value(0))
                                                                .toMap()
                                                                .value(QStringLiteral("802-11-wireless-security"))
                                                                .toMap();
                           const QString password = security.value(QStringLiteral("psk"),
                                                                   security.value(QStringLiteral("wep-key0")))
                                                            .toString();
                           if (password.isEmpty() || password == m_profilePassword)
                               return;
                           m_profilePassword = password;
                           Q_EMIT changed();
                       });
    }

    NetworkClient *m_client;
    QString m_name;
    QString m_password;
    QString m_profilePassword;
    const QString m_generatedPassword;
};

} // namespace

Configuration::Configuration(QObject *parent)
    : QObject(parent)
{
    connect(HotspotSettings::instance(), &HotspotSettings::changed, this, &Configuration::hotspotChanged);
}

QString Configuration::hotspotName() const
{
    return HotspotSettings::instance()->name();
}

void Configuration::setHotspotName(const QString &name)
{
    HotspotSettings::instance()->setName(name);
}

QString Configuration::hotspotPassword() const
{
    return HotspotSettings::instance()->password();
}

void Configuration::setHotspotPassword(const QString &password)
{
    HotspotSettings::instance()->setPassword(password);
}

#include "configuration.moc"
