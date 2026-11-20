/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "stereo-gtk.h"
#include "stereo-gtk-native.h"
#include <errno.h>
#include <math.h>

typedef struct {
    GtkWidget parent;
    GdkTexture *left, *right;
    GtkWidget *owner;
    StereoGtkNative *native;
    graphene_rect_t last_rect;
    double aspect;
    GPtrArray *above;
    gboolean enabled, shown, dirty, failed;
    guint repeat;
    guint tick;
} StereoGtkArea;
typedef GtkWidgetClass StereoGtkAreaClass;
G_DEFINE_TYPE(StereoGtkArea, stereo_gtk_area, GTK_TYPE_WIDGET)

static guint signals[2];
static graphene_rect_t content_rect(StereoGtkArea *self, graphene_rect_t allocation)
{
    if (!self->left) return allocation;
    double aspect = self->aspect > 0 ? self->aspect
                                     : (double)gdk_texture_get_width(self->left) / gdk_texture_get_height(self->left);
    int width = MIN((int)allocation.size.width, (int)(allocation.size.height * aspect + 0.5));
    int height = MIN((int)allocation.size.height, (int)(width / aspect + 0.5));
    allocation.origin.x += (int)(allocation.size.width - width) / 2;
    allocation.origin.y += (int)(allocation.size.height - height) / 2;
    allocation.size.width = width; allocation.size.height = height;
    return allocation;
}

