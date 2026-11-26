/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/*
 * Depth layers, the model every editor shares (GIMP in C, Krita through the Qt layer; Daniel's design of 2026-10-15): a
 * depth layer holds other layers at one depth the artist sets, in pixels of separation with three decimals. Positive is
 * behind the screen, negative in front of it, zero at the screen; the value is the artist's, never clamped. Each eye
 * sees the layer moved by half the value, the left eye to the left and the right eye to the right for a positive value
 * (the doubling rule: the same picture twice, one per eye; no cameras). A whole move copies the samples; a fractional
 * one resamples them with a Lanczos window of three lobes. Layers in no depth layer stay at the screen, and depth
 * layers inside each other are each at their own value, never the sum.
 */

#ifndef STEREO_DEPTH_H
#define STEREO_DEPTH_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef STEREO_POLICY_EXPORT
#define STEREO_POLICY_EXPORT __attribute__((visibility("default")))
#endif

enum stereo_depth_eye {
    STEREO_DEPTH_EYE_LEFT = 0,
    STEREO_DEPTH_EYE_RIGHT = 1,
};

/* Room for any value stereo_depth_format() writes, with its terminating zero. */
#define STEREO_DEPTH_TEXT_SIZE 32

/* Reads a value written in the C locale: an optional sign, digits, and an optional point with decimals, nothing else
 * (no spaces, no exponent, no comma). More than three decimals round to the nearest thousandth, half away from zero.
 * Returns 0 and the value, or -1 for anything else (and for a value beyond what three decimals in 63 bits hold). */
STEREO_POLICY_EXPORT int stereo_depth_parse(const char *text, double *separation);

/* Writes the value with exactly three decimals in the C locale, whatever the program's locale ("12.000", "-4.500",
 * "0.000"; never "-0.000"), rounded to the nearest thousandth, half away from zero. Returns the length written, or -1
 * for a value that is not a number or is infinite, or when size is too small. */
STEREO_POLICY_EXPORT int stereo_depth_format(double separation, char *out, size_t size);

/* The value rounded to the nearest thousandth, as parse and format read it. */
STEREO_POLICY_EXPORT double stereo_depth_round(double separation);

/* How far one eye's picture of the layer moves, in pixels, positive to the right: -separation/2 for the left eye,
 * +separation/2 for the right eye. */
STEREO_POLICY_EXPORT double stereo_depth_eye_shift(double separation, int eye);

/* Whether a move copies the samples (a whole number of pixels) or resamples them. */
STEREO_POLICY_EXPORT int stereo_depth_shift_is_whole(double dx);

/* Moves a picture of RGBA pixels dx pixels to the right (to the left when negative), within the same frame: what moves
 * out of it is lost and what is uncovered is transparent. Samples are 8 or 16-bit unsigned integers or 32-bit floats
 * (bytes_per_sample 1, 2 or 4), four per pixel, red, green, blue and alpha, each row stride bytes long. premultiplied
 * says whether the colour samples are already multiplied by alpha; a fractional move is computed on premultiplied
 * samples either way. Where the window rings, alpha is kept within [0, 1] and colour is never negative; integer colour
 * also stays within its range (at most alpha when premultiplied), while float colour may stay above 1 (deep and wide
 * pictures). Integer samples are rounded to the nearest value. dest may be src. Returns 0, or -1 for bad arguments
 * (a null picture, a width or height below 1, a stride too short, another sample size, dx not a finite number). */
STEREO_POLICY_EXPORT int stereo_depth_shift_rgba(const void *src, int width, int height, int stride, int bytes_per_sample,
                                                 int premultiplied, double dx, void *dest);

#ifdef __cplusplus
}
#endif

#endif
