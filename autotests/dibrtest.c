/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/* The DIBR core without Qt: the parallax of every depth convention (H.265 Table G.3, H.264 Table H-1, ISO/IEC 23002-3
 * as we read it, 2D-plus-depth), and views drawn with every warp, fill and smoothing, checked pixel by pixel on
 * pictures whose right answer is known: a flat depth, a uniform shift, and a near square on a far background. */

#include "stereo-dibr.h"

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

#define NEAR(a, b) (fabs((double)(a) - (double)(b)) < 1e-3)

static void conventions(void)
{
    float t[65536];
    struct stereo_depth_convention c;

    /* 2D-plus-depth: the screen in the middle, 60 pixels either way at 1920, 30 at 960 */
    memset(&c, 0, sizeof(c));
    c.kind = STEREO_DEPTH_NEARNESS;
    c.bits = 8;
    c.zero = 0.5;
    c.range = 60.0;
    CHECK(stereo_depth_parallax_table(&c, 960, t) == 0);
    CHECK(NEAR(t[0], 30.0) && NEAR(t[255], -30.0));
    CHECK(fabs(t[128]) < 0.2);
    /* the screen nearer: the far side keeps the whole range, the near side its share */
    c.zero = 0.75;
    CHECK(stereo_depth_parallax_table(&c, 1920, t) == 0);
    CHECK(NEAR(t[0], 60.0) && NEAR(t[255], -20.0));
    /* a horizontal image translation moves everything */
    c.zero = 0.5;
    c.shift = 10.0;
    CHECK(stereo_depth_parallax_table(&c, 1920, t) == 0);
    CHECK(NEAR(t[0], 70.0) && NEAR(t[255], -50.0));
    c.shift = 0.0;

    /* farness and type 0 (inverse Z, uniform in parallax) */
    c.kind = STEREO_DEPTH_FARNESS;
    CHECK(stereo_depth_parallax_table(&c, 1920, t) == 0);
    CHECK(NEAR(t[0], -60.0) && NEAR(t[255], 60.0));
    c.kind = STEREO_DEPTH_INVERSE_Z;
    CHECK(stereo_depth_parallax_table(&c, 1920, t) == 0);
    CHECK(NEAR(t[0], 60.0) && NEAR(t[255], -60.0));

    /* type 2, uniform Z between ZFar 4 and ZNear 1: a sample halfway is Z 2.5, 1/Z 0.4, a fifth of the way from 1/ZFar
     * to 1/ZNear, so 0.6 of the range behind the screen */
    c.kind = STEREO_DEPTH_Z;
    c.bits = 10;
    c.z_present = 1;
    c.z_near = 1.0;
    c.z_far = 4.0;
    CHECK(stereo_depth_parallax_table(&c, 1920, t) == 0);
    CHECK(NEAR(t[0], 60.0) && NEAR(t[1023], -60.0));
    {
        const double z = 4.0 + (511.0 / 1023.0) * (1.0 - 4.0), near = (1.0 / z - 0.25) / 0.75;
        CHECK(NEAR(t[511], 60.0 * (0.5 - near) / 0.5));
    }
    /* without ZNear and ZFar: as nearness */
    c.z_present = 0;
    CHECK(stereo_depth_parallax_table(&c, 1920, t) == 0);
    CHECK(NEAR(t[0], 60.0) && NEAR(t[1023], -60.0));

    /* type 1: disparity from DMin 2 to DMax 40 in a view 1920 wide, the parallax negated and halved for a view 960
     * wide */
    c.kind = STEREO_DEPTH_DISPARITY;
    c.bits = 8;
    c.d_present = 1;
    c.d_min = 2.0;
    c.d_max = 40.0;
    c.d_width = 1920;
    CHECK(stereo_depth_parallax_table(&c, 960, t) == 0);
    CHECK(NEAR(t[0], -1.0) && NEAR(t[255], -20.0));

    /* type 3: DepthLUT first; the identity gives type 1, a LUT that halves gives half the disparity span */
    {
        static uint16_t lut[256];
        for (int i = 0; i < 256; i++)
            lut[i] = (uint16_t)(i / 2);
        c.kind = STEREO_DEPTH_NONLINEAR_DISPARITY;
        c.lut = lut;
        CHECK(stereo_depth_parallax_table(&c, 1920, t) == 0);
        CHECK(NEAR(t[0], -2.0) && NEAR(t[255], -(2.0 + (127.0 / 255.0) * 38.0)));
        c.lut = NULL;
    }

    /* ISO/IEC 23002-3 parallax map, as we read it: (m - 128) / 64 cm on a screen 100 cm wide */
    memset(&c, 0, sizeof(c));
    c.kind = STEREO_DEPTH_PARALLAX;
    c.bits = 8;
    c.parallax_zero = 128.0;
    c.parallax_scale = 64.0;
    c.wref = 100.0;
    CHECK(stereo_depth_parallax_table(&c, 1920, t) == 0);
    CHECK(NEAR(t[128], 0.0) && NEAR(t[192], 19.2) && NEAR(t[0], -38.4));

    /* bad arguments */
    c.bits = 0;
    CHECK(stereo_depth_parallax_table(&c, 1920, t) == -1);
    c.bits = 8;
    c.kind = 99;
    CHECK(stereo_depth_parallax_table(&c, 1920, t) == -1);
    CHECK(stereo_depth_parallax_table(NULL, 1920, t) == -1);
}

