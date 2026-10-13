#pragma once

#include "networkdbustypes.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QHash>
#include <QObject>
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
    QVariantMap properties(const QString &path, const QString &interface) const;
    QVariant property(const QString &path, const QString &interface, const QString &name) const;
    QVariant manager(const QString &name) const;

    QStringList devices() const;
    uint deviceType(const QString &device) const;
    uint deviceState(const QString &device) const;
    QString activeConnection(const QString &device) const;
    QStringList activeConnections() const;
    QStringList accessPoints(const QString &device) const;
    // Access points the device no longer lists only because nothing scanned for a while;
    // their properties stay readable through property().
    QStringList rememberedAccessPoints(const QString &device) const;
    bool remembers(const QString &accessPoint) const;
    // The Wi-Fi device that lists the access point, or listed a remembered one.
    QString accessPointDevice(const QString &accessPoint) const;
    // `accessPoint` if NetworkManager lists it; for a remembered one, the same network's
    // access point once a scan lists it again, empty until then.
    QString scannedAccessPoint(const QString &accessPoint) const;
    // What `device` lists for the access point with these properties: the same BSSID, else
    // the strongest with the same name and security.
    QString listedAccessPoint(const QString &device, const QVariantMap &accessPoint) const;

    // The profiles this user may read.
    QStringList connections() const;
    bool readable(const QString &connection) const;
    // As NetworkManager sent them, so an Update hands back the D-Bus types it was given.
    NMVariantMapMap settings(const QString &connection) const;
    QVariant setting(const QString &connection, const QString &setting, const QString &key) const;
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
    bool keepsAccessPoints(const QString &device) const;
    void noteAccessPoints(const QString &device);
    void rememberAccessPoint(const QString &path, const QVariantMap &properties);
    void scanFinished(const QString &device);
    void pruneRemembered();

    QDBusConnection m_bus;
    QString m_owner;
    quint64 m_generation = 0;
    bool m_loading = false;
    bool m_loaded = false;
    bool m_ready = false;
    QHash<QString, QHash<QString, QVariantMap>> m_objects;
    QHash<QString, NMVariantMapMap> m_settings;
    // The newest GetSettings per profile; an older reply is stale.
    QHash<QString, quint64> m_settingsRequests;
    quint64 m_permissionsRequest = 0;
    quint64 m_nextRequest = 0;
    QMap<QString, QString> m_permissions;
    QHash<QString, QString> m_accessPointDevices;

    struct RememberedAccessPoint {
        QString device;
        QVariantMap properties;
        int missedScans = 0;
    };
    // wpa_supplicant drops an access point 250 s after a scan last saw it, and while
    // connected NetworkManager hardly scans, so a list would open with only the network in
    // use. These stay until two later scans miss them, as NetworkManager drops its own.
    QHash<QString, RememberedAccessPoint> m_remembered;
    QTimer m_changedTimer;
    QTimer m_retryTimer;
};
