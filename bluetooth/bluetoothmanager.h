#pragma once

#include "bluetoothobject.h"
#include <QDBusConnection>
#include <QDBusMessage>
#include <QSet>
#include <QTimer>
#include <functional>

class BluetoothAgent;
class BluetoothRfkill;

class BluetoothManager : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(Manager)
    QML_SINGLETON
    Q_PROPERTY(bool operational READ operational NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)
    // BlueZ has been read, or found absent; until then available reads false either way.
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool bluetoothOperational READ enabled NOTIFY enabledChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(bool bluetoothBlocked READ blocked WRITE setBlocked NOTIFY enabledChanged)
    Q_PROPERTY(bool hardwareBlocked READ hardwareBlocked NOTIFY changed)
    Q_PROPERTY(QList<BluetoothObject *> adapters READ adapters NOTIFY adaptersChanged)
    Q_PROPERTY(QList<BluetoothObject *> devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY changed)
    Q_PROPERTY(bool discoveryRequested READ discoveryRequested WRITE setDiscoveryRequested NOTIFY
                       changed)
    Q_PROPERTY(bool agentEnabled READ agentEnabled WRITE setAgentEnabled NOTIFY changed)
    Q_PROPERTY(bool agentReady READ agentReady NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorChanged)
    Q_PROPERTY(QString pairingKind READ pairingKind NOTIFY pairingChanged)
    Q_PROPERTY(QString pairingDevice READ pairingDevice NOTIFY pairingChanged)
    Q_PROPERTY(QString pairingCode READ pairingCode NOTIFY pairingChanged)
    Q_PROPERTY(int pairingEntered READ pairingEntered NOTIFY pairingChanged)
public:
    explicit BluetoothManager(QObject *parent = nullptr);
    BluetoothManager(const QDBusConnection &bus, bool monitorRfkill, QObject *parent = nullptr);
    ~BluetoothManager() override;

    bool operational() const { return m_operational; }

    bool available() const { return m_operational && !adapters().isEmpty(); }

    bool ready() const { return m_ready; }

    bool enabled() const;
    bool blocked() const;
    bool hardwareBlocked() const;
    QList<BluetoothObject *> adapters() const;
    QList<BluetoothObject *> devices() const;
    bool scanning() const;

    bool discoveryRequested() const { return m_discoveryRequested; }

    bool agentEnabled() const { return m_agentEnabled; }

    bool agentReady() const { return m_agentReady; }

    QString errorText() const { return m_error; }

    QString pairingKind() const { return m_pairingKind; }

    QString pairingDevice() const { return m_pairingDevice; }

    QString pairingCode() const { return m_pairingCode; }

    int pairingEntered() const { return m_pairingEntered; }

    void setEnabled(bool enabled);

    void setBlocked(bool blocked) { setEnabled(!blocked); }

    void setDiscoveryRequested(bool requested);
    void setAgentEnabled(bool enabled);
    Q_INVOKABLE void clearError();
    Q_INVOKABLE void respondPairing(bool accept, const QString &code = QString());
    Q_INVOKABLE void cancelPairing();
    Q_INVOKABLE void refresh();
    BluetoothObject *object(const QString &path) const;
    void setObjectProperty(BluetoothObject *object, const QString &property, const QVariant &value);
    void operate(BluetoothObject *object, const QString &method,
                 const QVariantList &arguments = {});
    void discovery(BluetoothObject *adapter, bool start);
    void mediaCommand(BluetoothObject *device, const QString &command);
    void fail(const QString &message);
    using Completion = std::function<void(const QDBusMessage &)>;
    // Background refreshes race object removal; their failures are not the user's concern.
    enum class Errors { Report, Ignore };
    void call(const QString &path, const QString &interface, const QString &method,
              const QVariantList &arguments = {}, Completion completion = {},
              Errors errors = Errors::Report, int timeout = 30000);
signals:
    void changed();
    void enabledChanged();
    void availableChanged();
    void readyChanged();
    void adaptersChanged();
    void devicesChanged();
    void errorChanged();
    void pairingChanged();
    // Device actions report here rather than through errorText; the default
    // constructor turns each one into a desktop notification.
    void deviceFailed(const QString &summary, const QString &body);
private slots:
    void ownerChanged(const QString &service, const QString &oldOwner, const QString &newOwner);
    void interfacesAdded(const QDBusObjectPath &path, const BluetoothInterfaces &interfaces);
    void interfacesRemoved(const QDBusObjectPath &path, const QStringList &interfaces);
    void propertiesChanged(const QString &interface, const QVariantMap &properties,
                           const QStringList &invalidated, const QDBusMessage &message);

private:
    friend class BluetoothAgent;
    void reset();
    void load(const QString &owner);
    void setReady();
    void syncDiscovery();
    void registerAgent();
    void finishPrompt(const QString &error = QStringLiteral("org.bluez.Error.Canceled"));
    void prompt(const QDBusMessage &message);
    void deviceFailure(const QString &method, const QString &device, const QString &body);
    QString describe(const QDBusMessage &reply) const;
    void notify(const QString &summary, const QString &body);
    QDBusConnection m_bus;
    QString m_owner;
    QMap<QString, BluetoothObject *> m_objects;
    BluetoothAgent *m_agent = nullptr;
    BluetoothRfkill *m_rfkill = nullptr;
    QTimer m_promptTimer;
    QTimer m_retryTimer;
    quint64 m_generation = 0;
    bool m_operational = false;
    bool m_ready = false;
    bool m_lastEnabled = false;
    bool m_lastAvailable = false;
    bool m_loading = false;
    bool m_discoveryRequested = false;
    bool m_agentEnabled = false;
    bool m_agentReady = false;
    bool m_registeringAgent = false;
    QSet<QString> m_discoveryOwned;
    QSet<QString> m_discoveryPending;
    QSet<QString> m_discoveryFailed;
    QMap<QString, int> m_stopRetries;
    QSet<QString> m_pairingOperations;
    QString m_error;
    QString m_pairingKind;
    QString m_pairingPath;
    QString m_pairingDevice;
    QString m_pairingCode;
    int m_pairingEntered = 0;
    QDBusMessage m_request;
    bool m_hasRequest = false;
    QList<std::function<void()>> m_events;
};