/* A picture of 8-bit luma and 4:2:0 chroma, or 16-bit luma only, in malloc'd planes. */
struct image {
    struct stereo_dibr_picture pic;
    uint8_t *buf[4];
};

static void image_init(struct image *im, int w, int h, int bytes, int chroma)
{
    memset(im, 0, sizeof(*im));
    im->pic.width = w;
    im->pic.height = h;
    im->pic.nb_planes = chroma ? 3 : 1;
    for (int k = 0; k < im->pic.nb_planes; k++) {
        struct stereo_dibr_plane *p = &im->pic.plane[k];
        p->bytes = bytes;
        p->log2_w = p->log2_h = k ? 1 : 0;
        p->stride = (ptrdiff_t)((w + 1) >> p->log2_w) * bytes;
        p->hole = k ? 128 : 16;
        im->buf[k] = calloc((size_t)p->stride * ((h + 1) >> p->log2_h), 1);
        p->data = im->buf[k];
    }
}

static void image_free(struct image *im)
{
    for (int k = 0; k < 4; k++)
        free(im->buf[k]);
}

static unsigned px(const struct image *im, int k, int x, int y)
{
    const struct stereo_dibr_plane *p = &im->pic.plane[k];
    const uint8_t *row = (const uint8_t *)p->data + y * p->stride;
    return p->bytes == 1 ? row[x] : ((const uint16_t *)row)[x];
}

static void set_px(struct image *im, int k, int x, int y, unsigned v)
{
    struct stereo_dibr_plane *p = &im->pic.plane[k];
    uint8_t *row = (uint8_t *)p->data + y * p->stride;
    if (p->bytes == 1)
        row[x] = (uint8_t)v;
    else
        ((uint16_t *)row)[x] = (uint16_t)v;
}

enum { W = 64, H = 16 };

/* A ramp picture (luma 2x + 10, chroma by column), a constant table of parallax p: the right view is the picture
 * shifted right by p, its first p columns the extension of column 0. */
