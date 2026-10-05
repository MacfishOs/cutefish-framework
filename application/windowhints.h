#ifndef CUTEFISH_WINDOWHINTS_H
#define CUTEFISH_WINDOWHINTS_H

#include <QObject>
#include <QPointer>
#include <QWindow>
#include <QtQml/qqml.h>

struct wl_surface;

namespace Cutefish {

class WindowHints : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool skipTaskbar READ skipTaskbar WRITE setSkipTaskbar NOTIFY skipTaskbarChanged)
    Q_PROPERTY(bool alwaysOnTop READ alwaysOnTop WRITE setAlwaysOnTop NOTIFY alwaysOnTopChanged)
    // When the window resizes itself, keep its centre rather than its top-left corner.
    Q_PROPERTY(bool resizeAroundCenter READ resizeAroundCenter WRITE setResizeAroundCenter
                       NOTIFY resizeAroundCenterChanged)
    // The compositor owns the resize handles along the window's edges.
    Q_PROPERTY(bool serverResize READ serverResize WRITE setServerResize NOTIFY serverResizeChanged)
    // Non-empty: the window reopens where the last one with this id was left,
    // for the rest of the session. Set it before the window is first shown.
    Q_PROPERTY(QString restoreId READ restoreId WRITE setRestoreId NOTIFY restoreIdChanged)
    // True while the user resizes the window by an edge or corner.
    Q_PROPERTY(bool interactiveResize READ interactiveResize NOTIFY interactiveResizeChanged)
    Q_PROPERTY(bool available READ available NOTIFY availableChanged)

public:
    explicit WindowHints(QObject *parent);
    static WindowHints *qmlAttachedProperties(QObject *object);
    static WindowHints *forWindow(QWindow *window);

    bool skipTaskbar() const { return m_skipTaskbar; }
    void setSkipTaskbar(bool skip);
    bool alwaysOnTop() const { return m_alwaysOnTop; }
    void setAlwaysOnTop(bool alwaysOnTop);
    bool resizeAroundCenter() const { return m_resizeAroundCenter; }
    void setResizeAroundCenter(bool center);
    bool serverResize() const { return m_serverResize; }
    void setServerResize(bool enabled);
    QString restoreId() const { return m_restoreId; }
    void setRestoreId(const QString &id);
    bool interactiveResize() const { return m_interactiveResize; }
    bool available() const;

Q_SIGNALS:
    void skipTaskbarChanged();
    void alwaysOnTopChanged();
    void resizeAroundCenterChanged();
    void serverResizeChanged();
    void restoreIdChanged();
    void interactiveResizeChanged();
    void availableChanged();

protected:
    bool eventFilter(QObject *object, QEvent *event) override;

private:
    void watchSurface();
    void apply();
    wl_surface *surface() const;
    void setInteractiveResize(bool active);

    QPointer<QWindow> m_window;
    QMetaObject::Connection m_createdConnection;
    QMetaObject::Connection m_destroyedConnection;
    bool m_skipTaskbar = false;
    bool m_alwaysOnTop = false;
    bool m_resizeAroundCenter = false;
    bool m_serverResize = false;
    QString m_restoreId;
    bool m_interactiveResize = false;
    bool m_surfaceDying = false;
};

} // namespace Cutefish

QML_DECLARE_TYPEINFO(Cutefish::WindowHints, QML_HAS_ATTACHED_PROPERTIES)

#endif
