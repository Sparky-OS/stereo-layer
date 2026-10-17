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
    static const struct {
        const char *name;
        int layout;
        int order;
    } depths[] = {
        { "depthSideBySidePictureFirst", STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE, STEREO_POLICY_PICTURE_FIRST },
        { "depthSideBySideDepthFirst", STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE, STEREO_POLICY_DEPTH_FIRST },
        { "depthTopBottomPictureFirst", STEREO_POLICY_LAYOUT_DEPTH_TOP_AND_BOTTOM, STEREO_POLICY_PICTURE_FIRST },
        { "depthTopBottomDepthFirst", STEREO_POLICY_LAYOUT_DEPTH_TOP_AND_BOTTOM, STEREO_POLICY_DEPTH_FIRST },
        { "depthGraphicsTopBottom", STEREO_POLICY_LAYOUT_DEPTH_GRAPHICS, STEREO_POLICY_PICTURE_FIRST },
        { "depthBackgroundQuarters", STEREO_POLICY_LAYOUT_DEPTH_BACKGROUND, STEREO_POLICY_PICTURE_FIRST },
        { "depthSeparate", STEREO_POLICY_LAYOUT_DEPTH_SEPARATE, STEREO_POLICY_PICTURE_FIRST },
    };
    set(layout, STEREO_POLICY_LAYOUT_NONE);
    set(order, STEREO_POLICY_LEFT_FIRST);
    set(anaglyph, STEREO_POLICY_ANAGLYPH_NONE);
    if (!value)
        return -1;
    if (strcmp(value, "mono") == 0)
        return 1;
    for (size_t i = 0; i < sizeof(depths) / sizeof(depths[0]); ++i) {
        if (strcmp(value, depths[i].name) == 0) {
            set(layout, depths[i].layout);
            set(order, depths[i].order);
            return 1;
        }
    }
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

int stereo_policy_is_depth_layout(int layout) { return layout >= STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE && layout <= STEREO_POLICY_LAYOUT_DEPTH_SEPARATE; }

int stereo_policy_video_side_by_side_by_shape(int width, int height) { return width > 0 && height > 0 && (long long)width >= (long long)height * 3; }

int stereo_policy_picture_pair_by_shape(int width, int height)
{
    return width > 0 && height > 0 && (long long)width >= (long long)height * 2 && (long long)width * 5 <= (long long)height * 18;
}

int stereo_policy_video_layout(
    int width, int height, const char *mark, int user_layout, int user_order, int swap_eyes, int *layout, int *order, int *source)
{
    /* the user may choose any packing of a frame, the depth ones included; a separate depth map is not one */
    if (width <= 0 || height <= 0 || !layout || !order || !source || user_layout < STEREO_POLICY_USER_FORCE_2D
        || user_layout > STEREO_POLICY_LAYOUT_DEPTH_BACKGROUND)
        return -1;
    *layout = STEREO_POLICY_LAYOUT_NONE;
    *order = STEREO_POLICY_LEFT_FIRST;
    *source = STEREO_POLICY_SOURCE_NONE;
    if (user_layout == STEREO_POLICY_USER_FORCE_2D) {
        /* the user says the video is not 3D, whatever the stream says */
        *source = STEREO_POLICY_SOURCE_USER;
    } else if (user_layout != STEREO_POLICY_LAYOUT_NONE) {
        *layout = user_layout;
        *order = user_order == STEREO_POLICY_RIGHT_FIRST ? STEREO_POLICY_RIGHT_FIRST : STEREO_POLICY_LEFT_FIRST;
        *source = STEREO_POLICY_SOURCE_USER;
    } else if (mark && mark[0]) {
        int anaglyph = STEREO_POLICY_ANAGLYPH_NONE;
        stereo_policy_read_mark(mark, layout, order, &anaglyph);
        if (anaglyph != STEREO_POLICY_ANAGLYPH_NONE) {
            /* one picture holding both views in colours: no packing */
            *layout = STEREO_POLICY_LAYOUT_NONE;
            *order = STEREO_POLICY_LEFT_FIRST;
        }
        *source = STEREO_POLICY_SOURCE_FILE_MARK;
    } else if (stereo_policy_video_side_by_side_by_shape(width, height)) {
        *layout = STEREO_POLICY_LAYOUT_SIDE_BY_SIDE;
        *source = STEREO_POLICY_SOURCE_SHAPE;
    }
    /* the user's swap reverses the eyes; in a depth layout the order is where the picture is, and the swap is the
     * drawn views' */
    if (swap_eyes && *layout != STEREO_POLICY_LAYOUT_NONE && !stereo_policy_is_depth_layout(*layout))
        *order = *order == STEREO_POLICY_LEFT_FIRST ? STEREO_POLICY_RIGHT_FIRST : STEREO_POLICY_LEFT_FIRST;
    return 0;
}

