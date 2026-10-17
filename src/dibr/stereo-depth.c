/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/* What a depth sample means: the parallax of each sample value (stereo-dibr.h). */

#include "stereo-dibr.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* The parallax of a nearness (0 farthest, 1 nearest) when the screen is at zero: range pixels behind the screen at
 * the far end of the longer side, in front at the near end, uniform between. */
static double relative(double nearness, double zero, double range)
{
    double half = fmax(zero, 1.0 - zero);

    return half > 0.0 ? range * (zero - nearness) / half : 0.0;
}

int stereo_depth_parallax_table(const struct stereo_depth_convention *c, int width, float *table)
{
    if (!c || !table || width <= 0 || c->bits < 1 || c->bits > 16)
        return -1;

    const int max_val = (1 << c->bits) - 1;
    const double per_1920 = width / 1920.0;
    const double range = c->range * per_1920;
    const double shift = c->shift * per_1920;
    const double zero = fmin(fmax(c->zero, 0.0), 1.0);
    const int z_known = c->z_present && c->z_near > 0.0 && c->z_far > 0.0 && c->z_near != c->z_far;
    const int d_known = c->d_present && c->d_width > 0;
    const int p_known = c->parallax_scale != 0.0 && c->wref > 0.0;

    for (int i = 0; i <= max_val; i++) {
        const double v = (double)i / max_val;
        double p;

        switch (c->kind) {
        case STEREO_DEPTH_NEARNESS:
        case STEREO_DEPTH_INVERSE_Z:
            p = relative(v, zero, range);
            break;
        case STEREO_DEPTH_FARNESS:
            p = relative(1.0 - v, zero, range);
            break;
        case STEREO_DEPTH_Z:
            if (z_known) {
                /* H.265 Table G.3 type 2: 0 is ZFar, maxVal ZNear; parallax follows 1/Z */
                const double z = c->z_far + v * (c->z_near - c->z_far);
                const double near = (1.0 / z - 1.0 / c->z_far) / (1.0 / c->z_near - 1.0 / c->z_far);
                p = relative(near, zero, range);
            } else {
                p = relative(v, zero, range);
            }
            break;
        case STEREO_DEPTH_DISPARITY:
        case STEREO_DEPTH_NONLINEAR_DISPARITY: {
            /* types 1 and 3: DepthLUT makes a type 3 sample uniform in disparity (equation G-10) */
            double u = v;
            if (c->kind == STEREO_DEPTH_NONLINEAR_DISPARITY && c->lut)
                u = (double)c->lut[i] / max_val;
            if (d_known)
                p = -(c->d_min + u * (c->d_max - c->d_min)) * width / c->d_width;
            else
                p = relative(u, zero, range);
            break;
        }
        case STEREO_DEPTH_RANGE_LINEAR:
            if (z_known) {
                /* Dynamic Depth 1.0, RangeLinear: d = v (far - near) + near */
                const double d = c->z_near + v * (c->z_far - c->z_near);
                p = relative((1.0 / d - 1.0 / c->z_far) / (1.0 / c->z_near - 1.0 / c->z_far), zero, range);
            } else {
                p = relative(1.0 - v, zero, range);
            }
            break;
        case STEREO_DEPTH_RANGE_INVERSE:
            /* RangeInverse: v = far (d - near) / (d (far - near)), uniform in 1/d, 0 at near */
            p = relative(1.0 - v, zero, range);
            break;
        case STEREO_DEPTH_METRIC:
            if (i == 0) {
                table[i] = NAN; /* no measurement */
                continue;
            }
            if (z_known)
                p = relative((1.0 / i - 1.0 / c->z_far) / (1.0 / c->z_near - 1.0 / c->z_far), zero, range);
            else
                p = relative(1.0 - v, zero, range);
            break;
        case STEREO_DEPTH_PARALLAX:
            /* ISO/IEC 23002-3 parallax map, our unverified reading: centimetres on a screen wref wide */
            if (p_known)
                p = (i - c->parallax_zero) / c->parallax_scale / c->wref * width;
            else
                p = relative(1.0 - v, zero, range);
            break;
        default:
            return -1;
        }

        table[i] = (float)(p + shift);
    }

    return 0;
}

