// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "waylandactivation_p.h"

#include <QGuiApplication>
#include <QWindow>
#include <QtGui/qguiapplication_platform.h>
#include <qpa/qplatformnativeinterface.h>
#include <wayland-client.h>
#include <cstring>

#include "xdg-activation-v1-client-protocol.h"

namespace Cutefish {

namespace {

struct Request {
    xdg_activation_v1 *activation = nullptr;
    QString token;
    bool done = false;
};

void global(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t)
{
    auto *request = static_cast<Request *>(data);
    if (std::strcmp(interface, xdg_activation_v1_interface.name) != 0 || request->activation)
        return;
    request->activation = static_cast<xdg_activation_v1 *>(
            wl_registry_bind(registry, name, &xdg_activation_v1_interface, 1));
}

void globalRemove(void *, wl_registry *, uint32_t)
{
}

void tokenDone(void *data, xdg_activation_token_v1 *, const char *token)
{
    auto *request = static_cast<Request *>(data);
    request->token = QString::fromUtf8(token);
    request->done = true;
}

} // namespace

QString WaylandActivation::requestToken()
{
    auto *native = qGuiApp ? qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>()
                           : nullptr;
    QWindow *window = QGuiApplication::focusWindow();
    if (!native || !window || !window->handle() || !native->lastInputSeat())
        return QString();
    auto *surface = static_cast<wl_surface *>(qGuiApp->platformNativeInterface()
            ->nativeResourceForWindow(QByteArrayLiteral("surface"), window));
    if (!surface)
        return QString();

    // A private queue, so the wait below never dispatches Qt's own events.
    wl_display *display = native->display();
    wl_event_queue *queue = wl_display_create_queue(display);
    auto *wrapper = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wrapper), queue);
    wl_registry *registry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper);

    Request request;
    static const wl_registry_listener registryListener = {global, globalRemove};
    wl_registry_add_listener(registry, &registryListener, &request);
    wl_display_roundtrip_queue(display, queue);

    if (request.activation) {
        xdg_activation_token_v1 *token = xdg_activation_v1_get_activation_token(request.activation);
        static const xdg_activation_token_v1_listener tokenListener = {tokenDone};
        xdg_activation_token_v1_add_listener(token, &tokenListener, &request);
        xdg_activation_token_v1_set_serial(token, native->lastInputSerial(),
                                           native->lastInputSeat());
        xdg_activation_token_v1_set_surface(token, surface);
        xdg_activation_token_v1_commit(token);
        while (!request.done && wl_display_dispatch_queue(display, queue) != -1) {
        }
        xdg_activation_token_v1_destroy(token);
        xdg_activation_v1_destroy(request.activation);
    }

    wl_registry_destroy(registry);
    wl_event_queue_destroy(queue);

    return request.token;
}

} // namespace Cutefish
