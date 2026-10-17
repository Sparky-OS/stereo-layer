/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/*
 * The DIBR core on the GPU (stereo-dibr-gl.h): the passes of glsl/, run in the order of stereo-dibr.c render(). The GL
 * functions come from the caller's resolver, and the few GL types and values used are written here, so the library
 * needs no GL headers or library.
 */

#include "stereo-dibr-gl.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef float GLfloat;
typedef unsigned char GLboolean;
typedef char GLchar;
typedef unsigned int GLbitfield;
typedef long GLintptr; /* unused, for completeness of the signatures below */

#define GL_NO_ERROR 0
#define GL_TRIANGLES 0x0004
#define GL_CULL_FACE 0x0B44
#define GL_DEPTH_TEST 0x0B71
#define GL_STENCIL_TEST 0x0B90
#define GL_VIEWPORT 0x0BA2
#define GL_DITHER 0x0BD0
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_UNPACK_ROW_LENGTH 0x0CF2
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_TEXTURE_2D 0x0DE1
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_NEAREST 0x2600
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_WRAP_S 0x2802
#define GL_TEXTURE_WRAP_T 0x2803
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_CLAMP_TO_EDGE 0x812F
#define GL_RG 0x8227
#define GL_RG32F 0x8230
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_TEXTURE0 0x84C0
#define GL_ACTIVE_TEXTURE 0x84E0
#define GL_VERTEX_ARRAY_BINDING 0x85B5
#define GL_RGBA32F 0x8814
#define GL_QUERY_RESULT 0x8866
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INFO_LOG_LENGTH 0x8B84
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_RASTERIZER_DISCARD 0x8C89
#define GL_ANY_SAMPLES_PASSED 0x8C2F
#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_COLOR_ATTACHMENT1 0x8CE1
#define GL_FRAMEBUFFER 0x8D40

#define GLFUNCTIONS(X)                                                                                                                               \
    X(void, ActiveTexture, (GLenum))                                                                                                                 \
    X(void, AttachShader, (GLuint, GLuint))                                                                                                          \
    X(void, BeginQuery, (GLenum, GLuint))                                                                                                            \
    X(void, BindFramebuffer, (GLenum, GLuint))                                                                                                       \
    X(void, BindTexture, (GLenum, GLuint))                                                                                                           \
    X(void, BindVertexArray, (GLuint))                                                                                                               \
    X(GLenum, CheckFramebufferStatus, (GLenum))                                                                                                      \
    X(void, CompileShader, (GLuint))                                                                                                                 \
    X(GLuint, CreateProgram, (void))                                                                                                                 \
    X(GLuint, CreateShader, (GLenum))                                                                                                                \
    X(void, DeleteFramebuffers, (GLsizei, const GLuint *))                                                                                           \
    X(void, DeleteProgram, (GLuint))                                                                                                                 \
    X(void, DeleteQueries, (GLsizei, const GLuint *))                                                                                                \
    X(void, DeleteShader, (GLuint))                                                                                                                  \
    X(void, DeleteTextures, (GLsizei, const GLuint *))                                                                                               \
    X(void, DeleteVertexArrays, (GLsizei, const GLuint *))                                                                                           \
    X(void, Disable, (GLenum))                                                                                                                       \
    X(void, DrawArrays, (GLenum, GLint, GLsizei))                                                                                                    \
    X(void, DrawBuffers, (GLsizei, const GLenum *))                                                                                                  \
    X(void, Enable, (GLenum))                                                                                                                        \
    X(void, EndQuery, (GLenum))                                                                                                                      \
    X(void, FramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint))                                                                           \
    X(void, GenFramebuffers, (GLsizei, GLuint *))                                                                                                    \
    X(void, GenQueries, (GLsizei, GLuint *))                                                                                                         \
    X(void, GenTextures, (GLsizei, GLuint *))                                                                                                        \
    X(void, GenVertexArrays, (GLsizei, GLuint *))                                                                                                    \
    X(GLenum, GetError, (void))                                                                                                                      \
    X(void, GetIntegerv, (GLenum, GLint *))                                                                                                          \
    X(void, GetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                                                                               \
    X(void, GetProgramiv, (GLuint, GLenum, GLint *))                                                                                                 \
    X(void, GetQueryObjectuiv, (GLuint, GLenum, GLuint *))                                                                                           \
    X(void, GetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))                                                                                \
    X(void, GetShaderiv, (GLuint, GLenum, GLint *))                                                                                                  \
    X(GLint, GetUniformLocation, (GLuint, const GLchar *))                                                                                           \
    X(GLboolean, IsEnabled, (GLenum))                                                                                                                \
    X(void, LinkProgram, (GLuint))                                                                                                                   \
    X(void, PixelStorei, (GLenum, GLint))                                                                                                            \
    X(void, ShaderSource, (GLuint, GLsizei, const GLchar *const *, const GLint *))                                                                   \
    X(void, TexImage2D, (GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *))                                               \
    X(void, TexParameteri, (GLenum, GLenum, GLint))                                                                                                  \
    X(void, Uniform1f, (GLint, GLfloat))                                                                                                             \
    X(void, Uniform1i, (GLint, GLint))                                                                                                               \
    X(void, Uniform2i, (GLint, GLint, GLint))                                                                                                        \
    X(void, Uniform4f, (GLint, GLfloat, GLfloat, GLfloat, GLfloat))                                                                                  \
    X(void, UseProgram, (GLuint))                                                                                                                    \
    X(void, Viewport, (GLint, GLint, GLsizei, GLsizei))