#include <ctype.h>

static int is_separator(char c) { return c == '.' || c == '_' || c == '-' || c == ' ' || c == '[' || c == ']' || c == '(' || c == ')'; }

/* the word at name[start, end), compared in upper case with a word of the vocabulary */
static int word_is(const char *name, size_t start, size_t end, const char *word)
{
    const size_t length = strlen(word);
    if (end - start != length)
        return 0;
    for (size_t i = 0; i < length; ++i)
        if (toupper((unsigned char)name[start + i]) != word[i])
            return 0;
    return 1;
}

static int packing_word(const char *name, size_t start, size_t end, int *layout, int *views)
{
    static const struct {
        const char *word;
        int layout;
        int views;
    } words[] = {
        { "SBS", STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, -1 }, { "HSBS", STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, 0 }, { "FSBS", STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, 1 },
        { "TAB", STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, -1 }, { "HTAB", STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, 0 }, { "FTAB", STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, 1 },
        { "TB", STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, -1 }, { "HTB", STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, 0 }, { "FTB", STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, 1 },
        { "OU", STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, -1 }, { "HOU", STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, 0 }, { "FOU", STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, 1 },
    };
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
        if (word_is(name, start, end, words[i].word)) {
            *layout = words[i].layout;
            *views = words[i].views;
            return 1;
        }
    }
    return 0;
}

int stereo_policy_name_tag(const char *file_name, int *out_layout, int *out_order, int *out_views, int *out_three_d)
{
    set(out_layout, STEREO_POLICY_LAYOUT_NONE);
    set(out_order, STEREO_POLICY_LEFT_FIRST);
    set(out_views, -1);
    set(out_three_d, 0);
    if (!file_name)
        return -1;
    int layout = STEREO_POLICY_LAYOUT_NONE, order = STEREO_POLICY_LEFT_FIRST, views = -1, three_d = 0, order_word = 0;
    int previous = 0; /* 1 after a lone H or HALF, 2 after a lone F or FULL, in the word before */
    const size_t length = strlen(file_name);
    size_t i = 0;
    while (i < length) {
        while (i < length && is_separator(file_name[i]))
            ++i;
        const size_t start = i;
        while (i < length && !is_separator(file_name[i]))
            ++i;
        const size_t end = i;
        if (end == start)
            break;
        int word_layout, word_views;
        if (packing_word(file_name, start, end, &word_layout, &word_views)) {
            if (layout == STEREO_POLICY_LAYOUT_NONE) {
                layout = word_layout;
                views = word_views;
                if (views < 0 && previous == 1)
                    views = 0;
                else if (views < 0 && previous == 2)
                    views = 1;
            }
            three_d = 1;
            previous = 0;
        } else if (word_is(file_name, start, end, "3D")) {
            three_d = 1;
            previous = 0;
        } else if (word_is(file_name, start, end, "LR") || word_is(file_name, start, end, "RL")) {
            order = toupper((unsigned char)file_name[start]) == 'R' ? STEREO_POLICY_RIGHT_FIRST : STEREO_POLICY_LEFT_FIRST;
            order_word = 1;
            previous = 0;
        } else if (word_is(file_name, start, end, "H") || word_is(file_name, start, end, "HALF")) {
            previous = 1;
        } else if (word_is(file_name, start, end, "F") || word_is(file_name, start, end, "FULL")) {
            previous = 2;
        } else {
            previous = 0;
        }
    }
    if (order_word && layout == STEREO_POLICY_LAYOUT_NONE) {
        layout = STEREO_POLICY_LAYOUT_SIDE_BY_SIDE;
        three_d = 1;
    }
    set(out_layout, layout);
    set(out_order, order);
    set(out_views, views);
    set(out_three_d, three_d);
    return three_d || order_word;
}

int stereo_policy_video_layout_named(int width, int height, const char *mark, const char *file_name, int halves, int user_layout, int user_order,
    int swap_eyes, int *layout, int *order, int *source, int *views)
{
    set(views, -1);
    if (stereo_policy_video_layout(width, height, mark, user_layout, user_order, swap_eyes, layout, order, source) != 0)
        return -1;
    if (*source != STEREO_POLICY_SOURCE_NONE && *source != STEREO_POLICY_SOURCE_SHAPE)
        return 0; /* the user's choice or the stream's mark: a tag never overrides one */
    int tag_layout, tag_order, tag_views, three_d;
    if (stereo_policy_name_tag(file_name, &tag_layout, &tag_order, &tag_views, &three_d) != 1 || !three_d)
        return 0;
    if (tag_layout == STEREO_POLICY_LAYOUT_NONE) {
        /* only "3D": the shape, else the halves, else nothing */
        if (*source == STEREO_POLICY_SOURCE_SHAPE)
            return 0; /* side by side by the shape rule, as without the tag */
        if (halves == STEREO_POLICY_HALVES_SIDE_BY_SIDE)
            tag_layout = STEREO_POLICY_LAYOUT_SIDE_BY_SIDE;
        else if (halves == STEREO_POLICY_HALVES_TOP_AND_BOTTOM)
            tag_layout = STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM;
        else
            return 0;
    }
    *layout = tag_layout;
    *order = tag_order;
    *source = STEREO_POLICY_SOURCE_NAME;
    set(views, tag_views);
    if (swap_eyes)
        *order = *order == STEREO_POLICY_LEFT_FIRST ? STEREO_POLICY_RIGHT_FIRST : STEREO_POLICY_LEFT_FIRST;
    return 0;
}