static GPtrArray *areas(GtkWidget *window)
{
    GPtrArray *list = g_object_get_data(G_OBJECT(window), "stereo-gtk-areas");
    if (!list) {
        list = g_ptr_array_new();
        g_object_set_data_full(G_OBJECT(window), "stereo-gtk-areas", list, (GDestroyNotify)g_ptr_array_unref);
    }
    return list;
}
static void shown(StereoGtkArea *self, gboolean value)
{
    if (self->shown != value) {
        self->shown = value;
        g_signal_emit(self, signals[0], 0, value);
        if (self->owner) gtk_widget_queue_draw(self->owner);
    }
}
static void clear_native(StereoGtkArea *self)
{
    stereo_gtk_native_free(self->native);
    self->native = NULL;
    shown(self, FALSE);
}
static gboolean update(GtkWidget *widget, GdkFrameClock *clock, gpointer data)
{
    StereoGtkArea *self = (StereoGtkArea *)widget;
    (void)clock; (void)data;
    if (!self->enabled || !self->left || !self->right || self->failed) {
        clear_native(self);
        return G_SOURCE_CONTINUE;
    }
    graphene_rect_t rect;
    if (!self->owner || !gtk_widget_compute_bounds(widget, self->owner, &rect) ||
        rect.size.width < 1 || rect.size.height < 1) {
        clear_native(self);
        return G_SOURCE_CONTINUE;
    }
    /* A nested clip must not let the native area escape its GTK rectangle. */
    for (GtkWidget *parent = gtk_widget_get_parent(widget); parent && parent != self->owner;
         parent = gtk_widget_get_parent(parent)) {
        graphene_rect_t bounds;
        if (gtk_widget_get_overflow(parent) == GTK_OVERFLOW_HIDDEN &&
            gtk_widget_compute_bounds(parent, self->owner, &bounds) &&
            !graphene_rect_contains_rect(&bounds, &rect)) {
            clear_native(self);
            return G_SOURCE_CONTINUE;
        }
    }
    rect = content_rect(self, rect);
    GError *error = NULL;
    if (!self->native) {
        self->native = stereo_gtk_native_new(widget, &error);
        self->dirty = TRUE;
        self->repeat = 10; /* Xwayland may redirect the declared child a frame later. */
    }
    if (self->native && (self->dirty || self->repeat || !graphene_rect_equal(&rect, &self->last_rect))) {
        graphene_rect_t surface_rect = rect;
        double x, y;
        gtk_native_get_surface_transform(GTK_NATIVE(self->owner), &x, &y);
        surface_rect.origin.x += x; surface_rect.origin.y += y;
        if (stereo_gtk_native_present(self->native, &surface_rect, self->left, self->right, &error)) {
            self->last_rect = rect;
            self->dirty = FALSE;
            if (self->repeat) self->repeat--;
            shown(self, TRUE);
        }
    }
    if (error) {
        clear_native(self);
        self->failed = TRUE;
        g_signal_emit(self, signals[1], 0, error->message);
        g_error_free(error);
        gtk_widget_queue_draw(widget);
    }
    return G_SOURCE_CONTINUE;
}
static void map(GtkWidget *widget)
{
    StereoGtkArea *self = (StereoGtkArea *)widget;
    GTK_WIDGET_CLASS(stereo_gtk_area_parent_class)->map(widget);
    self->owner = GTK_WIDGET(gtk_widget_get_native(widget));
    if (!G_TYPE_CHECK_INSTANCE_TYPE(self->owner, stereo_gtk_window_get_type()) &&
        !G_TYPE_CHECK_INSTANCE_TYPE(self->owner, stereo_gtk_application_window_get_type())) {
        self->failed = TRUE;
        g_signal_emit(self, signals[1], 0, g_strerror(ENOTSUP));
        return;
    }
    g_ptr_array_add(areas(self->owner), self);
    self->tick = gtk_widget_add_tick_callback(widget, update, NULL, NULL);
}
static void unmap(GtkWidget *widget)
{
    StereoGtkArea *self = (StereoGtkArea *)widget;
    if (self->tick) gtk_widget_remove_tick_callback(widget, self->tick);
    self->tick = 0;
    clear_native(self);
    if (self->owner) g_ptr_array_remove(areas(self->owner), self);
    self->owner = NULL;
    GTK_WIDGET_CLASS(stereo_gtk_area_parent_class)->unmap(widget);
}
static void measure(GtkWidget *widget, GtkOrientation orientation, int for_size,
                    int *minimum, int *natural, int *minimum_baseline, int *natural_baseline)
{
    StereoGtkArea *self = (StereoGtkArea *)widget;
    int height = self->left ? gdk_texture_get_height(self->left) : 360;
    int width = self->left ? (self->aspect > 0 ? (int)(height * self->aspect + 0.5) : gdk_texture_get_width(self->left)) : 640;
    *minimum = 1;
    *natural = orientation == GTK_ORIENTATION_HORIZONTAL ? width : height;
    if (for_size > 0)
        *natural = orientation == GTK_ORIENTATION_HORIZONTAL ? for_size * width / height : for_size * height / width;
    *minimum_baseline = *natural_baseline = -1;
}
static void snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    StereoGtkArea *self = (StereoGtkArea *)widget;
    if (self->left && !self->shown) {
        graphene_rect_t rect = GRAPHENE_RECT_INIT(0, 0, gtk_widget_get_width(widget), gtk_widget_get_height(widget));
        rect = content_rect(self, rect);
        gtk_snapshot_append_texture(snapshot, self->left, &rect);
    }
}
static void dispose(GObject *object)
{
    StereoGtkArea *self = (StereoGtkArea *)object;
    clear_native(self);
    g_clear_object(&self->left);
    g_clear_object(&self->right);
    g_clear_pointer(&self->above, g_ptr_array_unref);
    G_OBJECT_CLASS(stereo_gtk_area_parent_class)->dispose(object);
}
static void stereo_gtk_area_class_init(StereoGtkAreaClass *klass)
{
    GtkWidgetClass *widget = GTK_WIDGET_CLASS(klass);
    widget->map = map; widget->unmap = unmap; widget->measure = measure; widget->snapshot = snapshot;
    G_OBJECT_CLASS(klass)->dispose = dispose;
    gtk_widget_class_set_css_name(widget, "stereo-media-area");
    signals[0] = g_signal_new("stereo-shown", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_BOOLEAN);
    signals[1] = g_signal_new("stereo-error", G_TYPE_FROM_CLASS(klass), G_SIGNAL_RUN_LAST,
                              0, NULL, NULL, NULL, G_TYPE_NONE, 1, G_TYPE_STRING);
}
static void stereo_gtk_area_init(StereoGtkArea *self)
{
    self->dirty = TRUE;
    self->above = g_ptr_array_new_with_free_func(g_object_unref);
}
GtkWidget *stereo_gtk_area_new(void) { return g_object_new(stereo_gtk_area_get_type(), NULL); }
void stereo_gtk_area_set_pair(GtkWidget *widget, GdkTexture *left, GdkTexture *right)
{
    g_return_if_fail(G_TYPE_CHECK_INSTANCE_TYPE(widget, stereo_gtk_area_get_type()));
    StereoGtkArea *self = (StereoGtkArea *)widget;
    g_set_object(&self->left, left); g_set_object(&self->right, right);
    self->dirty = TRUE; self->failed = FALSE;
    gtk_widget_queue_resize(widget);
    gtk_widget_queue_draw(widget);
}
void stereo_gtk_area_set_enabled(GtkWidget *widget, gboolean enabled)
{
    g_return_if_fail(G_TYPE_CHECK_INSTANCE_TYPE(widget, stereo_gtk_area_get_type()));
    StereoGtkArea *self = (StereoGtkArea *)widget;
    self->enabled = !!enabled; self->failed = FALSE;
    if (!enabled) clear_native(self);
    gtk_widget_queue_draw(widget);
}
void stereo_gtk_area_set_view_aspect(GtkWidget *widget, double aspect)
{
    g_return_if_fail(G_TYPE_CHECK_INSTANCE_TYPE(widget, stereo_gtk_area_get_type()));
    StereoGtkArea *self = (StereoGtkArea *)widget;
    self->aspect = aspect > 0 ? aspect : 0;
    self->dirty = TRUE;
    gtk_widget_queue_resize(widget);
}
void stereo_gtk_area_keep_above(GtkWidget *widget, GtkWidget *above)
{
    g_return_if_fail(G_TYPE_CHECK_INSTANCE_TYPE(widget, stereo_gtk_area_get_type()) && GTK_IS_WIDGET(above));
    g_ptr_array_add(((StereoGtkArea *)widget)->above, g_object_ref(above));
}
gboolean stereo_gtk_area_get_shown(GtkWidget *widget)
{
    g_return_val_if_fail(G_TYPE_CHECK_INSTANCE_TYPE(widget, stereo_gtk_area_get_type()), FALSE);
    return ((StereoGtkArea *)widget)->shown;
}

