#ifndef CUTEFISH_WAYLANDWINDOWHINTS_P_H
#define CUTEFISH_WAYLANDWINDOWHINTS_P_H

#include <QObject>
#include <cstdint>

struct wl_registry;
struct wl_surface;
struct cutefish_window_hints_v1;

namespace Cutefish {

class WaylandWindowHints : public QObject
{
    Q_OBJECT

public:
    static WaylandWindowHints *instance();
    ~WaylandWindowHints() override;
    bool available() const { return m_manager != nullptr; }
    void setSkipTaskbar(wl_surface *surface, bool skip);
    void setAlwaysOnTop(wl_surface *surface, bool alwaysOnTop);

Q_SIGNALS:
    void availableChanged();

private:
    explicit WaylandWindowHints(QObject *parent);
    static void global(void *data, wl_registry *registry, uint32_t name,
                       const char *interface, uint32_t version);
    static void globalRemove(void *data, wl_registry *, uint32_t name);

    wl_registry *m_registry = nullptr;
    cutefish_window_hints_v1 *m_manager = nullptr;
    uint32_t m_name = 0;
};

} // namespace Cutefish

#endif
