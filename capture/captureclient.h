#ifndef CUTEFISH_CAPTURECLIENT_H
#define CUTEFISH_CAPTURECLIENT_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QRect>
#include <QString>

#include <memory>
#include <vector>

struct wl_display;
struct wl_output;
struct wl_registry;
struct wl_shm;
struct zwp_linux_dmabuf_v1;
struct zxdg_output_manager_v1;
struct zxdg_output_v1;
struct ext_output_image_capture_source_manager_v1;
struct ext_foreign_toplevel_image_capture_source_manager_v1;
struct ext_image_copy_capture_manager_v1;
struct ext_image_capture_source_v1;
struct ext_foreign_toplevel_list_v1;
struct ext_foreign_toplevel_handle_v1;

// The compositor's capture globals on a connection's default queue: Qt's in
// the shell, which Qt dispatches, or the recorder's own.
class CaptureClient : public QObject
{
    Q_OBJECT

public:
    struct Output {
        wl_output *output = nullptr;
        zxdg_output_v1 *xdgOutput = nullptr;
        uint32_t globalName = 0;
        //! The wl_output name, e.g. eDP-1.
        QString name;
        //! Where the output is in the compositor's global logical space.
        QRect logicalGeometry;
    };

    explicit CaptureClient(wl_display *display, QObject *parent = nullptr);
    ~CaptureClient() override;

    //! Binds the globals and learns the outputs and windows; blocks for the
    //! round trips that takes. False when this client may not capture.
    bool initialize();

    bool canCaptureOutputs() const;
    bool canCaptureWindows() const;

    wl_display *display() const { return m_display; }
    wl_shm *shm() const { return m_shm; }
    ext_image_copy_capture_manager_v1 *copyManager() const { return m_copyManager; }
    //! Null when frames can only go into shared memory.
    zwp_linux_dmabuf_v1 *dmabuf() const { return m_dmabuf; }

    QList<Output> outputs() const;
    const Output *outputNamed(const QString &name) const;

    //! The ext_foreign_toplevel_handle_v1 identifier the compositor gives the
    //! window with this cutefish_window_management_v1 id.
    static QString windowIdentifier(quint64 windowId);

    //! New sources, owned by the caller; null when there is nothing to capture.
    ext_image_capture_source_v1 *createOutputSource(const QString &outputName);
    ext_image_capture_source_v1 *createWindowSource(const QString &identifier);

    void flush();

private:
    friend struct CaptureClientListeners;

    struct Toplevel {
        ext_foreign_toplevel_handle_v1 *handle = nullptr;
        QString identifier;
    };

    void roundtrip();
    void dropClosedToplevels();

    wl_display *m_display = nullptr;
    wl_registry *m_registry = nullptr;
    wl_shm *m_shm = nullptr;
    zxdg_output_manager_v1 *m_xdgOutputManager = nullptr;
    ext_output_image_capture_source_manager_v1 *m_outputSources = nullptr;
    ext_foreign_toplevel_image_capture_source_manager_v1 *m_toplevelSources = nullptr;
    ext_image_copy_capture_manager_v1 *m_copyManager = nullptr;
    zwp_linux_dmabuf_v1 *m_dmabuf = nullptr;
    ext_foreign_toplevel_list_v1 *m_toplevelList = nullptr;
    std::vector<std::unique_ptr<Output>> m_outputs;
    std::vector<std::unique_ptr<Toplevel>> m_toplevels;
};

#endif // CUTEFISH_CAPTURECLIENT_H
