/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/* The DIBR core's GLSL passes against its C code: the same picture and depth drawn by stereo_dibr_render() and by
 * stereo_dibr_gl_render() in an OpenGL 3.3 core context of Mesa's software renderer (EGL, surfaceless), for every view,
 * warp, fill and smoothing, a depth map at another size, samples with no depth, and both kinds of second layer. The
 * views match to the rounding of a sample everywhere but where Telea's fill differs by its order (rings on the GPU, a
 * heap in C): there the mean difference is reported and bounded. Exits 77 (skipped) without a context. */

#include "stereo-dibr-gl.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_RED_INTEGER 0x8D94
#define GL_R8UI 0x8232
#define GL_R16UI 0x8234
#define GL_UNSIGNED_SHORT 0x1403
#define GL_NEAREST 0x2600
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_FRAMEBUFFER 0x8D40
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_PACK_ALIGNMENT 0x0D05

static void (*glGenTextures)(GLsizei, GLuint *);
static void (*glBindTexture)(GLenum, GLuint);
static void (*glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
static void (*glTexParameteri)(GLenum, GLenum, GLint);
static void (*glGenFramebuffers)(GLsizei, GLuint *);
static void (*glBindFramebuffer)(GLenum, GLuint);
static void (*glFramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
static void (*glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
static void (*glPixelStorei)(GLenum, GLint);
static void (*glFinish)(void);

static int failures = 0;
#define CHECK(condition)                                                                                                                             \
    do {                                                                                                                                             \
        if (!(condition)) {                                                                                                                          \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);                                                                     \
            ++failures;                                                                                                                              \
        }                                                                                                                                            \
    } while (0)

static stereo_dibr_gl_function resolve(const char *name) { return (stereo_dibr_gl_function)eglGetProcAddress(name); }

static int context(void)
{
    PFNEGLGETPLATFORMDISPLAYEXTPROC get_display = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (!get_display)
        return -1;
    EGLDisplay dpy = get_display(EGL_PLATFORM_SURFACELESS_MESA, EGL_DEFAULT_DISPLAY, NULL);
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, NULL, NULL) || !eglBindAPI(EGL_OPENGL_API))
        return -1;
    const EGLint config_attributes[] = { EGL_SURFACE_TYPE, EGL_PBUFFER_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_NONE };
    EGLConfig config;
    EGLint n = 0;
    if (!eglChooseConfig(dpy, config_attributes, &config, 1, &n) || n < 1)
        return -1;
    const EGLint context_attributes[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3, EGL_CONTEXT_OPENGL_PROFILE_MASK,
        EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, config, EGL_NO_CONTEXT, context_attributes);
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx))
        return -1;
