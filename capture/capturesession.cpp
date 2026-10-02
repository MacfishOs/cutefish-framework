#include "capturesession.h"
#include "captureclient.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QLoggingCategory>
#include <QTransform>

#include <algorithm>
#include <cstring>

#include <drm_fourcc.h>
#include <fcntl.h>
#include <gbm.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <wayland-client.h>

#include "ext-image-capture-source-v1-client-protocol.h"
#include "ext-image-copy-capture-v1-client-protocol.h"
#include "linux-dmabuf-v1-client-protocol.h"

Q_LOGGING_CATEGORY(cfCapture, "cutefish.capture", QtWarningMsg)

namespace {

// The DRM node with this st_rdev, which is how the compositor names its device.
QString nodeFor(dev_t device)
{
    QDirIterator nodes(QStringLiteral("/dev/dri"), QDir::System | QDir::Files);
    while (nodes.hasNext()) {
        const QString path = nodes.next();
        struct stat info = {};
        if (stat(QFile::encodeName(path).constData(), &info) == 0 && info.st_rdev == device)
            return path;
    }
    return QString();
}

// An implicit modifier (INVALID) is what a driver without modifiers reports for
// a buffer allocated linear by flag, as amdgpu does; only the flag form makes
// one. Some backends also refuse the modifier form for LINEAR itself.
gbm_bo *allocateBo(gbm_device *gbm, const QSize &size, uint32_t format, uint64_t modifier)
{
    const auto width = uint32_t(size.width());
    const auto height = uint32_t(size.height());
    if (modifier != DRM_FORMAT_MOD_INVALID) {
        if (gbm_bo *bo = gbm_bo_create_with_modifiers2(gbm, width, height, format, &modifier, 1,
                                                       GBM_BO_USE_RENDERING))
            return bo;
        if (modifier != DRM_FORMAT_MOD_LINEAR)
            return nullptr;
    }
    return gbm_bo_create(gbm, width, height, format, GBM_BO_USE_RENDERING | GBM_BO_USE_LINEAR);
}

} // namespace

struct CaptureSessionListeners {
    static void bufferSize(void *data, ext_image_copy_capture_session_v1 *, uint32_t width,
                           uint32_t height)
    {
        auto *self = static_cast<CaptureSession *>(data);
        self->m_pendingSize = width > 0 && height > 0 && width <= 16384 && height <= 16384
                ? QSize(int(width), int(height))
                : QSize();
        self->m_pendingArgb = false;
        self->m_pendingDevice = 0;
        self->m_pendingFormats.clear();
    }

    static void dmabufDevice(void *data, ext_image_copy_capture_session_v1 *, wl_array *device)
    {
        auto *self = static_cast<CaptureSession *>(data);
        if (device->size == sizeof(dev_t))
            memcpy(&self->m_pendingDevice, device->data, sizeof(dev_t));
    }

    static void dmabufFormat(void *data, ext_image_copy_capture_session_v1 *, uint32_t format,
                             wl_array *modifiers)
    {
        const auto *begin = static_cast<const uint64_t *>(modifiers->data);
        static_cast<CaptureSession *>(data)->m_pendingFormats.emplace_back(
                format,
                std::vector<uint64_t>(begin, begin + modifiers->size / sizeof(uint64_t)));
    }

    static void shmFormat(void *data, ext_image_copy_capture_session_v1 *, uint32_t format)
    {
        if (format == WL_SHM_FORMAT_ARGB8888)
            static_cast<CaptureSession *>(data)->m_pendingArgb = true;
    }

    static void done(void *data, ext_image_copy_capture_session_v1 *)
    {
        static_cast<CaptureSession *>(data)->applyConstraints();
    }

    static void sessionStopped(void *data, ext_image_copy_capture_session_v1 *)
    {
        auto *self = static_cast<CaptureSession *>(data);
        if (self->m_stopped)
            return;
        self->m_stopped = true;
        emit self->stopped();
    }

    static void transform(void *data, ext_image_copy_capture_frame_v1 *, uint32_t transform)
    {
        static_cast<CaptureSession *>(data)->m_frameTransform = transform;
    }

