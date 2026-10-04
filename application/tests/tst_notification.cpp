// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "cutefishnotification.h"

#include <QDBusConnection>
#include <QDBusContext>
#include <QDBusMessage>
#include <QPointer>
#include <QSignalSpy>
#include <QtTest>

using Cutefish::Notification;

namespace {
const QString kService = QStringLiteral("org.freedesktop.Notifications");
const QString kPath = QStringLiteral("/org/freedesktop/Notifications");

// The fake lives on a connection of its own, so every call crosses the bus.
QDBusConnection fakeBus()
{
    return QDBusConnection::connectToBus(QDBusConnection::SessionBus, QStringLiteral("fake"));
}

struct Call {
    QString appName;
    uint replacesId;
    QString icon;
    QString summary;
    QString body;
    QStringList actions;
    QVariantMap hints;
    int timeout;
};
}

class FakeServer : public QObject, protected QDBusContext
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.freedesktop.Notifications")

public:
    QList<Call> calls;
    QList<uint> closed;
    bool fails = false;
    bool holdsReplies = false;
    QList<QDBusMessage> held;
    uint nextId = 1;

    void releaseReplies()
    {
        const QList<QDBusMessage> messages = std::exchange(held, {});
        for (const QDBusMessage &message : messages)
            fakeBus().send(message);
    }

    void emitSignal(const QString &name, const QVariantList &arguments)
    {
        QDBusMessage signal = QDBusMessage::createSignal(kPath, kService, name);
        signal.setArguments(arguments);
        fakeBus().send(signal);
    }

public slots:
    uint Notify(const QString &appName, uint replacesId, const QString &icon,
                const QString &summary, const QString &body, const QStringList &actions,
                const QVariantMap &hints, int timeout)
    {
        if (fails) {
            sendErrorReply(QDBusError::ServiceUnknown, QStringLiteral("No server"));
            return 0;
        }

        calls.append({appName, replacesId, icon, summary, body, actions, hints, timeout});
        const uint id = replacesId ? replacesId : nextId++;

        if (holdsReplies) {
            setDelayedReply(true);
            held.append(message().createReply(id));
        }

        return id;
    }

    void CloseNotification(uint id) { closed.append(id); }
};

class NotificationTest : public QObject
{
    Q_OBJECT

private:
    FakeServer m_server;

    // Lets a call that should not happen reach the fake first.
    static void settle() { QTest::qWait(150); }

private slots:
    void initTestCase()
    {
        QDBusConnection bus = fakeBus();
        QVERIFY(bus.isConnected());
        QVERIFY(bus.registerObject(kPath, &m_server, QDBusConnection::ExportAllSlots));
        QVERIFY(bus.registerService(kService));
    }

    void init()
    {
        m_server.calls.clear();
        m_server.closed.clear();
        m_server.fails = false;
        m_server.holdsReplies = false;
        m_server.held.clear();
        m_server.nextId = 1;
    }

    void sendsTheFields()
    {
        Notification notification;
        notification.setDesktopEntry(QStringLiteral("cutefish-settings"));
        notification.setIconName(QStringLiteral("battery-caution"));
        notification.setImagePath(QStringLiteral("/tmp/shot.png"));
        notification.setSummary(QStringLiteral("Summary"));
        notification.setBody(QStringLiteral("Body"));
        notification.setUrgency(Notification::Critical);
        notification.setTimeout(5000);
        notification.addAction(QStringLiteral("default"));
        notification.addAction(QStringLiteral("open"), QStringLiteral("Open"));
        notification.show();

        QTRY_COMPARE(m_server.calls.size(), 1);
        const Call call = m_server.calls.at(0);
        QCOMPARE(call.appName, QString());
        QCOMPARE(call.replacesId, 0u);
        QCOMPARE(call.icon, QStringLiteral("battery-caution"));
        QCOMPARE(call.summary, QStringLiteral("Summary"));
        QCOMPARE(call.body, QStringLiteral("Body"));
        QCOMPARE(call.actions, (QStringList {"default", "", "open", "Open"}));
        QCOMPARE(call.hints.value("desktop-entry").toString(), QStringLiteral("cutefish-settings"));
        QCOMPARE(call.hints.value("image-path").toString(), QStringLiteral("/tmp/shot.png"));
        QCOMPARE(call.hints.value("urgency").typeId(), QMetaType::UChar);
        QCOMPARE(call.hints.value("urgency").toInt(), 2);
        QCOMPARE(call.timeout, 5000);
    }

