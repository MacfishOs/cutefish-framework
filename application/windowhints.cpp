#include "windowhints.h"
#include "waylandwindowhints_p.h"

#include <QGuiApplication>
#include <QPlatformSurfaceEvent>
#include <QtGui/qguiapplication_platform.h>
#include <qpa/qplatformnativeinterface.h>
#include <qpa/qplatformwindow_p.h>

namespace Cutefish {

WindowHints::WindowHints(QObject *parent)
    : QObject(parent)
    , m_window(qobject_cast<QWindow *>(parent))
{
    if (!m_window)
        return;
    connect(WaylandWindowHints::instance(), &WaylandWindowHints::availableChanged,
            this, [this] {
                Q_EMIT availableChanged();
                apply();
            });
    connect(WaylandWindowHints::instance(), &WaylandWindowHints::interactiveResize,
            this, [this](wl_surface *surface, bool active) {
                if (surface && surface == this->surface())
                    setInteractiveResize(active);
            });
    m_window->installEventFilter(this);
    watchSurface();
}

WindowHints *WindowHints::qmlAttachedProperties(QObject *object)
{
    return new WindowHints(object);
}

WindowHints *WindowHints::forWindow(QWindow *window)
{
    return window ? qobject_cast<WindowHints *>(qmlAttachedPropertiesObject<WindowHints>(window, true))
                  : nullptr;
}

void WindowHints::setSkipTaskbar(bool skip)
{
    if (m_skipTaskbar == skip)
        return;
    m_skipTaskbar = skip;
    apply();
    Q_EMIT skipTaskbarChanged();
}

void WindowHints::setAlwaysOnTop(bool alwaysOnTop)
{
    if (m_alwaysOnTop == alwaysOnTop)
        return;
    m_alwaysOnTop = alwaysOnTop;
    apply();
    Q_EMIT alwaysOnTopChanged();
}

void WindowHints::setResizeAroundCenter(bool center)
{
    if (m_resizeAroundCenter == center)
        return;
    m_resizeAroundCenter = center;
    apply();
    Q_EMIT resizeAroundCenterChanged();
}

void WindowHints::setServerResize(bool enabled)
{
    if (m_serverResize == enabled)
        return;
    m_serverResize = enabled;
    apply();
    Q_EMIT serverResizeChanged();
}

void WindowHints::setRestoreId(const QString &id)
{
    if (m_restoreId == id)
        return;
    m_restoreId = id;
    apply();
    Q_EMIT restoreIdChanged();
}

void WindowHints::setInteractiveResize(bool active)
{
    if (m_interactiveResize == active)
        return;
    m_interactiveResize = active;
    Q_EMIT interactiveResizeChanged();
}

bool WindowHints::available() const
{
    return m_window && WaylandWindowHints::instance()->available();
}

void WindowHints::watchSurface()
{
    if (!m_window || !m_window->handle()
        || !qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>())
        return;
    auto *native = m_window->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
    if (!native)
        return;
    disconnect(m_createdConnection);
    disconnect(m_destroyedConnection);
    m_createdConnection = connect(
            native, &QNativeInterface::Private::QWaylandWindow::surfaceCreated, this,
            [this] {
                m_surfaceDying = false;
                apply();
            }, Qt::DirectConnection);
    m_destroyedConnection = connect(
            native, &QNativeInterface::Private::QWaylandWindow::surfaceDestroyed, this,
            [this] {
                m_surfaceDying = true;
                setInteractiveResize(false);
            }, Qt::DirectConnection);
}

bool WindowHints::eventFilter(QObject *object, QEvent *event)
{
    if (object == m_window && event->type() == QEvent::PlatformSurface) {
        auto *surfaceEvent = static_cast<QPlatformSurfaceEvent *>(event);
        m_surfaceDying = surfaceEvent->surfaceEventType()
                == QPlatformSurfaceEvent::SurfaceAboutToBeDestroyed;
        if (!m_surfaceDying) {
            watchSurface();
            apply();
        } else {
            // A new surface starts outside any resize.
            setInteractiveResize(false);
        }
    }
    return QObject::eventFilter(object, event);
}

wl_surface *WindowHints::surface() const
{
    if (!available() || m_surfaceDying || !m_window->handle())
        return nullptr;
    auto *native = qGuiApp->platformNativeInterface();
    return static_cast<wl_surface *>(
            native->nativeResourceForWindow(QByteArrayLiteral("surface"), m_window));
}

void WindowHints::apply()
{
    wl_surface *surface = this->surface();
    if (!surface)
        return;
    WaylandWindowHints::instance()->setSkipTaskbar(surface, m_skipTaskbar);
    WaylandWindowHints::instance()->setAlwaysOnTop(surface, m_alwaysOnTop);
    WaylandWindowHints::instance()->setResizeAroundCenter(surface, m_resizeAroundCenter);
    WaylandWindowHints::instance()->setServerResize(surface, m_serverResize);
    WaylandWindowHints::instance()->setRestoreId(surface, m_restoreId);
}

} // namespace Cutefish
