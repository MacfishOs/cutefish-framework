#ifndef CUTEFISH_NETWORK_ACTIVECONNECTION_H
#define CUTEFISH_NETWORK_ACTIVECONNECTION_H

#include <network_export.h>

#include <QObject>
#include <QtQml/qqmlregistration.h>

class NetworkClient;

// The Wi-Fi network in use, for the status bar: its name and signal icon, both empty
// while no Wi-Fi connection is up.
class CUTEFISH_NETWORK_EXPORT ActiveConnection : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString wirelessName READ wirelessName NOTIFY wirelessNameChanged)
    Q_PROPERTY(QString wirelessIcon READ wirelessIcon NOTIFY wirelessIconChanged)

public:
    explicit ActiveConnection(QObject *parent = nullptr);

    QString wirelessName() const { return m_wirelessName; }
    QString wirelessIcon() const { return m_wirelessIcon; }

Q_SIGNALS:
    void wirelessNameChanged();
    void wirelessIconChanged();

private:
    void update();

    NetworkClient *m_client;
    QString m_wirelessName;
    QString m_wirelessIcon;
};

#endif // CUTEFISH_NETWORK_ACTIVECONNECTION_H
