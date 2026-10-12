#pragma once

#include "bluetoothdbustypes.h"
#include <QObject>
#include <QtQml/qqmlregistration.h>

class BluetoothManager;

class BluetoothObject : public QObject
{
    Q_OBJECT
    QML_NAMED_ELEMENT(BluetoothObject)
    QML_UNCREATABLE("Bluetooth objects are provided by Manager")
    Q_PROPERTY(QString ubi READ path CONSTANT)
    Q_PROPERTY(QString address READ address NOTIFY changed)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY changed)
    Q_PROPERTY(QString icon READ icon NOTIFY changed)
    Q_PROPERTY(bool powered READ powered WRITE setPowered NOTIFY changed)
    Q_PROPERTY(bool discovering READ discovering NOTIFY changed)
    Q_PROPERTY(bool discoverable READ discoverable WRITE setDiscoverable NOTIFY changed)
    Q_PROPERTY(bool pairable READ pairable WRITE setPairable NOTIFY changed)
    Q_PROPERTY(bool paired READ paired NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool trusted READ trusted WRITE setTrusted NOTIFY changed)
    Q_PROPERTY(bool blocked READ blocked WRITE setBlocked NOTIFY changed)
    Q_PROPERTY(bool servicesResolved READ servicesResolved NOTIFY changed)
    Q_PROPERTY(int rssi READ rssi NOTIFY changed)
    Q_PROPERTY(int batteryPercentage READ batteryPercentage NOTIFY changed)
    Q_PROPERTY(QStringList uuids READ uuids NOTIFY changed)
    Q_PROPERTY(QString adapterPath READ adapterPath NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // From the device's Device Information service; empty until readDeviceInformation()
    // finds them on a connected device.
    Q_PROPERTY(QString modelNumber READ modelNumber NOTIFY changed)
    Q_PROPERTY(QString firmwareRevision READ firmwareRevision NOTIFY changed)
public:
    BluetoothObject(const QString &path, BluetoothManager *manager);

    QString path() const { return m_path; }

    QString address() const;
    QString name() const;
    QString icon() const;
    bool powered() const;
    bool discovering() const;
    bool discoverable() const;
    bool pairable() const;
    bool paired() const;
    bool connected() const;
    bool trusted() const;
    bool blocked() const;
    bool servicesResolved() const;
    int rssi() const;
    int batteryPercentage() const;
    QStringList uuids() const;
    QString adapterPath() const;

    bool busy() const { return m_busy; }

    QString modelNumber() const { return m_modelNumber; }

    QString firmwareRevision() const { return m_firmwareRevision; }

    bool isAdapter() const;
    bool isDevice() const;

    bool hasInterface(const QString &interface) const { return m_interfaces.contains(interface); }
    bool hasBluezInterface() const;

    quint64 revision(const QString &interface, const QString &property) const;
    QVariant value(const QString &property) const;
    QVariant value(const QString &interface, const QString &property) const;
    void setName(const QString &name);
    void setPowered(bool enabled);
    void setDiscoverable(bool enabled);
    void setPairable(bool enabled);
    void setTrusted(bool enabled);
    void setBlocked(bool enabled);
    void update(const QString &interface, const QVariantMap &properties,
                const QStringList &invalidated = {});
    void removeInterface(const QString &interface);
    void setBusy(bool busy);
    void setModelNumber(const QString &modelNumber);
    void setFirmwareRevision(const QString &firmwareRevision);
    Q_INVOKABLE void connectToDevice();
    Q_INVOKABLE void disconnectFromDevice();
    Q_INVOKABLE void pair();
    Q_INVOKABLE void cancelPairing();
    Q_INVOKABLE void forget();
    Q_INVOKABLE void connectProfile(const QString &uuid);
    Q_INVOKABLE void disconnectProfile(const QString &uuid);
    Q_INVOKABLE void startDiscovery();
    Q_INVOKABLE void stopDiscovery();
    Q_INVOKABLE void mediaCommand(const QString &command);
    Q_INVOKABLE void readDeviceInformation();
signals:
    void changed();

private:
    void setValue(const QString &property, const QVariant &value);
    QString m_path;
    BluetoothManager *m_manager;
    BluetoothInterfaces m_interfaces;
    QMap<QString, QMap<QString, quint64>> m_revisions;
    quint64 m_revision = 0;
    bool m_busy = false;
    QString m_modelNumber;
    QString m_firmwareRevision;
};
