/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

#include "stereo-policy.h"

#include <errno.h>
#include <stddef.h>
#include <string.h>
#ifdef __linux__
#include <sys/xattr.h>
#endif

#define SWAP_EYES_ATTRIBUTE "user.stereo-policy.swap-eyes"

static void set(int *target, int value)
{
    if (target)
        *target = value;
}

int stereo_policy_read_mark(const char *value, int *layout, int *order, int *anaglyph)
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
    set(layout, STEREO_POLICY_LAYOUT_NONE);
    set(order, STEREO_POLICY_LEFT_FIRST);
    set(anaglyph, STEREO_POLICY_ANAGLYPH_NONE);
    if (!value)
        return -1;
    if (strcmp(value, "mono") == 0)
        return 1;
    for (size_t i = 0; i < sizeof(packings) / sizeof(packings[0]); ++i) {
        const size_t length = strlen(packings[i].name);
        if (strncmp(value, packings[i].name, length) != 0)
            continue;
        const char *rest = value + length;
        if (strcmp(rest, "LeftFirst") == 0 || strcmp(rest, "RightFirst") == 0) {
            set(layout, packings[i].layout);
            set(order, rest[0] == 'R' ? STEREO_POLICY_RIGHT_FIRST : STEREO_POLICY_LEFT_FIRST);
            return 1;
        }
    }
    /* One picture holding both views in colours: red/cyan glasses carry red before the left eye and green/magenta ones
     * green; JPS names the eye behind the red filter. */
    if (strcmp(value, "anaglyphCyanRed") == 0 || strcmp(value, "anaglyphRedLeft") == 0 || strcmp(value, "anaglyphRedRight") == 0) {
        set(anaglyph, STEREO_POLICY_ANAGLYPH_RED_CYAN);
        set(order, strcmp(value, "anaglyphRedRight") == 0 ? STEREO_POLICY_RIGHT_FIRST : STEREO_POLICY_LEFT_FIRST);
        return 1;
    }
    if (strcmp(value, "anaglyphGreenMagenta") == 0) {
        set(anaglyph, STEREO_POLICY_ANAGLYPH_GREEN_MAGENTA);
        return 1;
    }
    return 0;
}

int stereo_policy_video_side_by_side_by_shape(int width, int height) { return width > 0 && height > 0 && (long long)width >= (long long)height * 3; }

int stereo_policy_picture_pair_by_shape(int width, int height)
{
    return width > 0 && height > 0 && (long long)width >= (long long)height * 2 && (long long)width * 5 <= (long long)height * 18;
}

int stereo_policy_video_layout(
    int width, int height, const char *mark, int user_layout, int user_order, int swap_eyes, int *layout, int *order, int *source)
{
    if (width <= 0 || height <= 0 || !layout || !order || !source || user_layout < STEREO_POLICY_LAYOUT_NONE
        || user_layout > STEREO_POLICY_LAYOUT_BLOCK_LACED)
        return -1;
    *layout = STEREO_POLICY_LAYOUT_NONE;
    *order = STEREO_POLICY_LEFT_FIRST;
    *source = STEREO_POLICY_SOURCE_NONE;
    if (mark && mark[0]) {
        int anaglyph = STEREO_POLICY_ANAGLYPH_NONE;
        stereo_policy_read_mark(mark, layout, order, &anaglyph);
        if (anaglyph != STEREO_POLICY_ANAGLYPH_NONE) {
            /* one picture holding both views in colours: no packing */
            *layout = STEREO_POLICY_LAYOUT_NONE;
            *order = STEREO_POLICY_LEFT_FIRST;
        }
        *source = STEREO_POLICY_SOURCE_FILE_MARK;
    } else if (user_layout != STEREO_POLICY_LAYOUT_NONE) {
        *layout = user_layout;
        *order = user_order == STEREO_POLICY_RIGHT_FIRST ? STEREO_POLICY_RIGHT_FIRST : STEREO_POLICY_LEFT_FIRST;
        *source = STEREO_POLICY_SOURCE_USER;
    } else if (stereo_policy_video_side_by_side_by_shape(width, height)) {
        *layout = STEREO_POLICY_LAYOUT_SIDE_BY_SIDE;
        *source = STEREO_POLICY_SOURCE_SHAPE;
    }
    if (swap_eyes && *layout != STEREO_POLICY_LAYOUT_NONE)
        *order = *order == STEREO_POLICY_LEFT_FIRST ? STEREO_POLICY_RIGHT_FIRST : STEREO_POLICY_LEFT_FIRST;
    return 0;
}

int stereo_policy_video_full_views(int width, int height, int layout)
{
    if (width <= 0 || height <= 0)
        return -1;
    switch (layout) {
    case STEREO_POLICY_LAYOUT_SIDE_BY_SIDE:
        return (long long)width * 2 >= (long long)height * 5;
    case STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM:
        return (long long)width * 5 <= (long long)height * 6;
    case STEREO_POLICY_LAYOUT_ROWS:
    case STEREO_POLICY_LAYOUT_COLUMNS:
    case STEREO_POLICY_LAYOUT_CHECKERBOARD:
    case STEREO_POLICY_LAYOUT_SIDE_BY_SIDE_QUINCUNX:
        return 0;
    case STEREO_POLICY_LAYOUT_MULTI_VIEW:
    case STEREO_POLICY_LAYOUT_FRAME_SEQUENTIAL:
    case STEREO_POLICY_LAYOUT_BLOCK_LACED:
        return 1;
    default:
        return -1;
    }
}

int stereo_policy_file_swap_eyes(const char *path)
{
    if (!path || !path[0])
        return -1;
#ifdef __linux__
    char value[8];
    const ssize_t length = getxattr(path, SWAP_EYES_ATTRIBUTE, value, sizeof(value) - 1);
    if (length < 0)
        return errno == ENODATA ? 0 : -1;
    value[length] = 0;
    return strcmp(value, "1") == 0;
#else
    return -1;
#endif
}

int stereo_policy_set_file_swap_eyes(const char *path, int swap)
{
    if (!path || !path[0]) {
        errno = EINVAL;
        return -1;
    }
#ifdef __linux__
    if (swap)
        return setxattr(path, SWAP_EYES_ATTRIBUTE, "1", 1, 0);
    if (removexattr(path, SWAP_EYES_ATTRIBUTE) < 0 && errno != ENODATA)
        return -1;
    return 0;
#else
    (void)swap;
    errno = ENOTSUP;
    return -1;
#endif
}
