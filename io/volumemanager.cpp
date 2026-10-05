#include "volumemanager.h"

#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QFile>
#include <QFileInfo>
#include <QLocale>

namespace Cutefish::IO {

static const QString service = QStringLiteral("org.freedesktop.UDisks2");
static const QString root = QStringLiteral("/org/freedesktop/UDisks2");
static const QString blockInterface = service + ".Block";
static const QString driveInterface = service + ".Drive";
static const QString filesystemInterface = service + ".Filesystem";

static QString decodePath(QByteArray bytes)
{
    if (bytes.endsWith('\0'))
        bytes.chop(1);

    return QFile::decodeName(bytes);
}

VolumeManager::VolumeManager(QObject *parent)
    : VolumeManager(QDBusConnection::systemBus(), parent)
{
}

VolumeManager::VolumeManager(const QDBusConnection &bus, QObject *parent)
    : QObject(parent)
    , m_bus(bus)
{
    qDBusRegisterMetaType<VolumeInterfaces>();
    qDBusRegisterMetaType<VolumeObjects>();
    qDBusRegisterMetaType<QList<QByteArray>>();

    m_bus.connect(service, root, "org.freedesktop.DBus.ObjectManager", "InterfacesAdded", this,
                  SLOT(refresh()));
    m_bus.connect(service, root, "org.freedesktop.DBus.ObjectManager", "InterfacesRemoved", this,
                  SLOT(refresh()));
    m_bus.connect(service, QString(), "org.freedesktop.DBus.Properties", "PropertiesChanged", this,
                  SLOT(refresh()));

    auto *watcher =
            new QDBusServiceWatcher(service, m_bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceOwnerChanged, this,
            [this](const QString &, const QString &, const QString &owner) {
                ++m_generation;
                m_objects.clear();
                rebuild();

                if (!owner.isEmpty())
                    refresh();
            });

    refresh();
}

QList<Volume> VolumeManager::volumes() const
{
    return m_volumes.values();
}

Volume VolumeManager::volume(const QString &id) const
{
    return m_volumes.value(id);
}

void VolumeManager::refresh()
{
    if (m_refreshing) {
        m_refreshAgain = true;
        return;
    }

    m_refreshing = true;
    const auto generation = m_generation;
    auto message = QDBusMessage::createMethodCall(
            service, root, "org.freedesktop.DBus.ObjectManager", "GetManagedObjects");
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, generation] {
        QDBusPendingReply<VolumeObjects> reply = *watcher;
        watcher->deleteLater();
        m_refreshing = false;

        if (generation == m_generation) {
            if (reply.isError())
                emit errorOccurred(reply.error().message());
            else {
                m_objects = reply.value();
                rebuild();
            }
        }

        if (m_refreshAgain) {
            m_refreshAgain = false;
            refresh();
        }
    });
}

void VolumeManager::rebuild()
{
    QMap<QString, Volume> next;

    for (auto it = m_objects.cbegin(); it != m_objects.cend(); ++it) {
        if (!it->contains(blockInterface))
            continue;

        const auto block = it->value(blockInterface);
        if (block.value("HintIgnore").toBool())
            continue;

        Volume v;
        v.id = it.key().path();
        v.drive = qdbus_cast<QDBusObjectPath>(block.value("Drive")).path();
        const auto drive = m_objects.value(QDBusObjectPath(v.drive.isEmpty() ? "/" : v.drive))
                                   .value(driveInterface);

        v.device = decodePath(qdbus_cast<QByteArray>(block.value("PreferredDevice")));
        v.size = block.value("Size").toULongLong();
        v.filesystem = it->contains(filesystemInterface);
        v.optical = drive.value("Optical").toBool();
        v.removable = drive.value("Removable").toBool()
                || drive.value("ConnectionBus").toString() == "usb";
        v.ejectable = drive.value("Ejectable").toBool();
        v.canPowerOff = drive.value("CanPowerOff").toBool();

        v.name = block.value("HintName").toString();
        if (v.name.isEmpty())
            v.name = block.value("IdLabel").toString();
        if (v.name.isEmpty())
            v.name = v.size ? tr("%1 Volume").arg(QLocale().formattedDataSize(v.size))
                            : QFileInfo(v.device).fileName();

        v.icon = block.value("HintIconName").toString();
        if (v.icon.isEmpty())
            v.icon = v.optical    ? "drive-optical"
                    : v.removable ? "drive-removable-media"
                                  : "drive-harddisk";

        const auto points =
                qdbus_cast<QList<QByteArray>>(it->value(filesystemInterface).value("MountPoints"));
        for (const auto &point : points) {
            const auto path = decodePath(point);
            if (!path.isEmpty())
                v.mountPoints.append(path);
        }

        next.insert(v.id, v);
    }

    const auto previous = m_volumes;
    m_volumes = next;

    for (auto it = previous.cbegin(); it != previous.cend(); ++it)
        if (!next.contains(it.key()))
            emit volumeRemoved(it.key());

    for (auto it = next.cbegin(); it != next.cend(); ++it) {
        if (!previous.contains(it.key()))
            emit volumeAdded(it.key());
        else if (previous.value(it.key()) != it.value())
            emit volumeChanged(it.key());
    }
}

