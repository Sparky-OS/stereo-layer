/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/*
 * How long does each way of drawing the two views of a picture and its depth map take, at a size a player has to keep up
 * with? Times stereo_dibr_gl_render() (the GLSL passes, on a GPU, headless: EGL on a device of its own, no display) and
 * stereo_dibr_render() (the C code, on the CPU) for every way of autotests/dibrbench.c, on a layered picture and its depth
 * map, each frame to the end of its work. A source of 24, 30 or 60 frames a second leaves 41.7, 33.3 or 16.7 ms per frame:
 * a way keeps up with a rate when its slowest frames but one in twenty (the 95th percentile) fit.
 *
 *   dibrtime --list                         the EGL devices
 *   dibrtime --gpu N [--size WxH] [--frames N] [--upload]
 *   dibrtime --cpu [--size WxH] [--frames N]
 * --upload adds the frame's upload to the GPU (the picture and its depth map) to every frame, as a player pays it.
 * Prints a table of the median and the 95th percentile in ms and the rates kept.
 */

#define _GNU_SOURCE
#include "stereo-dibr-gl.h"
#include "stereo-dibr.h"

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLubyte;
#define GL_TEXTURE_2D 0x0DE1
#define GL_UNSIGNED_BYTE 0x1401
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_RED 0x1903
#define GL_R8 0x8229
#define GL_NEAREST 0x2600
#define GL_LINEAR 0x2601
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02

static void (*glGenTextures)(GLsizei, GLuint *);
static void (*glBindTexture)(GLenum, GLuint);
static void (*glTexImage2D)(GLenum, GLint, GLint, GLsizei, GLsizei, GLint, GLenum, GLenum, const void *);
static void (*glTexSubImage2D)(GLenum, GLint, GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, const void *);
static void (*glTexParameteri)(GLenum, GLenum, GLint);
static void (*glPixelStorei)(GLenum, GLint);
static void (*glFinish)(void);
static const GLubyte *(*glGetString)(GLenum);

static stereo_dibr_gl_function resolve(const char *name) { return (stereo_dibr_gl_function)eglGetProcAddress(name); }

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

/* ---- a picture and its depth map: layered textured shapes in front of a sloped background, depth as 8-bit nearness ---- */

static double hash(unsigned x, unsigned y, unsigned s)
{
    unsigned h = x * 374761393u + y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (double)((h ^ (h >> 16)) & 0xffff) / 65535.0;
}

static double noise(double x, double y, unsigned seed)
{
    double sum = 0, amp = 0.5, norm = 0;
    for (int o = 0; o < 4; ++o) {
        double fx = floor(x), fy = floor(y), tx = x - fx, ty = y - fy;
        tx = tx * tx * (3 - 2 * tx);
        ty = ty * ty * (3 - 2 * ty);
        double a = hash((unsigned)(int)fx, (unsigned)(int)fy, seed + o), b = hash((unsigned)(int)fx + 1, (unsigned)(int)fy, seed + o);
        double c = hash((unsigned)(int)fx, (unsigned)(int)fy + 1, seed + o), d = hash((unsigned)(int)fx + 1, (unsigned)(int)fy + 1, seed + o);
        sum += amp * ((a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty);
        norm += amp;
        amp *= 0.5;
        x *= 2.3;
        y *= 2.3;
    }
    return sum / norm;
}

static void make_content(int w, int h, unsigned char *rgba, unsigned char *depth)
{
    double cx[5], cy[5], rx[5], ry[5];
    int level[5];
    for (int i = 1; i < 5; ++i) {
        cx[i] = w * (0.15 + 0.7 * hash(7, i, 2));
        cy[i] = h * (0.25 + 0.55 * hash(7, i, 3));
        rx[i] = w * (0.05 + 0.11 * hash(7, i, 1));
        ry[i] = h * (0.08 + 0.14 * hash(7, i, 4));
        level[i] = 140 + 25 * i;
    }
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int layer = 0, n = (int)lround(255 * 0.35 * (0.4 + 0.6 * y / (double)h));
            for (int i = 1; i < 5; ++i) {
                double dx = (x - cx[i]) / rx[i], dy = (y - cy[i]) / ry[i];
                if (dx * dx + dy * dy <= 1.0 && level[i] > n) {
                    layer = i;
                    n = level[i];
                }
            }
            double s = layer ? 1 / 14.0 : 1 / 40.0;
            double px = x * s * (w > 960 ? 0.5 : 1), py = y * s * (w > 960 ? 0.5 : 1);
            rgba[4 * (y * w + x) + 0] = (unsigned char)(40 + 200 * noise(px, py, 100 + layer));
            rgba[4 * (y * w + x) + 1] = (unsigned char)(40 + 200 * noise(px + 31.7, py - 12.3, 107 + layer));
            rgba[4 * (y * w + x) + 2] = (unsigned char)(40 + 200 * noise(px - 8.1, py + 55.9, 113 + layer));
            rgba[4 * (y * w + x) + 3] = 255;
            depth[y * w + x] = (unsigned char)n;
        }
}

struct way {
    char name[64];
    struct stereo_dibr_params p;
    int is_default;
};

