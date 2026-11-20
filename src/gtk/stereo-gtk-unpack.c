/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "stereo-gtk.h"
#include <errno.h>
#include <math.h>
#include <stereo-dibr.h>
#include <string.h>

/* The views of one packed picture, each with its own samples: the same rule as storedView() and
 * Picture::fromAnaglyph() of the Qt layer (src/picture/render.cpp, src/picture/anaglyph.cpp), so a GTK
 * program hands Stereo KWin what a Qt program hands it for the same file. Nothing is resampled. */

static GdkTexture *texture(guchar *pixels, int width, int height)
{
    GBytes *bytes = g_bytes_new_take(pixels, (gsize)width * height * 4);
    GdkTexture *result = gdk_memory_texture_new(width, height, GDK_MEMORY_R8G8B8A8, bytes, (gsize)width * 4);
    g_bytes_unref(bytes);
    return result;
}

static guchar *view(const guchar *frame, int width, int height, int layout, int phase, int *out_width, int *out_height)
{
    int w = width, h = height;
    switch (layout) {
    case STEREO_POLICY_LAYOUT_SIDE_BY_SIDE:
    case STEREO_POLICY_LAYOUT_SIDE_BY_SIDE_QUINCUNX:
    case STEREO_POLICY_LAYOUT_COLUMNS:
    case STEREO_POLICY_LAYOUT_CHECKERBOARD:
        w = width / 2;
        break;
    case STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM:
    case STEREO_POLICY_LAYOUT_ROWS:
        h = height / 2;
        break;
    default:
        return NULL;
    }
    if (w < 1 || h < 1)
        return NULL;
    guchar *out = g_malloc((gsize)w * h * 4);
    for (int y = 0; y < h; ++y) {
        guchar *row = out + (gsize)y * w * 4;
        if (layout == STEREO_POLICY_LAYOUT_SIDE_BY_SIDE || layout == STEREO_POLICY_LAYOUT_SIDE_BY_SIDE_QUINCUNX) {
            memcpy(row, frame + ((gsize)y * width + (gsize)phase * w) * 4, (gsize)w * 4);
        } else if (layout == STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM) {
            memcpy(row, frame + ((gsize)(y + phase * h) * width) * 4, (gsize)w * 4);
        } else if (layout == STEREO_POLICY_LAYOUT_ROWS) {
            memcpy(row, frame + ((gsize)(2 * y + phase) * width) * 4, (gsize)w * 4);
        } else {
            /* a checkerboard view's samples shift by one column from row to row */
            const int first = layout == STEREO_POLICY_LAYOUT_CHECKERBOARD ? (phase + y) % 2 : phase;
            const guchar *in = frame + (gsize)y * width * 4;
            for (int x = 0; x < w; ++x)
                memcpy(row + x * 4, in + (gsize)MIN(2 * x + first, width - 1) * 4, 4);
        }
    }
    *out_width = w;
    *out_height = h;
    return out;
}

/* Picture::anaglyphTransform(): per eye, three channels of three weights, then three divisors. */
static const int transforms[3][2][12] = {
    {{1,0,0, 1,1,0, 1,0,1, 1,2,2}, {1,1,0, 0,1,0, 0,0,1, 2,1,1}},
    {{1,1,0, 0,1,0, 0,1,1, 2,1,2}, {1,0,0, 1,1,1, 0,0,1, 1,3,1}},
    {{1,0,0, 0,1,0, 0,1,1, 1,1,2}, {1,0,1, 0,1,1, 0,0,1, 2,2,1}},
};

static guchar *anaglyph_view(const guchar *frame, int width, int height, int pair, int eye)
{
    const int *t = transforms[pair - STEREO_POLICY_ANAGLYPH_RED_CYAN][eye];
    guchar *out = g_malloc((gsize)width * height * 4);
    for (gsize i = 0; i < (gsize)width * height; ++i) {
        const guchar *in = frame + i * 4;
        for (int channel = 0; channel < 3; ++channel) {
            int value = 0;
            for (int source = 0; source < 3; ++source)
                value += t[3 * channel + source] * in[source];
            out[i * 4 + channel] = (guchar)MIN(255, value / t[9 + channel]);
        }
        out[i * 4 + 3] = 255;
    }
    return out;
}

/* A picture and its depth map drawn as Picture::fromDepth() of the Qt layer draws them (src/picture/picture.cpp): the
 * depth map's gray as nearness, 8 bits, the library's one default (stereo_dibr_options_default(): its strength, screen
 * and way), the picture as RGBA planes. Nothing here reads or changes the person's depth limits. */
struct planes {
    guchar *data[4];
    struct stereo_dibr_picture picture;
};

