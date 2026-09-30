#pragma once

#include <QDBusConnection>
#include <QDBusObjectPath>
#include <QMap>
#include <QObject>
#include <QSet>
#include <QVariantMap>

namespace Cutefish::IO {

using VolumeInterfaces = QMap<QString, QVariantMap>;
using VolumeObjects = QMap<QDBusObjectPath, VolumeInterfaces>;

struct Volume
{
    QString id;
    QString drive;
    QString device;
    QString name;
    QString icon;
    QStringList mountPoints;
    quint64 size = 0;
    bool filesystem = false;
    bool optical = false;
    bool removable = false;
    bool ejectable = false;
    bool canPowerOff = false;

    bool operator==(const Volume &) const = default;
};

class VolumeManager : public QObject
{
    Q_OBJECT

public:
    explicit VolumeManager(QObject *parent = nullptr);
    explicit VolumeManager(const QDBusConnection &bus, QObject *parent = nullptr);

    QList<Volume> volumes() const;
    Volume volume(const QString &id) const;

    void mount(const QString &id);
    void unmount(const QString &id);
    void eject(const QString &id);

signals:
    void volumeAdded(const QString &id);
    void volumeRemoved(const QString &id);
    void volumeChanged(const QString &id);
    void operationFinished(const QString &id, const QString &operation, const QString &mountPoint);
    void operationFailed(const QString &id, const QString &operation, const QString &message);
    void errorOccurred(const QString &message);

private slots:
    void refresh();

private:
    void rebuild();
    void call(const QString &id, const QString &operation, const QString &path,
              const QString &interface, const QString &method);
    void perform(const QString &id, const QString &operation, const QString &path,
                 const QString &interface, const QString &method, QStringList unmounts,
                 quint64 generation);

    QDBusConnection m_bus;
    VolumeObjects m_objects;
    QMap<QString, Volume> m_volumes;
    QSet<QString> m_pending;
    bool m_refreshing = false;
    bool m_refreshAgain = false;
    quint64 m_generation = 0;
};

} // namespace Cutefish::IO

Q_DECLARE_METATYPE(Cutefish::IO::VolumeInterfaces)
Q_DECLARE_METATYPE(Cutefish::IO::VolumeObjects)
