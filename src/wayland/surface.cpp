// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "surface_p.h"
#include "wayland-viewporter-client-protocol.h"
#include <QGuiApplication>
#include <QPainter>
#include <QPointer>
#include <QThread>
#include <QWindow>
#include <QtGui/qpa/qplatformwindow_p.h>
#include <cstring>
#include <stereo-declare.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

namespace Stereo
{
namespace
{
struct Globals {
    wl_compositor *compositor = nullptr;
    wl_subcompositor *subcompositor = nullptr;
    wl_shm *shm = nullptr;
    wp_viewporter *viewporter = nullptr;
    bool stereo = false;
};

void registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    auto *globals = static_cast<Globals *>(data);
    if (!strcmp(interface, wl_compositor_interface.name)) {
        globals->compositor = static_cast<wl_compositor *>(wl_registry_bind(registry, name, &wl_compositor_interface, qMin(version, 4u)));
    } else if (!strcmp(interface, wl_subcompositor_interface.name)) {
        globals->subcompositor = static_cast<wl_subcompositor *>(wl_registry_bind(registry, name, &wl_subcompositor_interface, 1));
    } else if (!strcmp(interface, wl_shm_interface.name)) {
        globals->shm = static_cast<wl_shm *>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
    } else if (!strcmp(interface, wp_viewporter_interface.name)) {
        globals->viewporter = static_cast<wp_viewporter *>(wl_registry_bind(registry, name, &wp_viewporter_interface, 1));
    } else if (!strcmp(interface, "kde_stereo_content_manager_v1")) {
        globals->stereo = true;
    }
}

void registryGlobalRemove(void *, wl_registry *, uint32_t) { }

const wl_registry_listener registryListener = { registryGlobal, registryGlobalRemove };

wl_display *waylandDisplay()
{
    if (QGuiApplication::platformName() != QLatin1String("wayland")) {
        return nullptr;
    }
    auto *app = qGuiApp->nativeInterface<QNativeInterface::QWaylandApplication>();
    return app ? app->display() : nullptr;
}

wl_registry *readGlobals(wl_display *display, wl_event_queue *queue, Globals &globals)
{
    auto *wrapper = static_cast<wl_display *>(wl_proxy_create_wrapper(display));
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wrapper), queue);
    wl_registry *registry = wl_display_get_registry(wrapper);
    wl_proxy_wrapper_destroy(wrapper);
    wl_registry_add_listener(registry, &registryListener, &globals);
    wl_display_roundtrip_queue(display, queue);
    return registry;
}

void destroyGlobals(Globals &globals)
{
    if (globals.compositor) {
        wl_compositor_destroy(globals.compositor);
    }
    if (globals.subcompositor) {
        wl_subcompositor_destroy(globals.subcompositor);
    }
    if (globals.shm) {
        wl_shm_destroy(globals.shm);
    }
    if (globals.viewporter) {
        wp_viewporter_destroy(globals.viewporter);
    }
    globals = {};
}

struct Buffer {
    wl_buffer *buffer = nullptr;
    uchar *data = nullptr;
    size_t size = 0;
    QSize pixels;
    bool alpha = false;
    bool busy = false;
};

void bufferRelease(void *data, wl_buffer *) { static_cast<Buffer *>(data)->busy = false; }

const wl_buffer_listener bufferListener = { bufferRelease };

void destroyBuffer(Buffer &buffer)
{
    if (buffer.buffer) {
        wl_buffer_destroy(buffer.buffer);
    }
    if (buffer.data) {
        munmap(buffer.data, buffer.size);
    }
    buffer = {};
}

bool createBuffer(Buffer &buffer, wl_shm *shm, wl_event_queue *queue, const QSize &pixels, bool alpha)
{
    if (pixels.isEmpty() || pixels.width() > 16384 || pixels.height() > 16384 || qint64(pixels.width()) * pixels.height() > 128 * 1024 * 1024) {
        return false;
    }
    const int stride = pixels.width() * 4;
    const size_t size = size_t(stride) * pixels.height();
    const int fd = memfd_create("stereo-layer", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, off_t(size)) < 0) {
        if (fd >= 0) {
            close(fd);
        }
        return false;
    }
    void *data = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return false;
    }
    wl_shm_pool *pool = wl_shm_create_pool(shm, fd, int(size));
    close(fd);
    // Premultiplied alpha where the picture has some (wl_shm, ARGB8888), so that what is clear in it shows what is behind
    wl_buffer *wlBuffer
        = wl_shm_pool_create_buffer(pool, 0, pixels.width(), pixels.height(), stride, alpha ? WL_SHM_FORMAT_ARGB8888 : WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wlBuffer), queue);
    buffer.buffer = wlBuffer;
    buffer.data = static_cast<uchar *>(data);
    buffer.size = size;
    buffer.pixels = pixels;
    buffer.alpha = alpha;
    wl_buffer_add_listener(wlBuffer, &bufferListener, &buffer);
    return true;
}

