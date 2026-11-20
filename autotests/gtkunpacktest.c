/* SPDX-License-Identifier: GPL-2.0-or-later */
/* libstereo-gtk's unpacking of the packings that need a depth map or two pictures, against their rule:
 * two pictures in turn give the eyes as they come (or swapped when the right comes first), a picture and its depth map
 * in one frame give the picture as the left eye (libstereo-dibr's one default, VIEW_LEFT) and a drawn right eye of the
 * picture's size. No display is needed: the textures live in memory. */
#include <stereo-gtk.h>
#include <stdio.h>
#include <string.h>

static int failures;
#define CHECK(c) do { if (!(c)) { fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static GdkTexture *solid(int w, int h, guchar r, guchar g, guchar b)
{
    guchar *px = g_malloc((gsize)w * h * 4);
    for (gsize i = 0; i < (gsize)w * h; ++i) {
        px[i * 4] = r;
        px[i * 4 + 1] = g;
        px[i * 4 + 2] = b;
        px[i * 4 + 3] = 255;
    }
    GBytes *bytes = g_bytes_new_take(px, (gsize)w * h * 4);
    GdkTexture *t = gdk_memory_texture_new(w, h, GDK_MEMORY_R8G8B8A8, bytes, (gsize)w * 4);
    g_bytes_unref(bytes);
    return t;
}

/* a picture (left half: red with a blue column) and its depth map (right half: one gray value) */
static GdkTexture *picture_and_depth(int w, int h, guchar gray)
{
    guchar *px = g_malloc((gsize)2 * w * h * 4);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < 2 * w; ++x) {
            guchar *p = px + ((gsize)y * 2 * w + x) * 4;
            if (x < w) {
                p[0] = x == w / 2 ? 0 : 255;
                p[1] = 0;
                p[2] = x == w / 2 ? 255 : 0;
            } else {
                p[0] = p[1] = p[2] = gray;
            }
            p[3] = 255;
        }
    GBytes *bytes = g_bytes_new_take(px, (gsize)2 * w * h * 4);
    GdkTexture *t = gdk_memory_texture_new(2 * w, h, GDK_MEMORY_R8G8B8A8, bytes, (gsize)2 * w * 4);
    g_bytes_unref(bytes);
    return t;
}

static guchar first_red(GdkTexture *t)
{
    guchar px[4];
    GdkTextureDownloader *d = gdk_texture_downloader_new(t);
    gdk_texture_downloader_set_format(d, GDK_MEMORY_R8G8B8A8);
    guchar *all = g_malloc((gsize)gdk_texture_get_width(t) * gdk_texture_get_height(t) * 4);
    gdk_texture_downloader_download_into(d, all, (gsize)gdk_texture_get_width(t) * 4);
    gdk_texture_downloader_free(d);
    memcpy(px, all, 4);
    g_free(all);
    return px[0];
}

static gboolean same(GdkTexture *a, GdkTexture *b)
{
    const int w = gdk_texture_get_width(a), h = gdk_texture_get_height(a);
    if (w != gdk_texture_get_width(b) || h != gdk_texture_get_height(b))
        return FALSE;
    guchar *pa = g_malloc((gsize)w * h * 4), *pb = g_malloc((gsize)w * h * 4);
    GdkTextureDownloader *d = gdk_texture_downloader_new(a);
    gdk_texture_downloader_set_format(d, GDK_MEMORY_R8G8B8A8);
    gdk_texture_downloader_download_into(d, pa, (gsize)w * 4);
    gdk_texture_downloader_set_texture(d, b);
    gdk_texture_downloader_download_into(d, pb, (gsize)w * 4);
    gdk_texture_downloader_free(d);
    const gboolean equal = memcmp(pa, pb, (gsize)w * h * 4) == 0;
    g_free(pa);
    g_free(pb);
    return equal;
}