#define LOAD(name)                                                                                                                                   \
    if (!(*(void **)&name = (void *)eglGetProcAddress(#name)))                                                                                       \
        return -1;
    LOAD(glGenTextures)
    LOAD(glBindTexture)
    LOAD(glTexImage2D)
    LOAD(glTexParameteri)
    LOAD(glGenFramebuffers)
    LOAD(glBindFramebuffer)
    LOAD(glFramebufferTexture2D)
    LOAD(glReadPixels)
    LOAD(glPixelStorei)
    LOAD(glFinish)
    return 0;
}

static GLuint upload(GLint internal, int w, int h, GLenum format, GLenum type, const void *data)
{
    GLuint t;
    glGenTextures(1, &t);
    glBindTexture(GL_TEXTURE_2D, t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, format, type, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    return t;
}

enum { W = 160, H = 90 };

/* A picture with detail everywhere (so a wrong position shows), a near disc over a ramp. */
static unsigned char picture[H][W][4], layer_rgba[H][W][4];
static unsigned char depth8[H][W], layer_depth8[H][W];
static unsigned short depth16[H / 2][W / 2];

static void scene(void)
{
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            picture[y][x][0] = (unsigned char)(x * 3 + y);
            picture[y][x][1] = (unsigned char)((x / 5 + y / 5) % 2 ? 200 : 40);
            picture[y][x][2] = (unsigned char)(255 - x - y);
            picture[y][x][3] = 255;
            const int in = (x - 70) * (x - 70) + (y - 45) * (y - 45) < 22 * 22;
            depth8[y][x] = (unsigned char)(in ? 230 : 20 + x / 2);
            layer_rgba[y][x][0] = 250;
            layer_rgba[y][x][1] = (unsigned char)(x * 2);
            layer_rgba[y][x][2] = 30;
            layer_rgba[y][x][3] = 255;
            layer_depth8[y][x] = (unsigned char)(y >= 10 && y < 20 && x >= 20 && x < 60 ? 240 : 0);
        }
    for (int y = 0; y < H / 2; y++)
        for (int x = 0; x < W / 2; x++) {
            /* a sensor in millimetres, with no measurement in places */
            depth16[y][x] = (unsigned short)((x * 7 + y * 3) % 37 == 0 ? 0 : 900 + 40 * x);
        }
}

/* the C render into 4 planes, interleaved back into RGBA rows of 2W */
static int render_c(const struct stereo_dibr_params *prm, const float *table, int table_size, const void *depth, int dw, int dh, int bytes,
    int role, int with_layer, unsigned char out[H][2 * W][4])
{
    static unsigned char planes[4][H][W], lplanes[4][H][W], left[4][H][W], right[4][H][W];
    struct stereo_dibr_picture pic = { W, H, 4, {} }, lay = { W, H, 4, {} }, l = { W, H, 4, {} }, r = { W, H, 4, {} };
    for (int k = 0; k < 4; k++) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                planes[k][y][x] = picture[y][x][k];
                lplanes[k][y][x] = layer_rgba[y][x][k];
            }
        pic.plane[k] = (struct stereo_dibr_plane) { planes[k], W, 1, 0, 0, 0 };
        lay.plane[k] = (struct stereo_dibr_plane) { lplanes[k], W, 1, 0, 0, 0 };
        l.plane[k] = (struct stereo_dibr_plane) { left[k], W, 1, 0, 0, k == 3 ? 255u : 0u };
        r.plane[k] = (struct stereo_dibr_plane) { right[k], W, 1, 0, 0, k == 3 ? 255u : 0u };
    }
    struct stereo_dibr_depth dm = { depth, (ptrdiff_t)dw * bytes, bytes, dw, dh };
    struct stereo_dibr_depth lm = { layer_depth8, W, 1, W, H };
    stereo_dibr *d = stereo_dibr_alloc();
    int ret = with_layer ? stereo_dibr_render_layer(d, prm, table, table_size, &pic, &dm, role, &lay, &lm, &l, &r)
                         : stereo_dibr_render(d, prm, table, table_size, &pic, &dm, &l, &r);
    stereo_dibr_free(d);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            for (int k = 0; k < 4; k++) {
                out[y][x][k] = left[k][y][x];
                out[y][W + x][k] = right[k][y][x];
            }
    return ret;
}

static int render_gl(stereo_dibr_gl *gl, const struct stereo_dibr_params *prm, const float *table, int table_size, GLuint depth, int dw, int dh,
    int role, int with_layer, GLuint layer, GLuint layer_depth, unsigned char out[H][2 * W][4])
{
    static GLuint pic = 0, target = 0, fbo = 0;
    if (!pic) {
        pic = upload(GL_RGBA8, W, H, GL_RGBA, GL_UNSIGNED_BYTE, picture);
        target = upload(GL_RGBA8, 2 * W, H, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
        glGenFramebuffers(1, &fbo);
    }
    const float hole[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    int ret = with_layer
        ? stereo_dibr_gl_render_layer(gl, prm, table, table_size, pic, W, H, depth, dw, dh, 1, role, layer, layer_depth, W, H, target, hole)
        : stereo_dibr_gl_render(gl, prm, table, table_size, pic, W, H, depth, dw, dh, 1, target, hole);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target, 0);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, 2 * W, H, GL_RGBA, GL_UNSIGNED_BYTE, out);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return ret;
}

/* how far the two renders are apart: pixels with a channel more than 1 apart, the mean difference */
static void compare(const char *what, unsigned char a[H][2 * W][4], unsigned char b[H][2 * W][4], double max_mean, int exact)
{
    long far_ = 0;
    double sum = 0.0;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < 2 * W; x++) {
            int worst = 0;
            for (int k = 0; k < 4; k++) {
                const int d = abs((int)a[y][x][k] - (int)b[y][x][k]);
                worst = d > worst ? d : worst;
                sum += d;
            }
            far_ += worst > 1;
        }
    const double mean = sum / (H * 2.0 * W * 4.0);
    printf("%-44s pixels off by more than 1: %5ld of %d, mean difference %.3f\n", what, far_, H * 2 * W, mean);
    if (exact)
        CHECK(far_ * 1000 <= (long)H * 2 * W); /* at most a thousandth (float rounding at the splats' edges) */
    CHECK(mean <= max_mean);
}

