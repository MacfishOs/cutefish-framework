/*
 *   Copyright 2018 Martin Kacej <m.kacej@atlas.sk>
 *   Copyright 2021 Wang Rui <wangrui@jingos.com>
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU Library General Public License as
 *   published by the Free Software Foundation; either version 2 or
 *   (at your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU Library General Public License for more details
 *
 *   You should have received a copy of the GNU Library General Public
 *   License along with this program; if not, write to the
 *   Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include "wifisettings.h"
#include "networkclient.h"

#include <QDebug>
#include <QUuid>

using namespace NetworkDBus;

WifiSettings::WifiSettings(QObject *parent)
    : QObject(parent)
    , m_client(NetworkClient::instance())
{
}

bool WifiSettings::addOtherConnection(const QString &ssid, const QString &userName,
                                      const QString &password, const QString &type)
{
    const QByteArray ssidBytes = ssid.toUtf8();
    if (ssidBytes.isEmpty() || ssidBytes.size() > 32)
        return false;

    // Shared like a network joined from the list; see Handler::addAndActivateWireless.
    const bool shared = m_client->sharesNewConnections();
    const uint secretFlags = shared ? SecretSystemOwned : SecretAgentOwned;

    QVariantMap connection {
        {QStringLiteral("id"), ssid},
        {QStringLiteral("uuid"), QUuid::createUuid().toString(QUuid::WithoutBraces)},
        {QStringLiteral("type"), QStringLiteral("802-11-wireless")},
    };
    if (!shared)
        connection.insert(QStringLiteral("permissions"), QStringList {NetworkClient::userPermission()});

    NMVariantMapMap settings;
    settings.insert(QStringLiteral("connection"), connection);
    // Nothing can be learnt from an access point that hides its name, so everything is spelt out.
    settings.insert(QStringLiteral("802-11-wireless"),
                    {{QStringLiteral("ssid"), ssidBytes},
                     {QStringLiteral("mode"), QStringLiteral("infrastructure")},
                     {QStringLiteral("hidden"), true}});
    settings.insert(QStringLiteral("ipv4"), {{QStringLiteral("method"), QStringLiteral("auto")}});

    QVariantMap security;
    if (type == QLatin1String("wep")) {
        // The "key" type takes both 5/13-character ASCII and 10/26-digit hex keys.
        security = {{QStringLiteral("key-mgmt"), QStringLiteral("none")},
                    {QStringLiteral("auth-alg"), QStringLiteral("open")},
                    {QStringLiteral("wep-key-type"), 1u},
                    {QStringLiteral("wep-tx-keyidx"), 0u},
                    {QStringLiteral("wep-key0"), password},
                    {QStringLiteral("wep-key-flags"), secretFlags}};
    } else if (type == QLatin1String("wpa") || type == QLatin1String("wpa2-wpa3")) {
        // wpa-psk also joins WPA2/WPA3 transition networks.
        security = {{QStringLiteral("key-mgmt"), QStringLiteral("wpa-psk")},
                    {QStringLiteral("proto"),
                     QStringList {type == QLatin1String("wpa") ? QStringLiteral("wpa") : QStringLiteral("rsn")}},
                    {QStringLiteral("psk"), password},
                    {QStringLiteral("psk-flags"), secretFlags}};
    } else if (type == QLatin1String("wpa3")) {
        security = {{QStringLiteral("key-mgmt"), QStringLiteral("sae")},
                    {QStringLiteral("psk"), password},
                    {QStringLiteral("psk-flags"), secretFlags}};
    } else if (type.endsWith(QLatin1String("-eap"))) {
        security = {{QStringLiteral("key-mgmt"), QStringLiteral("wpa-eap")},
                    {QStringLiteral("proto"),
                     QStringList {type == QLatin1String("wpa-eap") ? QStringLiteral("wpa") : QStringLiteral("rsn")}}};
        // NM_SETTING_WIRELESS_SECURITY_PMF_REQUIRED, an int32 property.
        if (type == QLatin1String("wpa3-eap"))
            security.insert(QStringLiteral("pmf"), 3);

        // A user name and password is PEAP or TTLS with MSCHAPv2 inside, whichever
        // the server offers.
        settings.insert(QStringLiteral("802-1x"),
                        {{QStringLiteral("eap"), QStringList {QStringLiteral("peap"), QStringLiteral("ttls")}},
                         {QStringLiteral("phase2-auth"), QStringLiteral("mschapv2")},
                         {QStringLiteral("identity"), userName},
                         {QStringLiteral("password"), password},
                         {QStringLiteral("password-flags"), secretFlags}});
    }
    if (!security.isEmpty())
        settings.insert(QStringLiteral("802-11-wireless-security"), security);

    QString device;
    for (const QString &candidate : m_client->devices()) {
        if (m_client->deviceType(candidate) == DeviceWifi
            && m_client->deviceState(candidate) >= DeviceDisconnected
            && m_client->property(candidate, wirelessInterface, QStringLiteral("Mode")).toUInt() != ModeAp) {
            device = candidate;
            break;
        }
    }

    const auto report = [](const QDBusMessage &reply) {
        if (reply.type() == QDBusMessage::ErrorMessage)
            qWarning() << "Cannot add the network:" << reply.errorMessage();
    };

    // With no adapter to join it on now, the profile waits for one.
    if (device.isEmpty()) {
        m_client->call(settingsPath, settingsInterface, QStringLiteral("AddConnection"),
                       {QVariant::fromValue(settings)}, this, report, 2 * 60 * 1000);
        return true;
    }

    // A hidden network answers only probes that name it.
    m_client->call(device, wirelessInterface, QStringLiteral("RequestScan"),
                   {QVariantMap {{QStringLiteral("ssids"), QVariant::fromValue(QList<QByteArray> {ssidBytes})}}},
                   this);
    m_client->call(managerPath, managerInterface, QStringLiteral("AddAndActivateConnection"),
                   {QVariant::fromValue(settings), QVariant::fromValue(QDBusObjectPath(device)),
                    QVariant::fromValue(QDBusObjectPath(QStringLiteral("/")))},
                   this, report, 2 * 60 * 1000);
    return true;
}
