#include "wallpaper.h"

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>

static const QStringList imageNameFilters = {
    QStringLiteral("*.jpg"),  QStringLiteral("*.jpeg"), QStringLiteral("*.png"),
    QStringLiteral("*.webp"), QStringLiteral("*.bmp"),  QStringLiteral("*.tif"),
    QStringLiteral("*.tiff"), QStringLiteral("*.avif"),
};

// Sorted by file name, not by path: a wallpaper library may hold
// subdirectories, and what is read in the grid is the name.
static void sortByFileName(QStringList &paths)
{
    QCollator collator;
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(paths.begin(), paths.end(), [&collator](const QString &lhs, const QString &rhs) {
        const int order = collator.compare(QFileInfo(lhs).fileName(), QFileInfo(rhs).fileName());
        return order != 0 ? order < 0 : lhs < rhs;
    });
}

static QStringList wallpapersIn(const QStringList &directories)
{
    QStringList result;
    for (const QString &directory : directories) {
        QDirIterator iterator(directory, imageNameFilters, QDir::Files,
                              QDirIterator::Subdirectories);
        while (iterator.hasNext())
            result.append(iterator.next());
    }

    result.removeDuplicates();
    sortByFileName(result);
    return result;
}

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

QStringList Wallpaper::wallpapersInFolder(const QString &folder) const
{
    QStringList result = wallpaperFolderImages(folder);
    sortByFileName(result);
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
