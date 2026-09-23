#ifndef CUTEFISH_APPEARANCE_H
#define CUTEFISH_APPEARANCE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

#include <optional>

class QDBusInterface;

class Appearance : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool darkMode READ darkMode NOTIFY darkModeChanged)
    Q_PROPERTY(bool dimsWallpaper READ dimsWallpaper WRITE setDimsWallpaper NOTIFY
                       dimsWallpaperChanged)
    Q_PROPERTY(bool continuousCorners READ continuousCorners WRITE setContinuousCorners NOTIFY
                       continuousCornersChanged)
    Q_PROPERTY(int accentColorIndex READ accentColorIndex NOTIFY accentColorIndexChanged)
    Q_PROPERTY(qreal fontPointSize READ fontPointSize WRITE setFontPointSize NOTIFY
                       fontPointSizeChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY fontFamilyChanged)
    Q_PROPERTY(QString iconTheme READ iconTheme WRITE setIconTheme NOTIFY iconThemeChanged)
    Q_PROPERTY(QString fixedFontFamily READ fixedFontFamily WRITE setFixedFontFamily NOTIFY
                       fixedFontFamilyChanged)
    Q_PROPERTY(QString wallpaper READ wallpaper WRITE setWallpaper NOTIFY wallpaperChanged)
    Q_PROPERTY(QStringList userWallpapers READ userWallpapers NOTIFY userWallpapersChanged)
    Q_PROPERTY(QStringList wallpaperFolders READ wallpaperFolders NOTIFY wallpaperFoldersChanged)
    Q_PROPERTY(int backgroundType READ backgroundType WRITE setBackgroundType NOTIFY
                       backgroundTypeChanged)
    Q_PROPERTY(QString backgroundColor READ backgroundColor WRITE setBackgroundColor NOTIFY
                       backgroundColorChanged)
    Q_PROPERTY(bool backgroundVisible READ backgroundVisible NOTIFY backgroundVisibleChanged)

public:
    explicit Appearance(QObject *parent = nullptr);

    bool darkMode() const;
    Q_INVOKABLE void switchDarkMode(bool darkMode);

    bool dimsWallpaper() const;
    Q_INVOKABLE void setDimsWallpaper(bool value);

    // Whether rounded corners are drawn as continuous curves, the same shape
    // the compositor gives window silhouettes. On until turned off.
    bool continuousCorners() const;
    Q_INVOKABLE void setContinuousCorners(bool value);

    int accentColorIndex() const;
    Q_INVOKABLE void setAccentColor(int accentColor);

    qreal fontPointSize() const;
    Q_INVOKABLE void setFontPointSize(qreal fontPointSize);

    QString fontFamily() const;
    Q_INVOKABLE void setFontFamily(const QString &name);

    QString fixedFontFamily() const;
    Q_INVOKABLE void setFixedFontFamily(const QString &name);

    QString iconTheme() const;
    Q_INVOKABLE void setIconTheme(const QString &name);

    QString wallpaper() const;
    Q_INVOKABLE void setWallpaper(const QString &path);
    QStringList userWallpapers() const;
    Q_INVOKABLE void removeUserWallpaper(const QString &path);

    QStringList wallpaperFolders() const;
    Q_INVOKABLE void addWallpaperFolder(const QString &path);
    Q_INVOKABLE void removeWallpaperFolder(const QString &path);
    QStringList wallpaperFolderImages(const QString &folder) const;

    int backgroundType() const;
    Q_INVOKABLE void setBackgroundType(int type);

    QString backgroundColor() const;
    Q_INVOKABLE void setBackgroundColor(const QString &color);

    bool backgroundVisible() const;

    Q_INVOKABLE void setCursorTheme(const QString &theme);
    Q_INVOKABLE void applyFontSettings();

signals:
    void darkModeChanged();
    void dimsWallpaperChanged();
    void continuousCornersChanged();
    void accentColorIndexChanged();
    void fontPointSizeChanged();
    void fontFamilyChanged();
    void fixedFontFamilyChanged();
    void iconThemeChanged();
    void wallpaperChanged();
    void userWallpapersChanged();
    void wallpaperFoldersChanged();
    // The service finished copying a folder's images; whoever shows them
    // asks for the list again.
    void wallpaperFolderImagesChanged(const QString &folder);
    void backgroundTypeChanged();
    void backgroundColorChanged();
    void backgroundVisibleChanged();

private slots:
    void init();
    void onDarkModeChanged(bool darkMode);
    void onWallpaperChanged(const QString &path);
    void onAccentColorChanged(int accentColor);
    void onContinuousCornersChanged(bool continuousCorners);
    void onDarkModeDimsWallpaperChanged();
    void onFontPointSizeChanged();
    void onFontFamilyChanged();
    void onBackgroundTypeChanged();
    void onBackgroundColorChanged();
    void onBackgroundVisibleChanged();

private:
    QVariant serviceProperty(const char *name) const;
    void callService(const char *method);
    void callService(const char *method, const QVariant &value);

    QDBusInterface *m_interface;
    // Every rounded item asks for this as it is built, and a D-Bus round trip
    // per item is what made a menu slow to open. Read once, then kept current
    // by the service's own signal.
    mutable std::optional<bool> m_continuousCorners;
};

#endif // CUTEFISH_APPEARANCE_H
