#include "appearance.h"

#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#include <QDBusServiceWatcher>

static constexpr auto Service = "com.cutefish.Services";
static constexpr auto ObjectPath = "/com/cutefish/Services/Appearance";
static constexpr auto Interface = "com.cutefish.Services.Appearance";

Appearance::Appearance(QObject *parent)
    : QObject(parent)
    , m_interface(nullptr)
{
    auto *watcher =
            new QDBusServiceWatcher(QString::fromLatin1(Service), QDBusConnection::sessionBus(),
                                    QDBusServiceWatcher::WatchForRegistration, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &Appearance::init);

    init();
}

void Appearance::init()
{
    delete m_interface;
    m_continuousCorners.reset();
    m_interface =
            new QDBusInterface(QString::fromLatin1(Service), QString::fromLatin1(ObjectPath),
                               QString::fromLatin1(Interface), QDBusConnection::sessionBus(), this);

    if (!m_interface->isValid())
        return;

    connect(m_interface, SIGNAL(darkModeChanged(bool)), this, SIGNAL(darkModeChanged()));
    connect(m_interface, SIGNAL(darkModeScheduleChanged(int)), this,
            SIGNAL(darkModeScheduleChanged()));
    connect(m_interface, SIGNAL(wallpaperChanged(QString)), this, SIGNAL(wallpaperChanged()));
    connect(m_interface, SIGNAL(userWallpapersChanged()), this, SIGNAL(userWallpapersChanged()));
    connect(m_interface, SIGNAL(wallpaperFoldersChanged()), this,
            SIGNAL(wallpaperFoldersChanged()));
    connect(m_interface, SIGNAL(wallpaperFolderImagesChanged(QString)), this,
            SIGNAL(wallpaperFolderImagesChanged(QString)));
    connect(m_interface, SIGNAL(accentColorChanged(int)), this, SIGNAL(accentColorIndexChanged()));
    connect(m_interface, SIGNAL(continuousCornersChanged(bool)), this,
            SLOT(onContinuousCornersChanged(bool)));
    connect(m_interface, SIGNAL(systemFontPointSizeChanged()), this,
            SIGNAL(fontPointSizeChanged()));
    connect(m_interface, SIGNAL(systemFontChanged()), this, SIGNAL(fontFamilyChanged()));
    connect(m_interface, SIGNAL(iconThemeChanged()), this, SIGNAL(iconThemeChanged()));
    connect(m_interface, SIGNAL(backgroundTypeChanged()), this, SIGNAL(backgroundTypeChanged()));
    connect(m_interface, SIGNAL(backgroundColorChanged()), this, SIGNAL(backgroundColorChanged()));
    connect(m_interface, SIGNAL(backgroundVisibleChanged()), this,
            SIGNAL(backgroundVisibleChanged()));

    emit darkModeChanged();
    emit darkModeScheduleChanged();
    emit wallpaperChanged();
    emit userWallpapersChanged();
    emit wallpaperFoldersChanged();
    emit accentColorIndexChanged();
    emit continuousCornersChanged();
    emit fontPointSizeChanged();
    emit fontFamilyChanged();
    emit fixedFontFamilyChanged();
    emit iconThemeChanged();
    emit backgroundTypeChanged();
    emit backgroundColorChanged();
    emit backgroundVisibleChanged();
}

QVariant Appearance::serviceProperty(const char *name) const
{
    return m_interface && m_interface->isValid() ? m_interface->property(name) : QVariant();
}

void Appearance::callService(const char *method, const QVariant &value)
{
    if (m_interface && m_interface->isValid())
        m_interface->call(method, value);
}

bool Appearance::darkMode() const
{
    return serviceProperty("isDarkMode").toBool();
}

void Appearance::switchDarkMode(bool darkMode)
{
    callService("setDarkMode", darkMode);
}

int Appearance::darkModeSchedule() const
{
    return serviceProperty("darkModeSchedule").toInt();
}

void Appearance::setDarkModeSchedule(int schedule)
{
    callService("setDarkModeSchedule", schedule);
}

