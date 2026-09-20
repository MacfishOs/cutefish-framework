#include "screenrecordingpermissions.h"

#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QSettings>
#include <QStandardPaths>

namespace {

const QString group = QStringLiteral("ScreenRecording");

QString defaultPath()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
            + QStringLiteral("/cutefish/privacy.conf");
}

// Application ids hold dots but no slashes, so they nest under the group as-is.
QString allowedKey(const QString &appId)
{
    return group + QLatin1Char('/') + appId + QStringLiteral("/allowed");
}

QString monitorsKey(const QString &appId)
{
    return group + QLatin1Char('/') + appId + QStringLiteral("/monitors");
}

} // namespace

ScreenRecordingPermissions::ScreenRecordingPermissions(QObject *parent)
    : ScreenRecordingPermissions(defaultPath(), parent)
{
}

ScreenRecordingPermissions::ScreenRecordingPermissions(const QString &path, QObject *parent)
    : QObject(parent)
    , m_path(path)
    , m_watcher(new QFileSystemWatcher(this))
{
    connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this] {
        watch();
        Q_EMIT changed();
    });
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this, [this] {
        // QSettings replaces the file, which drops the file watch.
        if (watch())
            Q_EMIT changed();
    });
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    watch();
}

// Returns whether the file watch was (re)established.
bool ScreenRecordingPermissions::watch()
{
    const QString directory = QFileInfo(m_path).absolutePath();
    if (!m_watcher->directories().contains(directory))
        m_watcher->addPath(directory);
    if (m_watcher->files().contains(m_path))
        return false;
    return QFileInfo::exists(m_path) && m_watcher->addPath(m_path);
}

QList<ScreenRecordingPermissions::Grant> ScreenRecordingPermissions::grants() const
{
    QSettings settings(m_path, QSettings::IniFormat);
    settings.beginGroup(group);
    QList<Grant> result;
    const auto ids = settings.childGroups();
    for (const auto &appId : ids) {
        result.append({appId, settings.value(appId + QStringLiteral("/allowed")).toBool(),
                       settings.value(appId + QStringLiteral("/monitors")).toStringList()});
    }
    return result;
}

std::optional<ScreenRecordingPermissions::Grant>
ScreenRecordingPermissions::grant(const QString &appId) const
{
    if (appId.isEmpty())
        return std::nullopt;
    QSettings settings(m_path, QSettings::IniFormat);
    if (!settings.contains(allowedKey(appId)))
        return std::nullopt;
    return Grant {appId, settings.value(allowedKey(appId)).toBool(),
                  settings.value(monitorsKey(appId)).toStringList()};
}

void ScreenRecordingPermissions::record(const QString &appId)
{
    if (appId.isEmpty())
        return;
    QSettings settings(m_path, QSettings::IniFormat);
    if (!settings.contains(allowedKey(appId)))
        settings.setValue(allowedKey(appId), false);
}

void ScreenRecordingPermissions::allow(const QString &appId, const QStringList &monitors)
{
    if (appId.isEmpty())
        return;
    QSettings settings(m_path, QSettings::IniFormat);
    settings.setValue(allowedKey(appId), true);
    settings.setValue(monitorsKey(appId), monitors);
}

void ScreenRecordingPermissions::setAllowed(const QString &appId, bool allowed)
{
    if (appId.isEmpty())
        return;
    QSettings settings(m_path, QSettings::IniFormat);
    settings.setValue(allowedKey(appId), allowed);
}

void ScreenRecordingPermissions::remove(const QString &appId)
{
    if (appId.isEmpty())
        return;
    QSettings settings(m_path, QSettings::IniFormat);
    settings.remove(group + QLatin1Char('/') + appId);
}