void VolumeManager::mount(const QString &id)
{
    call(id, "mount", id, filesystemInterface, "Mount");
}

void VolumeManager::unmount(const QString &id)
{
    call(id, "unmount", id, filesystemInterface, "Unmount");
}

void VolumeManager::eject(const QString &id)
{
    const auto v = volume(id);
    call(id, "eject", v.drive, driveInterface, v.ejectable ? "Eject" : "PowerOff");
}

void VolumeManager::call(const QString &id, const QString &operation, const QString &path,
                         const QString &interface, const QString &method)
{
    const auto v = volume(id);
    if (v.id.isEmpty()
        || (operation == "eject" ? !(v.ejectable || v.canPowerOff) : !v.filesystem)) {
        emit operationFailed(id, operation, tr("The device does not support this operation."));
        return;
    }

    bool busy = m_pending.contains(id);
    for (const auto &pending : m_pending)
        busy |= v.drive != "/" && !v.drive.isEmpty() && volume(pending).drive == v.drive;

    if (busy) {
        emit operationFailed(id, operation, tr("A device operation is already in progress."));
        return;
    }

    m_pending.insert(id);

    QStringList unmounts;
    if (operation == "eject") {
        for (const auto &other : m_volumes)
            if (other.drive == v.drive && !other.mountPoints.isEmpty())
                unmounts.append(other.id);
    }

    perform(id, operation, path, interface, method, unmounts, m_generation);
}

void VolumeManager::perform(const QString &id, const QString &operation, const QString &path,
                            const QString &interface, const QString &method, QStringList unmounts,
                            quint64 generation)
{
    const bool unmounting = !unmounts.isEmpty();
    const QString target = unmounting ? unmounts.takeFirst() : path;
    auto message = QDBusMessage::createMethodCall(service, target,
                                                  unmounting ? filesystemInterface : interface,
                                                  unmounting ? "Unmount" : method);
    message << QVariantMap();
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message, 120000), this);

    connect(watcher, &QDBusPendingCallWatcher::finished, this,
            [this, watcher, id, operation, path, interface, method, unmounts, unmounting,
             generation] {
                const QDBusMessage reply = watcher->reply();
                watcher->deleteLater();

                if (generation != m_generation
                    || (operation != "eject" && !m_volumes.contains(id))) {
                    m_pending.remove(id);
                    emit operationFailed(id, operation, tr("The device is no longer available."));
                    return;
                }

                if (reply.type() == QDBusMessage::ErrorMessage) {
                    m_pending.remove(id);
                    emit operationFailed(id, operation, reply.errorMessage());
                } else if (unmounting) {
                    perform(id, operation, path, interface, method, unmounts, generation);
                    return;
                } else {
                    m_pending.remove(id);

                    QString mountPoint;
                    if (operation == "mount" && !reply.arguments().isEmpty())
                        mountPoint = reply.arguments().first().toString();

                    emit operationFinished(id, operation, mountPoint);
                }

                refresh();
            });
}

} // namespace Cutefish::IO
