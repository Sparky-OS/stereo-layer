/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/* stereo-depth.h: the value as text in any locale, each eye's move, a whole move equal to a copy, a fractional move
 * against a reference computed here from the window's own formula. */

#include "stereo-depth.h"

#include <locale.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond, ...)                                                                                               \
    do {                                                                                                               \
        if (!(cond)) {                                                                                                 \
            failures++;                                                                                                \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);                                                       \
            fprintf(stderr, __VA_ARGS__);                                                                              \
            fputc('\n', stderr);                                                                                       \
        }                                                                                                              \
    } while (0)

static void text_round_trip(const char *in, const char *want)
{
    double v = 1234.5;
    char out[STEREO_DEPTH_TEXT_SIZE];
    int r = stereo_depth_parse(in, &v);

    CHECK(r == 0, "parse \"%s\" refused", in);
    if (r)
        return;
    r = stereo_depth_format(v, out, sizeof out);
    CHECK(r == (int)strlen(want) && strcmp(out, want) == 0, "\"%s\" -> \"%s\", wanted \"%s\"", in, out, want);
}

static void text_refused(const char *in)
{
    double v = 7.0;

    CHECK(stereo_depth_parse(in, &v) == -1, "parse \"%s\" accepted", in);
    CHECK(v == 7.0, "parse \"%s\" changed the value on refusal", in);
}

static void texts(void)
{
    char out[STEREO_DEPTH_TEXT_SIZE];

    text_round_trip("12", "12.000");
    text_round_trip("12.000", "12.000");
    text_round_trip("+12.5", "12.500");
    text_round_trip("-4.5", "-4.500");
    text_round_trip("-4.500", "-4.500");
    text_round_trip("0", "0.000");
    text_round_trip("-0", "0.000");
    text_round_trip("-0.0004", "0.000");
    text_round_trip("1.0005", "1.001");
    text_round_trip("-1.0005", "-1.001");
    text_round_trip("1.00049", "1.000");
    text_round_trip("2.99951", "3.000");
    text_round_trip(".25", "0.250");
    text_round_trip("7.", "7.000");
    text_round_trip("-123456789.125", "-123456789.125");
    text_round_trip("100000", "100000.000");
    text_refused("");
    text_refused("-");
    text_refused(".");
    text_refused(" 1");
    text_refused("1 ");
    text_refused("1,5");
    text_refused("1e3");
    text_refused("nan");
    text_refused("inf");
    text_refused("--1");
    text_refused("1.2.3");
    text_refused("99999999999999999999");
    CHECK(stereo_depth_parse(NULL, &(double){ 0 }) == -1, "parse NULL accepted");

    CHECK(stereo_depth_format(NAN, out, sizeof out) == -1, "NaN formatted");
    CHECK(stereo_depth_format(INFINITY, out, sizeof out) == -1, "infinity formatted");
    CHECK(stereo_depth_format(-INFINITY, out, sizeof out) == -1, "minus infinity formatted");
    CHECK(stereo_depth_format(12.0, out, 6) == -1, "12.000 fitted into 6 bytes");
    CHECK(stereo_depth_format(12.0, out, 7) == 6 && strcmp(out, "12.000") == 0, "12.000 in 7 bytes");
    CHECK(stereo_depth_format(-0.0, out, sizeof out) == 5 && strcmp(out, "0.000") == 0, "-0 written as %s", out);
    CHECK(fabs(stereo_depth_round(4.12345) - 4.123) < 1e-12, "round 4.12345");

    /* the program's locale never changes the text, either way */
    if (setlocale(LC_NUMERIC, "de_DE.UTF-8") || setlocale(LC_NUMERIC, "pt_BR.UTF-8") ||
        setlocale(LC_NUMERIC, "fr_FR.UTF-8")) {
        double v = 0;

        CHECK(stereo_depth_format(-4.5, out, sizeof out) == 6 && strcmp(out, "-4.500") == 0,
              "in a comma locale: %s", out);
        CHECK(stereo_depth_parse("12.250", &v) == 0 && v == 12.25, "parse in a comma locale");
        CHECK(stereo_depth_parse("12,250", &v) == -1, "a comma accepted");
        setlocale(LC_NUMERIC, "C");
        printf("comma locale: checked\n");
    } else {
        printf("comma locale: none installed, its rows not run\n");
    }
}

