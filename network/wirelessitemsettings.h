#ifndef WIRELESSITEMSETTINGS_H
#define WIRELESSITEMSETTINGS_H

#include <network_export.h>

#include <QObject>
#include <QVariantMap>
#include <QtQml/qqmlregistration.h>

class NetworkClient;

// One saved profile's name and switches, written back to NetworkManager as they change.
class CUTEFISH_NETWORK_EXPORT WirelessItemSettings : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(QString name READ name WRITE setName NOTIFY nameChanged)
    Q_PROPERTY(bool autoConnect READ autoConnect WRITE setAutoConnect NOTIFY autoConnectChanged)
    Q_PROPERTY(bool availableToOtherUsers READ availableToOtherUsers WRITE setAvailableToOtherUsers NOTIFY availableToOtherUsersChanged)

public:
    explicit WirelessItemSettings(QObject *parent = nullptr);

    QString path() const { return m_path; }
    void setPath(const QString &path);

    QString name() const;
    void setName(const QString &name);
    bool autoConnect() const;
    void setAutoConnect(bool value);
    bool availableToOtherUsers() const;
    void setAvailableToOtherUsers(bool value);

Q_SIGNALS:
    void pathChanged();
    void nameChanged();
    void autoConnectChanged();
    void availableToOtherUsersChanged();

private:
    void update(const QString &key, const QVariant &value);
    QVariant cached(const QString &key, const QVariant &defaultValue = {});
    void refresh();

    NetworkClient *m_client;
    QString m_path;
    // Values written but not yet read back; the stale cache must not undo them.
    QVariantMap m_pending;
    QString m_name;
    bool m_autoConnect = true;
    bool m_availableToOtherUsers = false;
};

#endif // WIRELESSITEMSETTINGS_H