static void planes_from(struct planes *p, const guchar *frame, int stride, int x0, int y0, int w, int h)
{
    memset(p, 0, sizeof(*p));
    p->picture.width = w;
    p->picture.height = h;
    p->picture.nb_planes = 4;
    for (int k = 0; k < 4; ++k) {
        p->data[k] = g_malloc0((gsize)w * h);
        p->picture.plane[k] = (struct stereo_dibr_plane){p->data[k], w, 1, 0, 0, k == 3 ? 255u : 0u};
    }
    if (!frame)
        return;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            for (int k = 0; k < 4; ++k)
                p->data[k][(gsize)y * w + x] = frame[((gsize)(y0 + y) * stride + x0 + x) * 4 + k];
}

static guchar *planes_rgba(const struct planes *p)
{
    const int w = p->picture.width, h = p->picture.height;
    guchar *out = g_malloc((gsize)w * h * 4);
    for (gsize i = 0; i < (gsize)w * h; ++i)
        for (int k = 0; k < 4; ++k)
            out[i * 4 + k] = p->data[k][i];
    return out;
}

static void planes_clear(struct planes *p)
{
    for (int k = 0; k < 4; ++k)
        g_free(p->data[k]);
}

/* QImage's Grayscale8 conversion (qGray), so a depth map reads as in the Qt layer */
static guchar *gray_from(const guchar *frame, int stride, int x0, int y0, int w, int h)
{
    guchar *out = g_malloc((gsize)w * h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const guchar *in = frame + ((gsize)(y0 + y) * stride + x0 + x) * 4;
            out[(gsize)y * w + x] = (guchar)((in[0] * 11 + in[1] * 16 + in[2] * 5) / 32);
        }
    return out;
}

struct region {
    int x, y, w, h;
};

/* The views of picture with its depth map (and a second layer with its own map: graphics over it or the background it
 * hides), drawn by libstereo-dibr. frame and second_frame are RGBA with the given strides in pixels. */
static gboolean depth_views(const guchar *frame, int stride, struct region pic, const guchar *depth_frame, int depth_stride,
                            struct region dep, const struct region *layer, const struct region *layer_dep, int layer_role,
                            guchar **left, guchar **right, int *out_w, int *out_h)
{
    struct stereo_dibr_options options;
    struct stereo_depth_convention convention = {0};
    struct stereo_dibr_params params;
    stereo_dibr_options_default(&options);
    stereo_dibr_params_default(&params);
    convention.kind = STEREO_DEPTH_NEARNESS;
    convention.bits = 8;
    stereo_dibr_options_apply(&options, &convention, &params);
    float table[256], layer_table[256];
    if (pic.w < 1 || pic.h < 1 || dep.w < 1 || dep.h < 1 || stereo_depth_parallax_table(&convention, pic.w, table) < 0)
        return FALSE;
    struct planes in, l, r, second;
    planes_from(&in, frame, stride, pic.x, pic.y, pic.w, pic.h);
    planes_from(&l, NULL, 0, 0, 0, pic.w, pic.h);
    planes_from(&r, NULL, 0, 0, 0, pic.w, pic.h);
    guchar *gray = gray_from(depth_frame, depth_stride, dep.x, dep.y, dep.w, dep.h);
    const struct stereo_dibr_depth map = {gray, dep.w, 1, dep.w, dep.h};
    stereo_dibr *dibr = stereo_dibr_alloc();
    int ret = -2;
    if (dibr && !layer) {
        ret = stereo_dibr_render(dibr, &params, table, 256, &in.picture, &map, &l.picture, &r.picture);
    } else if (dibr) {
        planes_from(&second, frame, stride, layer->x, layer->y, layer->w, layer->h);
        guchar *layer_gray = gray_from(frame, stride, layer_dep->x, layer_dep->y, layer_dep->w, layer_dep->h);
        const struct stereo_dibr_depth layer_map = {layer_gray, layer_dep->w, 1, layer_dep->w, layer_dep->h};
        /* a layer's samples of 0 hold nothing: no background there, no graphics there */
        memcpy(layer_table, table, sizeof(table));
        layer_table[0] = NAN;
        ret = stereo_dibr_render_layer(dibr, &params, table, 256, &in.picture, &map, layer_role, &second.picture, layer_table, 256,
                                       &layer_map, &l.picture, &r.picture);
        g_free(layer_gray);
        planes_clear(&second);
    }
    stereo_dibr_free(dibr);
    g_free(gray);
    if (ret == 0) {
        *left = planes_rgba(&l);
        *right = planes_rgba(&r);
        *out_w = pic.w;
        *out_h = pic.h;
    }
    planes_clear(&in);
    planes_clear(&l);
    planes_clear(&r);
    return ret == 0;
}