int stereo_depth_nonlinear_lut(const uint16_t *model, int nb_model, int bits, uint16_t *lut)
{
    if (!model || !lut || nb_model < 3 || nb_model > 65 || bits < 1 || bits > 16)
        return -1;

    /* H.265 equation G-10: the deviations model[k] from the straight line are spread evenly over 0 to maxVal, the
     * first and the last 0; between each two nodes the curve is a line segment */
    const int max_val = (1 << bits) - 1, segments = nb_model - 1;
    for (int x = 0; x <= max_val; x++)
        lut[x] = (uint16_t)x;
    for (int k = 0; k < segments; k++) {
        const long pos1 = (long)max_val * k / segments, pos2 = (long)max_val * (k + 1) / segments;
        const long dev1 = k ? model[k] : 0, dev2 = k + 1 < segments ? model[k + 1] : 0;
        const long x1 = pos1 - dev1, y1 = pos1 + dev1, x2 = pos2 - dev2, y2 = pos2 + dev2;
        if (x2 == x1)
            continue;
        for (long x = x1 > 0 ? x1 : 0; x <= (x2 < max_val ? x2 : max_val); x++) {
            const double y = (double)(x - x1) * (y2 - y1) / (x2 - x1) + y1;
            const long r = (long)floor(y + 0.5);
            lut[x] = (uint16_t)(r < 0 ? 0 : r > max_val ? max_val : r);
        }
    }
    return 0;
}

/* The words of the mark, in the order of enum stereo_depth_kind. */
static const char *const kind_words[] = {
    "nearness", "farness", "inverseZ", "disparity", "z", "nonlinearDisparity", "parallax", "rangeLinear", "rangeInverse", "metric",
};

/* A number in the C notation, whatever the locale: [+-]digits[.digits][(e|E)[+-]digits]. */
static int read_number(const char *s, const char *end, double *out)
{
    double mantissa = 0.0;
    int sign = 1, digits = 0, fraction = 0, exp_sign = 1, exp = 0;

    if (s < end && (*s == '+' || *s == '-'))
        sign = *s++ == '-' ? -1 : 1;
    for (; s < end && *s >= '0' && *s <= '9'; s++, digits++)
        mantissa = mantissa * 10.0 + (*s - '0');
    if (s < end && *s == '.')
        for (s++; s < end && *s >= '0' && *s <= '9'; s++, digits++, fraction++)
            mantissa = mantissa * 10.0 + (*s - '0');
    if (!digits)
        return -1;
    if (s < end && (*s == 'e' || *s == 'E')) {
        s++;
        if (s < end && (*s == '+' || *s == '-'))
            exp_sign = *s++ == '-' ? -1 : 1;
        if (s == end || *s < '0' || *s > '9')
            return -1;
        for (; s < end && *s >= '0' && *s <= '9'; s++)
            exp = exp * 10 + (*s - '0') > 400 ? 400 : exp * 10 + (*s - '0');
    }
    if (s != end)
        return -1;
    /* one rounding: the digits as an integer, scaled once by a power of ten */
    const int scale = exp_sign * exp - fraction;
    *out = sign * (scale < 0 ? mantissa / pow(10.0, -scale) : mantissa * pow(10.0, scale));
    return 0;
}

/* A number in the C notation, whatever the locale: the fewest decimals that read back the same, plain between 1e-4 and
 * 1e15, with an exponent otherwise. */
static int write_number(char *buf, size_t size, double v)
{
    char tmp[64];
    const int plain = v == 0.0 || (fabs(v) >= 1e-4 && fabs(v) < 1e15);

    for (int precision = plain ? 0 : 1; precision <= 17; precision++) {
        double back = 0.0;
        int n = plain ? snprintf(tmp, sizeof(tmp), "%.*f", precision, v) : snprintf(tmp, sizeof(tmp), "%.*e", precision, v);
        /* a locale may write a comma for the point */
        for (int i = 0; i < n; i++)
            if (tmp[i] == ',')
                tmp[i] = '.';
        if (read_number(tmp, tmp + n, &back) == 0 && back == v)
            break;
    }
    return snprintf(buf, size, "%s", tmp);
}