// Without the service there is nothing to read, so the default applies.
bool Appearance::continuousCorners() const
{
    if (m_continuousCorners.has_value())
        return *m_continuousCorners;

    const QVariant value = serviceProperty("continuousCorners");
    if (!value.isValid())
        return true;

    m_continuousCorners = value.toBool();
    return *m_continuousCorners;
}

void Appearance::setContinuousCorners(bool value)
{
    callService("setContinuousCorners", value);
}

int Appearance::accentColorIndex() const
{
    return serviceProperty("accentColor").toInt();
}

void Appearance::setAccentColor(int accentColor)
{
    callService("setAccentColor", accentColor);
}

qreal Appearance::fontPointSize() const
{
    return serviceProperty("systemFontPointSize").toReal();
}

void Appearance::setFontPointSize(qreal fontPointSize)
{
    callService("setSystemFontPointSize", fontPointSize);
}

QString Appearance::fontFamily() const
{
    return serviceProperty("systemFont").toString();
}

void Appearance::setFontFamily(const QString &name)
{
    if (!name.isEmpty())
        callService("setSystemFont", name);
}

QString Appearance::fixedFontFamily() const
{
    return serviceProperty("systemFixedFont").toString();
}

// com.cutefish.Services.Appearance has no systemFixedFontChanged signal, so
// the change is announced here.
void Appearance::setFixedFontFamily(const QString &name)
{
    if (!name.isEmpty()) {
        callService("setSystemFixedFont", name);
        emit fixedFontFamilyChanged();
    }
}

QString Appearance::wallpaper() const
{
    return serviceProperty("wallpaper").toString();
}

void Appearance::setWallpaper(const QString &path)
{
    if (!path.isEmpty())
        callService("setWallpaper", path);
}

QStringList Appearance::userWallpapers() const
{
    return serviceProperty("userWallpapers").toStringList();
}

void Appearance::removeUserWallpaper(const QString &path)
{
    if (!path.isEmpty())
        callService("removeUserWallpaper", path);
}

QStringList Appearance::wallpaperFolders() const
{
    return serviceProperty("wallpaperFolders").toStringList();
}

void Appearance::addWallpaperFolder(const QString &path)
{
    if (!path.isEmpty())
        callService("addWallpaperFolder", path);
}

void Appearance::removeWallpaperFolder(const QString &path)
{
    if (!path.isEmpty())
        callService("removeWallpaperFolder", path);
}

// The service owns the copies a wallpaper folder is made of, so the list is
// asked for rather than scanned here.
QStringList Appearance::wallpaperFolderImages(const QString &folder) const
{
    if (folder.isEmpty() || !m_interface || !m_interface->isValid())
        return {};

    const QDBusReply<QStringList> reply = m_interface->call("wallpaperFolderImages", folder);
    return reply.isValid() ? reply.value() : QStringList();
}

int Appearance::backgroundType() const
{
    return serviceProperty("backgroundType").toInt();
}

void Appearance::setBackgroundType(int type)
{
    callService("setBackgroundType", type);
}

QString Appearance::backgroundColor() const
{
    return serviceProperty("backgroundColor").toString();
}

void Appearance::setBackgroundColor(const QString &color)
{
    callService("setBackgroundColor", color);
}

bool Appearance::backgroundVisible() const
{
    return serviceProperty("backgroundVisible").toBool();
}

QString Appearance::iconTheme() const
{
    return serviceProperty("iconTheme").toString();
}

void Appearance::setIconTheme(const QString &name)
{
    if (!name.isEmpty())
        callService("setIconTheme", name);
}

void Appearance::setCursorTheme(const QString &theme)
{
    if (!theme.isEmpty())
        callService("setCursorTheme", theme);
}

void Appearance::setFontRendering(bool antialias, const QString &hintStyle)
{
    if (m_interface && m_interface->isValid())
        m_interface->call("setFontRendering", antialias, hintStyle);
}

void Appearance::onContinuousCornersChanged(bool continuousCorners)
{
    m_continuousCorners = continuousCorners;
    emit continuousCornersChanged();
}
