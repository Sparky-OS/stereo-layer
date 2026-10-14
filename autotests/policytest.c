/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/* The stereo policy without Qt: every mark of the set, the video packing in its order of precedence, the user's
 * correction, full or half views and the shape rules, checked against what RFC 9559, Table D-8 and the edition say. */

#include "stereo-policy.h"

#include <stdio.h>
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
    /* the mark first, whatever the shape and the user's choice */
    CHECK(stereo_policy_video_layout(1920, 1080, "sideBySideRightFirst", STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, 0, 0, &layout, &order, &source) == 0);
    CHECK(layout == STEREO_POLICY_LAYOUT_SIDE_BY_SIDE && order == STEREO_POLICY_RIGHT_FIRST && source == STEREO_POLICY_SOURCE_FILE_MARK);
    CHECK(stereo_policy_video_layout(3840, 1080, "mono", 0, 0, 0, &layout, &order, &source) == 0);
    CHECK(layout == STEREO_POLICY_LAYOUT_NONE && source == STEREO_POLICY_SOURCE_FILE_MARK);
    CHECK(stereo_policy_video_layout(3840, 1080, "anaglyphCyanRed", 0, 0, 0, &layout, &order, &source) == 0);
    CHECK(layout == STEREO_POLICY_LAYOUT_NONE && source == STEREO_POLICY_SOURCE_FILE_MARK);
    CHECK(stereo_policy_video_layout(1920, 1080, "frameSequentialLeftFirst", 0, 0, 0, &layout, &order, &source) == 0);
    CHECK(layout == STEREO_POLICY_LAYOUT_FRAME_SEQUENTIAL && source == STEREO_POLICY_SOURCE_FILE_MARK);
    /* then the user's choice */
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

int main(void)
{
    marks();
    fileCorrection();
    videoLayout();
    fullViews();
    shapes();
    if (failures)
        fprintf(stderr, "%d check(s) failed\n", failures);
    else
        printf("policytest: all checks passed\n");
    return failures ? 1 : 0;
}
