// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cutefishnotification.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QVariantMap>

static const QString kService = QStringLiteral("org.freedesktop.Notifications");
static const QString kPath = QStringLiteral("/org/freedesktop/Notifications");

namespace Cutefish {

Notification::Notification(QObject *parent)
    : QObject(parent)
{
}

void Notification::setDesktopEntry(const QString &id)
{
    m_desktopEntry = id;
}

void Notification::setAppName(const QString &name)
{
    m_appName = name;
}

void Notification::setIconName(const QString &name)
{
    m_iconName = name;
}

void Notification::setImagePath(const QString &path)
{
    m_imagePath = path;
}

void Notification::setSummary(const QString &summary)
{
    m_summary = summary;
}

void Notification::setBody(const QString &body)
{
    m_body = body;
}

void Notification::setUrgency(Urgency urgency)
{
    m_urgency = urgency;
}

void Notification::setTimeout(int timeout)
{
    m_timeout = timeout;
}

void Notification::addAction(const QString &key, const QString &label)
{
    m_actions << key << label;
}

void Notification::clearActions()
{
    m_actions.clear();
}

void Notification::setAutoDelete(bool autoDelete)
{
    m_autoDelete = autoDelete;
}

void Notification::show()
{
    m_closeWhenShown = false;

    // The reply brings the id the new content has to replace.
    if (m_pending) {
        m_showAgain = true;
        return;
    }

    send();
}

void Notification::close()
{
    m_showAgain = false;

    if (m_pending) {
        m_closeWhenShown = true;
        return;
    }

    sendClose();

    if (m_autoDelete)
        deleteLater();
}

void Notification::onActionInvoked(uint id, const QString &key)
{
    if (id != 0 && id == m_id)
        Q_EMIT actionInvoked(key);
}

void Notification::onNotificationClosed(uint id, uint reason)
{
    Q_UNUSED(reason);

    if (id == 0 || id != m_id)
        return;

    // A later show() is a new notification.
    m_id = 0;
    Q_EMIT closed();

    if (m_autoDelete)
        deleteLater();
}

void Notification::send()
{
    QDBusConnection bus = QDBusConnection::sessionBus();

    if (!m_connected) {
        bus.connect(kService, kPath, kService, QStringLiteral("ActionInvoked"), this,
                    SLOT(onActionInvoked(uint, QString)));
        bus.connect(kService, kPath, kService, QStringLiteral("NotificationClosed"), this,
                    SLOT(onNotificationClosed(uint, uint)));
        m_connected = true;
    }

    QVariantMap hints;
    hints.insert(QStringLiteral("urgency"), QVariant::fromValue<uchar>(m_urgency));
    if (!m_desktopEntry.isEmpty())
        hints.insert(QStringLiteral("desktop-entry"), m_desktopEntry);
    if (!m_imagePath.isEmpty())
        hints.insert(QStringLiteral("image-path"), m_imagePath);

    QDBusMessage message = QDBusMessage::createMethodCall(kService, kPath, kService,
                                                          QStringLiteral("Notify"));
    // Only a running notification server shows it; none is started for it.
    message.setAutoStartService(false);
    message << m_appName << m_id << m_iconName << m_summary << m_body << m_actions << hints
            << m_timeout;

    m_pending = true;

    auto *watcher = new QDBusPendingCallWatcher(bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        m_pending = false;

        const QDBusPendingReply<uint> reply = *watcher;
        if (reply.isError()) {
            m_id = 0;
            m_showAgain = false;
            m_closeWhenShown = false;
            Q_EMIT failed();

            if (m_autoDelete)
                deleteLater();
            return;
        }

        // 0: the server dropped it, as the shell does with a repeat within a second.
        m_id = reply.value();

        if (m_closeWhenShown) {
            m_closeWhenShown = false;
            close();
        } else if (m_showAgain) {
            m_showAgain = false;
            send();
        } else if (m_autoDelete && (m_actions.isEmpty() || m_id == 0)) {
            deleteLater();
        }
    });
}

void Notification::sendClose()
{
    if (m_id == 0)
        return;

    QDBusMessage message = QDBusMessage::createMethodCall(kService, kPath, kService,
                                                          QStringLiteral("CloseNotification"));
    message.setAutoStartService(false);
    message << m_id;
    QDBusConnection::sessionBus().asyncCall(message);

    m_id = 0;
}

} // namespace Cutefish