int main(void)
{
    if (context() < 0) {
        printf("no OpenGL 3.3 context (EGL surfaceless): skipped\n");
        return 77;
    }
    stereo_dibr_gl *gl = stereo_dibr_gl_create(resolve, STEREO_DIBR_GL_DESKTOP);
    CHECK(gl != NULL);
    if (!gl)
        return 1;
    scene();

    static unsigned char c_out[H][2 * W][4], g_out[H][2 * W][4];
    static float table[65536];
    struct stereo_depth_convention conv;
    memset(&conv, 0, sizeof(conv));
    conv.kind = STEREO_DEPTH_NEARNESS;
    conv.bits = 8;
    conv.zero = 0.5;
    conv.range = 58.0 * 1920 / W / 4; /* 14.5 pixels at 160, wide holes */
    CHECK(stereo_depth_parallax_table(&conv, W, table) == 0);
    GLuint depth = upload(GL_R8UI, W, H, GL_RED_INTEGER, GL_UNSIGNED_BYTE, depth8);

    static const char *const views[] = { "left", "center" }, *const warps[] = { "forward", "backward", "layered" },
                              *const fills[] = { "background", "depth-aided", "telea", "none" };
    for (int view = 0; view < 2; view++)
        for (int warp = 0; warp < 3; warp++)
            for (int fill = 0; fill < 4; fill++)
                for (int smooth = 0; smooth < 2; smooth++) {
                    struct stereo_dibr_params prm;
                    char what[96];
                    stereo_dibr_params_default(&prm);
                    prm.view = view;
                    prm.warp = warp;
                    prm.fill = fill;
                    prm.smooth = smooth;
                    snprintf(what, sizeof(what), "%s %s %s%s", views[view], warps[warp], fills[fill], smooth ? " smoothed" : "");
                    CHECK(render_c(&prm, table, 256, depth8, W, H, 1, 0, 0, c_out) == 0);
                    CHECK(render_gl(gl, &prm, table, 256, depth, W, H, 0, 0, 0, 0, g_out) == 0);
                    if (fill == STEREO_DIBR_FILL_TELEA)
                        compare(what, c_out, g_out, 1.5, 0);
                    else
                        compare(what, c_out, g_out, 0.05, 1);
                }

    /* a 16-bit sensor depth at half the size, with samples that hold no depth */
    {
        struct stereo_dibr_params prm;
        stereo_dibr_params_default(&prm);
        prm.view = STEREO_DIBR_VIEW_CENTER;
        conv.kind = STEREO_DEPTH_METRIC;
        conv.bits = 16;
        conv.z_present = 1;
        conv.z_near = 900;
        conv.z_far = 900 + 40 * 79;
        CHECK(stereo_depth_parallax_table(&conv, W, table) == 0);
        GLuint d16 = upload(GL_R16UI, W / 2, H / 2, GL_RED_INTEGER, GL_UNSIGNED_SHORT, depth16);
        CHECK(render_c(&prm, table, 65536, depth16, W / 2, H / 2, 2, 0, 0, c_out) == 0);
        CHECK(render_gl(gl, &prm, table, 65536, d16, W / 2, H / 2, 0, 0, 0, 0, g_out) == 0);
        compare("center forward background, metric 16-bit half", c_out, g_out, 0.05, 1);
    }

    /* the second layers */
    {
        struct stereo_dibr_params prm;
        stereo_dibr_params_default(&prm);
        conv.kind = STEREO_DEPTH_NEARNESS;
        conv.bits = 8;
        conv.z_present = 0;
        CHECK(stereo_depth_parallax_table(&conv, W, table) == 0);
        table[0] = NAN;
        GLuint lay = upload(GL_RGBA8, W, H, GL_RGBA, GL_UNSIGNED_BYTE, layer_rgba);
        GLuint ldepth = upload(GL_R8UI, W, H, GL_RED_INTEGER, GL_UNSIGNED_BYTE, layer_depth8);
        for (int role = 0; role < 2; role++) {
            prm.fill = STEREO_DIBR_FILL_NONE;
            CHECK(render_c(&prm, table, 256, depth8, W, H, 1, role, 1, c_out) == 0);
            CHECK(render_gl(gl, &prm, table, 256, depth, W, H, role, 1, lay, ldepth, g_out) == 0);
            compare(role ? "left forward none, graphics layer" : "left forward none, background layer", c_out, g_out, 0.05, 1);
        }
    }

    glFinish();
    stereo_dibr_gl_free(gl);
    if (failures)
        fprintf(stderr, "%d failures\n", failures);
    return failures != 0;
}
