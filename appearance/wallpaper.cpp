#include "wallpaper.h"

#include <QDir>
#include <QDirIterator>
#include <QStandardPaths>

namespace {
QStringList wallpapersIn(const QStringList &directories)
{
    QStringList result;
    for (const QString &directory : directories) {
        QDirIterator iterator(directory,
                              {QStringLiteral("*.jpg"), QStringLiteral("*.jpeg"),
                               QStringLiteral("*.png"), QStringLiteral("*.webp")},
                              QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext())
            result.append(iterator.next());
    }

    result.removeDuplicates();
    result.sort();
    return result;
}
} // namespace

Wallpaper::Wallpaper(QObject *parent)
    : Appearance(parent)
{
    connect(this, &Appearance::backgroundTypeChanged, this, &Wallpaper::typeChanged);
    connect(this, &Appearance::wallpaperChanged, this, &Wallpaper::pathChanged);
    connect(this, &Appearance::backgroundColorChanged, this, &Wallpaper::colorChanged);
}

int Wallpaper::type() const
{
    return backgroundType();
}

void Wallpaper::setType(int type)
{
    setBackgroundType(type);
}

QString Wallpaper::path() const
{
    return wallpaper();
}

void Wallpaper::setPath(const QString &path)
{
    setWallpaper(path);
}

QString Wallpaper::color() const
{
    return backgroundColor();
}

void Wallpaper::setColor(const QString &color)
{
    setBackgroundColor(color);
}

QStringList Wallpaper::backgrounds() const
{
    QStringList result = systemWallpapers();
    result.append(userWallpapers());
    result.removeDuplicates();
    return result;
}

QStringList Wallpaper::systemWallpapers() const
{
    QStringList directories;
    const QString userDataPath = QDir::cleanPath(
            QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation));
    const QStringList dataPaths =
            QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation);

    for (const QString &dataPath : dataPaths) {
        if (QDir::cleanPath(dataPath) == userDataPath)
            continue;

        const QString directory = QDir(dataPath).absoluteFilePath(
                QStringLiteral("backgrounds/cutefishos"));
        if (QDir(directory).exists())
            directories.append(directory);
    }

    return wallpapersIn(directories);
}