/* the shaders, from glsl/ (stereo-dibr-glsl.c, made by cmake/EmbedGlsl.cmake) */
#define SHADERS(X)                                                                                                                                   \
    X(depth_frag)                                                                                                                                    \
    X(fill_row_frag)                                                                                                                                 \
    X(fill_column_frag)                                                                                                                              \
    X(resample_frag)                                                                                                                                 \
    X(blur_frag)                                                                                                                                     \
    X(band_frag)                                                                                                                                     \
    X(splat_frag)                                                                                                                                    \
    X(holez_frag)                                                                                                                                    \
    X(sample_frag)                                                                                                                                   \
    X(fill_background_frag)                                                                                                                          \
    X(fill_rest_frag)                                                                                                                                \
    X(hole_background_frag)                                                                                                                          \
    X(fill_depth_aided_frag)                                                                                                                         \
    X(telea_init_frag)                                                                                                                               \
    X(telea_distance_frag)                                                                                                                           \
    X(telea_fill_frag)                                                                                                                               \
    X(compose_frag)                                                                                                                                  \
    X(copy_frag)                                                                                                                                     \
    X(copy_state_frag)                                                                                                                               \
    X(count_frag)

#define DECLARE_SOURCE(name) extern const char stereo_dibr_glsl_##name[];
SHADERS(DECLARE_SOURCE)
extern const char stereo_dibr_glsl_fullscreen_vert[];

enum {
#define PROGRAM_INDEX(name) P_##name,
    SHADERS(PROGRAM_INDEX) P_depth_integer,
    NB_PROGRAMS
};

/* the working textures */
enum {
    T_TABLE, /* the parallax table, 256 wide */
    T_DPAR0, /* at the depth map's size: parallax, valid */
    T_DPAR1,
    T_PAR, /* at the picture's size */
    T_PAR2,
    T_LPAR, /* the second layer's */
    T_BAND, /* the layered warp's band */
    T_SPLAT, /* a view's splat */
    T_MAP, /* a view: picture position, parallax */
    T_BOUNDARY, /* the layered warp's boundary layer */
    T_LSPLAT, /* the second layer's view */
    T_LMAP,
    T_COLOR0, /* colour and state, twice, ping-pong */
    T_STATE0,
    T_COLOR1,
    T_STATE1,
    NB_TEXTURES
};

struct stereo_dibr_gl {
#define DECLARE_FUNCTION(ret, name, args) ret(*name) args;
    GLFUNCTIONS(DECLARE_FUNCTION)
    int api;
    GLuint programs[NB_PROGRAMS];
    GLuint textures[NB_TEXTURES];
    int tex_w[NB_TEXTURES], tex_h[NB_TEXTURES];
    GLuint fbo, vao, query;
    char error[512];
};

static void set_error(stereo_dibr_gl *gl, const char *what, const char *log)
{
    snprintf(gl->error, sizeof(gl->error), "%s%s%s", what, log ? ": " : "", log ? log : "");
}