int main(void)
{
    GdkTexture *red = solid(64, 32, 255, 0, 0), *blue = solid(64, 32, 0, 0, 255), *small = solid(32, 32, 0, 255, 0);
    GdkTexture *left = NULL, *right = NULL;
    GError *error = NULL;

    /* two pictures in turn, for each packing that alternates views */
    const int turns[] = {STEREO_POLICY_LAYOUT_FRAME_SEQUENTIAL, STEREO_POLICY_LAYOUT_BLOCK_LACED, STEREO_POLICY_LAYOUT_MULTI_VIEW};
    for (unsigned i = 0; i < G_N_ELEMENTS(turns); ++i) {
        CHECK(stereo_gtk_layout_needs_two_pictures(turns[i]));
        CHECK(stereo_gtk_unpack_pair(red, blue, turns[i], STEREO_POLICY_LEFT_FIRST, &left, &right, NULL));
        CHECK(left == red && right == blue);
        g_clear_object(&left);
        g_clear_object(&right);
        CHECK(stereo_gtk_unpack_pair(red, blue, turns[i], STEREO_POLICY_RIGHT_FIRST, &left, &right, NULL));
        CHECK(left == blue && right == red);
        g_clear_object(&left);
        g_clear_object(&right);
        /* one picture alone is said, not shown flat in silence */
        CHECK(!stereo_gtk_unpack(red, turns[i], STEREO_POLICY_LEFT_FIRST, STEREO_POLICY_ANAGLYPH_NONE, &left, &right, &error));
        CHECK(error && error->code == G_IO_ERROR_NOT_SUPPORTED);
        g_clear_error(&error);
    }
    CHECK(!stereo_gtk_unpack_pair(red, small, STEREO_POLICY_LAYOUT_FRAME_SEQUENTIAL, STEREO_POLICY_LEFT_FIRST, &left, &right, &error));
    g_clear_error(&error);
    CHECK(!stereo_gtk_layout_needs_two_pictures(STEREO_POLICY_LAYOUT_SIDE_BY_SIDE));

    /* a picture and its depth map side by side: the picture is the left eye, the right eye is drawn at its size */
    GdkTexture *packed = picture_and_depth(64, 32, 255), *picture = NULL;
    CHECK(stereo_gtk_unpack(packed, STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE, STEREO_POLICY_LEFT_FIRST, STEREO_POLICY_ANAGLYPH_NONE,
                            &left, &right, &error));
    CHECK(error == NULL);
    if (left && right) {
        CHECK(gdk_texture_get_width(left) == 64 && gdk_texture_get_height(left) == 32);
        CHECK(gdk_texture_get_width(right) == 64 && gdk_texture_get_height(right) == 32);
        CHECK(first_red(left) == 255);
        /* a near depth moves the blue column in the right eye: the eyes differ */
        CHECK(!same(left, right));
    }
    g_clear_object(&left);
    g_clear_object(&right);
    g_clear_error(&error);

    /* the same picture and depth map as two pictures (a separate depth map) give the same eyes */
    GdkTexture *depth = solid(64, 32, 255, 255, 255);
    GdkTexture *l1 = NULL, *r1 = NULL;
    CHECK(stereo_gtk_unpack(packed, STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE, STEREO_POLICY_LEFT_FIRST, STEREO_POLICY_ANAGLYPH_NONE,
                            &l1, &r1, NULL));
    {
        guchar *px = g_malloc((gsize)64 * 32 * 4);
        GdkTextureDownloader *d = gdk_texture_downloader_new(packed);
        gdk_texture_downloader_set_format(d, GDK_MEMORY_R8G8B8A8);
        guchar *all = g_malloc((gsize)128 * 32 * 4);
        gdk_texture_downloader_download_into(d, all, (gsize)128 * 4);
        gdk_texture_downloader_free(d);
        for (int y = 0; y < 32; ++y)
            memcpy(px + (gsize)y * 64 * 4, all + (gsize)y * 128 * 4, 64 * 4);
        g_free(all);
        GBytes *bytes = g_bytes_new_take(px, (gsize)64 * 32 * 4);
        picture = gdk_memory_texture_new(64, 32, GDK_MEMORY_R8G8B8A8, bytes, (gsize)64 * 4);
        g_bytes_unref(bytes);
    }
    CHECK(stereo_gtk_unpack_pair(picture, depth, STEREO_POLICY_LAYOUT_DEPTH_SEPARATE, STEREO_POLICY_LEFT_FIRST, &left, &right, NULL));
    if (left && right && l1 && r1) {
        CHECK(same(left, l1));
        CHECK(same(right, r1));
    }
    g_clear_object(&left);
    g_clear_object(&right);
    g_clear_object(&l1);
    g_clear_object(&r1);

    /* the four rows and the four quarters cut to a picture a quarter or half the frame */
    GdkTexture *rows = solid(64, 128, 128, 128, 128), *quarters = solid(128, 64, 128, 128, 128);
    CHECK(stereo_gtk_unpack(rows, STEREO_POLICY_LAYOUT_DEPTH_GRAPHICS, STEREO_POLICY_LEFT_FIRST, STEREO_POLICY_ANAGLYPH_NONE,
                            &left, &right, NULL));
    CHECK(left && gdk_texture_get_width(left) == 64 && gdk_texture_get_height(left) == 32);
    g_clear_object(&left);
    g_clear_object(&right);
    CHECK(stereo_gtk_unpack(quarters, STEREO_POLICY_LAYOUT_DEPTH_BACKGROUND, STEREO_POLICY_LEFT_FIRST, STEREO_POLICY_ANAGLYPH_NONE,
                            &left, &right, NULL));
    CHECK(left && gdk_texture_get_width(left) == 64 && gdk_texture_get_height(left) == 32);
    g_clear_object(&left);
    g_clear_object(&right);

    g_object_unref(red);
    g_object_unref(blue);
    g_object_unref(small);
    g_object_unref(packed);
    g_object_unref(picture);
    g_object_unref(depth);
    g_object_unref(rows);
    g_object_unref(quarters);
    if (failures)
        fprintf(stderr, "%d failures\n", failures);
    return failures != 0;
}
