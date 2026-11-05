/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

#include "stereo-policy.h"

/* The depth of a display (stereo-policy.h): the angular budget of the reference monitor, scaled to the display. */
#include <math.h>

#define REFERENCE_WIDTH_MM 531
#define REFERENCE_HEIGHT_MM 299
#define REFERENCE_SUNK_PX 10.0
#define REFERENCE_POPPED_PX 4.0
#define HEADSET_WINDOW_DISTANCE_MM 700

static int viewing_distance_mm(double diagonal_mm)
{
    return (int)lround(600.0 + (diagonal_mm > 500.0 ? (diagonal_mm - 500.0) * 1.8 : 0.0));
}

int stereo_policy_display_depth(int kind, int width_mm, int height_mm, double strength_percent, double sunk_override_px,
    double popped_override_px, struct stereo_policy_depth *out)
{
    if (!out || kind < STEREO_POLICY_DISPLAY_FRAMED || kind > STEREO_POLICY_DISPLAY_HEADSET || !(strength_percent >= 0 && strength_percent <= 1000)
        || sunk_override_px < 0 || popped_override_px < 0)
        return -1;
    const int known = width_mm > 0 && height_mm > 0;
    const double w = known ? width_mm : REFERENCE_WIDTH_MM;
    const double h = known ? height_mm : REFERENCE_HEIGHT_MM;
    const double distance = kind == STEREO_POLICY_DISPLAY_HEADSET ? HEADSET_WINDOW_DISTANCE_MM : viewing_distance_mm(hypot(w, h));
    /* the angle a reference limit makes at the eyes, kept: pixels of 1920 at this width, seen from this distance */
    const double reference_distance = viewing_distance_mm(hypot(REFERENCE_WIDTH_MM, REFERENCE_HEIGHT_MM));
    const double scale = (distance / reference_distance) * (REFERENCE_WIDTH_MM / w);
    const double kinds[] = { 1.0, 0.7, 2.5 };
    const double factor = scale * kinds[kind] * strength_percent / 100.0;
    out->sunk_px = sunk_override_px > 0 ? sunk_override_px : REFERENCE_SUNK_PX * factor;
    out->popped_px = popped_override_px > 0 ? popped_override_px : REFERENCE_POPPED_PX * factor;
    out->viewing_distance_mm = (int)lround(distance);
    out->size_known = known;
    return 0;
}