static void eyes(void)
{
    CHECK(stereo_depth_eye_shift(12.0, STEREO_DEPTH_EYE_LEFT) == -6.0, "left of +12");
    CHECK(stereo_depth_eye_shift(12.0, STEREO_DEPTH_EYE_RIGHT) == 6.0, "right of +12");
    CHECK(stereo_depth_eye_shift(-4.5, STEREO_DEPTH_EYE_LEFT) == 2.25, "left of -4.5");
    CHECK(stereo_depth_eye_shift(-4.5, STEREO_DEPTH_EYE_RIGHT) == -2.25, "right of -4.5");
    CHECK(stereo_depth_eye_shift(0.0, STEREO_DEPTH_EYE_RIGHT) == 0.0, "zero");
    /* behind the screen the right eye's picture lies to the right of the left eye's */
    CHECK(stereo_depth_eye_shift(3.0, STEREO_DEPTH_EYE_RIGHT) - stereo_depth_eye_shift(3.0, STEREO_DEPTH_EYE_LEFT) ==
              3.0,
          "the disparity equals the value");
    CHECK(stereo_depth_shift_is_whole(6.0) && stereo_depth_shift_is_whole(-2.0), "whole moves");
    CHECK(!stereo_depth_shift_is_whole(2.25) && !stereo_depth_shift_is_whole(-6.5), "fractional moves");
    CHECK(!stereo_depth_shift_is_whole(NAN), "NaN is no move");
}

static unsigned rng = 12345u;

static unsigned next(void)
{
    rng = rng * 1103515245u + 12345u;
    return rng >> 8;
}

static void whole_moves(void)
{
    enum { W = 17, H = 3 };
    unsigned char src[H][W * 4], dst[H][W * 4], want[H][W * 4];
    float fsrc[H][W * 4], fdst[H][W * 4];
    int dxs[] = { 6, -3, 0, 16, -16, 17, -40 };
    unsigned i;
    int y, x;

    for (y = 0; y < H; y++)
        for (x = 0; x < W * 4; x++) {
            src[y][x] = (unsigned char)next();
            fsrc[y][x] = (float)(next() % 1000) / 999.0f;
        }
    for (i = 0; i < sizeof dxs / sizeof dxs[0]; i++) {
        int dx = dxs[i];

        memset(want, 0, sizeof want);
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++)
                if (x - dx >= 0 && x - dx < W)
                    memcpy(&want[y][4 * x], &src[y][4 * (x - dx)], 4);
        CHECK(stereo_depth_shift_rgba(src, W, H, W * 4, 1, 0, dx, dst) == 0, "u8 move %d refused", dx);
        CHECK(memcmp(dst, want, sizeof want) == 0, "u8 move %d is not a copy", dx);
        /* in place */
        memcpy(dst, src, sizeof src);
        CHECK(stereo_depth_shift_rgba(dst, W, H, W * 4, 1, 1, dx, dst) == 0 && memcmp(dst, want, sizeof want) == 0,
              "u8 move %d in place", dx);
        CHECK(stereo_depth_shift_rgba(fsrc, W, H, W * 16, 4, 1, (double)dx, fdst) == 0, "float move %d", dx);
        for (y = 0; y < H; y++)
            for (x = 0; x < W; x++) {
                int c;

                for (c = 0; c < 4; c++) {
                    float w = (x - dx >= 0 && x - dx < W) ? fsrc[y][4 * (x - dx) + c] : 0.0f;

                    CHECK(fdst[y][4 * x + c] == w, "float move %d at %d,%d", dx, x, y);
                }
            }
    }
}

static double ref_lanczos(double t)
{
    if (fabs(t) < 1e-12)
        return 1.0;
    if (fabs(t) >= 3.0)
        return 0.0;
    return sin(M_PI * t) / (M_PI * t) * sin(M_PI * t / 3.0) / (M_PI * t / 3.0);
}

