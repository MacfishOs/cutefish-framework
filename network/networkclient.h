#pragma once

#include "networkdbustypes.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QHash>
#include <QObject>
#include <QSet>
#include <QTimer>

#include <functional>

// NetworkManager as one GetManagedObjects and the signals since describe it, shared by
// everything in the process that shows or changes networks. Nothing here blocks: every
// call is asynchronous, and the cache is only ever read.
class CUTEFISH_NETWORK_EXPORT NetworkClient : public QObject
{
    Q_OBJECT

public:
    using Completion = std::function<void(const QDBusMessage &reply)>;

    explicit NetworkClient(const QDBusConnection &bus, QObject *parent = nullptr);
    ~NetworkClient() override;

    // The system bus's client, made on first use.
    static NetworkClient *instance();

    QDBusConnection bus() const { return m_bus; }
    // NetworkManager's unique bus name; empty while it is not running.
    QString owner() const { return m_owner; }
    // The objects and the settings of every profile have arrived.
    bool ready() const { return m_ready; }

    bool has(const QString &path, const QString &interface) const;
    QVariant property(const QString &path, const QString &interface, const QString &name) const;
    QVariant manager(const QString &name) const;

    QStringList devices() const;
    uint deviceType(const QString &device) const;
    uint deviceState(const QString &device) const;
    QString activeConnection(const QString &device) const;
    QStringList activeConnections() const;
    QStringList accessPoints(const QString &device) const;

    // The profiles this user may read.
    QStringList connections() const;
    bool readable(const QString &connection) const;
    // As NetworkManager sent them, so an Update hands back the D-Bus types it was given.
    NMVariantMapMap settings(const QString &connection) const;
    QVariant setting(const QString &connection, const QString &setting, const QString &key) const;
    QString connectionByUuid(const QString &uuid) const;
    // The profile that shares this machine's connection over Wi-Fi.
    QString hotspotConnection() const;

    QString permission(const QString &name) const;
    // New profiles are for every user when this user may change system profiles without
    // an administrator password; otherwise a non-administrator could not join at all.
    bool sharesNewConnections() const;
    // This user's entry in a profile's connection.permissions.
    static QString userPermission();

    // The reply reaches `completion` unless `context` is gone by then.
    void call(const QString &path, const QString &interface, const QString &method,
              const QVariantList &arguments = {}, QObject *context = nullptr,
              Completion completion = {}, int timeout = -1);
    void setProperty(const QString &path, const QString &interface, const QString &name,
                     const QVariant &value, QObject *context = nullptr, Completion completion = {});

    // An object path property, empty for NetworkManager's "/".
    static QString path(const QVariant &value);

Q_SIGNALS:
    void readyChanged();
    // Once per batch of applied changes.
    void changed();
    void activeStateChanged(const QString &path, uint state, uint reason);
    void objectRemoved(const QString &path);

private Q_SLOTS:
    void interfacesAdded(const QDBusObjectPath &path, const NMVariantMapMap &interfaces);
    void interfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces);
    void propertiesChanged(const QString &interface, const QVariantMap &properties,
                           const QStringList &invalidated, const QDBusMessage &message);
    void connectionUpdated(const QDBusMessage &message);
    void activeStateSignal(uint state, uint reason, const QDBusMessage &message);
    void fetchPermissions();

private:
    void refresh();
    void reset();
    void load(const QString &owner);
    void subscribe(const QString &owner, bool connect);
    void addInterfaces(const QString &path, const QVariantMap &interfaces);
    void fetchSettings(const QString &path);
    void scheduleChanged();
    void updateReady();

    QDBusConnection m_bus;
    QString m_owner;
    quint64 m_generation = 0;
    bool m_loading = false;
    bool m_loaded = false;
    bool m_ready = false;
    QHash<QString, QHash<QString, QVariantMap>> m_objects;
    QHash<QString, NMVariantMapMap> m_settings;
    QSet<QString> m_unreadable;
    // The newest GetSettings per profile; an older reply is stale.
    QHash<QString, quint64> m_settingsRequests;
    quint64 m_permissionsRequest = 0;
    quint64 m_nextRequest = 0;
    QMap<QString, QString> m_permissions;
    QTimer m_changedTimer;
    QTimer m_retryTimer;
};