/* The mean absolute difference of two regions of a picture, on a grid, the second shifted by dx: a, b are the top left
 * samples of the two regions, w and h their size. */
static long long region_difference(const unsigned char *a, const unsigned char *b, int stride, int w, int h, int dx, int *count)
{
    const int step_x = w >= 128 ? w / 64 : 1;
    const int step_y = h >= 128 ? h / 64 : 1;
    long long sum = 0;
    int n = 0;
    for (int y = 0; y < h; y += step_y) {
        for (int x = 0; x < w; x += step_x) {
            const int bx = x + dx;
            if (bx < 0 || bx >= w)
                continue;
            const int d = (int)a[(size_t)y * stride + x] - (int)b[(size_t)y * stride + bx];
            sum += d < 0 ? -d : d;
            ++n;
        }
    }
    *count = n;
    return sum;
}

/* the smallest mean difference (times 256, for precision) of two regions over the allowed shifts */
static long long best_difference(const unsigned char *a, const unsigned char *b, int stride, int w, int h)
{
    const int range = w / 32;
    const int step = range >= 8 ? range / 8 : 1;
    long long best = -1;
    for (int dx = -range; dx <= range; dx += step) {
        int n;
        const long long sum = region_difference(a, b, stride, w, h, dx, &n);
        if (n < 64)
            continue;
        const long long mean = sum * 256 / n;
        if (best < 0 || mean < best)
            best = mean;
    }
    return best;
}

int stereo_policy_halves_test(const unsigned char *luma, int width, int height, int stride)
{
    if (!luma || width < 64 || height < 64 || stride < width)
        return STEREO_POLICY_HALVES_UNKNOWN;
    const int half_w = width / 2, half_h = height / 2;
    const long long side = best_difference(luma, luma + half_w, stride, half_w, height);
    const long long top = best_difference(luma, luma + (size_t)half_h * stride, stride, width, half_h);
    if (side < 0 || top < 0)
        return STEREO_POLICY_HALVES_UNKNOWN;
    const long long close = 255LL * 256 * STEREO_POLICY_HALVES_CLOSE_PERCENT / 100;
    long long better = side < top ? side : top;
    long long other = side < top ? top : side;
    if (better > close || better * 100 >= other * STEREO_POLICY_HALVES_MARGIN_PERCENT)
        return STEREO_POLICY_HALVES_UNKNOWN;
    return side < top ? STEREO_POLICY_HALVES_SIDE_BY_SIDE : STEREO_POLICY_HALVES_TOP_AND_BOTTOM;
}

