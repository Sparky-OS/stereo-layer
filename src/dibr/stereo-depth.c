/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/* What a depth sample means: the parallax of each sample value (stereo-dibr.h). */

#include "stereo-dibr.h"

#include <math.h>

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
