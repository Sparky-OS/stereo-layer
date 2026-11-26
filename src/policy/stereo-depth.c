/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/*
 * Depth layers: the value as text, each eye's move, and the move of a picture (stereo-depth.h).
 */

#include "stereo-depth.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Thousandths in a signed 64-bit integer: |value| up to about 9.2e15 pixels. */
#define DEPTH_LIMIT_THOUSANDTHS 9000000000000000000LL

/* Lanczos window of three lobes: six taps */
#define LANCZOS_A 3

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static int to_thousandths(double separation, long long *out)
{
    double t;

    if (!isfinite(separation))
        return -1;
    t = separation * 1000.0;
    if (fabs(t) >= (double)DEPTH_LIMIT_THOUSANDTHS)
        return -1;
    /* half away from zero; a value written with three decimals lands exactly on its thousandth */
    *out = llround(t);
    return 0;
}

int stereo_depth_parse(const char *text, double *separation)
{
    const char *p = text;
    int negative = 0;
    int digits = 0;
    int decimals = 0;
    unsigned long long whole = 0;
    unsigned long long frac = 0;
    int round_up = 0;
    long long thousandths;

    if (!text || !separation)
        return -1;
    if (*p == '+' || *p == '-') {
        negative = *p == '-';
        p++;
    }
    for (; *p >= '0' && *p <= '9'; p++) {
        if (whole > (unsigned long long)(DEPTH_LIMIT_THOUSANDTHS / 1000) / 10)
            return -1;
        whole = whole * 10 + (unsigned)(*p - '0');
        digits++;
    }
    if (*p == '.') {
        p++;
        for (; *p >= '0' && *p <= '9'; p++) {
            if (decimals < 3)
                frac = frac * 10 + (unsigned)(*p - '0');
            else if (decimals == 3)
                round_up = *p >= '5';
            decimals++;
            digits++;
        }
    }
    if (*p != '\0' || digits == 0)
        return -1;
    for (; decimals < 3; decimals++)
        frac *= 10;
    if (whole > (unsigned long long)(DEPTH_LIMIT_THOUSANDTHS / 1000))
        return -1;
    thousandths = (long long)(whole * 1000 + frac + (round_up ? 1 : 0));
    if (thousandths >= DEPTH_LIMIT_THOUSANDTHS)
        return -1;
    if (negative)
        thousandths = -thousandths;
    *separation = (double)thousandths / 1000.0;
    return 0;
}

int stereo_depth_format(double separation, char *out, size_t size)
{
    long long t;
    unsigned long long a;
    int n;

    if (!out || to_thousandths(separation, &t) < 0)
        return -1;
    a = t < 0 ? (unsigned long long)(-t) : (unsigned long long)t;
    /* integers only: the program's LC_NUMERIC never reaches the point */
    n = snprintf(out, size, "%s%llu.%03llu", t < 0 ? "-" : "", a / 1000, a % 1000);
    if (n < 0 || (size_t)n >= size)
        return -1;
    return n;
}

double stereo_depth_round(double separation)
{
    long long t;

    if (to_thousandths(separation, &t) < 0)
        return separation;
    return (double)t / 1000.0;
}

double stereo_depth_eye_shift(double separation, int eye)
{
    double half = separation / 2.0;

    return eye == STEREO_DEPTH_EYE_LEFT ? -half : half;
}

int stereo_depth_shift_is_whole(double dx)
{
    return isfinite(dx) && fabs(dx - nearbyint(dx)) < 1e-9;
}

static double lanczos(double t)
{
    double pt;

    if (t == 0.0)
        return 1.0;
    if (t <= -LANCZOS_A || t >= LANCZOS_A)
        return 0.0;
    pt = M_PI * t;
    return LANCZOS_A * sin(pt) * sin(pt / LANCZOS_A) / (pt * pt);
}

static float sample_get(const unsigned char *row, int i, int bps)
{
    switch (bps) {
    case 1:
        return row[i] / 255.0f;
    case 2: {
        uint16_t v;
        memcpy(&v, row + 2 * (size_t)i, 2);
        return v / 65535.0f;
    }
    default: {
        float v;
        memcpy(&v, row + 4 * (size_t)i, 4);
        return v;
    }
    }
}

