#include "waylandwindowhints_p.h"

#include <QGuiApplication>
#include <QtGui/qguiapplication_platform.h>
#include <wayland-client.h>
#include <algorithm>
#include <cstring>

#include "cutefish-window-hints-v1-client-protocol.h"

namespace Cutefish {

static void flushDisplay()
{
    auto *native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    wl_display_flush(native->display());
}

WaylandWindowHints *WaylandWindowHints::instance()
{
    static auto *manager = new WaylandWindowHints(qApp);
    return manager;
}

WaylandWindowHints::WaylandWindowHints(QObject *parent)
    : QObject(parent)
{
    auto *native = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    if (!native)
        return;
    m_registry = wl_display_get_registry(native->display());
    static const wl_registry_listener listener = {global, globalRemove};
    wl_registry_add_listener(m_registry, &listener, this);
    // Bind before the first window maps, so its initial hint cannot arrive late.
    wl_display_roundtrip(native->display());
}

WaylandWindowHints::~WaylandWindowHints()
{
    if (m_manager)
        cutefish_window_hints_v1_destroy(m_manager);
    if (m_registry)
        wl_registry_destroy(m_registry);
}

void WaylandWindowHints::global(void *data, wl_registry *registry, uint32_t name,
                                const char *interface, uint32_t)
{
    auto *self = static_cast<WaylandWindowHints *>(data);
    if (std::strcmp(interface, "cutefish_window_hints_v1") != 0 || self->m_manager)
        return;
    self->m_manager = static_cast<cutefish_window_hints_v1 *>(
            wl_registry_bind(registry, name, &cutefish_window_hints_v1_interface, 1));
    self->m_name = name;
    static const cutefish_window_hints_v1_listener listener = {handleInteractiveResize};
    cutefish_window_hints_v1_add_listener(self->m_manager, &listener, self);
    Q_EMIT self->availableChanged();
}

void WaylandWindowHints::globalRemove(void *data, wl_registry *, uint32_t name)
{
    auto *self = static_cast<WaylandWindowHints *>(data);
    if (name != self->m_name || !self->m_manager)
        return;
    cutefish_window_hints_v1_destroy(self->m_manager);
    self->m_manager = nullptr;
    self->m_name = 0;
    Q_EMIT self->availableChanged();
}

void WaylandWindowHints::handleInteractiveResize(void *data, cutefish_window_hints_v1 *,
                                                 wl_surface *surface, uint32_t active)
{
    Q_EMIT static_cast<WaylandWindowHints *>(data)->interactiveResize(surface, active != 0);
}

void WaylandWindowHints::setSkipTaskbar(wl_surface *surface, bool skip)
{
    if (!m_manager || !surface)
        return;
    cutefish_window_hints_v1_set_skip_taskbar(m_manager, surface, skip);
    flushDisplay();
}

void WaylandWindowHints::setAlwaysOnTop(wl_surface *surface, bool alwaysOnTop)
{
    if (!m_manager || !surface)
        return;
    cutefish_window_hints_v1_set_always_on_top(m_manager, surface, alwaysOnTop);
    flushDisplay();
}

void WaylandWindowHints::setResizeAroundCenter(wl_surface *surface, bool center)
{
    if (!m_manager || !surface)
        return;
    cutefish_window_hints_v1_set_resize_anchor(m_manager, surface,
            center ? CUTEFISH_WINDOW_HINTS_V1_RESIZE_ANCHOR_CENTER
                   : CUTEFISH_WINDOW_HINTS_V1_RESIZE_ANCHOR_TOP_LEFT);
    flushDisplay();
}

void WaylandWindowHints::setServerResize(wl_surface *surface, bool enabled)
{
    if (!m_manager || !surface)
        return;
    cutefish_window_hints_v1_set_server_resize(m_manager, surface, enabled);
    flushDisplay();
}

void WaylandWindowHints::setRestoreId(wl_surface *surface, const QString &id)
{
    if (!m_manager || !surface)
        return;
    cutefish_window_hints_v1_set_restore_id(m_manager, surface, id.toUtf8().constData());
    flushDisplay();
}

void WaylandWindowHints::setRestoreSize(wl_surface *surface, const QSize &size)
{
    if (!m_manager || !surface)
        return;
    cutefish_window_hints_v1_set_restore_size(m_manager, surface, std::max(size.width(), 0),
                                              std::max(size.height(), 0));
    flushDisplay();
}

} // namespace Cutefish