    void showAgainReplaces()
    {
        Notification notification;
        notification.setSummary(QStringLiteral("First"));
        notification.show();
        QTRY_COMPARE(m_server.calls.size(), 1);
        settle();

        notification.setSummary(QStringLiteral("Second"));
        notification.show();
        QTRY_COMPARE(m_server.calls.size(), 2);
        QCOMPARE(m_server.calls.at(1).replacesId, 1u);
        QCOMPARE(m_server.calls.at(1).summary, QStringLiteral("Second"));
    }

    void showWhilePendingWaitsForTheId()
    {
        m_server.holdsReplies = true;
        Notification notification;
        notification.setSummary(QStringLiteral("First"));
        notification.show();
        notification.setSummary(QStringLiteral("Second"));
        notification.show();
        notification.setSummary(QStringLiteral("Third"));
        notification.show();
        QTRY_COMPARE(m_server.calls.size(), 1);

        m_server.holdsReplies = false;
        m_server.releaseReplies();
        QTRY_COMPARE(m_server.calls.size(), 2);
        QCOMPARE(m_server.calls.at(1).replacesId, 1u);
        QCOMPARE(m_server.calls.at(1).summary, QStringLiteral("Third"));
        settle();
        QCOMPARE(m_server.calls.size(), 2);
    }

    void closeWhilePendingClosesOnceShown()
    {
        m_server.holdsReplies = true;
        Notification notification;
        notification.show();
        notification.close();
        QTRY_COMPARE(m_server.calls.size(), 1);

        m_server.releaseReplies();
        QTRY_COMPARE(m_server.closed, QList<uint> {1});
    }

    void closedByTheServer()
    {
        Notification notification;
        QSignalSpy closed(&notification, &Notification::closed);
        notification.show();
        QTRY_COMPARE(m_server.calls.size(), 1);
        settle();

        m_server.emitSignal(QStringLiteral("NotificationClosed"), {2u, 2u});
        settle();
        QCOMPARE(closed.size(), 0);

        m_server.emitSignal(QStringLiteral("NotificationClosed"), {1u, 2u});
        QTRY_COMPARE(closed.size(), 1);

        notification.show();
        QTRY_COMPARE(m_server.calls.size(), 2);
        QCOMPARE(m_server.calls.at(1).replacesId, 0u);
    }

    void actionsOfItsOwn()
    {
        Notification notification;
        notification.addAction(QStringLiteral("open"), QStringLiteral("Open"));
        QSignalSpy invoked(&notification, &Notification::actionInvoked);
        notification.show();
        QTRY_COMPARE(m_server.calls.size(), 1);
        settle();

        m_server.emitSignal(QStringLiteral("ActionInvoked"), {2u, QStringLiteral("open")});
        m_server.emitSignal(QStringLiteral("ActionInvoked"), {1u, QStringLiteral("open")});
        QTRY_COMPARE(invoked.size(), 1);
        QCOMPARE(invoked.at(0).at(0).toString(), QStringLiteral("open"));
    }

    void failsWithoutAServer()
    {
        m_server.fails = true;
        Notification notification;
        QSignalSpy failed(&notification, &Notification::failed);
        notification.show();
        QTRY_COMPARE(failed.size(), 1);
    }

    void autoDeleteWithoutActions()
    {
        QPointer<Notification> notification = new Notification;
        notification->setAutoDelete(true);
        notification->show();
        QTRY_VERIFY(notification.isNull());
        QCOMPARE(m_server.calls.size(), 1);
    }

    void autoDeleteWaitsForTheClose()
    {
        QPointer<Notification> notification = new Notification;
        notification->setAutoDelete(true);
        notification->addAction(QStringLiteral("default"));
        notification->show();
        QTRY_COMPARE(m_server.calls.size(), 1);
        settle();
        QVERIFY(!notification.isNull());

        m_server.emitSignal(QStringLiteral("NotificationClosed"), {1u, 1u});
        QTRY_VERIFY(notification.isNull());
    }
};

QTEST_GUILESS_MAIN(NotificationTest)

#include "tst_notification.moc"
