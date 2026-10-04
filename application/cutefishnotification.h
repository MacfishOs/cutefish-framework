// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef CUTEFISH_CUTEFISHNOTIFICATION_H
#define CUTEFISH_CUTEFISHNOTIFICATION_H

#include <QObject>
#include <QString>
#include <QStringList>

namespace Cutefish {

// One notification on the session's notification server; show() again replaces it.
class Notification : public QObject
{
    Q_OBJECT

public:
    enum Urgency { Low, Normal, Critical };
    Q_ENUM(Urgency)

    explicit Notification(QObject *parent = nullptr);

    // The shell titles a notification with its desktop entry's name and files it
    // under that application; an app name is only for senders without an entry.
    void setDesktopEntry(const QString &id);
    void setAppName(const QString &name);
    void setIconName(const QString &name);
    void setImagePath(const QString &path);
    void setSummary(const QString &summary);
    void setBody(const QString &body);
    // Critical stays until it is dismissed.
    void setUrgency(Urgency urgency);
    // In milliseconds; -1 leaves it to the server, 0 keeps it until dismissed.
    void setTimeout(int timeout);
    // "default" is a click on the notification itself and gets no button.
    void addAction(const QString &key, const QString &label = QString());
    void clearActions();
    // Deleted once closed, or once shown when it has no actions to report.
    void setAutoDelete(bool autoDelete);

    void show();
    void close();

Q_SIGNALS:
    void actionInvoked(const QString &key);
    // Dismissed, expired or revoked by the server; close() does not emit it.
    void closed();
    // No notification server answered.
    void failed();

private Q_SLOTS:
    void onActionInvoked(uint id, const QString &key);
    void onNotificationClosed(uint id, uint reason);

private:
    void send();
    void sendClose();

    QString m_desktopEntry;
    QString m_appName;
    QString m_iconName;
    QString m_imagePath;
    QString m_summary;
    QString m_body;
    QStringList m_actions;
    Urgency m_urgency = Normal;
    int m_timeout = -1;
    bool m_autoDelete = false;

    bool m_connected = false;
    // The server's id; a Notify in flight has none yet.
    uint m_id = 0;
    bool m_pending = false;
    bool m_showAgain = false;
    bool m_closeWhenShown = false;
};

} // namespace Cutefish

#endif // CUTEFISH_CUTEFISHNOTIFICATION_H
