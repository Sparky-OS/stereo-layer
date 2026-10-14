/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/* The stereo policy without Qt: every mark of the set, the video packing in its order of precedence, the user's
 * correction, full or half views and the shape rules, checked against what RFC 9559, Table D-8 and the edition say
 * (H.264 V15, 08/2024 numbers that table D-8; V16, 06/2026 D-9). */

#include "stereo-policy.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

#define CHECK(condition)                                                                                                                             \
    do {                                                                                                                                             \
        if (!(condition)) {                                                                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                                                     \
            ++failures;                                                                                                                              \
        }                                                                                                                                            \
    } while (0)

static void marks(void)
{
    static const struct {
        const char *name;
        int layout;
    } packings[] = {
        { "sideBySide", STEREO_POLICY_LAYOUT_SIDE_BY_SIDE },
        { "sideBySideQuincunx", STEREO_POLICY_LAYOUT_SIDE_BY_SIDE_QUINCUNX },
        { "topBottom", STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM },
        { "checkerboard", STEREO_POLICY_LAYOUT_CHECKERBOARD },
        { "rowInterleaved", STEREO_POLICY_LAYOUT_ROWS },
        { "columnInterleaved", STEREO_POLICY_LAYOUT_COLUMNS },
        { "frameSequential", STEREO_POLICY_LAYOUT_FRAME_SEQUENTIAL },
        { "blockLaced", STEREO_POLICY_LAYOUT_BLOCK_LACED },
        { "multiView", STEREO_POLICY_LAYOUT_MULTI_VIEW },
    };
    int layout, order, anaglyph;
    for (size_t i = 0; i < sizeof(packings) / sizeof(packings[0]); ++i) {
        char value[64];
        snprintf(value, sizeof(value), "%sLeftFirst", packings[i].name);
        CHECK(stereo_policy_read_mark(value, &layout, &order, &anaglyph) == 1);
        CHECK(layout == packings[i].layout && order == STEREO_POLICY_LEFT_FIRST && anaglyph == STEREO_POLICY_ANAGLYPH_NONE);
        snprintf(value, sizeof(value), "%sRightFirst", packings[i].name);
        CHECK(stereo_policy_read_mark(value, &layout, &order, &anaglyph) == 1);
        CHECK(layout == packings[i].layout && order == STEREO_POLICY_RIGHT_FIRST && anaglyph == STEREO_POLICY_ANAGLYPH_NONE);
    }
    CHECK(stereo_policy_read_mark("mono", &layout, &order, &anaglyph) == 1);
    CHECK(layout == STEREO_POLICY_LAYOUT_NONE && anaglyph == STEREO_POLICY_ANAGLYPH_NONE);
    CHECK(stereo_policy_read_mark("anaglyphCyanRed", &layout, &order, &anaglyph) == 1);
    CHECK(layout == STEREO_POLICY_LAYOUT_NONE && order == STEREO_POLICY_LEFT_FIRST && anaglyph == STEREO_POLICY_ANAGLYPH_RED_CYAN);
    CHECK(stereo_policy_read_mark("anaglyphGreenMagenta", &layout, &order, &anaglyph) == 1);
    CHECK(order == STEREO_POLICY_LEFT_FIRST && anaglyph == STEREO_POLICY_ANAGLYPH_GREEN_MAGENTA);
    CHECK(stereo_policy_read_mark("anaglyphRedLeft", &layout, &order, &anaglyph) == 1);
    CHECK(order == STEREO_POLICY_LEFT_FIRST && anaglyph == STEREO_POLICY_ANAGLYPH_RED_CYAN);
    CHECK(stereo_policy_read_mark("anaglyphRedRight", &layout, &order, &anaglyph) == 1);
    CHECK(order == STEREO_POLICY_RIGHT_FIRST && anaglyph == STEREO_POLICY_ANAGLYPH_RED_CYAN);
    /* outside the set: one picture */
    const char *unknown[]
        = { "", "sideBySide", "sideBySideLeft", "SideBySideLeftFirst", "topBottomleftFirst", "anaglyphCyan", "quincunx", "sideBySideLeftFirstX" };
    for (size_t i = 0; i < sizeof(unknown) / sizeof(unknown[0]); ++i) {
        CHECK(stereo_policy_read_mark(unknown[i], &layout, &order, &anaglyph) == 0);
        CHECK(layout == STEREO_POLICY_LAYOUT_NONE && order == STEREO_POLICY_LEFT_FIRST && anaglyph == STEREO_POLICY_ANAGLYPH_NONE);
    }
    CHECK(stereo_policy_read_mark(NULL, &layout, &order, &anaglyph) == -1);
    CHECK(stereo_policy_read_mark("topBottomLeftFirst", NULL, NULL, NULL) == 1);
}