static GLuint compile(stereo_dibr_gl *gl, GLenum type, const char *source, const char *defines)
{
    static const char desktop[] = "#version 330 core\n";
    static const char es[] = "#version 300 es\nprecision highp float;\nprecision highp int;\nprecision highp sampler2D;\n"
                             "precision highp usampler2D;\n";
    const GLchar *parts[3] = { gl->api == STEREO_DIBR_GL_ES ? es : desktop, defines ? defines : "", source };
    GLuint shader = gl->CreateShader(type);
    GLint ok = 0;

    gl->ShaderSource(shader, 3, parts, NULL);
    gl->CompileShader(shader);
    gl->GetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[400] = "";
        gl->GetShaderInfoLog(shader, sizeof(log), NULL, log);
        set_error(gl, "a pass does not compile", log);
        fprintf(stderr, "stereo-dibr-gl: %s\n%.200s\n", log, source);
        gl->DeleteShader(shader);
        return 0;
    }
    return shader;
}

static GLuint program(stereo_dibr_gl *gl, const char *fragment, const char *defines)
{
    GLuint vs = compile(gl, GL_VERTEX_SHADER, stereo_dibr_glsl_fullscreen_vert, NULL);
    GLuint fs = vs ? compile(gl, GL_FRAGMENT_SHADER, fragment, defines) : 0;
    GLint ok = 0;

    if (!vs || !fs) {
        if (vs)
            gl->DeleteShader(vs);
        return 0;
    }
    GLuint prog = gl->CreateProgram();
    gl->AttachShader(prog, vs);
    gl->AttachShader(prog, fs);
    gl->LinkProgram(prog);
    gl->DeleteShader(vs);
    gl->DeleteShader(fs);
    gl->GetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[400] = "";
        gl->GetProgramInfoLog(prog, sizeof(log), NULL, log);
        set_error(gl, "a pass does not link", log);
        fprintf(stderr, "stereo-dibr-gl: %s\n", log);
        gl->DeleteProgram(prog);
        return 0;
    }
    return prog;
}

