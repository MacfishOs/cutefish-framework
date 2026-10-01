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
            [this] { m_surfaceDying = true; }, Qt::DirectConnection);
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
        }
    }
    return QObject::eventFilter(object, event);
}

void WindowHints::apply()
{
    if (!available() || m_surfaceDying || !m_window->handle())
        return;
    auto *native = qGuiApp->platformNativeInterface();
    auto *surface = static_cast<wl_surface *>(
            native->nativeResourceForWindow(QByteArrayLiteral("surface"), m_window));
    WaylandWindowHints::instance()->setSkipTaskbar(surface, m_skipTaskbar);
    WaylandWindowHints::instance()->setAlwaysOnTop(surface, m_alwaysOnTop);
    WaylandWindowHints::instance()->setResizeAroundCenter(surface, m_resizeAroundCenter);
}

} // namespace Cutefish