static int list_ways(struct way *ways)
{
    static const char *warps[] = { "forward", "backward", "layered" };
    static const char *fills[] = { "background", "depth-aided", "telea" };
    struct stereo_dibr_options def;
    struct stereo_dibr_params defp;
    stereo_dibr_options_default(&def);
    stereo_dibr_options_apply(&def, NULL, &defp);
    int n = 0;
    for (int warp = 0; warp < 3; ++warp)
        for (int fill = 0; fill < 3; ++fill) {
            struct way *w = &ways[n++];
            stereo_dibr_params_default(&w->p);
            w->p.warp = warp;
            w->p.fill = fill;
            snprintf(w->name, sizeof(w->name), "left/%s/%s", warps[warp], fills[fill]);
            w->is_default = w->p.warp == defp.warp && w->p.fill == defp.fill && w->p.view == defp.view && w->p.smooth == defp.smooth;
        }
    struct way *w = &ways[n++];
    stereo_dibr_params_default(&w->p);
    w->p.smooth = STEREO_DIBR_SMOOTH_ASYMMETRIC_GAUSSIAN;
    snprintf(w->name, sizeof(w->name), "left/forward/background/smooth");
    w = &ways[n++];
    stereo_dibr_params_default(&w->p);
    w->p.view = STEREO_DIBR_VIEW_CENTER;
    snprintf(w->name, sizeof(w->name), "centre/forward/background");
    w = &ways[n++];
    stereo_dibr_params_default(&w->p);
    w->p.view = STEREO_DIBR_VIEW_CENTER;
    w->p.fill = STEREO_DIBR_FILL_DEPTH_AIDED;
    snprintf(w->name, sizeof(w->name), "centre/forward/depth-aided");
    return n;
}

static void report(const char *name, double *t, int frames, int is_default)
{
    qsort(t, frames, sizeof(double), cmp_double);
    double med = t[frames / 2], p95 = t[(int)(frames * 0.95) < frames ? (int)(frames * 0.95) : frames - 1];
    printf("%-30s %8.2f %8.2f   %8.1f   %-3s %-3s %-3s%s\n", name, med, p95, 1000.0 / med, p95 <= 1000.0 / 24 ? "24" : "-", p95 <= 1000.0 / 30 ? "30" : "-",
        p95 <= 1000.0 / 60 ? "60" : "-", is_default ? "  <- default" : "");
    fflush(stdout);
}