static void videoLayout(void)
{
    int layout, order, source;
    /* the user's explicit choice first, a packing or not 3D, over a mark that is wrong */
    CHECK(stereo_policy_video_layout(1920, 1080, "sideBySideRightFirst", STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, 0, 0, &layout, &order, &source) == 0);
    CHECK(layout == STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM && order == STEREO_POLICY_LEFT_FIRST && source == STEREO_POLICY_SOURCE_USER);
    CHECK(stereo_policy_video_layout(3840, 1080, "sideBySideLeftFirst", STEREO_POLICY_USER_FORCE_2D, 0, 0, &layout, &order, &source) == 0);
    CHECK(layout == STEREO_POLICY_LAYOUT_NONE && source == STEREO_POLICY_SOURCE_USER);
    CHECK(stereo_policy_video_layout(3840, 1080, NULL, STEREO_POLICY_USER_FORCE_2D, 0, 1, &layout, &order, &source) == 0);
    CHECK(layout == STEREO_POLICY_LAYOUT_NONE && order == STEREO_POLICY_LEFT_FIRST && source == STEREO_POLICY_SOURCE_USER);
    /* then the stream's own mark, whatever the shape */
    CHECK(stereo_policy_video_layout(1920, 1080, "sideBySideRightFirst", 0, 0, 0, &layout, &order, &source) == 0);
    CHECK(layout == STEREO_POLICY_LAYOUT_SIDE_BY_SIDE && order == STEREO_POLICY_RIGHT_FIRST && source == STEREO_POLICY_SOURCE_FILE_MARK);
    CHECK(stereo_policy_video_layout(3840, 1080, "mono", 0, 0, 0, &layout, &order, &source) == 0);
    CHECK(layout == STEREO_POLICY_LAYOUT_NONE && source == STEREO_POLICY_SOURCE_FILE_MARK);
    CHECK(stereo_policy_video_layout(3840, 1080, "anaglyphCyanRed", 0, 0, 0, &layout, &order, &source) == 0);
    CHECK(layout == STEREO_POLICY_LAYOUT_NONE && source == STEREO_POLICY_SOURCE_FILE_MARK);
    CHECK(stereo_policy_video_layout(1920, 1080, "frameSequentialLeftFirst", 0, 0, 0, &layout, &order, &source) == 0);
    CHECK(layout == STEREO_POLICY_LAYOUT_FRAME_SEQUENTIAL && source == STEREO_POLICY_SOURCE_FILE_MARK);
    /* the user's choice covers what the shape no longer guesses */
    CHECK(stereo_policy_video_layout(1920, 800, NULL, STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, STEREO_POLICY_RIGHT_FIRST, 0, &layout, &order, &source)
        == 0);
    CHECK(layout == STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM && order == STEREO_POLICY_RIGHT_FIRST && source == STEREO_POLICY_SOURCE_USER);
    /* then the shape: three times as wide as high */
    CHECK(stereo_policy_video_layout(3840, 1080, "", 0, 0, 0, &layout, &order, &source) == 0);
    CHECK(layout == STEREO_POLICY_LAYOUT_SIDE_BY_SIDE && order == STEREO_POLICY_LEFT_FIRST && source == STEREO_POLICY_SOURCE_SHAPE);
    CHECK(stereo_policy_video_layout(2999, 1000, NULL, 0, 0, 0, &layout, &order, &source) == 0);
    CHECK(layout == STEREO_POLICY_LAYOUT_NONE && source == STEREO_POLICY_SOURCE_NONE);
    CHECK(stereo_policy_video_layout(1080, 1920, NULL, 0, 0, 0, &layout, &order, &source) == 0);
    CHECK(layout == STEREO_POLICY_LAYOUT_NONE);
    /* the user's correction reverses the order over the mark, the choice and the shape */
    CHECK(stereo_policy_video_layout(1920, 1080, "sideBySideRightFirst", 0, 0, 1, &layout, &order, &source) == 0);
    CHECK(layout == STEREO_POLICY_LAYOUT_SIDE_BY_SIDE && order == STEREO_POLICY_LEFT_FIRST && source == STEREO_POLICY_SOURCE_FILE_MARK);
    CHECK(stereo_policy_video_layout(1920, 1080, "topBottomLeftFirst", 0, 0, 1, &layout, &order, &source) == 0);
    CHECK(order == STEREO_POLICY_RIGHT_FIRST);
    CHECK(stereo_policy_video_layout(1920, 800, NULL, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, STEREO_POLICY_LEFT_FIRST, 1, &layout, &order, &source) == 0);
    CHECK(order == STEREO_POLICY_RIGHT_FIRST && source == STEREO_POLICY_SOURCE_USER);
    CHECK(stereo_policy_video_layout(3840, 1080, NULL, 0, 0, 1, &layout, &order, &source) == 0);
    CHECK(order == STEREO_POLICY_RIGHT_FIRST && source == STEREO_POLICY_SOURCE_SHAPE);
    CHECK(stereo_policy_video_layout(3840, 1080, "mono", 0, 0, 1, &layout, &order, &source) == 0);
    CHECK(layout == STEREO_POLICY_LAYOUT_NONE && order == STEREO_POLICY_LEFT_FIRST);
    /* bad arguments */
    CHECK(stereo_policy_video_layout(0, 1080, NULL, 0, 0, 0, &layout, &order, &source) == -1);
    CHECK(stereo_policy_video_layout(1920, 1080, NULL, 0, 0, 0, NULL, &order, &source) == -1);
    CHECK(stereo_policy_video_layout(1920, 1080, NULL, 42, 0, 0, &layout, &order, &source) == -1);
    CHECK(stereo_policy_video_layout(1920, 1080, NULL, -2, 0, 0, &layout, &order, &source) == -1);
}