    static void presentationTime(void *data, ext_image_copy_capture_frame_v1 *, uint32_t secHi,
                                 uint32_t secLo, uint32_t nsec)
    {
        const qint64 seconds = qint64((quint64(secHi) << 32) | secLo);
        static_cast<CaptureSession *>(data)->m_framePresentedNs = seconds * 1000000000 + nsec;
    }

    static void ready(void *data, ext_image_copy_capture_frame_v1 *)
    {
        auto *self = static_cast<CaptureSession *>(data);
        const int id = self->m_frameBuffer;
        CaptureSession::Frame frame;
        frame.transform = self->m_frameTransform;
        frame.presentedNs = self->m_framePresentedNs;
        self->finishFrame();

        const auto it = self->m_buffers.find(id);
        if (it == self->m_buffers.end() || it->second->retired) {
            if (it != self->m_buffers.end())
                self->freeBuffer(id);
            emit self->frameFailed();
            return;
        }
        CaptureSession::Buffer *buffer = it->second.get();
        buffer->held = true;
        frame.buffer = id;
        if (buffer->pixels)
            frame.image = QImage(buffer->pixels, buffer->size.width(), buffer->size.height(),
                                 buffer->size.width() * 4, QImage::Format_ARGB32_Premultiplied);
        frame.dmabuf = buffer->dmabuf;
        emit self->frameReady(frame);
    }

    static void failed(void *data, ext_image_copy_capture_frame_v1 *, uint32_t reason)
    {
        auto *self = static_cast<CaptureSession *>(data);
        const int id = self->m_frameBuffer;
        self->finishFrame();
        if (auto it = self->m_buffers.find(id);
            it != self->m_buffers.end() && it->second->retired && !it->second->held)
            self->freeBuffer(id);
        qCDebug(cfCapture) << "frame failed, reason" << reason;
        if (reason == EXT_IMAGE_COPY_CAPTURE_FRAME_V1_FAILURE_REASON_STOPPED) {
            if (!self->m_stopped) {
                self->m_stopped = true;
                emit self->stopped();
            }
        } else {
            // Unknown failures are retryable, and a buffer that no longer fits
            // is replaced once the new constraints arrive.
            emit self->frameFailed();
        }
    }

    static const ext_image_copy_capture_session_v1_listener s_sessionListener;
    static const ext_image_copy_capture_frame_v1_listener s_frameListener;
};

const ext_image_copy_capture_session_v1_listener CaptureSessionListeners::s_sessionListener = {
        CaptureSessionListeners::bufferSize,
        CaptureSessionListeners::shmFormat,
        CaptureSessionListeners::dmabufDevice,
        CaptureSessionListeners::dmabufFormat,
        CaptureSessionListeners::done,
        CaptureSessionListeners::sessionStopped,
};

const ext_image_copy_capture_frame_v1_listener CaptureSessionListeners::s_frameListener = {
        CaptureSessionListeners::transform,
        [](void *, ext_image_copy_capture_frame_v1 *, int32_t, int32_t, int32_t, int32_t) {},
        CaptureSessionListeners::presentationTime,
        CaptureSessionListeners::ready,
        CaptureSessionListeners::failed,
};