int main(int argc, char **argv)
{
    int gpu = -1, cpu = 0, list = 0, upload = 0, frames = 0, w = 1920, h = 1080, radius = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--list"))
            list = 1;
        else if (!strcmp(argv[i], "--cpu"))
            cpu = 1;
        else if (!strcmp(argv[i], "--upload"))
            upload = 1;
        else if (!strcmp(argv[i], "--gpu") && i + 1 < argc)
            gpu = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--radius") && i + 1 < argc)
            radius = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--frames") && i + 1 < argc)
            frames = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--size") && i + 1 < argc)
            sscanf(argv[++i], "%dx%d", &w, &h);
        else {
            fprintf(stderr, "usage: dibrtime --list | --gpu N [--size WxH] [--frames N] [--upload] [--radius N] | --cpu [--size WxH] [--frames N] [--radius N]\n");
            return 2;
        }
    }
    PFNEGLQUERYDEVICESEXTPROC queryDevices = (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
    PFNEGLQUERYDEVICESTRINGEXTPROC queryDeviceString = (PFNEGLQUERYDEVICESTRINGEXTPROC)eglGetProcAddress("eglQueryDeviceStringEXT");
    PFNEGLGETPLATFORMDISPLAYEXTPROC getPlatformDisplay = (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    EGLDeviceEXT devices[16];
    EGLint ndev = 0;
    if (list || gpu >= 0) {
        if (!queryDevices || !queryDevices(16, devices, &ndev)) {
            fprintf(stderr, "no EGL devices\n");
            return 3;
        }
    }
    if (list) {
        for (int i = 0; i < ndev; ++i) {
            const char *node = queryDeviceString ? queryDeviceString(devices[i], EGL_DRM_RENDER_NODE_FILE_EXT) : NULL;
            printf("device %d: %s\n", i, node ? node : "(no render node)");
        }
        return 0;
    }

    struct way ways[24];
    int nways = list_ways(ways);
    if (radius > 0)
        for (int k = 0; k < nways; ++k)
            ways[k].p.radius = radius; /* the reach of the depth-aided and Telea fills */
    unsigned char *rgba = malloc((size_t)w * h * 4), *depth = malloc((size_t)w * h);
    make_content(w, h, rgba, depth);
    struct stereo_depth_convention c;
    struct stereo_dibr_options def;
    stereo_dibr_options_default(&def);
    memset(&c, 0, sizeof(c));
    c.kind = STEREO_DEPTH_NEARNESS;
    c.bits = 8;
    stereo_dibr_options_apply(&def, &c, NULL);
    float table[256];
    stereo_depth_parallax_table(&c, w, table);

    printf("%-30s %8s %8s   %8s   rates kept by the 95th percentile\n", "way", "median", "p95 ms", "fps");
    if (cpu) {
        /* a player's frame is YUV 4:2:0: three planes, the chroma at half size */
        int cw = (w + 1) / 2, ch = (h + 1) / 2;
        unsigned char *pl[3] = { malloc((size_t)w * h), malloc((size_t)cw * ch), malloc((size_t)cw * ch) };
        unsigned char *ol[3] = { malloc((size_t)w * h * 2), malloc((size_t)cw * ch * 2), malloc((size_t)cw * ch * 2) };
        for (long i = 0; i < (long)w * h; ++i)
            pl[0][i] = rgba[4 * i + 1];
        memset(pl[1], 128, (size_t)cw * ch);
        memset(pl[2], 128, (size_t)cw * ch);
        struct stereo_dibr_picture in;
        memset(&in, 0, sizeof(in));
        in.width = w;
        in.height = h;
        in.nb_planes = 3;
        for (int k = 0; k < 3; ++k) {
            in.plane[k].data = pl[k];
            in.plane[k].stride = k ? cw : w;
            in.plane[k].bytes = 1;
            in.plane[k].log2_w = in.plane[k].log2_h = k ? 1 : 0;
        }
        struct stereo_dibr_depth d = { depth, w, 1, w, h };
        int n = frames ? frames : 8;
        double *t = malloc(sizeof(double) * n);
        stereo_dibr *dibr = stereo_dibr_alloc();
        for (int k = 0; k < nways; ++k) {
            struct stereo_dibr_picture left = in, right = in;
            for (int p = 0; p < 3; ++p) {
                left.plane[p].data = ol[p];
                left.plane[p].stride = in.plane[p].stride * 2;
                right.plane[p].data = ol[p] + (p ? cw : w);
                right.plane[p].stride = in.plane[p].stride * 2;
            }
            for (int i = -1; i < n; ++i) {
                double t0 = now_ms();
                stereo_dibr_render(dibr, &ways[k].p, table, 256, &in, &d, &left, &right);
                if (i >= 0)
                    t[i] = now_ms() - t0;
            }
            report(ways[k].name, t, n, ways[k].is_default);
        }
        stereo_dibr_free(dibr);
        return 0;
    }

    EGLDisplay dpy = getPlatformDisplay ? getPlatformDisplay(EGL_PLATFORM_DEVICE_EXT, devices[gpu], NULL) : EGL_NO_DISPLAY;
    EGLint major, minor;
    if (dpy == EGL_NO_DISPLAY || !eglInitialize(dpy, &major, &minor)) {
        fprintf(stderr, "EGL did not start on device %d\n", gpu);
        return 3;
    }
    eglBindAPI(EGL_OPENGL_API);
    static const EGLint attribs[] = { EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 3, EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE };
    EGLContext ctx = eglCreateContext(dpy, EGL_NO_CONFIG_KHR, EGL_NO_CONTEXT, attribs);
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx)) {
        fprintf(stderr, "no OpenGL 3.3 core context on device %d (0x%x)\n", gpu, eglGetError());
        return 3;
    }
#define LOAD(f) *(void **)&f = (void *)eglGetProcAddress(#f)
    LOAD(glGenTextures); LOAD(glBindTexture); LOAD(glTexImage2D); LOAD(glTexSubImage2D); LOAD(glTexParameteri); LOAD(glPixelStorei); LOAD(glFinish); LOAD(glGetString);
    printf("# GL_RENDERER: %s, %s; %dx%d per eye%s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION), w, h, upload ? ", with the upload of the frame" : "");
    GLuint tex[3];
    glGenTextures(3, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glBindTexture(GL_TEXTURE_2D, tex[0]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, tex[1]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, w, h, 0, GL_RED, GL_UNSIGNED_BYTE, depth);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, tex[2]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2 * w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    stereo_dibr_gl *gl = stereo_dibr_gl_create(resolve, STEREO_DIBR_GL_DESKTOP);
    if (!gl) {
        fprintf(stderr, "the GLSL passes did not compile on device %d\n", gpu);
        return 3;
    }
    const float hole[4] = { 0, 0, 0, 1 };
    int n = frames ? frames : 120;
    double *t = malloc(sizeof(double) * n);
    for (int k = 0; k < nways; ++k) {
        int bad = 0;
        for (int i = -5; i < n; ++i) {
            double t0 = now_ms();
            if (upload) {
                glBindTexture(GL_TEXTURE_2D, tex[0]);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
                glBindTexture(GL_TEXTURE_2D, tex[1]);
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RED, GL_UNSIGNED_BYTE, depth);
            }
            int r = stereo_dibr_gl_render(gl, &ways[k].p, table, 256, tex[0], w, h, tex[1], w, h, 0, tex[2], hole);
            glFinish();
            if (r != 0) {
                bad = r;
                break;
            }
            if (i >= 0)
                t[i] = now_ms() - t0;
        }
        if (bad)
            printf("%-30s failed (%d): %s\n", ways[k].name, bad, stereo_dibr_gl_error(gl));
        else
            report(ways[k].name, t, n, ways[k].is_default);
    }
    stereo_dibr_gl_free(gl);
    return 0;
}
