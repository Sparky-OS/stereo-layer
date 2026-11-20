/* SPDX-License-Identifier: GPL-2.0-or-later */
#define _GNU_SOURCE
#include "stereo-gtk-native.h"
#include <gdk/wayland/gdkwayland.h>
#include <gdk/x11/gdkx.h>
#include <stereo-declare.h>
#include <wayland-client.h>
#include "viewporter-client-protocol.h"
#include <X11/Xutil.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

typedef struct NativeBuffer NativeBuffer;
struct StereoGtkNative {
    GdkSurface *parent;
    struct wl_display *display;
    struct wl_event_queue *queue;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_subcompositor *subcompositor;
    struct wl_shm *shm;
    struct wp_viewporter *viewporter;
    struct wl_surface *surface;
    struct wl_subsurface *subsurface;
    struct wp_viewport *viewport;
    GList *buffers;
    Display *xdisplay;
    Window owner, child;
    unsigned last_width, last_height;
};
struct NativeBuffer {
    struct wl_buffer *buffer;
    void *pixels;
    size_t size;
    StereoGtkNative *native;
};

static gboolean fail(GError **error, int number)
{
    g_set_error_literal(error, G_IO_ERROR, g_io_error_from_errno(number), g_strerror(number));
    return FALSE;
}
static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version)
{
    StereoGtkNative *n = data;
    if (!strcmp(interface, "wl_compositor"))
        n->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, MIN(version, 4));
    else if (!strcmp(interface, "wl_subcompositor"))
        n->subcompositor = wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
    else if (!strcmp(interface, "wl_shm"))
        n->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (!strcmp(interface, "wp_viewporter"))
        n->viewporter = wl_registry_bind(registry, name, &wp_viewporter_interface, 1);
}
static void removed(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data; (void)registry; (void)name;
}
static const struct wl_registry_listener registry_listener = { global, removed };
static void release_buffer(void *data, struct wl_buffer *buffer)
{
    NativeBuffer *b = data;
    b->native->buffers = g_list_remove(b->native->buffers, b);
    wl_buffer_destroy(buffer);
    munmap(b->pixels, b->size);
    g_free(b);
}
static const struct wl_buffer_listener buffer_listener = { release_buffer };
/* libstereo-declare keeps one state per thread for the application's lifetime on one display: it is set up with the
 * first area and kept when an area goes ("Enable 3D" off), so the next area declares on it. Setting it up again
 * answers EBUSY. */
static struct wl_event_queue *declaration_queue;
static struct wl_display *declaration_display;

StereoGtkNative *stereo_gtk_native_new(GtkWidget *area, GError **error)
{
    GtkNative *owner = gtk_widget_get_native(area);
    GdkSurface *parent = owner ? gtk_native_get_surface(owner) : NULL;
    if (!parent) {
        fail(error, ENODEV);
        return NULL;
    }
    StereoGtkNative *n = g_new0(StereoGtkNative, 1);
    n->parent = g_object_ref(parent);
    if (GDK_IS_WAYLAND_SURFACE(parent)) {
        n->display = gdk_wayland_display_get_wl_display(gdk_surface_get_display(parent));
        if (!declaration_queue) declaration_queue = wl_display_create_queue(n->display);
        n->queue = declaration_queue;
        n->registry = wl_display_get_registry(n->display);
        wl_proxy_set_queue((struct wl_proxy *)n->registry, n->queue);
        wl_registry_add_listener(n->registry, &registry_listener, n);
        int result = 0;
        if (declaration_display != n->display) {
            result = declaration_display ? -EBUSY : stereo_declare_wayland_init(n->display, n->queue);
            if (result >= 0)
                declaration_display = n->display;
        }
        if (result < 0 || wl_display_roundtrip_queue(n->display, n->queue) < 0 ||
            !n->compositor || !n->subcompositor || !n->shm || !n->viewporter || !stereo_supported_wayland()) {
            fail(error, result < 0 ? -result : ENOTSUP);
            stereo_gtk_native_free(n);
            return NULL;
        }
        n->surface = wl_compositor_create_surface(n->compositor);
        struct wl_surface *owner_surface = gdk_wayland_surface_get_wl_surface(parent);
        n->subsurface = wl_subcompositor_get_subsurface(n->subcompositor, n->surface, owner_surface);
        wl_subsurface_place_below(n->subsurface, owner_surface);
        wl_subsurface_set_desync(n->subsurface);
        n->viewport = wp_viewporter_get_viewport(n->viewporter, n->surface);
        struct wl_region *input = wl_compositor_create_region(n->compositor);
        wl_surface_set_input_region(n->surface, input);
        wl_region_destroy(input);
        result = stereo_declare_wayland(n->surface, STEREO_SBS_FULL, STEREO_CLASS_VIDEO, STEREO_VIDEO_CURRENT);
        if (result < 0) {
            fail(error, -result);
            stereo_gtk_native_free(n);
            return NULL;
        }
    } else if (GDK_IS_X11_SURFACE(parent)) {
        n->xdisplay = gdk_x11_display_get_xdisplay(gdk_surface_get_display(parent));
        n->owner = gdk_x11_surface_get_xid(parent);
        if (stereo_supported_x11(n->xdisplay) < 8) {
            fail(error, ENOTSUP);
            stereo_gtk_native_free(n);
            return NULL;
        }
        n->child = XCreateSimpleWindow(n->xdisplay, n->owner, 0, 0, 2, 1, 0, 0, 0);
        int result = stereo_declare_x11_child(n->xdisplay, n->owner, n->child, STEREO_SBS_FULL,
                                               STEREO_CLASS_VIDEO, STEREO_VIDEO_CURRENT, 1, 1);
        if (result < 0) {
            fail(error, -result);
            stereo_gtk_native_free(n);
            return NULL;
        }
        XMapWindow(n->xdisplay, n->child);
    } else {
        fail(error, ENOTSUP);
        stereo_gtk_native_free(n);
        return NULL;
    }
    return n;
}