/* Mask the whole ordinary window, including its CSS background, only where
 * an active native media surface has successfully presented both eyes. */
static void window_snapshot(GtkWidget *widget, GtkSnapshot *snapshot, GtkWidgetClass *parent)
{
    GPtrArray *list = areas(widget);
    gboolean active = FALSE;
    for (guint i = 0; i < list->len; ++i)
        active |= ((StereoGtkArea *)g_ptr_array_index(list, i))->shown;
    if (active) {
        gtk_snapshot_push_mask(snapshot, GSK_MASK_MODE_INVERTED_ALPHA);
        for (guint i = 0; i < list->len; ++i) {
            StereoGtkArea *area = g_ptr_array_index(list, i);
            if (!area->shown)
                continue;
            /* The hole is the media rectangle less the 2D widgets drawn over it (a player's controls), which stay
             * in the window, the same in both eyes. */
            cairo_rectangle_int_t hole = {(int)area->last_rect.origin.x, (int)area->last_rect.origin.y,
                                          (int)area->last_rect.size.width, (int)area->last_rect.size.height};
            cairo_region_t *region = cairo_region_create_rectangle(&hole);
            for (guint j = 0; j < area->above->len; ++j) {
                GtkWidget *above = g_ptr_array_index(area->above, j);
                graphene_rect_t bounds;
                if (gtk_widget_get_mapped(above) && gtk_widget_get_opacity(above) > 0 &&
                    gtk_widget_compute_bounds(above, widget, &bounds)) {
                    cairo_rectangle_int_t cut = {(int)floor(bounds.origin.x), (int)floor(bounds.origin.y),
                                                 (int)ceil(bounds.size.width) + 1, (int)ceil(bounds.size.height) + 1};
                    cairo_region_subtract_rectangle(region, &cut);
                }
            }
            const GdkRGBA white = {1, 1, 1, 1};
            for (int k = 0; k < cairo_region_num_rectangles(region); ++k) {
                cairo_rectangle_int_t r;
                cairo_region_get_rectangle(region, k, &r);
                graphene_rect_t rect = GRAPHENE_RECT_INIT(r.x, r.y, r.width, r.height);
                gtk_snapshot_append_color(snapshot, &white, &rect);
            }
            cairo_region_destroy(region);
        }
        gtk_snapshot_pop(snapshot);
    }
    parent->snapshot(widget, snapshot);
    if (active) gtk_snapshot_pop(snapshot);
}
typedef GtkWindow StereoGtkWindow;
typedef GtkWindowClass StereoGtkWindowClass;
G_DEFINE_TYPE(StereoGtkWindow, stereo_gtk_window, GTK_TYPE_WINDOW)
static void ordinary_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    window_snapshot(widget, snapshot, GTK_WIDGET_CLASS(stereo_gtk_window_parent_class));
}
static void stereo_gtk_window_class_init(StereoGtkWindowClass *klass)
{
    GTK_WIDGET_CLASS(klass)->snapshot = ordinary_snapshot;
}
static void stereo_gtk_window_init(StereoGtkWindow *self) { (void)self; }
typedef GtkApplicationWindow StereoGtkApplicationWindow;
typedef GtkApplicationWindowClass StereoGtkApplicationWindowClass;
G_DEFINE_TYPE(StereoGtkApplicationWindow, stereo_gtk_application_window, GTK_TYPE_APPLICATION_WINDOW)
static void application_snapshot(GtkWidget *widget, GtkSnapshot *snapshot)
{
    window_snapshot(widget, snapshot, GTK_WIDGET_CLASS(stereo_gtk_application_window_parent_class));
}
static void stereo_gtk_application_window_class_init(StereoGtkApplicationWindowClass *klass)
{
    GTK_WIDGET_CLASS(klass)->snapshot = application_snapshot;
}
static void stereo_gtk_application_window_init(StereoGtkApplicationWindow *self) { (void)self; }
