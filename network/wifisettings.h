/*
 *   Copyright 2018 Martin Kacej <m.kacej@atlas.sk>
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

#ifndef WIFISETTINGS_H
#define WIFISETTINGS_H

#include <network_export.h>

#include <QObject>
#include <QtQml/qqmlregistration.h>

class NetworkClient;

class CUTEFISH_NETWORK_EXPORT WifiSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT

public:
    explicit WifiSettings(QObject *parent = nullptr);

    // Joins a network that doesn't broadcast its name. `type` is one of "none", "wep",
    // "wpa", "wpa2-wpa3", "wpa3", "wpa-eap", "wpa2-eap" and "wpa3-eap".
    Q_INVOKABLE bool addOtherConnection(const QString &ssid, const QString &userName,
                                        const QString &password, const QString &type);

private:
    NetworkClient *m_client;
};

#endif // WIFISETTINGS_H
