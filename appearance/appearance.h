#ifndef CUTEFISH_APPEARANCE_H
#define CUTEFISH_APPEARANCE_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariant>

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
    Q_PROPERTY(QString fixedFontFamily READ fixedFontFamily WRITE setFixedFontFamily NOTIFY
                       fixedFontFamilyChanged)
    Q_PROPERTY(QString wallpaper READ wallpaper WRITE setWallpaper NOTIFY wallpaperChanged)
    Q_PROPERTY(QStringList userWallpapers READ userWallpapers NOTIFY userWallpapersChanged)
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

    QString wallpaper() const;
    Q_INVOKABLE void setWallpaper(const QString &path);
    QStringList userWallpapers() const;
    Q_INVOKABLE void removeUserWallpaper(const QString &path);

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
    void wallpaperChanged();
    void userWallpapersChanged();
    void backgroundTypeChanged();
    void backgroundColorChanged();
    void backgroundVisibleChanged();

private slots:
    void init();
    void onDarkModeChanged(bool darkMode);
    void onWallpaperChanged(const QString &path);
    void onAccentColorChanged(int accentColor);
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
};

#endif // CUTEFISH_APPEARANCE_H