static void uniform_shift(int warp, int fill, int smooth)
{
    struct image pic, left, right;
    uint8_t depth[W * H];
    float table[256];
    struct stereo_dibr_params prm;
    struct stereo_dibr_depth dm = { depth, W, 1, W, H };
    stereo_dibr *d = stereo_dibr_alloc();
    const int p = 4;

    image_init(&pic, W, H, 1, 1);
    image_init(&left, W, H, 1, 1);
    image_init(&right, W, H, 1, 1);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            set_px(&pic, 0, x, y, 2 * x + 10);
    for (int y = 0; y < H / 2; y++)
        for (int x = 0; x < W / 2; x++) {
            set_px(&pic, 1, x, y, 100 + x);
            set_px(&pic, 2, x, y, 150 - x);
        }
    memset(depth, 77, sizeof(depth));
    for (int i = 0; i < 256; i++)
        table[i] = (float)p;

    stereo_dibr_params_default(&prm);
    prm.warp = warp;
    prm.fill = fill;
    prm.smooth = smooth;
    CHECK(stereo_dibr_render(d, &prm, table, 256, &pic.pic, &dm, &left.pic, &right.pic) == 0);

    int bad_left = 0, bad_right = 0, bad_chroma = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            bad_left += px(&left, 0, x, y) != px(&pic, 0, x, y);
            unsigned want = x >= p ? 2 * (x - p) + 10 : fill == STEREO_DIBR_FILL_NONE ? 16 : 10;
            if ((fill == STEREO_DIBR_FILL_TELEA || fill == STEREO_DIBR_FILL_DEPTH_AIDED) && x < p)
                want = px(&right, 0, x, y); /* inpainted: checked below */
            bad_right += px(&right, 0, x, y) != want;
        }
    for (int y = 0; y < H / 2; y++)
        for (int x = p / 2; x < W / 2; x++)
            bad_chroma += px(&right, 1, x, y) != (unsigned)(100 + x - p / 2) || px(&right, 2, x, y) != (unsigned)(150 - (x - p / 2));
    CHECK(bad_left == 0);
    CHECK(bad_right == 0);
    CHECK(bad_chroma == 0);
    if (bad_left || bad_right || bad_chroma)
        fprintf(stderr, "  uniform shift: warp %d fill %d smooth %d: %d %d %d\n", warp, fill, smooth, bad_left, bad_right, bad_chroma);
    if (fill == STEREO_DIBR_FILL_TELEA || fill == STEREO_DIBR_FILL_DEPTH_AIDED)
        for (int y = 0; y < H; y++)
            for (int x = 0; x < p; x++)
                CHECK(abs((int)px(&right, 0, x, y) - 10) <= 12); /* continues the ramp's start, give or take its slope */

    stereo_dibr_free(d);
    image_free(&pic);
    image_free(&left);
    image_free(&right);
}

/* A near square (luma 200) on a far flat background (luma 50): background parallax +6, square -6. In the right view
 * the square moves 6 left, the background 6 right, and the 12 columns right of the square are a disocclusion that
 * every fill but none fills with the background. */
