/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/* The stereo policy without Qt: every mark of the set, the video packing in its order of precedence, the user's
 * correction, full or half views and the shape rules, checked against what RFC 9559, Table D-8 and the edition say. */

#include "stereo-policy.h"

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

static void videoViewsTable(void)
{
    char line[512];
    int rows = 0;
    FILE *file = fopen(VECTORS_DIR "/video-views.tsv", "r");
    CHECK(file != NULL);
    while (file && fgets(line, sizeof(line), file)) {
        if (line[0] == '#' || line[0] == '\n')
            continue;
        char *name = strtok(line, "\t");
        int v[10];
        int i = 0;
        for (char *field = strtok(NULL, "\t\n"); field && i < 10; field = strtok(NULL, "\t\n"))
            v[i++] = atoi(field);
        CHECK(name != NULL && i == 10);
        int by = -1;
        const int full = stereo_policy_video_views(v[1], v[2], v[3], v[4], v[5], v[6], v[7], v[0], &by);
        if (full != v[8] || (full >= 0 && by != v[9]))
            fprintf(stderr, "vector '%s': got %d decided by %d, expected %d decided by %d\n", name, full, by, v[8], v[9]);
        CHECK(full == v[8]);
        if (full >= 0)
            CHECK(by == v[9]);
        rows++;
    }
    if (file)
        fclose(file);
    CHECK(rows >= 20);
}

static void matroskaPlane(void)
{
    int w, h;
    /* FFmpeg: mkvmerge's 3840x1080 side by side with a 1920x1080 plane has a ratio of 1:1, with a 3840x1080 plane 2:1 */
    CHECK(stereo_policy_matroska_plane(3840, 1080, 1, 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, &w, &h) == 0 && w == 1920 && h == 1080);
    CHECK(stereo_policy_matroska_plane(3840, 1080, 2, 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, &w, &h) == 0 && w == 3840 && h == 1080);
    CHECK(stereo_policy_matroska_plane(1920, 1080, 2, 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, &w, &h) == 0 && w == 1920 && h == 1080);
    CHECK(stereo_policy_matroska_plane(1920, 2160, 1, 1, STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, &w, &h) == 0 && w == 1920 && h == 1080);
    CHECK(stereo_policy_matroska_plane(1920, 1080, 1, 2, STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, &w, &h) == 0 && w == 1920 && h == 1080);
    CHECK(stereo_policy_matroska_plane(1920, 1080, 1, 1, STEREO_POLICY_LAYOUT_ROWS, &w, &h) == -1);
    CHECK(stereo_policy_matroska_plane(1920, 1080, 0, 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, &w, &h) == -1);
    /* through the views: ratio 1:1 is full, 2:1 is half, for both packings */
    CHECK(stereo_policy_matroska_plane(3840, 1080, 1, 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, &w, &h) == 0
        && stereo_policy_video_views(3840, 1080, 1, 1, w, h, -1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, NULL) == 1);
    CHECK(stereo_policy_matroska_plane(1920, 1080, 2, 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, &w, &h) == 0
        && stereo_policy_video_views(1920, 1080, 2, 1, w, h, -1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, NULL) == 0);
    CHECK(stereo_policy_matroska_plane(1920, 2160, 1, 1, STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, &w, &h) == 0
        && stereo_policy_video_views(1920, 2160, 1, 1, w, h, -1, STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, NULL) == 1);
    CHECK(stereo_policy_matroska_plane(1920, 1080, 1, 2, STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, &w, &h) == 0
        && stereo_policy_video_views(1920, 1080, 1, 2, w, h, -1, STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, NULL) == 0);
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
    videoViewsTable();
    matroskaPlane();
    shapes();
    if (failures)
        fprintf(stderr, "%d check(s) failed\n", failures);
    else
        printf("policytest: all checks passed\n");
    return failures ? 1 : 0;
}
