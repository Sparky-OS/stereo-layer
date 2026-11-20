/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "stereo-gtk.h"
#include <errno.h>
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

gboolean stereo_gtk_unpack(GdkTexture *frame, int layout, int order, int anaglyph,
                           GdkTexture **left, GdkTexture **right, GError **error)
{
    g_return_val_if_fail(GDK_IS_TEXTURE(frame) && left && right, FALSE);
    *left = *right = NULL;
    const gboolean spatial = layout == STEREO_POLICY_LAYOUT_SIDE_BY_SIDE || layout == STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM ||
        layout == STEREO_POLICY_LAYOUT_ROWS || layout == STEREO_POLICY_LAYOUT_COLUMNS ||
        layout == STEREO_POLICY_LAYOUT_CHECKERBOARD || layout == STEREO_POLICY_LAYOUT_SIDE_BY_SIDE_QUINCUNX;
    const gboolean colours = anaglyph >= STEREO_POLICY_ANAGLYPH_RED_CYAN && anaglyph <= STEREO_POLICY_ANAGLYPH_AMBER_BLUE;
    if (!spatial && !colours) {
        /* Multiview, frames in turn, block laced and the depth packings need a second picture or a depth engine. */
        g_set_error_literal(error, G_IO_ERROR, G_IO_ERROR_NOT_SUPPORTED,
                            g_dpgettext2("stereo-gtk", "Stereo::FileChoiceMenu|",
                                         "This 3D packing needs more than one picture; it is shown flat here."));
        return FALSE;
    }
    const int width = gdk_texture_get_width(frame), height = gdk_texture_get_height(frame);
    guchar *pixels = g_malloc((gsize)width * height * 4);
    GdkTextureDownloader *downloader = gdk_texture_downloader_new(frame);
    gdk_texture_downloader_set_format(downloader, GDK_MEMORY_R8G8B8A8);
    gdk_texture_downloader_download_into(downloader, pixels, (gsize)width * 4);
    gdk_texture_downloader_free(downloader);
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
