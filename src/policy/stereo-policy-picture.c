/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/* Whether the two halves of an unmarked still picture are views of one scene. Shape only says a picture may be a pair
 * (stereo_policy_picture_pair_by_shape()); the pixels say whether it is. */

#include "stereo-policy.h"

#include <math.h>
#include <stdlib.h>

#ifndef VIEW_WIDTH_MAX
#define VIEW_WIDTH_MAX 192 /* the views are compared at no more than this width */
#endif
#ifndef BLOCK
#define BLOCK 16 /* side of a block, in compared pixels */
#endif
#ifndef SHIFT_MAX_DIV
#define SHIFT_MAX_DIV 8 /* the horizontal search: one eighth of a view's width, either way */
#endif
#ifndef VERTICAL_MAX
#define VERTICAL_MAX 2 /* the vertical search, in compared pixels, either way */
#endif
#ifndef TEXTURE_MIN
#define TEXTURE_MIN 3.0f /* a block with a smaller standard deviation of luminance matches anything and says nothing */
#endif
#ifndef BLOCKS_MIN
#define BLOCKS_MIN 4 /* textured blocks needed to decide */
#endif
#ifndef MATCH_MIN
#define MATCH_MIN 0.85f /* normalised cross-correlation of a matched block */
#endif
#ifndef MATCHED_SHARE
#define MATCHED_SHARE 0.60f /* share of the textured blocks that match */
#endif
#ifndef COPY_NCC
#define COPY_NCC 0.999f /* a block this correlated at no shift ... */
#endif
#ifndef COPY_DIFFERENCE
#define COPY_DIFFERENCE 1.0f /* ... and this close in luminance, on average, is a copy */
#endif
#ifndef LEVEL_SHARE
#define LEVEL_SHARE 0.70f /* share of the matched blocks within one pixel vertically */
#endif

/* Luminance of each view, the box-averaged mean of whole pixels (Rec. 601 weights), views of vw by vh. */
static void views(const unsigned char *rgb, int stride, int factor, int half, int vw, int vh, float *left, float *right)
{
    for (int view = 0; view < 2; ++view) {
        float *out = view ? right : left;
        for (int y = 0; y < vh; ++y) {
            for (int x = 0; x < vw; ++x) {
                long sum = 0;
                for (int j = 0; j < factor; ++j) {
                    const unsigned char *p = rgb + (long)(y * factor + j) * stride + (long)(view * half + x * factor) * 3;
                    for (int i = 0; i < factor; ++i, p += 3)
                        sum += 299 * p[0] + 587 * p[1] + 114 * p[2];
                }
                out[y * vw + x] = (float)sum / (1000.0f * factor * factor);
            }
        }
    }
}

int stereo_policy_picture_pair_by_content(const unsigned char *rgb, int width, int height, int stride)
{
    if (!rgb || width < 2 || height < 1 || stride < width * 3)
        return -1;
    const int half = width / 2;
    const int factor = (half + VIEW_WIDTH_MAX - 1) / VIEW_WIDTH_MAX;
    const int vw = half / factor;
    const int vh = height / factor;
    const int shift = vw / SHIFT_MAX_DIV;
    if (vw < 4 * BLOCK || vh < 2 * BLOCK)
        return 0;
    float *left = malloc(sizeof(float) * vw * vh * 2);
    if (!left)
        return -1;
    float *right = left + vw * vh;
    views(rgb, stride, factor, half, vw, vh, left, right);

    int textured = 0, matched = 0, level = 0;
    for (int by = VERTICAL_MAX; by + BLOCK <= vh - VERTICAL_MAX; by += BLOCK) {
        for (int bx = 0; bx + BLOCK <= vw; bx += BLOCK) {
            float mean = 0;
            for (int y = 0; y < BLOCK; ++y)
                for (int x = 0; x < BLOCK; ++x)
                    mean += left[(by + y) * vw + bx + x];
            mean /= BLOCK * BLOCK;
            float energy = 0;
            for (int y = 0; y < BLOCK; ++y)
                for (int x = 0; x < BLOCK; ++x) {
                    const float d = left[(by + y) * vw + bx + x] - mean;
                    energy += d * d;
                }
            if (sqrtf(energy / (BLOCK * BLOCK)) < TEXTURE_MIN)
                continue;
            float best = -2;
            int best_dy = 0, best_dx = 0;
            float best_difference = 0;
            for (int dy = -VERTICAL_MAX; dy <= VERTICAL_MAX; ++dy) {
                for (int dx = -shift; dx <= shift; ++dx) {
                    if (bx + dx < 0 || bx + dx + BLOCK > vw)
                        continue;
                    float other = 0;
                    for (int y = 0; y < BLOCK; ++y)
                        for (int x = 0; x < BLOCK; ++x)
                            other += right[(by + dy + y) * vw + bx + dx + x];
                    other /= BLOCK * BLOCK;
                    float cross = 0, other_energy = 0;
                    for (int y = 0; y < BLOCK; ++y)
                        for (int x = 0; x < BLOCK; ++x) {
                            const float a = left[(by + y) * vw + bx + x] - mean;
                            const float b = right[(by + dy + y) * vw + bx + dx + x] - other;
                            cross += a * b;
                            other_energy += b * b;
                        }
                    if (other_energy <= 0)
                        continue;
                    const float ncc = cross / sqrtf(energy * other_energy);
                    if (ncc > best) {
                        best = ncc;
                        best_dy = dy;
                        best_dx = dx;
                        best_difference = 0;
                        if (dx == 0 && dy == 0 && ncc > COPY_NCC) {
                            for (int y = 0; y < BLOCK; ++y)
                                for (int x = 0; x < BLOCK; ++x)
                                    best_difference += fabsf(left[(by + y) * vw + bx + x] - right[(by + y) * vw + bx + x]);
                            best_difference /= BLOCK * BLOCK;
                        }
                    }
                }
            }
            if (best_dx == 0 && best_dy == 0 && best > COPY_NCC && best_difference < COPY_DIFFERENCE)
                continue; /* a copy of the block: the same on a flat screen or a wallpaper as in a pair with no depth, it proves nothing */
            ++textured;
            if (best >= MATCH_MIN) {
                ++matched;
                if (abs(best_dy) <= 1)
                    ++level;
            }
        }
    }
    free(left);
    return textured >= BLOCKS_MIN && matched >= (int)ceilf(MATCHED_SHARE * textured) && level >= (int)ceilf(LEVEL_SHARE * matched);
}