static void fullViews(void)
{
    /* side by side: full at 2.5 or more */
    CHECK(stereo_policy_video_full_views(3840, 1080, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE) == 1);
    CHECK(stereo_policy_video_full_views(2500, 1000, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE) == 1);
    CHECK(stereo_policy_video_full_views(2499, 1000, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE) == 0);
    CHECK(stereo_policy_video_full_views(1920, 1080, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE) == 0);
    /* top and bottom: full at 1.2 or less */
    CHECK(stereo_policy_video_full_views(1920, 2160, STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM) == 1);
    CHECK(stereo_policy_video_full_views(1200, 1000, STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM) == 1);
    CHECK(stereo_policy_video_full_views(1201, 1000, STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM) == 0);
    CHECK(stereo_policy_video_full_views(1920, 1080, STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM) == 0);
    /* interleaved, checkerboard and quincunx: half; whole frames: full */
    CHECK(stereo_policy_video_full_views(3840, 1080, STEREO_POLICY_LAYOUT_ROWS) == 0);
    CHECK(stereo_policy_video_full_views(1920, 1080, STEREO_POLICY_LAYOUT_COLUMNS) == 0);
    CHECK(stereo_policy_video_full_views(1920, 1080, STEREO_POLICY_LAYOUT_CHECKERBOARD) == 0);
    CHECK(stereo_policy_video_full_views(3840, 1080, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE_QUINCUNX) == 0);
    CHECK(stereo_policy_video_full_views(1920, 1080, STEREO_POLICY_LAYOUT_MULTI_VIEW) == 1);
    CHECK(stereo_policy_video_full_views(1920, 1080, STEREO_POLICY_LAYOUT_FRAME_SEQUENTIAL) == 1);
    CHECK(stereo_policy_video_full_views(1920, 1080, STEREO_POLICY_LAYOUT_BLOCK_LACED) == 1);
    CHECK(stereo_policy_video_full_views(1920, 1080, STEREO_POLICY_LAYOUT_NONE) == -1);
    CHECK(stereo_policy_video_full_views(0, 1080, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE) == -1);
}

static void shapes(void)
{
    CHECK(stereo_policy_video_side_by_side_by_shape(3000, 1000) == 1);
    CHECK(stereo_policy_video_side_by_side_by_shape(2999, 1000) == 0);
    CHECK(stereo_policy_video_side_by_side_by_shape(0, 0) == 0);
    CHECK(stereo_policy_picture_pair_by_shape(2000, 1000) == 1);
    CHECK(stereo_policy_picture_pair_by_shape(1999, 1000) == 0);
    CHECK(stereo_policy_picture_pair_by_shape(3600, 1000) == 1);
    CHECK(stereo_policy_picture_pair_by_shape(3601, 1000) == 0);
    CHECK(stereo_policy_picture_pair_by_shape(1000, 2000) == 0);
}

static void fileCorrection(void)
{
    /* in the build folder, which keeps extended attributes on the edition's file systems; skipped where it does not */
    const char *path = "policytest-correction.tmp";
    FILE *file = fopen(path, "w");
    CHECK(file != NULL);
    if (!file)
        return;
    fputs("pair", file);
    fclose(file);
    const int before = stereo_policy_file_swap_eyes(path);
    if (before < 0) {
        printf("policytest: no extended attributes here, the file correction is not checked\n");
        remove(path);
        return;
    }
    CHECK(before == 0);
    CHECK(stereo_policy_set_file_swap_eyes(path, 1) == 0);
    CHECK(stereo_policy_file_swap_eyes(path) == 1);
    CHECK(stereo_policy_set_file_swap_eyes(path, 0) == 0);
    CHECK(stereo_policy_file_swap_eyes(path) == 0);
    CHECK(stereo_policy_set_file_swap_eyes(path, 0) == 0); /* removing what is not there is no error */
    remove(path);
    CHECK(stereo_policy_file_swap_eyes(path) == -1);
    CHECK(stereo_policy_file_swap_eyes(NULL) == -1);
}