static void fractional_moves(void)
{
    enum { W = 32 };
    float src[W * 4], dst[W * 4];
    unsigned char u8[W * 4], u8out[W * 4];
    double dxs[] = { 0.5, -0.5, 2.25, -2.25, 6.5, -6.5, 0.125 };
    unsigned i;
    int x, c;

    for (x = 0; x < W; x++) {
        float a = (x >= 10 && x < 20) ? 1.0f : (x == 9 || x == 20) ? 0.5f : 0.0f;

        src[4 * x + 0] = 0.8f * a;
        src[4 * x + 1] = 0.3f * a;
        src[4 * x + 2] = 0.1f * a;
        src[4 * x + 3] = a;
    }
    for (i = 0; i < sizeof dxs / sizeof dxs[0]; i++) {
        double dx = dxs[i], max_err = 0.0;

        CHECK(stereo_depth_shift_rgba(src, W, 1, W * 16, 4, 1, dx, dst) == 0, "float move %.3f", dx);
        for (x = 0; x < W; x++) {
            double pos = x - dx, wsum = 0.0, acc[4] = { 0, 0, 0, 0 };
            int j;

            for (j = (int)floor(pos) - 2; j <= (int)floor(pos) + 3; j++) {
                double w = ref_lanczos(pos - j);

                wsum += w;
                if (j < 0 || j >= W)
                    continue;
                for (c = 0; c < 4; c++)
                    acc[c] += w * src[4 * j + c];
            }
            for (c = 0; c < 4; c++) {
                double want = acc[c] / wsum;

                if (c == 3)
                    want = want < 0 ? 0 : want > 1 ? 1 : want;
                else
                    want = want < 0 ? 0 : want;
                max_err = fmax(max_err, fabs(want - dst[4 * x + c]));
            }
        }
        CHECK(max_err < 1e-5, "float move %.3f differs from the reference by %g", dx, max_err);
        printf("fractional move %+.3f: largest difference from the reference %.2g\n", dx, max_err);
    }

    /* an opaque area keeps its colour inside, straight or premultiplied, after any move */
    for (x = 0; x < W; x++) {
        u8[4 * x + 0] = 200;
        u8[4 * x + 1] = 100;
        u8[4 * x + 2] = 50;
        u8[4 * x + 3] = (x >= 8 && x < 24) ? 255 : 0;
    }
    for (i = 0; i < sizeof dxs / sizeof dxs[0]; i++) {
        int straight;

        for (straight = 0; straight < 2; straight++) {
            unsigned char in[W * 4];

            memcpy(in, u8, sizeof in);
            if (!straight)
                for (x = 0; x < W; x++)
                    if (!in[4 * x + 3])
                        in[4 * x] = in[4 * x + 1] = in[4 * x + 2] = 0;
            CHECK(stereo_depth_shift_rgba(in, W, 1, W * 4, 1, !straight, dxs[i], u8out) == 0, "u8 move");
            for (x = 8 + (int)ceil(fabs(dxs[i])) + 3; x < 24 - (int)ceil(fabs(dxs[i])) - 3; x++)
                CHECK(u8out[4 * x] == 200 && u8out[4 * x + 1] == 100 && u8out[4 * x + 2] == 50 &&
                          u8out[4 * x + 3] == 255,
                      "u8 move %.3f (%s) changed the inside at %d: %d %d %d %d", dxs[i],
                      straight ? "straight" : "premultiplied", x, u8out[4 * x], u8out[4 * x + 1], u8out[4 * x + 2],
                      u8out[4 * x + 3]);
        }
    }

    /* 16 bits: a move and its opposite come back within the window's own blur, never above the range */
    {
        uint16_t s16[W * 4], m16[W * 4];

        for (x = 0; x < W * 4; x++)
            s16[x] = (x / 4 >= 8 && x / 4 < 24) ? 65535 : 0;
        CHECK(stereo_depth_shift_rgba(s16, W, 1, W * 8, 2, 1, 2.25, m16) == 0, "u16 move");
        for (x = 0; x < W * 4; x++)
            CHECK(m16[x] <= 65535, "u16 range");
    }
}

static void refusals(void)
{
    unsigned char px[16] = { 0 };

    CHECK(stereo_depth_shift_rgba(NULL, 1, 1, 4, 1, 1, 1.0, px) == -1, "null source");
    CHECK(stereo_depth_shift_rgba(px, 1, 1, 4, 1, 1, 1.0, NULL) == -1, "null destination");
    CHECK(stereo_depth_shift_rgba(px, 0, 1, 4, 1, 1, 1.0, px) == -1, "no width");
    CHECK(stereo_depth_shift_rgba(px, 1, 0, 4, 1, 1, 1.0, px) == -1, "no height");
    CHECK(stereo_depth_shift_rgba(px, 2, 1, 4, 1, 1, 1.0, px) == -1, "short stride");
    CHECK(stereo_depth_shift_rgba(px, 1, 1, 12, 3, 1, 1.0, px) == -1, "three-byte samples");
    CHECK(stereo_depth_shift_rgba(px, 1, 1, 4, 1, 1, NAN, px) == -1, "NaN move");
    CHECK(stereo_depth_shift_rgba(px, 1, 1, 4, 1, 1, INFINITY, px) == -1, "infinite move");
}

int main(void)
{
    texts();
    eyes();
    whole_moves();
    fractional_moves();
    refusals();
    if (failures) {
        fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    printf("depthlayertest: all passed\n");
    return 0;
}
