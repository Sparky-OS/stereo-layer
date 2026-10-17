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
 * layer_depth one of the layer's depth, of the same kind as depth. */
STEREO_DIBR_EXPORT int stereo_dibr_gl_render_layer(stereo_dibr_gl *gl, const struct stereo_dibr_params *p, const float *table, int table_size,
    unsigned picture, int width, int height, unsigned depth, int depth_width, int depth_height, int depth_integer, int role, unsigned layer,
    unsigned layer_depth, int layer_depth_width, int layer_depth_height, unsigned target, const float hole[4]);

#ifdef __cplusplus
}
#endif

#endif