struct Connection {
    wl_display *display = nullptr;
    wl_event_queue *queue = nullptr;
    wl_registry *registry = nullptr;
    Globals globals;
    bool initialized = false;
    Connection()
    {
        Q_ASSERT(QThread::currentThread() == qGuiApp->thread());
        display = waylandDisplay();
        if (!display)
            return;
        queue = wl_display_create_queue(display);
        registry = readGlobals(display, queue, globals);
        if (globals.compositor && globals.subcompositor && globals.shm && globals.viewporter && globals.stereo) {
            initialized = stereo_declare_wayland_init(display, queue) == 0;
            if (initialized)
                wl_display_roundtrip_queue(display, queue);
        }
    }
    ~Connection()
    {
        if (initialized)
            stereo_declare_wayland_finish();
        destroyGlobals(globals);
        if (registry)
            wl_registry_destroy(registry);
        if (queue)
            wl_event_queue_destroy(queue);
        if (display)
            wl_display_flush(display);
    }
    bool supported() const { return initialized && stereo_supported_wayland(); }
};
std::shared_ptr<Connection> connection()
{
    static std::weak_ptr<Connection> shared;
    auto result = shared.lock();
    if (!result) {
        result = std::make_shared<Connection>();
        shared = result;
    }
    return result;
}
}

struct Surface::Private {
    std::shared_ptr<Connection> connection;
    QPointer<QWindow> parent;
    wl_surface *parentSurface = nullptr;
    wl_surface *surface = nullptr;
    wl_subsurface *subsurface = nullptr;
    wp_viewport *viewport = nullptr;
    Buffer buffers[3];
    bool declared = false;
    bool shown = false;
    QRect placed;
    bool alpha = false;
};
Surface::Surface()
    : d(std::make_unique<Private>())
{
}
Surface::~Surface() { reset(); }
bool Surface::isSupported() { return connection()->supported(); }
void Surface::reset()
{
    if (d->declared)
        stereo_remove_wayland(d->surface);
    if (d->viewport)
        wp_viewport_destroy(d->viewport);
    if (d->subsurface)
        wl_subsurface_destroy(d->subsurface);
    if (d->surface)
        wl_surface_destroy(d->surface);
    for (auto &buffer : d->buffers)
        destroyBuffer(buffer);
    if (d->parent)
        d->parent->requestUpdate();
    d = std::make_unique<Private>();
}
void Surface::hide()
{
    if (d->surface && d->shown) {
        wl_surface_attach(d->surface, nullptr, 0, 0);
        wl_surface_commit(d->surface);
        wl_display_flush(d->connection->display);
        d->shown = false;
    }
}
bool Surface::show(QWindow *parent, const QRect &rect, const QImage &packed)
{
    Q_ASSERT(QThread::currentThread() == qGuiApp->thread());
    if (!parent || !parent->isVisible() || rect.isEmpty() || packed.isNull()) {
        hide();
        return false;
    }
    if (QGuiApplication::platformName() != QLatin1String("wayland"))
        return false;
    auto *native = parent->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
    if (!native || !native->surface())
        return false;
    if (d->parent != parent || d->parentSurface != native->surface())
        reset();
    if (!d->surface) {
        d->connection = connection();
        if (!d->connection->supported())
            return false;
        d->parent = parent;
        d->parentSurface = native->surface();
        auto &globals = d->connection->globals;
        d->surface = wl_compositor_create_surface(globals.compositor);
        d->subsurface = wl_subcompositor_get_subsurface(globals.subcompositor, d->surface, native->surface());
        wl_subsurface_place_below(d->subsurface, native->surface());
        wl_subsurface_set_desync(d->subsurface);
        d->viewport = wp_viewporter_get_viewport(globals.viewporter, d->surface);
        wl_region *empty = wl_compositor_create_region(globals.compositor);
        wl_surface_set_input_region(d->surface, empty);
        wl_region_destroy(empty);
    }
    auto &c = *d->connection;
    wl_display_dispatch_queue_pending(c.display, c.queue);
    const bool alpha = packed.hasAlphaChannel();
    Buffer *buffer = nullptr;
    for (auto &candidate : d->buffers) {
        if (candidate.busy)
            continue;
        if (candidate.buffer && (candidate.pixels != packed.size() || candidate.alpha != alpha))
            destroyBuffer(candidate);
        if (!candidate.buffer && !createBuffer(candidate, c.globals.shm, c.queue, packed.size(), alpha))
            continue;
        buffer = &candidate;
        break;
    }
    if (!buffer)
        return d->shown;
    QImage target(
        buffer->data, packed.width(), packed.height(), packed.width() * 4, alpha ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32);
    QPainter painter(&target);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.drawImage(0, 0, packed);
    painter.end();
    // The parent surface starts at Qt's own decoration, when it draws one.
    const QRect placed = rect.translated(parent->frameMargins().left(), parent->frameMargins().top());
    if (d->placed != placed || d->alpha != alpha) {
        wl_subsurface_set_position(d->subsurface, placed.x(), placed.y());
        wp_viewport_set_destination(d->viewport, rect.width(), rect.height());
        wl_region *opaque = wl_compositor_create_region(c.globals.compositor);
        if (!alpha)
            wl_region_add(opaque, 0, 0, rect.width(), rect.height());
        wl_surface_set_opaque_region(d->surface, opaque);
        wl_region_destroy(opaque);
        d->placed = placed;
        d->alpha = alpha;
        parent->requestUpdate();
    }
    if (!d->declared) {
        const int result = stereo_declare_wayland(d->surface, STEREO_SBS_FULL, STEREO_CLASS_NONE, STEREO_SUBCLASS_UNSPECIFIED);
        if (result < 0)
            return false;
        d->declared = true;
    }
    buffer->busy = true;
    wl_surface_attach(d->surface, buffer->buffer, 0, 0);
    wl_surface_damage_buffer(d->surface, 0, 0, packed.width(), packed.height());
    wl_surface_commit(d->surface);
    wl_display_flush(c.display);
    d->shown = true;
    return true;
}
}