static void near_square(int view, int warp, int fill, int bytes)
{
    struct image pic, left, right;
    uint8_t depth[W * H];
    float table[256];
    struct stereo_dibr_params prm;
    struct stereo_dibr_depth dm = { depth, W, 1, W, H };
    stereo_dibr *d = stereo_dibr_alloc();
    const int x0 = 24, x1 = 40, bg = 50 * (bytes == 2 ? 256 : 1), fg = 200 * (bytes == 2 ? 256 : 1);

    image_init(&pic, W, H, bytes, bytes == 1);
    image_init(&left, W, H, bytes, bytes == 1);
    image_init(&right, W, H, bytes, bytes == 1);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            const int in = x >= x0 && x < x1;
            set_px(&pic, 0, x, y, in ? fg : bg);
            depth[y * W + x] = in ? 255 : 0;
        }
    if (bytes == 1)
        for (int y = 0; y < H / 2; y++)
            for (int x = 0; x < W / 2; x++) {
                set_px(&pic, 1, x, y, 128);
                set_px(&pic, 2, x, y, 128);
            }
    for (int i = 0; i < 256; i++)
        table[i] = 6.0f - 12.0f * i / 255.0f;

    stereo_dibr_params_default(&prm);
    prm.view = view;
    prm.warp = warp;
    prm.fill = fill;
    CHECK(stereo_dibr_render(d, &prm, table, 256, &pic.pic, &dm, &left.pic, &right.pic) == 0);

    /* the right view: s = 1 (left-eye picture) or 1/2 (middle picture) */
    const double s = view == STEREO_DIBR_VIEW_LEFT ? 1.0 : 0.5;
    const int fs = (int)lround(x0 - 6 * s), fe = (int)lround(x1 - 6 * s); /* the square */
    const int he = (int)lround(x1 + 6 * s);                               /* the end of the disocclusion */
    int bad_square = 0, bad_hole = 0, bad_bg = 0;
    for (int y = 0; y < H; y++) {
        for (int x = fs + 1; x < fe - 1; x++)
            bad_square += px(&right, 0, x, y) != (unsigned)fg;
        for (int x = fe + 1; x < he; x++) {
            const unsigned v = px(&right, 0, x, y);
            if (fill == STEREO_DIBR_FILL_NONE)
                /* the layered warp's main layer carries the background on under the square's edge */
                bad_hole += v != 16 && !(warp == STEREO_DIBR_WARP_LAYERED && v == (unsigned)bg);
            else
                bad_hole += abs((int)v - bg) > (fill == STEREO_DIBR_FILL_TELEA ? bg / 10 : 0);
        }
        for (int x = he + 1; x < W; x++)
            bad_bg += px(&right, 0, x, y) != (unsigned)bg;
        for (int x = 0; x < fs - 1; x++)
            bad_bg += x >= (int)lround(6 * s) && px(&right, 0, x, y) != (unsigned)bg;
    }
    CHECK(bad_square == 0);
    CHECK(bad_hole == 0);
    CHECK(bad_bg == 0);
    if (bad_square || bad_hole || bad_bg)
        fprintf(stderr, "  near square: view %d warp %d fill %d bytes %d: %d %d %d\n", view, warp, fill, bytes, bad_square, bad_hole, bad_bg);

    if (view == STEREO_DIBR_VIEW_CENTER) {
        /* the left view mirrors it: the square 3 right, the disocclusion left of it */
        int bad_mirror = 0;
        for (int y = 0; y < H; y++) {
            for (int x = x0 + 3 + 1; x < x1 + 3 - 1; x++)
                bad_mirror += px(&left, 0, x, y) != (unsigned)fg;
            if (fill != STEREO_DIBR_FILL_NONE)
                for (int x = x0 - 3 + 1; x < x0 + 3; x++)
                    bad_mirror += abs((int)px(&left, 0, x, y) - bg) > (fill == STEREO_DIBR_FILL_TELEA ? bg / 10 : 0);
        }
        CHECK(bad_mirror == 0);
        if (bad_mirror)
            fprintf(stderr, "  mirror: warp %d fill %d: %d\n", warp, fill, bad_mirror);
    } else {
        int bad_left = 0;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                bad_left += px(&left, 0, x, y) != px(&pic, 0, x, y);
        CHECK(bad_left == 0);
    }

    stereo_dibr_free(d);
    image_free(&pic);
    image_free(&left);
    image_free(&right);
}