CaptureSession::CaptureSession(CaptureClient *client, ext_image_capture_source_v1 *source,
                               bool paintCursors, int bufferCount, Memory memory, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_bufferCount(qMax(1, bufferCount))
    , m_memory(memory)
{
    if (!source || !client->copyManager()) {
        m_stopped = true;
        if (source)
            ext_image_capture_source_v1_destroy(source);
        return;
    }
    m_session = ext_image_copy_capture_manager_v1_create_session(
            client->copyManager(), source,
            paintCursors ? EXT_IMAGE_COPY_CAPTURE_MANAGER_V1_OPTIONS_PAINT_CURSORS : 0);
    ext_image_copy_capture_session_v1_add_listener(m_session,
                                                   &CaptureSessionListeners::s_sessionListener,
                                                   this);
    // A session outlives its source.
    ext_image_capture_source_v1_destroy(source);
    m_client->flush();
}

CaptureSession::~CaptureSession()
{
    if (m_frame)
        ext_image_copy_capture_frame_v1_destroy(m_frame);
    if (m_session)
        ext_image_copy_capture_session_v1_destroy(m_session);
    while (!m_buffers.empty())
        freeBuffer(m_buffers.begin()->first);
    if (m_gbm)
        gbm_device_destroy(m_gbm);
    if (m_deviceFd >= 0)
        close(m_deviceFd);
    m_client->flush();
}

QImage CaptureSession::upright(const QImage &image, uint32_t transform)
{
    if (transform == 0)
        return image;
    QTransform matrix;
    if (transform & 4)
        matrix.scale(-1, 1);
    matrix.rotate(90 * int(transform & 3));
    return image.transformed(matrix);
}

bool CaptureSession::capture()
{
    if (m_stopped || m_frame || m_size.isEmpty())
        return false;

    const auto it = std::find_if(m_buffers.begin(), m_buffers.end(),
                                 [](const auto &entry) {
                                     return !entry.second->held && !entry.second->retired;
                                 });
    if (it == m_buffers.end())
        return false;

    Buffer *buffer = it->second.get();
    m_frameBuffer = it->first;
    m_frameTransform = 0;
    m_framePresentedNs = 0;
    m_frame = ext_image_copy_capture_session_v1_create_frame(m_session);
    ext_image_copy_capture_frame_v1_add_listener(m_frame, &CaptureSessionListeners::s_frameListener,
                                                 this);
    ext_image_copy_capture_frame_v1_attach_buffer(m_frame, buffer->buffer);
    ext_image_copy_capture_frame_v1_damage_buffer(m_frame, 0, 0, buffer->size.width(),
                                                  buffer->size.height());
    ext_image_copy_capture_frame_v1_capture(m_frame);
    m_client->flush();
    return true;
}

void CaptureSession::releaseBuffer(int id)
{
    const auto it = m_buffers.find(id);
    if (it == m_buffers.end())
        return;
    it->second->held = false;
    if (it->second->retired && id != m_frameBuffer)
        freeBuffer(id);
}

void CaptureSession::applyConstraints()
{
    // The kind of memory is chosen once; a DMA-BUF session keeps its device.
    if (m_memory == Memory::DmaBuf && !m_gbm && m_size.isEmpty() && !openDevice())
        m_memory = Memory::Shm;
    if (m_pendingSize.isEmpty() || (m_memory == Memory::Shm && !m_pendingArgb)) {
        qCWarning(cfCapture) << "no shared memory format this client can read";
        m_stopped = true;
        emit stopped();
        return;
    }
    if (m_pendingSize == m_size)
        return;

    m_size = m_pendingSize;
    for (auto it = m_buffers.begin(); it != m_buffers.end();) {
        Buffer *buffer = it->second.get();
        const int id = it->first;
        ++it;
        if (buffer->held || id == m_frameBuffer)
            buffer->retired = true;
        else
            freeBuffer(id);
    }
    if (!allocate(m_bufferCount) && !fallBackToShm()) {
        m_stopped = true;
        emit stopped();
        return;
    }
    emit constraintsChanged();
}

bool CaptureSession::openDevice()
{
    if (!m_client->dmabuf() || !m_pendingDevice)
        return false;
    // Opaque first: an encoder has no use for alpha. Linear is what the
    // compositor offers and what a video engine can read.
    for (uint32_t format : {DRM_FORMAT_XRGB8888, DRM_FORMAT_ARGB8888}) {
        const auto it = std::find_if(m_pendingFormats.begin(), m_pendingFormats.end(),
                                     [format](const auto &entry) {
                                         return entry.first == format;
                                     });
        if (it == m_pendingFormats.end() || it->second.empty())
            continue;
        const bool linear = std::find(it->second.begin(), it->second.end(), DRM_FORMAT_MOD_LINEAR)
                != it->second.end();
        m_drmFormat = format;
        m_modifier = linear ? DRM_FORMAT_MOD_LINEAR : it->second.front();
        break;
    }
    if (!m_drmFormat)
        return false;

    m_renderNode = nodeFor(m_pendingDevice);
    if (m_renderNode.isEmpty())
        return false;
    m_deviceFd = open(QFile::encodeName(m_renderNode).constData(), O_RDWR | O_CLOEXEC);
    if (m_deviceFd < 0)
        return false;
    m_gbm = gbm_create_device(m_deviceFd);
    if (!m_gbm) {
        close(m_deviceFd);
        m_deviceFd = -1;
        return false;
    }
    return true;
}

bool CaptureSession::fallBackToShm()
{
    if (m_memory != Memory::DmaBuf || !m_pendingArgb)
        return false;
    qCWarning(cfCapture, "cannot allocate DMA-BUFs on %s, capturing into shared memory",
              qPrintable(m_renderNode));
    // Only this size's buffers go; retired ones may still be held, and keep the device.
    m_memory = Memory::Shm;
    for (auto it = m_buffers.begin(); it != m_buffers.end();) {
        const int id = it->first;
        const bool retired = it->second->retired;
        ++it;
        if (!retired)
            freeBuffer(id);
    }
    return allocate(m_bufferCount);
}

bool CaptureSession::allocate(int count)
{
    for (int i = 0; i < count; ++i) {
        auto buffer = std::make_unique<Buffer>();
        buffer->size = m_size;
        const bool allocated = m_memory == Memory::DmaBuf ? allocateDmaBuf(buffer.get())
                                                          : allocateShm(buffer.get());
        const int id = m_nextBuffer++;
        m_buffers.emplace(id, std::move(buffer));
        if (!allocated) {
            freeBuffer(id);
            return false;
        }
    }
    return true;
}

bool CaptureSession::allocateShm(Buffer *buffer)
{
    buffer->length = size_t(m_size.width()) * m_size.height() * 4;
    const int fd = memfd_create("cutefish-capture", MFD_CLOEXEC);
    if (fd < 0)
        return false;
    if (ftruncate(fd, off_t(buffer->length)) != 0) {
        close(fd);
        return false;
    }
    void *pixels = mmap(nullptr, buffer->length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (pixels == MAP_FAILED) {
        close(fd);
        return false;
    }
    buffer->pixels = static_cast<uchar *>(pixels);
    wl_shm_pool *pool = wl_shm_create_pool(m_client->shm(), fd, int32_t(buffer->length));
    close(fd);
    buffer->buffer = wl_shm_pool_create_buffer(pool, 0, m_size.width(), m_size.height(),
                                               m_size.width() * 4, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool);
    return true;
}

bool CaptureSession::allocateDmaBuf(Buffer *buffer)
{
    buffer->bo = allocateBo(m_gbm, m_size, m_drmFormat, m_modifier);
    if (!buffer->bo || gbm_bo_get_plane_count(buffer->bo) != 1)
        return false;
    DmaBuf &dmabuf = buffer->dmabuf;
    dmabuf.fd = gbm_bo_get_fd(buffer->bo);
    if (dmabuf.fd < 0)
        return false;
    dmabuf.format = m_drmFormat;
    dmabuf.modifier = gbm_bo_get_modifier(buffer->bo);
    dmabuf.offset = gbm_bo_get_offset(buffer->bo, 0);
    dmabuf.stride = gbm_bo_get_stride_for_plane(buffer->bo, 0);
    dmabuf.size = m_size;

    zwp_linux_buffer_params_v1 *params = zwp_linux_dmabuf_v1_create_params(m_client->dmabuf());
    zwp_linux_buffer_params_v1_add(params, dmabuf.fd, 0, dmabuf.offset, dmabuf.stride,
                                   uint32_t(dmabuf.modifier >> 32),
                                   uint32_t(dmabuf.modifier & 0xffffffff));
    buffer->buffer = zwp_linux_buffer_params_v1_create_immed(params, m_size.width(),
                                                             m_size.height(), m_drmFormat, 0);
    zwp_linux_buffer_params_v1_destroy(params);
    return buffer->buffer != nullptr;
}

void CaptureSession::freeBuffer(int id)
{
    const auto it = m_buffers.find(id);
    if (it == m_buffers.end())
        return;
    Buffer *buffer = it->second.get();
    if (buffer->buffer)
        wl_buffer_destroy(buffer->buffer);
    if (buffer->pixels)
        munmap(buffer->pixels, buffer->length);
    if (buffer->dmabuf.fd >= 0)
        close(buffer->dmabuf.fd);
    if (buffer->bo)
        gbm_bo_destroy(buffer->bo);
    m_buffers.erase(it);
}

void CaptureSession::finishFrame()
{
    if (m_frame)
        ext_image_copy_capture_frame_v1_destroy(m_frame);
    m_frame = nullptr;
    m_frameBuffer = -1;
}
