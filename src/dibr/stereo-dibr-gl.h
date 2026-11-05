/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/*
 * The DIBR core of stereo-dibr.h on the GPU: the same steps as stereo_dibr_render(), each a GLSL pass (the shaders
 * are installed in share/stereo-dibr/glsl and built into the library), with every option of struct
 * stereo_dibr_params. It runs in the caller's current context, OpenGL 3.3 core or OpenGL ES 3.0 with
 * EXT_color_buffer_float (ES 3.2 has it), and leaves that context's state as it found it. It needs no GL library of its
 * own: the caller hands it the context's way to find functions (eglGetProcAddress, glXGetProcAddress,
 * QOpenGLContext::getProcAddress).
 *
 * The passes give stereo_dibr_render()'s views, rounding aside, for a picture without subsampling (the GPU works on
 * RGBA), with one difference: Telea's fill marches in rings of distance, all pixels of a ring at once, where the C code
 * takes them one by one in the order of a heap.
 */

#ifndef STEREO_DIBR_GL_H
#define STEREO_DIBR_GL_H

#include "stereo-dibr.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*stereo_dibr_gl_function)(void);
typedef stereo_dibr_gl_function (*stereo_dibr_gl_resolver)(const char *name);

enum stereo_dibr_gl_api {
    STEREO_DIBR_GL_DESKTOP = 0, /* OpenGL 3.3 core */
    STEREO_DIBR_GL_ES = 1, /* OpenGL ES 3.0 with EXT_color_buffer_float */
};

typedef struct stereo_dibr_gl stereo_dibr_gl;

/* In the current context: compiles the passes. NULL when a function is missing or a pass does not compile
 * (stereo_dibr_gl_error() of a NULL context says nothing; the log goes to stderr). */
STEREO_DIBR_EXPORT stereo_dibr_gl *stereo_dibr_gl_create(stereo_dibr_gl_resolver resolve, int api);
/* In the same context, current. */
STEREO_DIBR_EXPORT void stereo_dibr_gl_free(stereo_dibr_gl *gl);
/* The last error, or "". */
STEREO_DIBR_EXPORT const char *stereo_dibr_gl_error(const stereo_dibr_gl *gl);

/*
 * Draws both views of picture, a texture width x height read as RGBA, into target, a texture 2 width x height that can
 * be drawn into: full side by side, the left eye first. depth is a one-channel texture depth_width x depth_height of
 * the table's samples: unsigned integer (GL_R8UI, GL_R16UI) when depth_integer is set, else normalized (GL_R8, GL_R16),
 * the sample being round(value x (table_size - 1)). table and p are as for stereo_dibr_render(); hole is the RGBA a hole
 * gets with STEREO_DIBR_FILL_NONE. Returns 0, -1 for a bad argument, -3 for a GL error (stereo_dibr_gl_error()).
 */
STEREO_DIBR_EXPORT int stereo_dibr_gl_render(stereo_dibr_gl *gl, const struct stereo_dibr_params *p, const float *table, int table_size,
    unsigned picture, int width, int height, unsigned depth, int depth_width, int depth_height, int depth_integer, unsigned target,
    const float hole[4]);

/* stereo_dibr_gl_render() with a second layer, as stereo_dibr_render_layer(): layer is a texture of the picture's size,
 * layer_depth one of the layer's depth, of the same kind as depth, read through layer_table. */
STEREO_DIBR_EXPORT int stereo_dibr_gl_render_layer(stereo_dibr_gl *gl, const struct stereo_dibr_params *p, const float *table, int table_size,
    unsigned picture, int width, int height, unsigned depth, int depth_width, int depth_height, int depth_integer, int role, unsigned layer,
    const float *layer_table, int layer_table_size, unsigned layer_depth, int layer_depth_width, int layer_depth_height, unsigned target,
    const float hole[4]);

/* The GPU's name, as the cache of timings keys it (the context's vendor, renderer and version strings). */
STEREO_DIBR_EXPORT const char *stereo_dibr_gl_gpu(stereo_dibr_gl *gl);

/*
 * The times of the ways of stereo_dibr_ways() (n of them) on this GPU at width x height, in the current context, for a video of
 * fps frames a second: the ladder is timed from the cheapest way up (stereo_dibr_time_ladder()) and stops at the first way that
 * misses the frame's budget, so the ways that cost seconds never run; ways not timed are -1 in p95_ms (they do not keep up). The
 * 95th percentile of twelve frames each (a synthetic picture and its depth map), every pass in bands of rows and finished before the
 * next. With with_transfer the frame's upload and the views' read back are in every frame's time. The person's cache is read first
 * and added to; *measured is 1 when this call timed a way. A context that comes back reset (robustness) ends the timing with -1.
 * Returns 0, or -1 (stereo_dibr_gl_error()).
 */
STEREO_DIBR_EXPORT int stereo_dibr_gl_measure(
    stereo_dibr_gl *gl, int width, int height, int with_transfer, double fps, double *p95_ms, int n, int *measured);

/*
 * The context's reset status (glGetGraphicsResetStatus of KHR_robustness, when the context has it): 0 for none or unknown, else the
 * status (GL_GUILTY_CONTEXT_RESET 0x8253, INNOCENT 0x8254, UNKNOWN 0x8255). Once a reset is seen the GL path refuses to draw
 * (stereo_dibr_gl_render() returns -4) and the program says so: the caller draws elsewhere, never silently.
 */
STEREO_DIBR_EXPORT int stereo_dibr_gl_reset_status(stereo_dibr_gl *gl);

/* The bit depth of the content the next drawings handle (8 to 16): the colour textures of the passes are half float up to 10 bits and float above, so
 * that they never hold less than the source. Default 8. */
STEREO_DIBR_EXPORT void stereo_dibr_gl_set_precision(stereo_dibr_gl *gl, int bits);
/* The bytes of the working textures now (what the passes hold between drawings). */
STEREO_DIBR_EXPORT double stereo_dibr_gl_memory_used(stereo_dibr_gl *gl);

/* The GPU memory the driver says is free, bytes (GL_ATI_meminfo or GL_NVX_gpu_memory_info); -1 when it does not say. */
STEREO_DIBR_EXPORT double stereo_dibr_gl_free_memory(stereo_dibr_gl *gl);

/* How the passes are paced: bands of band_rows rows (0: whole), each flushed, or finished with sync_passes. Timing sets its own. */
STEREO_DIBR_EXPORT void stereo_dibr_gl_set_limits(stereo_dibr_gl *gl, int band_rows, int sync_passes);

#ifdef __cplusplus
}
#endif

#endif