gboolean stereo_gtk_native_present(StereoGtkNative *n, const graphene_rect_t *rect,
                                  GdkTexture *left, GdkTexture *right, GError **error)
{
    int width = gdk_texture_get_width(left), height = gdk_texture_get_height(left);
    if (width != gdk_texture_get_width(right) || height != gdk_texture_get_height(right) ||
        width < 1 || height < 1 || width > INT_MAX / 8 || (size_t)height > SIZE_MAX / (width * 8))
        return fail(error, EINVAL);
    size_t stride = width * 8, size = stride * height;
    if (size > INT_MAX) return fail(error, EOVERFLOW);
    unsigned char *pixels = g_malloc(size);
    unsigned char *eye = g_malloc((size_t)width * height * 4);
    for (int index = 0; index < 2; ++index) {
        gdk_texture_download(index ? right : left, eye, width * 4);
        for (int y = 0; y < height; ++y)
            memcpy(pixels + y * stride + index * width * 4, eye + y * width * 4, width * 4);
    }
    g_free(eye);
    /* GDK_MEMORY_DEFAULT is native premultiplied ARGB32. The pair is opaque. */
    for (size_t p = 0; p < size / 4; ++p)
        ((guint32 *)pixels)[p] |= 0xff000000;
    if (n->surface) {
        wl_display_dispatch_queue_pending(n->display, n->queue);
        int fd = memfd_create("stereo-gtk-frame", MFD_CLOEXEC);
        if (fd < 0 || ftruncate(fd, size) < 0) {
            int saved = errno;
            if (fd >= 0) close(fd);
            g_free(pixels);
            return fail(error, saved);
        }
        NativeBuffer *b = g_new0(NativeBuffer, 1);
        b->size = size; b->native = n;
        b->pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (b->pixels == MAP_FAILED) {
            int saved = errno; close(fd); g_free(b); g_free(pixels);
            return fail(error, saved);
        }
        memcpy(b->pixels, pixels, size);
        struct wl_shm_pool *pool = wl_shm_create_pool(n->shm, fd, size);
        b->buffer = wl_shm_pool_create_buffer(pool, 0, width * 2, height, stride, WL_SHM_FORMAT_XRGB8888);
        wl_shm_pool_destroy(pool); close(fd);
        wl_buffer_add_listener(b->buffer, &buffer_listener, b);
        n->buffers = g_list_prepend(n->buffers, b);
        wl_subsurface_set_position(n->subsurface, rect->origin.x, rect->origin.y);
        wp_viewport_set_destination(n->viewport, MAX(1, (int)rect->size.width), MAX(1, (int)rect->size.height));
        wl_surface_attach(n->surface, b->buffer, 0, 0);
        wl_surface_damage_buffer(n->surface, 0, 0, INT_MAX, INT_MAX);
        wl_surface_commit(n->surface);
        wl_display_flush(n->display);
    } else {
        int scale = gdk_surface_get_scale_factor(n->parent);
        struct stereo_x11_child_place place = {
            rect->origin.x * scale, rect->origin.y * scale,
            MAX(1, (int)rect->size.width * scale), MAX(1, (int)rect->size.height * scale), 1
        };
        XResizeWindow(n->xdisplay, n->child, width * 2, height);
        int result = stereo_declare_x11_child_place(n->xdisplay, n->child, &place);
        if (result < 0) { g_free(pixels); return fail(error, -result); }
        XWindowAttributes attributes;
        XGetWindowAttributes(n->xdisplay, n->child, &attributes);
        XImage *image = XCreateImage(n->xdisplay, attributes.visual,
                                    attributes.depth, ZPixmap, 0, (char *)pixels, width * 2, height, 32, stride);
        GC gc = XCreateGC(n->xdisplay, n->child, 0, NULL);
        XPutImage(n->xdisplay, n->child, gc, image, 0, 0, 0, 0, width * 2, height);
        XFreeGC(n->xdisplay, gc);
        image->data = NULL; XDestroyImage(image);
        XFlush(n->xdisplay);
    }
    g_free(pixels);
    return TRUE;
}

void stereo_gtk_native_free(StereoGtkNative *n)
{
    if (!n) return;
    if (n->child) {
        stereo_remove_x11_child(n->xdisplay, n->owner, n->child);
        XDestroyWindow(n->xdisplay, n->child);
        XFlush(n->xdisplay);
    }
    if (n->surface) stereo_remove_wayland(n->surface);
    if (n->viewport) wp_viewport_destroy(n->viewport);
    if (n->subsurface) wl_subsurface_destroy(n->subsurface);
    if (n->surface) wl_surface_destroy(n->surface);
    while (n->buffers) release_buffer(n->buffers->data, ((NativeBuffer *)n->buffers->data)->buffer);
    if (n->viewporter) wp_viewporter_destroy(n->viewporter);
    if (n->shm) wl_shm_destroy(n->shm);
    if (n->subcompositor) wl_subcompositor_destroy(n->subcompositor);
    if (n->compositor) wl_compositor_destroy(n->compositor);
    if (n->registry) wl_registry_destroy(n->registry);
    /* libstereo-declare's registry has application lifetime on this display. */
    g_clear_object(&n->parent);
    g_free(n);
}