/* A scene of smooth texture: sums of waves, one set per seed, no two seeds alike. */
static double scene(double x, double y, int seed)
{
    return 128 + 50 * sin(x * (0.21 + 0.013 * seed) + y * 0.13) + 35 * sin(x * 0.047 - y * (0.31 + 0.007 * seed)) + 25 * sin(x * 0.11 * (seed + 1) + y * 0.19)
        + 20 * sin(x * 0.37 + y * 0.29 * (seed + 2));
}

/* Both views side by side, w by h each: the right view is the left one seen from nearer or farther (shift pixels, more at
 * the bottom), a row lower (down), with its own gain. unrelated puts another scene on the right, wide one scene across both. */
static unsigned char *picture(int w, int h, double shift, int down, double gain, int unrelated, int wide, int copy)
{
    unsigned char *rgb = malloc((size_t)2 * w * h * 3);
    const double k = 192.0 / w; /* the same scene at any size */
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < 2 * w; ++x) {
            double v;
            if (wide)
                v = scene(x * k, y * k, 0);
            else if (x < w || copy)
                v = scene((x % w) * k, y * k, 0);
            else if (unrelated)
                v = scene((x - w) * k, y * k, 3);
            else
                v = gain * scene((x - w) * k + shift * (1 + (double)y / h), (y + down) * k, 0);
            v = v < 0 ? 0 : v > 255 ? 255 : v;
            unsigned char *p = rgb + ((size_t)y * 2 * w + x) * 3;
            p[0] = (unsigned char)v;
            p[1] = (unsigned char)(v * 0.9);
            p[2] = (unsigned char)(v * 0.8);
        }
    }
    return rgb;
}

static int byContent(int w, int h, double shift, int down, double gain, int unrelated, int wide, int copy)
{
    unsigned char *rgb = picture(w, h, shift, down, gain, unrelated, wide, copy);
    const int result = stereo_policy_picture_pair_by_content(rgb, 2 * w, h, 2 * w * 3);
    free(rgb);
    return result;
}

static void contents(void)
{
    /* what two views of one scene are: shifted either way, a row off, another gain */
    CHECK(byContent(192, 108, 6, 0, 1.0, 0, 0, 0) == 1);
    CHECK(byContent(192, 108, -9, 0, 1.0, 0, 0, 0) == 1);
    CHECK(byContent(192, 108, 4, 1, 1.0, 0, 0, 0) == 1);
    CHECK(byContent(192, 108, 5, 0, 0.9, 0, 0, 0) == 1);
    CHECK(byContent(960, 540, 6, 0, 1.0, 0, 0, 0) == 1); /* reduced here to 192 a view */
    /* what they are not: another scene, one wide scene, a copy of one half (proves nothing), a flat picture, too small */
    CHECK(byContent(192, 108, 0, 0, 1.0, 1, 0, 0) == 0);
    CHECK(byContent(192, 108, 0, 0, 1.0, 0, 1, 0) == 0);
    CHECK(byContent(192, 108, 0, 0, 1.0, 0, 0, 1) == 0);
    CHECK(byContent(192, 108, 40, 0, 1.0, 0, 0, 0) == 0); /* beyond the search: a shift of a fifth of a view */
    CHECK(byContent(192, 108, 6, 6, 1.0, 0, 0, 0) == 0); /* six rows off: not two views of one camera pair */
    unsigned char *flat = calloc((size_t)384 * 108 * 3, 1);
    memset(flat, 90, (size_t)384 * 108 * 3);
    CHECK(stereo_policy_picture_pair_by_content(flat, 384, 108, 384 * 3) == 0);
    CHECK(stereo_policy_picture_pair_by_content(flat, 40, 20, 40 * 3) == 0);
    CHECK(stereo_policy_picture_pair_by_content(NULL, 384, 108, 384 * 3) == -1);
    CHECK(stereo_policy_picture_pair_by_content(flat, 1, 108, 3) == -1);
    CHECK(stereo_policy_picture_pair_by_content(flat, 384, 108, 100) == -1);
    free(flat);
}

int main(void)
{
    marks();
    fileCorrection();
    videoLayout();
    fullViews();
    shapes();
    contents();
    if (failures)
        fprintf(stderr, "%d check(s) failed\n", failures);
    else
        printf("policytest: all checks passed\n");
    return failures ? 1 : 0;
}