/* The four packings of a picture and its depth map in one frame, cut as the Qt layer's unpackWithDepth() cuts them: the
 * order says where the picture is (side by side and top and bottom), the views are drawn, so they are never swapped. */
static gboolean depth_packed(const guchar *frame, int width, int height, int layout, int order, guchar **left, guchar **right,
                             int *w, int *h)
{
    const int first = order == STEREO_POLICY_RIGHT_FIRST;
    if (layout == STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE && width % 2 == 0 && width >= 2) {
        struct region a = {0, 0, width / 2, height}, b = {width / 2, 0, width / 2, height};
        return depth_views(frame, width, first ? b : a, frame, width, first ? a : b, NULL, NULL, 0, left, right, w, h);
    }
    if (layout == STEREO_POLICY_LAYOUT_DEPTH_TOP_AND_BOTTOM && height % 2 == 0 && height >= 2) {
        struct region a = {0, 0, width, height / 2}, b = {0, height / 2, width, height / 2};
        return depth_views(frame, width, first ? b : a, frame, width, first ? a : b, NULL, NULL, 0, left, right, w, h);
    }
    if (layout == STEREO_POLICY_LAYOUT_DEPTH_GRAPHICS && height % 4 == 0 && height >= 4) {
        const int q = height / 4;
        struct region pic = {0, 0, width, q}, dep = {0, q, width, q}, lay = {0, 2 * q, width, q}, lay_dep = {0, 3 * q, width, q};
        return depth_views(frame, width, pic, frame, width, dep, &lay, &lay_dep, STEREO_DIBR_LAYER_OVERLAY, left, right, w, h);
    }
    if (layout == STEREO_POLICY_LAYOUT_DEPTH_BACKGROUND && width % 2 == 0 && height % 2 == 0 && width >= 2 && height >= 2) {
        const int hw = width / 2, hh = height / 2;
        struct region pic = {0, 0, hw, hh}, dep = {hw, 0, hw, hh}, lay = {0, hh, hw, hh}, lay_dep = {hw, hh, hw, hh};
        return depth_views(frame, width, pic, frame, width, dep, &lay, &lay_dep, STEREO_DIBR_LAYER_BACKGROUND, left, right, w, h);
    }
    return FALSE;
}

static guchar *download(GdkTexture *frame)
{
    const int width = gdk_texture_get_width(frame), height = gdk_texture_get_height(frame);
    guchar *pixels = g_malloc((gsize)width * height * 4);
    GdkTextureDownloader *downloader = gdk_texture_downloader_new(frame);
    gdk_texture_downloader_set_format(downloader, GDK_MEMORY_R8G8B8A8);
    gdk_texture_downloader_download_into(downloader, pixels, (gsize)width * 4);
    gdk_texture_downloader_free(downloader);
    return pixels;
}

static gboolean is_depth(int layout)
{
    return layout >= STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE && layout <= STEREO_POLICY_LAYOUT_DEPTH_BACKGROUND;
}

gboolean stereo_gtk_layout_needs_two_pictures(int layout)
{
    return layout == STEREO_POLICY_LAYOUT_MULTI_VIEW || layout == STEREO_POLICY_LAYOUT_FRAME_SEQUENTIAL ||
           layout == STEREO_POLICY_LAYOUT_BLOCK_LACED || layout == STEREO_POLICY_LAYOUT_DEPTH_SEPARATE;
}