stereo_dibr_gl *stereo_dibr_gl_create(stereo_dibr_gl_resolver resolve, int api)
{
    if (!resolve || (api != STEREO_DIBR_GL_DESKTOP && api != STEREO_DIBR_GL_ES))
        return NULL;
    stereo_dibr_gl *gl = calloc(1, sizeof(*gl));
    if (!gl)
        return NULL;
    gl->api = api;
#define RESOLVE(ret, name, args)                                                                                                                     \
    if (!(gl->name = (ret(*) args)resolve("gl" #name))) {                                                                                            \
        fprintf(stderr, "stereo-dibr-gl: gl" #name " is missing\n");                                                                                 \
        free(gl);                                                                                                                                    \
        return NULL;                                                                                                                                 \
    }
    GLFUNCTIONS(RESOLVE)

#define BUILD(name)                                                                                                                                  \
    if (!(gl->programs[P_##name] = program(gl, stereo_dibr_glsl_##name, NULL))) {                                                                    \
        stereo_dibr_gl_free(gl);                                                                                                                     \
        return NULL;                                                                                                                                 \
    }
    SHADERS(BUILD)
    if (!(gl->programs[P_depth_integer] = program(gl, stereo_dibr_glsl_depth_frag, "#define DEPTH_INTEGER 1\n"))) {
        stereo_dibr_gl_free(gl);
        return NULL;
    }
    gl->GenFramebuffers(1, &gl->fbo);
    gl->GenVertexArrays(1, &gl->vao);
    gl->GenQueries(1, &gl->query);
    gl->GenTextures(NB_TEXTURES, gl->textures);
    if (gl->GetError() != GL_NO_ERROR) {
        stereo_dibr_gl_free(gl);
        return NULL;
    }
    return gl;
}

void stereo_dibr_gl_free(stereo_dibr_gl *gl)
{
    if (!gl)
        return;
    for (int i = 0; i < NB_PROGRAMS; i++)
        if (gl->programs[i])
            gl->DeleteProgram(gl->programs[i]);
    if (gl->textures[0])
        gl->DeleteTextures(NB_TEXTURES, gl->textures);
    if (gl->fbo)
        gl->DeleteFramebuffers(1, &gl->fbo);
    if (gl->vao)
        gl->DeleteVertexArrays(1, &gl->vao);
    if (gl->query)
        gl->DeleteQueries(1, &gl->query);
    free(gl);
}

const char *stereo_dibr_gl_error(const stereo_dibr_gl *gl) { return gl ? gl->error : ""; }

/* A working texture of a float format and size (made again when the size changes). */
static void texture(stereo_dibr_gl *gl, int t, GLint format, int w, int h, const void *data)
{
    gl->BindTexture(GL_TEXTURE_2D, gl->textures[t]);
    if (gl->tex_w[t] != w || gl->tex_h[t] != h || data) {
        gl->TexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, format == GL_RG32F ? GL_RG : GL_RGBA, GL_FLOAT, data);
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->TexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        gl->tex_w[t] = w;
        gl->tex_h[t] = h;
    }
}

/* Draws a pass of program prog into the textures out (one or two, -1 for none; a raw GL name when raw is set) with a
 * viewport of w x h at x offset; with query set, returns whether any pixel was drawn. */
struct pass {
    stereo_dibr_gl *gl;
    GLuint prog;
    int unit;
};

static struct pass use(stereo_dibr_gl *gl, int index)
{
    struct pass p = { gl, gl->programs[index], 0 };
    gl->UseProgram(p.prog);
    return p;
}

static void bind(struct pass *p, const char *name, GLuint texture_name)
{
    p->gl->ActiveTexture(GL_TEXTURE0 + p->unit);
    p->gl->BindTexture(GL_TEXTURE_2D, texture_name);
    p->gl->Uniform1i(p->gl->GetUniformLocation(p->prog, name), p->unit);
    p->unit++;
}

static void bind_t(struct pass *p, const char *name, int t) { bind(p, name, p->gl->textures[t]); }
static void set_i(struct pass *p, const char *name, int v) { p->gl->Uniform1i(p->gl->GetUniformLocation(p->prog, name), v); }
static void set_f(struct pass *p, const char *name, float v) { p->gl->Uniform1f(p->gl->GetUniformLocation(p->prog, name), v); }
static void set_2i(struct pass *p, const char *name, int a, int b) { p->gl->Uniform2i(p->gl->GetUniformLocation(p->prog, name), a, b); }

static int draw(struct pass *p, GLuint out0, GLuint out1, int x, int w, int h, int query)
{
    stereo_dibr_gl *gl = p->gl;
    static const GLenum buffers[2] = { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
    GLuint any = 1;

    gl->BindFramebuffer(GL_FRAMEBUFFER, gl->fbo);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, out0, 0);
    gl->FramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, out1, 0);
    gl->DrawBuffers(out1 ? 2 : 1, buffers);
    if (gl->CheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        set_error(gl, "a pass cannot draw into its texture", NULL);
        return -1;
    }
    gl->Viewport(x, 0, w, h);
    gl->BindVertexArray(gl->vao);
    if (query)
        gl->BeginQuery(GL_ANY_SAMPLES_PASSED, gl->query);
    gl->DrawArrays(GL_TRIANGLES, 0, 3);
    if (query) {
        gl->EndQuery(GL_ANY_SAMPLES_PASSED);
        gl->GetQueryObjectuiv(gl->query, GL_QUERY_RESULT, &any);
    }
    return any ? 1 : 0;
}

static int draw_t(struct pass *p, int t0, int t1, int w, int h, int query)
{
    return draw(p, p->gl->textures[t0], t1 >= 0 ? p->gl->textures[t1] : 0, 0, w, h, query);
}

/* the context's state this code changes, kept and given back */
struct saved {
    GLint fbo, viewport[4], program, active, vao, textures[8], unpack_alignment, unpack_row_length;
    GLboolean caps[7];
};

static const GLenum caps[7] = { GL_BLEND, GL_DEPTH_TEST, GL_SCISSOR_TEST, GL_STENCIL_TEST, GL_CULL_FACE, GL_DITHER, GL_RASTERIZER_DISCARD };

static void save(stereo_dibr_gl *gl, struct saved *s)
{
    gl->GetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &s->fbo);
    gl->GetIntegerv(GL_VIEWPORT, s->viewport);
    gl->GetIntegerv(GL_CURRENT_PROGRAM, &s->program);
    gl->GetIntegerv(GL_ACTIVE_TEXTURE, &s->active);
    gl->GetIntegerv(GL_VERTEX_ARRAY_BINDING, &s->vao);
    gl->GetIntegerv(GL_UNPACK_ALIGNMENT, &s->unpack_alignment);
    gl->GetIntegerv(GL_UNPACK_ROW_LENGTH, &s->unpack_row_length);
    for (int i = 0; i < 8; i++) {
        gl->ActiveTexture(GL_TEXTURE0 + i);
        gl->GetIntegerv(GL_TEXTURE_BINDING_2D, &s->textures[i]);
    }
    for (int i = 0; i < 7; i++) {
        s->caps[i] = gl->IsEnabled(caps[i]);
        gl->Disable(caps[i]);
    }
    gl->PixelStorei(GL_UNPACK_ALIGNMENT, 4);
    gl->PixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

static void restore(stereo_dibr_gl *gl, const struct saved *s)
{
    for (int i = 0; i < 7; i++)
        if (s->caps[i])
            gl->Enable(caps[i]);
    for (int i = 0; i < 8; i++) {
        gl->ActiveTexture(GL_TEXTURE0 + i);
        gl->BindTexture(GL_TEXTURE_2D, (GLuint)s->textures[i]);
    }
    gl->ActiveTexture((GLenum)s->active);
    gl->BindVertexArray((GLuint)s->vao);
    gl->UseProgram((GLuint)s->program);
    gl->BindFramebuffer(GL_FRAMEBUFFER, (GLuint)s->fbo);
    gl->Viewport(s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3]);
    gl->PixelStorei(GL_UNPACK_ALIGNMENT, s->unpack_alignment);
    gl->PixelStorei(GL_UNPACK_ROW_LENGTH, s->unpack_row_length);
}

/* depth to parallax at the picture's size, into T_PAR (fill set: samples with no depth filled) or T_LPAR */
static int parallax(stereo_dibr_gl *gl, int table_size, GLuint depth, int dw, int dh, int integer, int w, int h, int fill, int out)
{
    struct pass p = use(gl, integer ? P_depth_integer : P_depth_frag);
    texture(gl, T_DPAR0, GL_RG32F, dw, dh, NULL);
    texture(gl, T_DPAR1, GL_RG32F, dw, dh, NULL);
    bind(&p, "u_depth", depth);
    bind_t(&p, "u_table", T_TABLE);
    set_i(&p, "u_table_size", table_size);
    if (draw_t(&p, T_DPAR0, -1, dw, dh, 0) < 0)
        return -1;
    if (fill) {
        p = use(gl, P_fill_row_frag);
        bind_t(&p, "u_par", T_DPAR0);
        set_i(&p, "u_width", dw);
        if (draw_t(&p, T_DPAR1, -1, dw, dh, 0) < 0)
            return -1;
        p = use(gl, P_fill_column_frag);
        bind_t(&p, "u_par", T_DPAR1);
        set_i(&p, "u_height", dh);
        if (draw_t(&p, T_DPAR0, -1, dw, dh, 0) < 0)
            return -1;
    }
    texture(gl, out, GL_RG32F, w, h, NULL);
    p = use(gl, P_resample_frag);
    bind_t(&p, "u_par", T_DPAR0);
    set_2i(&p, "u_depth_size", dw, dh);
    set_2i(&p, "u_size", w, h);
    return draw_t(&p, out, -1, w, h, 0);
}

/* a view's map: picture position and parallax of each view pixel (holez.frag), from par with the shift s */
static int view_map(stereo_dibr_gl *gl, const struct stereo_dibr_params *prm, int w, int h, float s, float stretch, int par, int layered, int lo,
    int hi, int splat, int map)
{
    struct pass p;

    texture(gl, splat, GL_RGBA32F, w, h, NULL);
    texture(gl, map, GL_RG32F, w, h, NULL);
    if (layered) {
        const int width = (int)lround(3.0 * w / 1920.0);
        texture(gl, T_BAND, GL_RGBA32F, w, h, NULL);
        texture(gl, T_BOUNDARY, GL_RGBA32F, w, h, NULL);
        p = use(gl, P_band_frag);
        bind_t(&p, "u_par", par);
        set_i(&p, "u_width", w);
        set_i(&p, "u_band", width < 1 ? 1 : width > 16 ? 16 : width);
        if (draw_t(&p, T_BAND, -1, w, h, 0) < 0)
            return -1;
    }
    for (int layer = 0; layer < (layered ? 2 : 1); layer++) {
        p = use(gl, P_splat_frag);
        bind_t(&p, "u_par", par);
        bind_t(&p, "u_band", T_BAND);
        set_i(&p, "u_mode", layered ? 1 + layer : 0);
        set_f(&p, "u_s", s);
        set_f(&p, "u_stretch", stretch);
        set_i(&p, "u_lo", lo);
        set_i(&p, "u_hi", hi);
        set_i(&p, "u_width", w);
        if (draw_t(&p, layer ? T_BOUNDARY : splat, -1, w, h, 0) < 0)
            return -1;
    }
    p = use(gl, P_holez_frag);
    bind_t(&p, "u_splat", splat);
    set_i(&p, "u_width", w);
    set_i(&p, "u_backward", !layered && prm->warp == STEREO_DIBR_WARP_BACKWARD);
    set_f(&p, "u_s", s);
    return draw_t(&p, map, -1, w, h, 0);
}

/* a pass over colour and state, from one of the pair into the other; returns the pair it drew into */
static int step(stereo_dibr_gl *gl, int index, int from, int w, int h, int copy_first, int *any, void (*uniforms)(struct pass *, void *), void *data)
{
    const int to = !from;
    const int c_from = from ? T_COLOR1 : T_COLOR0, s_from = from ? T_STATE1 : T_STATE0;
    const int c_to = to ? T_COLOR1 : T_COLOR0, s_to = to ? T_STATE1 : T_STATE0;
    struct pass p;

    if (copy_first) {
        p = use(gl, P_copy_state_frag);
        bind_t(&p, "u_color", c_from);
        bind_t(&p, "u_state", s_from);
        if (draw_t(&p, c_to, s_to, w, h, 0) < 0)
            return -1;
    }
    p = use(gl, index);
    bind_t(&p, "u_color", c_from);
    bind_t(&p, "u_state", s_from);
    set_i(&p, "u_width", w);
    set_2i(&p, "u_size", w, h);
    if (uniforms)
        uniforms(&p, data);
    const int r = draw_t(&p, c_to, s_to, w, h, any != NULL);
    if (r < 0)
        return -1;
    if (any)
        *any = r;
    return to;
}

static void radius_uniform(struct pass *p, void *data) { set_i(p, "u_radius", *(int *)data); }

struct ring {
    int radius;
    float ring;
};

static void ring_uniforms(struct pass *p, void *data)
{
    const struct ring *r = data;
    set_i(p, "u_radius", r->radius);
    set_f(p, "u_ring", r->ring);
}

static void hole_uniform(struct pass *p, void *data)
{
    const float *hole = data;
    p->gl->Uniform4f(p->gl->GetUniformLocation(p->prog, "u_hole"), hole[0], hole[1], hole[2], hole[3]);
}

static int render(stereo_dibr_gl *gl, const struct stereo_dibr_params *prm, const float *table, int table_size, GLuint picture, int w, int h,
    GLuint depth, int dw, int dh, int integer, int role, GLuint layer, GLuint layer_depth, int ldw, int ldh, GLuint target, const float hole[4])
{
    if (!gl || !prm || !table || table_size < 1 || table_size > 65536 || !picture || w <= 0 || h <= 0 || !depth || dw <= 0 || dh <= 0 || !target
        || !hole)
        return -1;
    if (layer && (!layer_depth || ldw <= 0 || ldh <= 0 || (role != STEREO_DIBR_LAYER_BACKGROUND && role != STEREO_DIBR_LAYER_OVERLAY)))
        return -1;

    struct saved saved;
    int ret = 0;
    save(gl, &saved);
    gl->error[0] = 0;

    /* the table, 256 wide: (parallax, 1), or (0, 0) for no depth; its range bounds the splats' search */
    const int rows = (table_size + 255) / 256;
    float *data = calloc((size_t)rows * 256 * 2, sizeof(float));
    float pmin = 0.0f, pmax = 0.0f;
    int first = 1;
    if (!data) {
        restore(gl, &saved);
        return -1;
    }
    for (int i = 0; i < table_size; i++) {
        if (isnan(table[i]))
            continue;
        data[2 * i] = table[i];
        data[2 * i + 1] = 1.0f;
        if (first || table[i] < pmin)
            pmin = table[i];
        if (first || table[i] > pmax)
            pmax = table[i];
        first = 0;
    }
    texture(gl, T_TABLE, GL_RG32F, 256, rows, data);
    gl->tex_w[T_TABLE] = -1; /* uploaded each time */
    free(data);

    if (parallax(gl, table_size, depth, dw, dh, integer, w, h, 1, T_PAR) < 0)
        goto fail;
    const int smooth = prm->smooth == STEREO_DIBR_SMOOTH_ASYMMETRIC_GAUSSIAN;
    if (smooth) {
        const float per_1920 = (float)(w / 1920.0);
        texture(gl, T_PAR2, GL_RG32F, w, h, NULL);
        struct pass p = use(gl, P_blur_frag);
        bind_t(&p, "u_par", T_PAR);
        set_2i(&p, "u_size", w, h);
        set_2i(&p, "u_step", 1, 0);
        set_f(&p, "u_sigma", (float)(prm->sigma_h * per_1920));
        if (draw_t(&p, T_PAR2, -1, w, h, 0) < 0)
            goto fail;
        p = use(gl, P_blur_frag);
        bind_t(&p, "u_par", T_PAR2);
        set_2i(&p, "u_size", w, h);
        set_2i(&p, "u_step", 0, 1);
        set_f(&p, "u_sigma", (float)(prm->sigma_v * per_1920));
        if (draw_t(&p, T_PAR, -1, w, h, 0) < 0)
            goto fail;
    }
    if (layer && parallax(gl, table_size, layer_depth, ldw, ldh, integer, w, h, 0, T_LPAR) < 0)
        goto fail;

    for (int i = T_COLOR0; i <= T_STATE1; i++)
        texture(gl, i, GL_RGBA32F, w, h, NULL);

    for (int eye = 0; eye < 2; eye++) {
        if (!eye && prm->view == STEREO_DIBR_VIEW_LEFT && (!layer || role == STEREO_DIBR_LAYER_BACKGROUND)) {
            struct pass p = use(gl, P_copy_frag);
            bind(&p, "u_picture", picture);
            set_i(&p, "u_offset", 0);
            if (draw(&p, target, 0, 0, w, h, 0) < 0)
                goto fail;
            continue;
        }
        const float s = prm->view == STEREO_DIBR_VIEW_LEFT ? (eye ? 1.0f : 0.0f) : eye ? 0.5f : -0.5f;
        const int layered = prm->warp == STEREO_DIBR_WARP_LAYERED;
        /* the picture pixels x that can land on view pixel t: x + s p near t, p within the table's range */
        const float a = -s * pmin, b = -s * pmax;
        const int lo = (int)floorf(fminf(a, b)) - 2, hi = (int)ceilf(fmaxf(a, b)) + 2;

        if (view_map(gl, prm, w, h, s, smooth ? 3.4e38f : 2.0f, T_PAR, layered, lo, hi, T_SPLAT, T_MAP) < 0)
            goto fail;
        if (layer) {
            struct stereo_dibr_params lp = *prm;
            if (lp.warp == STEREO_DIBR_WARP_LAYERED)
                lp.warp = STEREO_DIBR_WARP_FORWARD;
            if (view_map(gl, &lp, w, h, s, 2.0f, T_LPAR, 0, lo, hi, T_LSPLAT, T_LMAP) < 0)
                goto fail;
        }

        struct pass p = use(gl, P_sample_frag);
        bind(&p, "u_picture", picture);
        bind_t(&p, "u_map", T_MAP);
        bind(&p, "u_layer_picture", layer ? layer : picture);
        bind_t(&p, "u_layer_map", layer ? T_LMAP : T_MAP);
        set_i(&p, "u_layer", layer && role == STEREO_DIBR_LAYER_BACKGROUND);
        set_i(&p, "u_width", w);
        if (draw_t(&p, T_COLOR0, T_STATE0, w, h, 0) < 0)
            goto fail;
        int cur = 0, any = 0, radius = prm->radius < 1 ? 1 : prm->radius;

        if (prm->fill == STEREO_DIBR_FILL_DEPTH_AIDED) {
            if ((cur = step(gl, P_hole_background_frag, cur, w, h, 0, NULL, NULL, NULL)) < 0)
                goto fail;
            for (int ring = 0; ring < w + h; ring++) {
                if ((cur = step(gl, P_fill_depth_aided_frag, cur, w, h, 1, &any, radius_uniform, &radius)) < 0)
                    goto fail;
                if (!any)
                    break;
            }
        } else if (prm->fill == STEREO_DIBR_FILL_TELEA) {
            if ((cur = step(gl, P_hole_background_frag, cur, w, h, 0, NULL, NULL, NULL)) < 0
                || (cur = step(gl, P_telea_init_frag, cur, w, h, 0, NULL, NULL, NULL)) < 0)
                goto fail;
            for (int pass_ = 0; pass_ < 4 * (w + h); pass_++) {
                if ((cur = step(gl, P_telea_distance_frag, cur, w, h, 1, &any, NULL, NULL)) < 0)
                    goto fail;
                if (!any)
                    break;
            }
            struct ring r = { radius, 0.0f };
            for (int k = 0; k < 4 * (w + h); k++) {
                struct pass c = use(gl, P_count_frag);
                bind_t(&c, "u_state", cur ? T_STATE1 : T_STATE0);
                const int left = draw_t(&c, cur ? T_COLOR0 : T_COLOR1, -1, w, h, 1);
                if (left < 0)
                    goto fail;
                if (!left)
                    break;
                r.ring = (float)k;
                if ((cur = step(gl, P_telea_fill_frag, cur, w, h, 1, &any, ring_uniforms, &r)) < 0)
                    goto fail;
            }
        }
        if (prm->fill != STEREO_DIBR_FILL_NONE) {
            if ((cur = step(gl, P_fill_background_frag, cur, w, h, 0, NULL, NULL, NULL)) < 0
                || (cur = step(gl, P_fill_rest_frag, cur, w, h, 0, NULL, hole_uniform, (void *)hole)) < 0)
                goto fail;
        }

        p = use(gl, P_compose_frag);
        bind_t(&p, "u_color", cur ? T_COLOR1 : T_COLOR0);
        bind_t(&p, "u_state", cur ? T_STATE1 : T_STATE0);
        bind_t(&p, "u_map", T_MAP);
        bind_t(&p, "u_boundary", layered ? T_BOUNDARY : T_MAP);
        bind(&p, "u_picture", picture);
        bind(&p, "u_layer_picture", layer ? layer : picture);
        bind_t(&p, "u_layer_map", layer ? T_LMAP : T_MAP);
        set_i(&p, "u_layered", layered);
        set_i(&p, "u_overlay", layer && role == STEREO_DIBR_LAYER_OVERLAY);
        hole_uniform(&p, (void *)hole);
        set_i(&p, "u_width", w);
        set_i(&p, "u_offset", eye * w);
        if (draw(&p, target, 0, eye * w, w, h, 0) < 0)
            goto fail;
    }
    if (gl->GetError() != GL_NO_ERROR) {
        set_error(gl, "a GL error", NULL);
        ret = -3;
    }
    restore(gl, &saved);
    return ret;

fail:
    if (!gl->error[0])
        set_error(gl, "a pass failed", NULL);
    restore(gl, &saved);
    return -3;
}

int stereo_dibr_gl_render(stereo_dibr_gl *gl, const struct stereo_dibr_params *p, const float *table, int table_size, unsigned picture, int width,
    int height, unsigned depth, int depth_width, int depth_height, int depth_integer, unsigned target, const float hole[4])
{
    return render(gl, p, table, table_size, picture, width, height, depth, depth_width, depth_height, depth_integer, 0, 0, 0, 0, 0, target, hole);
}

int stereo_dibr_gl_render_layer(stereo_dibr_gl *gl, const struct stereo_dibr_params *p, const float *table, int table_size, unsigned picture,
    int width, int height, unsigned depth, int depth_width, int depth_height, int depth_integer, int role, unsigned layer, unsigned layer_depth,
    int layer_depth_width, int layer_depth_height, unsigned target, const float hole[4])
{
    if (!layer)
        return -1;
    return render(gl, p, table, table_size, picture, width, height, depth, depth_width, depth_height, depth_integer, role, layer, layer_depth,
        layer_depth_width, layer_depth_height, target, hole);
}