/* A depth map at a quarter of the picture's size gives the same views as the full one when the depth is flat. */
static void scaled_depth(void)
{
    struct image pic, l1, r1, l2, r2;
    uint8_t full[W * H], quarter[(W / 2) * (H / 2)];
    float table[256];
    struct stereo_dibr_params prm;
    struct stereo_dibr_depth d1 = { full, W, 1, W, H }, d2 = { quarter, W / 2, 1, W / 2, H / 2 };
    stereo_dibr *d = stereo_dibr_alloc();

    image_init(&pic, W, H, 1, 1);
    image_init(&l1, W, H, 1, 1);
    image_init(&r1, W, H, 1, 1);
    image_init(&l2, W, H, 1, 1);
    image_init(&r2, W, H, 1, 1);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            set_px(&pic, 0, x, y, (x * 7 + y * 3) & 255);
    memset(full, 200, sizeof(full));
    memset(quarter, 200, sizeof(quarter));
    for (int i = 0; i < 256; i++)
        table[i] = 3.0f - i / 40.0f;
    stereo_dibr_params_default(&prm);
    prm.view = STEREO_DIBR_VIEW_CENTER;
    CHECK(stereo_dibr_render(d, &prm, table, 256, &pic.pic, &d1, &l1.pic, &r1.pic) == 0);
    CHECK(stereo_dibr_render(d, &prm, table, 256, &pic.pic, &d2, &l2.pic, &r2.pic) == 0);
    int diff = 0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            diff += px(&l1, 0, x, y) != px(&l2, 0, x, y) || px(&r1, 0, x, y) != px(&r2, 0, x, y);
    CHECK(diff == 0);

    /* and a picture whose depth is at the screen is drawn unchanged into both views, smoothed or not */
    for (int i = 0; i < 256; i++)
        table[i] = 0.0f;
    prm.smooth = STEREO_DIBR_SMOOTH_ASYMMETRIC_GAUSSIAN;
    for (int warp = 0; warp < 3; warp++) {
        prm.warp = warp;
        CHECK(stereo_dibr_render(d, &prm, table, 256, &pic.pic, &d1, &l1.pic, &r1.pic) == 0);
        diff = 0;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++)
                diff += px(&l1, 0, x, y) != px(&pic, 0, x, y) || px(&r1, 0, x, y) != px(&pic, 0, x, y);
        CHECK(diff == 0);
    }

    /* bad arguments */
    CHECK(stereo_dibr_render(d, NULL, table, 256, &pic.pic, &d1, &l1.pic, &r1.pic) == -1);
    l1.pic.width = W - 1;
    CHECK(stereo_dibr_render(d, &prm, table, 256, &pic.pic, &d1, &l1.pic, &r1.pic) == -1);

    stereo_dibr_free(d);
    image_free(&pic);
    image_free(&l1);
    image_free(&r1);
    image_free(&l2);
    image_free(&r2);
}

/* Smoothing shrinks the disocclusion: the asymmetric Gaussian turns the depth step into a ramp. */
static void smoothing_shrinks_holes(void)
{
    struct image pic, left, right;
    uint8_t depth[W * H];
    float table[256];
    struct stereo_dibr_params prm;
    struct stereo_dibr_depth dm = { depth, W, 1, W, H };
    stereo_dibr *d = stereo_dibr_alloc();

    image_init(&pic, W, H, 1, 0);
    image_init(&left, W, H, 1, 0);
    image_init(&right, W, H, 1, 0);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            set_px(&pic, 0, x, y, 100);
            depth[y * W + x] = x >= 24 && x < 40 ? 255 : 0;
        }
    for (int i = 0; i < 256; i++)
        table[i] = 6.0f - 12.0f * i / 255.0f;
    stereo_dibr_params_default(&prm);
    prm.fill = STEREO_DIBR_FILL_NONE;
    int holes[2] = { 0, 0 };
    for (int k = 0; k < 2; k++) {
        prm.smooth = k ? STEREO_DIBR_SMOOTH_ASYMMETRIC_GAUSSIAN : STEREO_DIBR_SMOOTH_OFF;
        prm.sigma_h = 60.0; /* 2 pixels at 64 */
        prm.sigma_v = 180.0;
        CHECK(stereo_dibr_render(d, &prm, table, 256, &pic.pic, &dm, &left.pic, &right.pic) == 0);
        for (int x = 0; x < W; x++)
            holes[k] += px(&right, 0, x, H / 2) == 16;
    }
    CHECK(holes[0] >= 12);
    CHECK(holes[1] < holes[0]);
    stereo_dibr_free(d);
    image_free(&pic);
    image_free(&left);
    image_free(&right);
}

int main(void)
{
    conventions();
    for (int warp = 0; warp < 3; warp++)
        for (int fill = 0; fill < 4; fill++)
            for (int smooth = 0; smooth < 2; smooth++)
                uniform_shift(warp, fill, smooth);
    for (int view = 0; view < 2; view++)
        for (int warp = 0; warp < 3; warp++)
            for (int fill = 0; fill < 4; fill++) {
                near_square(view, warp, fill, 1);
                near_square(view, warp, fill, 2);
            }
    scaled_depth();
    smoothing_shrinks_holes();
    if (failures)
        fprintf(stderr, "%d failures\n", failures);
    return failures != 0;
}
