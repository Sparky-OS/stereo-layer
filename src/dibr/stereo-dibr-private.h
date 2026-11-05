/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */
#ifndef STEREO_DIBR_PRIVATE_H
#define STEREO_DIBR_PRIVATE_H

#include "stereo-dibr.h"

/* A layered test picture (RGBA, 4 bytes a pixel) and its 8 bit depth map: shapes in front of a sloped background, textured, as
 * the picture of a video has depth edges to open holes behind. For timing. */
void stereo_dibr_synthetic(int width, int height, unsigned char *rgba, unsigned char *depth);
/* Milliseconds from a monotonic clock. */
double stereo_dibr_now_ms(void);
/* The 95th percentile (the slowest of twelve) of n times; sorts them. */
double stereo_dibr_p95(double *ms, int n);
/* How many frames a way is timed on, the warm-up frames before them, and the time over which a way is given up on (ms). */
#define STEREO_DIBR_TIMED_FRAMES 12
#define STEREO_DIBR_GIVE_UP_MS 250.0
/* The depth convention and the parallax table the timing draws with (the options' default, 8 bit nearness). */
void stereo_dibr_timing_table(int width, float *table);

#endif