gboolean stereo_gtk_unpack(GdkTexture *frame, int layout, int order, int anaglyph,
                           GdkTexture **left, GdkTexture **right, GError **error)
{
    g_return_val_if_fail(GDK_IS_TEXTURE(frame) && left && right, FALSE);
    *left = *right = NULL;
    const gboolean spatial = layout == STEREO_POLICY_LAYOUT_SIDE_BY_SIDE || layout == STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM ||
        layout == STEREO_POLICY_LAYOUT_ROWS || layout == STEREO_POLICY_LAYOUT_COLUMNS ||
        layout == STEREO_POLICY_LAYOUT_CHECKERBOARD || layout == STEREO_POLICY_LAYOUT_SIDE_BY_SIDE_QUINCUNX;
    const gboolean colours = anaglyph >= STEREO_POLICY_ANAGLYPH_RED_CYAN && anaglyph <= STEREO_POLICY_ANAGLYPH_AMBER_BLUE;
    if (!spatial && !colours && is_depth(layout)) {
        const int width = gdk_texture_get_width(frame), height = gdk_texture_get_height(frame);
        guchar *pixels = download(frame), *eyes[2] = {NULL, NULL};
        int w = 0, h = 0;
        const gboolean drawn = depth_packed(pixels, width, height, layout, order, &eyes[0], &eyes[1], &w, &h);
        g_free(pixels);
        if (!drawn) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, g_strerror(EINVAL));
            return FALSE;
        }
        *left = texture(eyes[0], w, h);
        *right = texture(eyes[1], w, h);
        return TRUE;
    }
    if (!spatial && !colours) {
        /* Multiview, frames in turn, block laced and a separate depth map need a second picture
         * (stereo_gtk_unpack_pair()). */
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            g_dpgettext2("stereo-gtk", "Stereo::FileChoiceMenu|",
                                         "This 3D packing needs more than one picture; it is shown flat here."));
        return FALSE;
    }
    const int width = gdk_texture_get_width(frame), height = gdk_texture_get_height(frame);
    guchar *pixels = download(frame);
    guchar *eyes[2] = {NULL, NULL};
    int w[2] = {width, width}, h[2] = {height, height};
    for (int eye = 0; eye < 2; ++eye) {
        if (colours)
            eyes[eye] = anaglyph_view(pixels, width, height, anaglyph, eye);
        else
            eyes[eye] = view(pixels, width, height, layout, eye ^ (order == STEREO_POLICY_RIGHT_FIRST), &w[eye], &h[eye]);
    }
    g_free(pixels);
    if (!eyes[0] || !eyes[1] || w[0] != w[1] || h[0] != h[1]) {
        g_free(eyes[0]);
        g_free(eyes[1]);
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, g_strerror(EINVAL));
        return FALSE;
    }
    int first = colours && order == STEREO_POLICY_RIGHT_FIRST;
    *left = texture(eyes[first], w[0], h[0]);
    *right = texture(eyes[!first], w[1], h[1]);
    return TRUE;
}

gboolean stereo_gtk_unpack_pair(GdkTexture *first, GdkTexture *second, int layout, int order,
                                GdkTexture **left, GdkTexture **right, GError **error)
{
    g_return_val_if_fail(GDK_IS_TEXTURE(first) && GDK_IS_TEXTURE(second) && left && right, FALSE);
    *left = *right = NULL;
    if (!stereo_gtk_layout_needs_two_pictures(layout)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, g_strerror(EINVAL));
        return FALSE;
    }
    if (layout == STEREO_POLICY_LAYOUT_DEPTH_SEPARATE) {
        /* first is the picture, second its depth map (any size: the library scales it to the picture) */
        const int width = gdk_texture_get_width(first), height = gdk_texture_get_height(first);
        const int dw = gdk_texture_get_width(second), dh = gdk_texture_get_height(second);
        guchar *pixels = download(first), *depth = download(second), *eyes[2] = {NULL, NULL};
        int w = 0, h = 0;
        const gboolean drawn = depth_views(pixels, width, (struct region){0, 0, width, height}, depth, dw,
                                           (struct region){0, 0, dw, dh}, NULL, NULL, 0, &eyes[0], &eyes[1], &w, &h);
        g_free(pixels);
        g_free(depth);
        if (!drawn) {
            g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, g_strerror(EINVAL));
            return FALSE;
        }
        *left = texture(eyes[0], w, h);
        *right = texture(eyes[1], w, h);
        return TRUE;
    }
    /* Two views in turn (frames in turn, both views in one block, two views of a multiview stream): the first decoded
     * picture is the left eye unless the right comes first. Each keeps its own samples; their sizes must match. */
    if (gdk_texture_get_width(first) != gdk_texture_get_width(second) ||
        gdk_texture_get_height(first) != gdk_texture_get_height(second)) {
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, g_strerror(EINVAL));
        return FALSE;
    }
    const gboolean swap = order == STEREO_POLICY_RIGHT_FIRST;
    *left = g_object_ref(swap ? second : first);
    *right = g_object_ref(swap ? first : second);
    return TRUE;
}

double stereo_gtk_view_aspect(int width, int height, double frame_aspect, int layout)
{
    if (width < 1 || height < 1 || frame_aspect <= 0)
        return frame_aspect;
    const int full = stereo_policy_video_full_views((int)(height * frame_aspect + 0.5), height, layout) == 1;
    switch (layout) {
    case STEREO_POLICY_LAYOUT_SIDE_BY_SIDE:
    case STEREO_POLICY_LAYOUT_SIDE_BY_SIDE_QUINCUNX:
    case STEREO_POLICY_LAYOUT_COLUMNS:
    case STEREO_POLICY_LAYOUT_CHECKERBOARD:
        return full ? frame_aspect / 2 : frame_aspect;
    case STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM:
    case STEREO_POLICY_LAYOUT_ROWS:
        return full ? frame_aspect * 2 : frame_aspect;
    default:
        return frame_aspect;
    }
}
