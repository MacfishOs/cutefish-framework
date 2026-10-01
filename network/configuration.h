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

#ifndef CUTEFISH_NETWORK_CONFIGURATION_H
#define CUTEFISH_NETWORK_CONFIGURATION_H

#include <network_export.h>

#include <QObject>
#include <QtQml/qqmlregistration.h>

// The hotspot's name and password. Every instance in a process shows the same values,
// which live in NetworkManager's hotspot profile once it exists.
class CUTEFISH_NETWORK_EXPORT Configuration : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString hotspotName READ hotspotName WRITE setHotspotName NOTIFY hotspotChanged)
    Q_PROPERTY(QString hotspotPassword READ hotspotPassword WRITE setHotspotPassword NOTIFY hotspotChanged)

public:
    explicit Configuration(QObject *parent = nullptr);

    QString hotspotName() const;
    void setHotspotName(const QString &name);
    QString hotspotPassword() const;
    void setHotspotPassword(const QString &password);

Q_SIGNALS:
    void hotspotChanged();
};

#endif // CUTEFISH_NETWORK_CONFIGURATION_H