int stereo_depth_read_mark(const char *mark, struct stereo_depth_convention *c, uint16_t *model, int *nb_model)
{
    if (!mark || !c)
        return -1;

    const char *s = mark;
    while (*s == ' ')
        s++;
    const char *word_end = s;
    while (*word_end && *word_end != ' ')
        word_end++;
    int kind = -1;
    for (int k = 0; k < (int)(sizeof(kind_words) / sizeof(kind_words[0])); k++)
        if ((size_t)(word_end - s) == strlen(kind_words[k]) && !strncmp(s, kind_words[k], word_end - s))
            kind = k;
    if (kind < 0)
        return 0;

    struct stereo_depth_convention r = *c;
    int n_model = 0;
    r.kind = kind;
    r.z_present = r.d_present = 0;
    for (s = word_end; *s;) {
        while (*s == ' ')
            s++;
        if (!*s)
            break;
        const char *end = s;
        while (*end && *end != ' ')
            end++;
        const char *eq = memchr(s, '=', end - s);
        if (!eq)
            return -1;
        const size_t key = eq - s;
        const char *value = eq + 1;
        double v = 0.0;
#define KEY(name) (key == strlen(name) && !strncmp(s, name, key))
        if (KEY("model")) {
            for (const char *t = value; t < end;) {
                const char *comma = memchr(t, ',', end - t);
                const char *item_end = comma ? comma : end;
                if (read_number(t, item_end, &v) || v < 0 || v > 65535 || n_model >= 65)
                    return -1;
                if (model)
                    model[n_model] = (uint16_t)v;
                n_model++;
                t = comma ? comma + 1 : end;
            }
        } else {
            if (read_number(value, end, &v))
                return -1;
            if (KEY("zero"))
                r.zero = v;
            else if (KEY("range"))
                r.range = v;
            else if (KEY("shift"))
                r.shift = v;
            else if (KEY("near"))
                r.z_near = v, r.z_present |= 1;
            else if (KEY("far"))
                r.z_far = v, r.z_present |= 2;
            else if (KEY("min"))
                r.d_min = v, r.d_present |= 1;
            else if (KEY("max"))
                r.d_max = v, r.d_present |= 2;
            else if (KEY("width"))
                r.d_width = (int)v;
            else if (KEY("parallaxZero"))
                r.parallax_zero = v;
            else if (KEY("parallaxScale"))
                r.parallax_scale = v;
            else if (KEY("wref"))
                r.wref = v;
            /* other keys are for later versions of the mark: skipped */
        }
#undef KEY
        s = end;
    }
    /* both ends or neither */
    r.z_present = r.z_present == 3;
    r.d_present = r.d_present == 3;
    *c = r;
    if (nb_model)
        *nb_model = n_model;
    return 1;
}

int stereo_depth_write_mark(const struct stereo_depth_convention *c, const uint16_t *model, int nb_model, char *buf, size_t size)
{
    if (!c || !buf || c->kind < 0 || c->kind >= (int)(sizeof(kind_words) / sizeof(kind_words[0])))
        return -1;

    char out[2048], number[64];
    size_t n = 0;
#define PUT(...) (n += (size_t)snprintf(out + n, n < sizeof(out) ? sizeof(out) - n : 0, __VA_ARGS__))
#define PUT_NUMBER(key, value) (write_number(number, sizeof(number), value), PUT(" %s=%s", key, number))
    PUT("%s", kind_words[c->kind]);
    switch (c->kind) {
    case STEREO_DEPTH_INVERSE_Z:
    case STEREO_DEPTH_Z:
    case STEREO_DEPTH_RANGE_LINEAR:
    case STEREO_DEPTH_RANGE_INVERSE:
    case STEREO_DEPTH_METRIC:
        if (c->z_present) {
            PUT_NUMBER("near", c->z_near);
            PUT_NUMBER("far", c->z_far);
        }
        break;
    case STEREO_DEPTH_DISPARITY:
    case STEREO_DEPTH_NONLINEAR_DISPARITY:
        if (c->d_present) {
            PUT_NUMBER("min", c->d_min);
            PUT_NUMBER("max", c->d_max);
            PUT(" width=%d", c->d_width);
        }
        if (c->kind == STEREO_DEPTH_NONLINEAR_DISPARITY && model && nb_model > 0) {
            PUT(" model=");
            for (int i = 0; i < nb_model; i++)
                PUT("%s%u", i ? "," : "", (unsigned)model[i]);
        }
        break;
    case STEREO_DEPTH_PARALLAX:
        PUT_NUMBER("parallaxZero", c->parallax_zero);
        PUT_NUMBER("parallaxScale", c->parallax_scale);
        PUT_NUMBER("wref", c->wref);
        break;
    default:
        break;
    }
    if (c->kind != STEREO_DEPTH_PARALLAX) {
        PUT_NUMBER("zero", c->zero);
        PUT_NUMBER("range", c->range);
    }
    if (c->shift != 0.0)
        PUT_NUMBER("shift", c->shift);
#undef PUT_NUMBER
#undef PUT
    if (n >= sizeof(out) || n + 1 > size)
        return -1;
    memcpy(buf, out, n + 1);
    return (int)n;
}
