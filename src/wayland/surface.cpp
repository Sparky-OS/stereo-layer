// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com>
#include "surface_p.h"
#include "wayland-viewporter-client-protocol.h"
#include <QGuiApplication>
#include <QLoggingCategory>
#include <QPainter>
#include <QPointer>
#include <QSet>
#include <QThread>
#include <QWindow>
#include <QtGui/qpa/qplatformwindow_p.h>
#include <cmath>
#include <cstring>
#include <numeric>
#include <stereo-declare.h>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client.h>

Q_LOGGING_CATEGORY(qLcLayer, "org.sparky.stereo.layer")

namespace Stereo
{
namespace
{
// The message every player shows: what failed, and that the picture is then shown flat
QString failureMessage(const QString &what)
{
    return QStringLiteral("the compositor's stereo content declaration failed (%1): the picture is shown flat, one eye, not in 3D").arg(what);
}
struct Globals {
    wl_compositor *compositor = nullptr;
    wl_subcompositor *subcompositor = nullptr;
    wl_shm *shm = nullptr;
    wp_viewporter *viewporter = nullptr;
    QSet<uint32_t> shmFormats;
    bool stereo = false;
};

void shmFormat(void *data, wl_shm *, uint32_t format)
{
    static_cast<Globals *>(data)->shmFormats.insert(format);
}
const wl_shm_listener shmListener = {shmFormat};

void registryGlobal(void *data, wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    auto *globals = static_cast<Globals *>(data);
    if (!strcmp(interface, wl_compositor_interface.name)) {
        globals->compositor = static_cast<wl_compositor *>(wl_registry_bind(registry, name, &wl_compositor_interface, qMin(version, 4u)));
    } else if (!strcmp(interface, wl_subcompositor_interface.name)) {
        globals->subcompositor = static_cast<wl_subcompositor *>(wl_registry_bind(registry, name, &wl_subcompositor_interface, 1));
    } else if (!strcmp(interface, wl_shm_interface.name)) {
        globals->shm = static_cast<wl_shm *>(wl_registry_bind(registry, name, &wl_shm_interface, 1));
        wl_shm_add_listener(globals->shm, &shmListener, globals);
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
    bool deep = false;
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

bool createBuffer(Buffer &buffer, wl_shm *shm, wl_event_queue *queue, const QSize &pixels, bool alpha, bool deep)
{
    if (pixels.isEmpty() || pixels.width() > 16384 || pixels.height() > 16384 || qint64(pixels.width()) * pixels.height() * (deep ? 8 : 4) > 512 * 1024 * 1024) {
        return false;
    }
    const int stride = pixels.width() * (deep ? 8 : 4);
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
    const uint32_t format = deep ? (alpha ? WL_SHM_FORMAT_ABGR16161616 : WL_SHM_FORMAT_XBGR16161616)
                                 : (alpha ? WL_SHM_FORMAT_ARGB8888 : WL_SHM_FORMAT_XRGB8888);
    wl_buffer *wlBuffer = wl_shm_pool_create_buffer(pool, 0, pixels.width(), pixels.height(), stride, format);
    wl_shm_pool_destroy(pool);
    wl_proxy_set_queue(reinterpret_cast<wl_proxy *>(wlBuffer), queue);
    buffer.buffer = wlBuffer;
    buffer.data = static_cast<uchar *>(data);
    buffer.size = size;
    buffer.pixels = pixels;
    buffer.alpha = alpha;
    buffer.deep = deep;
    wl_buffer_add_listener(wlBuffer, &bufferListener, &buffer);
    return true;
}

struct Connection {
    wl_display *display = nullptr;
    wl_event_queue *queue = nullptr;
    wl_registry *registry = nullptr;
    Globals globals;
    bool initialized = false;
    bool announced = false; // the compositor announces the stereo global
    int initError = 0;
    Connection()
    {
        Q_ASSERT(QThread::currentThread() == qGuiApp->thread());
        display = waylandDisplay();
        if (!display)
            return;
        queue = wl_display_create_queue(display);
        registry = readGlobals(display, queue, globals);
        if (globals.compositor && globals.subcompositor && globals.shm && globals.viewporter && globals.stereo) {
            announced = true;
            initError = -stereo_declare_wayland_init(display, queue);
            initialized = initError == 0;
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
    // The compositor announces the stereo global and the library still cannot declare: said, never a silent flat picture
    QString failure() const
    {
        if (!announced || supported())
            return QString();
        if (!initialized)
            return failureMessage(QStringLiteral("the declaration library could not start: %1").arg(QString::fromLocal8Bit(strerror(initError))));
        return failureMessage(QStringLiteral("the library did not take the compositor's stereo global"));
    }
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

SharedConnection sharedConnection()
{
    auto shared = connection();
    SharedConnection result;
    result.keep = shared;
    result.failure = shared->failure();
    if (shared->supported()) {
        result.display = shared->display;
        result.queue = shared->queue;
    }
    return result;
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
    QString failureText;
    bool shown = false;
    bool above = false;
    bool stereo = true;
    QRect placed;
    bool alpha = false;
};
void Surface::fail(const QString &what)
{
    d->failureText = failureMessage(what);
    qCWarning(qLcLayer).noquote() << d->failureText;
}
QString Surface::failure() const { return d->failureText; }
int Surface::wholePixelStep(qreal ratio)
{
    const int scale120 = qRound(ratio * 120);
    if (scale120 <= 0 || qAbs(scale120 / 120.0 - ratio) > 1e-9)
        return 0;
    return 120 / std::gcd(scale120, 120);
}
QRect Surface::onWholePixels(const QWindow *parent, const QRect &rect, QRect *pixels)
{
    const qreal ratio = parent->devicePixelRatio();
    const int step = wholePixelStep(ratio);
    const auto down = [step](int v) { return step ? int(std::floor(double(v) / step)) * step : 0; };
    const auto up = [step](int v) { return step ? int(std::ceil(double(v) / step)) * step : v; };
    const QRect grown(QPoint(qMax(0, down(rect.x())), qMax(0, down(rect.y()))), QPoint(up(rect.x() + rect.width()) - 1, up(rect.y() + rect.height()) - 1));
    if (pixels) {
        *pixels = QRect(QPoint(qRound(grown.x() * ratio), qRound(grown.y() * ratio)),
            QPoint(qRound((grown.x() + grown.width()) * ratio) - 1, qRound((grown.y() + grown.height()) * ratio) - 1));
    }
    return grown;
}
QRect Surface::covering(const QWindow *parent, const QRect &rect)
{
    const qreal ratio = parent->devicePixelRatio();
    const QMargins frame = parent->frameMargins();
    const auto edge = [ratio](int logical) { return qRound(logical * ratio); };
    // one axis: the hole's device pixels as the window paints them, and the first place and size whose device pixels hold them
    const auto axis = [&edge](int margin, int from, int length, int *start, int *size) {
        const int holeFrom = edge(margin) + edge(from);
        const int holeTo = edge(margin) + edge(from + length);
        for (int grow = 0; grow <= 4; ++grow) {
            for (int back = 0; back <= grow; ++back) {
                const int at = from - back;
                const int extent = length + grow;
                const int placedFrom = edge(margin + at);
                // the far edge under both roundings: the size rounded on its own, and the edge rounded from the parent's corner
                const int placedTo = qMin(placedFrom + edge(extent), edge(margin + at + extent));
                if (placedFrom <= holeFrom && placedTo >= holeTo) {
                    *start = at;
                    *size = extent;
                    return;
                }
            }
        }
        *start = from;
        *size = length;
    };
    int x = rect.x(), y = rect.y(), width = rect.width(), height = rect.height();
    axis(frame.left(), rect.x(), rect.width(), &x, &width);
    axis(frame.top(), rect.y(), rect.height(), &y, &height);
    return QRect(x, y, width, height);
}
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
bool Surface::show(QWindow *parent, const QRect &hole, const QImage &packed, bool exact, bool above, bool stereo)
{
    Q_ASSERT(QThread::currentThread() == qGuiApp->thread());
    if (!parent || !parent->isVisible() || hole.isEmpty() || packed.isNull()) {
        hide();
        return false;
    }
    if (QGuiApplication::platformName() != QLatin1String("wayland"))
        return false;
    if (!d->failureText.isEmpty() && d->parent == parent)
        return false;
    // where the surface lies: on whole device pixels for a buffer of the window's own pixels, else over the whole of its hole
    const QRect rect = exact ? hole : covering(parent, hole);
    auto *native = parent->nativeInterface<QNativeInterface::Private::QWaylandWindow>();
    if (!native || !native->surface()) {
        d->parent = parent;
        fail(QStringLiteral("the window has no wl_surface"));
        return false;
    }
    if (d->parent != parent || d->parentSurface != native->surface() || d->above != above || d->stereo != stereo)
        reset();
    if (!d->surface) {
        d->connection = connection();
        if (!d->connection->supported()) {
            const QString text = d->connection->failure();
            if (!text.isEmpty()) {
                d->parent = parent;
                d->failureText = text;
                qCWarning(qLcLayer).noquote() << text;
            }
            return false;
        }
        d->parent = parent;
        d->parentSurface = native->surface();
        auto &globals = d->connection->globals;
        d->surface = wl_compositor_create_surface(globals.compositor);
        d->subsurface = wl_subcompositor_get_subsurface(globals.subcompositor, d->surface, native->surface());
        d->above = above;
        d->stereo = stereo;
        if (above) {
            wl_subsurface_place_above(d->subsurface, native->surface());
        } else {
            wl_subsurface_place_below(d->subsurface, native->surface());
        }
        wl_subsurface_set_desync(d->subsurface);
        d->viewport = wp_viewporter_get_viewport(globals.viewporter, d->surface);
        wl_region *empty = wl_compositor_create_region(globals.compositor);
        wl_surface_set_input_region(d->surface, empty);
        wl_region_destroy(empty);
    }
    auto &c = *d->connection;
    wl_display_dispatch_queue_pending(c.display, c.queue);
    const bool alpha = packed.hasAlphaChannel();
    const bool deep = above && (packed.format() == QImage::Format_RGBA64_Premultiplied || packed.format() == QImage::Format_RGBX64);
    if (deep && !c.globals.shmFormats.contains(alpha ? WL_SHM_FORMAT_ABGR16161616 : WL_SHM_FORMAT_XBGR16161616)) {
        fail(QStringLiteral("the compositor did not offer a 16-bit shared-memory format"));
        return false;
    }
    Buffer *buffer = nullptr;
    for (auto &candidate : d->buffers) {
        if (candidate.busy)
            continue;
        if (candidate.buffer && (candidate.pixels != packed.size() || candidate.alpha != alpha || candidate.deep != deep))
            destroyBuffer(candidate);
        if (!candidate.buffer && !createBuffer(candidate, c.globals.shm, c.queue, packed.size(), alpha, deep))
            continue;
        buffer = &candidate;
        break;
    }
    if (!buffer)
        return d->shown;
    if (deep) {
        const qsizetype rowBytes = qsizetype(packed.width()) * 8;
        for (int row = 0; row < packed.height(); ++row) {
            memcpy(buffer->data + row * rowBytes, packed.constScanLine(row), rowBytes);
        }
    } else {
        QImage target(
            buffer->data, packed.width(), packed.height(), packed.width() * 4, alpha ? QImage::Format_ARGB32_Premultiplied : QImage::Format_RGB32);
        QPainter painter(&target);
        painter.setCompositionMode(QPainter::CompositionMode_Source);
        painter.drawImage(0, 0, packed);
    }
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
    if (stereo && !d->declared) {
        const int result = stereo_declare_wayland(d->surface, STEREO_SBS_FULL, STEREO_CLASS_NONE, STEREO_SUBCLASS_UNSPECIFIED);
        if (result < 0) {
            // nothing is attached to a surface the compositor did not take as stereo content, and it is not tried again until reset()
            fail(QString::fromLocal8Bit(strerror(-result)));
            return false;
        }
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