static void sample_put(unsigned char *row, int i, int bps, float v)
{
    switch (bps) {
    case 1: {
        long q = lrintf(v * 255.0f);
        row[i] = (unsigned char)(q < 0 ? 0 : q > 255 ? 255 : q);
        break;
    }
    case 2: {
        long q = lrintf(v * 65535.0f);
        uint16_t s = (uint16_t)(q < 0 ? 0 : q > 65535 ? 65535 : q);
        memcpy(row + 2 * (size_t)i, &s, 2);
        break;
    }
    default:
        memcpy(row + 4 * (size_t)i, &v, 4);
        break;
    }
}

int stereo_depth_shift_rgba(const void *src, int width, int height, int stride, int bytes_per_sample,
                            int premultiplied, double dx, void *dest)
{
    const int bps = bytes_per_sample;
    const size_t pixel = 4 * (size_t)bps;
    float *in = NULL, *out = NULL;
    double w[2 * LANCZOS_A], sum = 0.0;
    long long n;
    double s;
    int k, y;

    if (!src || !dest || width < 1 || height < 1 || (bps != 1 && bps != 2 && bps != 4) || !isfinite(dx) ||
        stride < 0 || (size_t)stride < pixel * (size_t)width)
        return -1;

    if (stereo_depth_shift_is_whole(dx)) {
        unsigned char *row = malloc(pixel * (size_t)width);

        if (!row)
            return -1;
        n = llrint(dx);
        for (y = 0; y < height; y++) {
            const unsigned char *s_row = (const unsigned char *)src + (size_t)y * (size_t)stride;
            unsigned char *d_row = (unsigned char *)dest + (size_t)y * (size_t)stride;

            memset(row, 0, pixel * (size_t)width);
            if (n > -(long long)width && n < (long long)width) {
                long long from = n < 0 ? -n : 0;
                long long to = n < 0 ? 0 : n;
                long long count = width - (n < 0 ? -n : n);

                memcpy(row + (size_t)to * pixel, s_row + (size_t)from * pixel, (size_t)count * pixel);
            }
            memcpy(d_row, row, pixel * (size_t)width);
        }
        free(row);
        return 0;
    }

    /* every output sample x reads x - dx; its fractional place f is the same along the row, so are the six weights */
    s = -dx;
    n = (long long)floor(s);
    {
        double f = s - (double)n;

        for (k = 0; k < 2 * LANCZOS_A; k++) {
            w[k] = lanczos(f - (double)(k - (LANCZOS_A - 1)));
            sum += w[k];
        }
        for (k = 0; k < 2 * LANCZOS_A; k++)
            w[k] /= sum;
    }

    in = malloc(sizeof(float) * 4 * (size_t)width);
    out = malloc(sizeof(float) * 4 * (size_t)width);
    if (!in || !out) {
        free(in);
        free(out);
        return -1;
    }

    for (y = 0; y < height; y++) {
        const unsigned char *s_row = (const unsigned char *)src + (size_t)y * (size_t)stride;
        unsigned char *d_row = (unsigned char *)dest + (size_t)y * (size_t)stride;
        int x, c;

        for (x = 0; x < width; x++) {
            float a = sample_get(s_row, 4 * x + 3, bps);

            for (c = 0; c < 3; c++) {
                float v = sample_get(s_row, 4 * x + c, bps);

                in[4 * x + c] = premultiplied ? v : v * a;
            }
            in[4 * x + 3] = a;
        }
        for (x = 0; x < width; x++) {
            double acc[4] = { 0.0, 0.0, 0.0, 0.0 };
            float a;

            for (k = 0; k < 2 * LANCZOS_A; k++) {
                long long i = (long long)x + n + (k - (LANCZOS_A - 1));

                if (i < 0 || i >= width)
                    continue;
                for (c = 0; c < 4; c++)
                    acc[c] += w[k] * in[4 * i + c];
            }
            a = (float)acc[3];
            a = a < 0.0f ? 0.0f : a > 1.0f ? 1.0f : a;
            out[4 * x + 3] = a;
            for (c = 0; c < 3; c++) {
                float v = (float)acc[c];

                if (v < 0.0f)
                    v = 0.0f;
                if (bps != 4 && v > a)
                    v = a;
                if (!premultiplied)
                    v = a > 0.0f ? v / a : 0.0f;
                if (bps != 4 && v > 1.0f)
                    v = 1.0f;
                out[4 * x + c] = v;
            }
        }
        for (x = 0; x < 4 * width; x++)
            sample_put(d_row, x, bps, out[x]);
    }
    free(in);
    free(out);
    return 0;
}