int stereo_policy_video_views_by_shape(int width, int height, int layout)
{
    if (width <= 0 || height <= 0)
        return -1;
    switch (layout) {
    case STEREO_POLICY_LAYOUT_SIDE_BY_SIDE:
    case STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE:
        /* wider than a single picture can be: two pictures next to each other */
        return (long long)width * STEREO_POLICY_SINGLE_PICTURE_WIDEST_DEN > (long long)height * STEREO_POLICY_SINGLE_PICTURE_WIDEST_NUM;
    case STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM:
    case STEREO_POLICY_LAYOUT_DEPTH_TOP_AND_BOTTOM:
        /* narrower than a single picture can be: two pictures on top of each other */
        return (long long)width * STEREO_POLICY_SINGLE_PICTURE_NARROWEST_DEN < (long long)height * STEREO_POLICY_SINGLE_PICTURE_NARROWEST_NUM;
    case STEREO_POLICY_LAYOUT_DEPTH_GRAPHICS:
        /* narrower than two single pictures on top of each other can be: four pictures */
        return (long long)width * STEREO_POLICY_SINGLE_PICTURE_NARROWEST_DEN * 2 < (long long)height * STEREO_POLICY_SINGLE_PICTURE_NARROWEST_NUM;
    case STEREO_POLICY_LAYOUT_DEPTH_BACKGROUND:
        /* each quarter has the frame's shape: shown at the frame's size */
        return 0;
    case STEREO_POLICY_LAYOUT_DEPTH_SEPARATE:
        return 1;
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

int stereo_policy_video_full_views(int width, int height, int layout) { return stereo_policy_video_views_by_shape(width, height, layout); }

/* a and b are the same shape, within one percent: a_w / a_h == b_w / b_h */
static int same_shape(long long a_w, long long a_h, long long b_w, long long b_h)
{
    const long long left = a_w * b_h;
    const long long right = b_w * a_h;
    const long long difference = left > right ? left - right : right - left;
    return difference * 100 <= right;
}

int stereo_policy_matroska_plane(int coded_width, int coded_height, int sar_num, int sar_den, int layout, int *plane_width, int *plane_height)
{
    if (coded_width <= 0 || coded_height <= 0 || sar_num <= 0 || sar_den <= 0 || !plane_width || !plane_height)
        return -1;
    /* FFmpeg's Matroska demuxer (libavformat/matroskadec.c) turns DisplayWidth and DisplayHeight into the stream's
     * sample aspect ratio after multiplying the stacked axis of a side by side or top and bottom track by two, so that
     * the frame as shown holds both planes: undo it. */
    const long long shown_width = ((long long)coded_width * sar_num + sar_den / 2) / sar_den;
    const long long shown_height = ((long long)coded_height * sar_den + sar_num / 2) / sar_num;
    if (layout == STEREO_POLICY_LAYOUT_SIDE_BY_SIDE || layout == STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE) {
        *plane_width = (int)(shown_width / 2);
        *plane_height = coded_height;
    } else if (layout == STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM || layout == STEREO_POLICY_LAYOUT_DEPTH_TOP_AND_BOTTOM) {
        *plane_width = coded_width;
        *plane_height = (int)(shown_height / 2);
    } else {
        return -1;
    }
    return 0;
}

int stereo_policy_video_views(int coded_width, int coded_height, int sar_num, int sar_den, int plane_width, int plane_height,
    int upsampled_aspect_ratio, int layout, int *decided_by)
{
    if (coded_width <= 0 || coded_height <= 0)
        return -1;
    if (sar_num <= 0 || sar_den <= 0) {
        sar_num = 1;
        sar_den = 1;
    }
    set(decided_by, STEREO_POLICY_VIEWS_BY_LAYOUT);
    const int side_by_side = layout == STEREO_POLICY_LAYOUT_SIDE_BY_SIDE || layout == STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE;
    const int packed = side_by_side || layout == STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM || layout == STEREO_POLICY_LAYOUT_DEPTH_TOP_AND_BOTTOM;
    if (!packed)
        return stereo_policy_video_views_by_shape(coded_width, coded_height, layout);

    /* The container's mark: a Matroska plane size (DisplayWidth and DisplayHeight, RFC 9559 section 18.10, elements
     * 5.1.4.1.28.12 and .13) is the size of one plane, so it says how much of the frame one view is. A plane as large as the frame is
     * the elements' default (the pixel size) and says nothing. */
    if (plane_width > 0 && plane_height > 0 && !(plane_width == coded_width && plane_height == coded_height)) {
        const long long view_width = side_by_side ? coded_width / 2 : coded_width;
        const long long view_height = side_by_side ? coded_height : coded_height / 2;
        if (same_shape(view_width, view_height, plane_width, plane_height)) {
            set(decided_by, STEREO_POLICY_VIEWS_BY_MARK);
            return 1;
        }
        if (same_shape(coded_width, coded_height, plane_width, plane_height)) {
            set(decided_by, STEREO_POLICY_VIEWS_BY_MARK);
            return 0;
        }
    }

    /* The H.265 frame packing SEI (D.3.16): upsampled_aspect_ratio_flag 1 says the sample aspect ratio is that of the
     * samples after the views are up-converted to the frame, so each view is shown at the frame's shape: half views;
     * 0 says it is that of the samples as stored, so each view is shown as stored: full views. H.264 (D.2.26) has no
     * such flag: its NOTE 2 only says the SAR is meant for a decoder that does not interpret the SEI message. */
    if (upsampled_aspect_ratio == 1 || upsampled_aspect_ratio == 0) {
        set(decided_by, STEREO_POLICY_VIEWS_BY_MARK);
        return upsampled_aspect_ratio == 0;
    }

    /* The marks are silent: a frame marked side by side or top and bottom whose size, with the sample aspect ratio
     * applied, is that of a single picture carries half views; one twice as wide or twice as tall carries full views. */
    set(decided_by, STEREO_POLICY_VIEWS_BY_SHAPE);
    const long long shown_width = ((long long)coded_width * sar_num + sar_den / 2) / sar_den;
    return stereo_policy_video_views_by_shape(shown_width > 0x7fffffff ? 0x7fffffff : (int)shown_width, coded_height, layout);
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
