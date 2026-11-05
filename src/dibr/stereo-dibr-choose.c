/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/*
 * Which way of drawing the views keeps up with a video? The ladder of ways (best first), the choice from timings, the person's
 * cache of timings and the timing on the CPU. The timing on a GPU is in stereo-dibr-gl.c. See stereo-dibr.h.
 */

#define _GNU_SOURCE
#include "stereo-dibr-private.h"

#include <errno.h>
#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define NB_WAYS 4

/*
 * In the order of the mean error of dibrbench over both eyes (the best first), the ways that no other way beats at its cost: layered
 * warping with depth-aided fill is the best way measured; the pyramid fill has about the error of depth-aided (a hundredth more) at
 * a tenth of its time on a GPU, with the layered warp (which has the lower error where the picture shows nothing) or the forward
 * warp (a little less to draw); background extension with the forward warp is the least costly way there is. Ways with more error
 * than another that costs no more (forward depth-aided, layered background, backward warping) are not in it.
 */
static const struct stereo_dibr_way ladder[NB_WAYS] = {
    { STEREO_DIBR_WARP_LAYERED, STEREO_DIBR_FILL_DEPTH_AIDED },
    { STEREO_DIBR_WARP_LAYERED, STEREO_DIBR_FILL_PYRAMID },
    { STEREO_DIBR_WARP_FORWARD, STEREO_DIBR_FILL_PYRAMID },
    { STEREO_DIBR_WARP_FORWARD, STEREO_DIBR_FILL_BACKGROUND },
};

int stereo_dibr_ways(struct stereo_dibr_way *ways, int max)
{
    for (int i = 0; ways && i < NB_WAYS && i < max; i++)
        ways[i] = ladder[i];
    return NB_WAYS;
}

int stereo_dibr_choose_from_times(const double *p95_ms, int n, double fps, struct stereo_dibr_choice *out)
{
    if (!p95_ms || n != NB_WAYS || !out || !(fps > 0.0) || fps > 1000.0)
        return -1;
    const double budget = 0.8 * 1000.0 / fps;
    int pick = -1, quickest = -1;
    for (int i = 0; i < n; i++) {
        if (p95_ms[i] < 0.0)
            continue;
        if (quickest < 0 || p95_ms[i] < p95_ms[quickest])
            quickest = i;
        if (pick < 0 && p95_ms[i] <= budget)
            pick = i;
    }
    memset(out, 0, sizeof(*out));
    out->skipped_warp = out->skipped_fill = -1;
    out->budget_ms = budget;
    out->keeps_up = pick >= 0;
    if (pick < 0)
        pick = quickest;
    if (pick < 0)
        return -1;
    out->rank = pick;
    out->warp = ladder[pick].warp;
    out->fill = ladder[pick].fill;
    out->ms = p95_ms[pick];
    if (pick > 0) {
        /* the best way that did not keep up: the first of the ladder (a way not timed does not keep up, with no time) */
        out->skipped_warp = ladder[0].warp;
        out->skipped_fill = ladder[0].fill;
        out->skipped_ms = p95_ms[0];
    }
    return 0;
}

/* What the driver takes beyond the passes' textures: measured as the driver's free memory before and after the first drawing of each way at 1920x1080 a
 * view in its own process (GL_NVX_gpu_memory_info on the GeForce RTX 3060, GL_ATI_meminfo on the Radeon R5 M330): 1.13 to 1.39 times the peak counted here
 * (evidence/2g/gpu-memory-*). One number for the guard of the automatic choice and for the sentence beside a way in the list. */
#define DRIVER_OVERHEAD 1.35

/* The GPU memory a way needs at width x height, bytes, as the driver takes it: the textures of its passes (positions, splats, colour and state twice over,
 * the layered warp's band and boundary, the pyramid's levels), counted from the passes at their peak, times what the driver adds. */
double stereo_dibr_way_memory(int warp, int fill, int width, int height, int bits)
{
    const double px = (double)width * height;
    const double color = bits > 10 ? 16.0 : 8.0; /* RGBA float or half float */
    /* the passes' textures at their peak, two phases: while the view's map is made (parallax 8, splat 16, map 8, the layered warp's band and boundary 32), and
     * after the splat and the band are handed back (parallax, map, colour twice, state twice 32, the boundary, the pyramid's levels) */
    const double phase1 = 8 + 16 + 8 + (warp == STEREO_DIBR_WARP_LAYERED ? 32 : 0);
    double phase2 = 8 + 8 + 2 * color + 32 + (warp == STEREO_DIBR_WARP_LAYERED ? 16 : 0);
    if (fill == STEREO_DIBR_FILL_PYRAMID)
        phase2 += (2 * color + 32) / 3.0; /* a third of the picture: the levels up and down, colour and state */
    double bytes = px * (phase1 > phase2 ? phase1 : phase2);
    /* the picture, its depth and the two views, at the content's depth: RGBA8, R8 and two RGBA8 up to 8 bits; RGBA16, R16UI and two RGBA16 above */
    bytes += px * (bits > 8 ? 8 + 2 + 2 * 8 : 4 + 1 + 2 * 4);
    return bytes * DRIVER_OVERHEAD;
}

/* Whether a way fits in free_bytes of GPU memory (a negative free_bytes is the driver not saying: it fits). */
int stereo_dibr_way_fits(int warp, int fill, int width, int height, int bits, double free_bytes)
{
    return free_bytes < 0.0 || stereo_dibr_way_memory(warp, fill, width, height, bits) <= free_bytes;
}

/* ---- the clock, the statistics and the synthetic picture ------------------------------------------------------------ */

double stereo_dibr_now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

static int cmp_double(const void *a, const void *b)
{
    const double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

double stereo_dibr_p95(double *ms, int n)
{
    qsort(ms, (size_t)n, sizeof(double), cmp_double);
    int i = (int)ceil(0.95 * n) - 1;
    return ms[i < 0 ? 0 : i >= n ? n - 1 : i];
}

static unsigned hash2(unsigned x, unsigned y, unsigned s)
{
    unsigned h = x * 374761393u + y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

void stereo_dibr_synthetic(int w, int h, unsigned char *rgba, unsigned char *depth)
{
    static const struct { double cx, cy, rx, ry; int nearness; } shape[] = {
        { 0.30, 0.55, 0.16, 0.30, 230 }, { 0.62, 0.45, 0.12, 0.22, 190 }, { 0.80, 0.65, 0.10, 0.25, 160 }, { 0.48, 0.30, 0.20, 0.10, 120 },
    };
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            int d = 20 + (int)(60.0 * y / h);
            unsigned seed = 1;
            for (int s = 0; s < 4; s++) {
                const double dx = ((double)x / w - shape[s].cx) / shape[s].rx, dy = ((double)y / h - shape[s].cy) / shape[s].ry;
                if (dx * dx + dy * dy <= 1.0) {
                    d = shape[s].nearness;
                    seed = 2 + s;
                }
            }
            depth[(size_t)y * w + x] = (unsigned char)d;
            const unsigned n = hash2((unsigned)(x >> 2), (unsigned)(y >> 2), seed);
            unsigned char *p = rgba + 4 * ((size_t)y * w + x);
            p[0] = (unsigned char)(60 + (n & 127) + seed * 10);
            p[1] = (unsigned char)(60 + ((n >> 8) & 127));
            p[2] = (unsigned char)(60 + ((n >> 16) & 127));
            p[3] = 255;
        }
}

void stereo_dibr_timing_table(int width, float *table)
{
    struct stereo_dibr_options o;
    struct stereo_depth_convention c;
    memset(&c, 0, sizeof(c));
    stereo_dibr_options_default(&o);
    c.kind = STEREO_DEPTH_NEARNESS;
    c.bits = 8;
    stereo_dibr_options_apply(&o, &c, NULL);
    stereo_depth_parallax_table(&c, width, table);
}

/* ---- the person's cache of timings ---------------------------------------------------------------------------------- */

#define CACHE_HEADER "stereo-dibr-timings 1"

static int cache_path(char *path, size_t size, int create)
{
    const char *xdg = getenv("XDG_CACHE_HOME"), *home = getenv("HOME");
    char dir[1024];
    if (xdg && *xdg == '/')
        snprintf(dir, sizeof(dir), "%s/stereo-dibr", xdg);
    else if (home && *home == '/')
        snprintf(dir, sizeof(dir), "%s/.cache/stereo-dibr", home);
    else
        return -1;
    if (create) {
        char parent[1024];
        snprintf(parent, sizeof(parent), "%s", dir);
        char *slash = strrchr(parent, '/');
        if (slash) {
            *slash = 0;
            mkdir(parent, 0700);
        }
        if (mkdir(dir, 0700) && errno != EEXIST)
            return -1;
    }
    return snprintf(path, size, "%s/timings", dir) < (int)size ? 0 : -1;
}

/* A line: key TAB width x height TAB the ladder as WARP/FILL numbers TAB the times. The key has no TAB or newline. */
static void ladder_text(char *buf, size_t size)
{
    size_t n = 0;
    for (int i = 0; i < NB_WAYS; i++)
        n += (size_t)snprintf(buf + n, n < size ? size - n : 0, "%s%d%d", i ? "," : "", ladder[i].warp, ladder[i].fill);
}

int stereo_dibr_cache_load(const char *key, int width, int height, double *p95_ms, int n)
{
    char path[1100], want[96], lad[64], line[2048];
    if (!key || n != NB_WAYS || !p95_ms || cache_path(path, sizeof(path), 0))
        return 0;
    FILE *f = fopen(path, "r");
    if (!f)
        return 0;
    ladder_text(lad, sizeof(lad));
    snprintf(want, sizeof(want), "%dx%d", width, height);
    int found = 0;
    if (fgets(line, sizeof(line), f) && !strncmp(line, CACHE_HEADER, strlen(CACHE_HEADER)))
        while (fgets(line, sizeof(line), f)) {
            char *k = strtok(line, "\t"), *size = strtok(NULL, "\t"), *l = strtok(NULL, "\t"), *times = strtok(NULL, "\t\n");
            if (!k || !size || !l || !times || strcmp(k, key) || strcmp(size, want) || strcmp(l, lad))
                continue;
            double v[NB_WAYS];
            char *p = times;
            int i = 0;
            for (; i < NB_WAYS; i++) {
                char *end;
                v[i] = strtod(p, &end);
                if (end == p)
                    break;
                p = *end == ',' ? end + 1 : end;
            }
            if (i == NB_WAYS) {
                memcpy(p95_ms, v, sizeof(v));
                found = 1;
            }
        }
    fclose(f);
    return found;
}

int stereo_dibr_cache_store(const char *key, int width, int height, const double *p95_ms, int n)
{
    char path[1100], tmp[1120], want[96], lad[64], line[2048];
    if (!key || n != NB_WAYS || !p95_ms || strchr(key, '\t') || strchr(key, '\n') || cache_path(path, sizeof(path), 1))
        return -1;
    snprintf(tmp, sizeof(tmp), "%s.%d", path, (int)getpid());
    const int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    FILE *out = fd >= 0 ? fdopen(fd, "w") : NULL;
    if (!out) {
        if (fd >= 0)
            close(fd);
        return -1;
    }
    ladder_text(lad, sizeof(lad));
    snprintf(want, sizeof(want), "%dx%d", width, height);
    fprintf(out, CACHE_HEADER "\n");
    FILE *in = fopen(path, "r");
    if (in) {
        if (fgets(line, sizeof(line), in) && !strncmp(line, CACHE_HEADER, strlen(CACHE_HEADER)))
            while (fgets(line, sizeof(line), in)) {
                char copy[2048];
                snprintf(copy, sizeof(copy), "%s", line);
                char *k = strtok(copy, "\t"), *size = strtok(NULL, "\t"), *l = strtok(NULL, "\t");
                if (k && size && l && !strcmp(k, key) && !strcmp(size, want) && !strcmp(l, lad))
                    continue; /* replaced below */
                fputs(line, out);
            }
        fclose(in);
    }
    fprintf(out, "%s\t%s\t%s\t", key, want, lad);
    for (int i = 0; i < NB_WAYS; i++)
        fprintf(out, "%s%.3f", i ? "," : "", p95_ms[i]);
    fprintf(out, "\n");
    if (fclose(out) || rename(tmp, path)) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

/* ---- the CPU ------------------------------------------------------------------------------------------------------- */

static void cpu_key(char *key, size_t size)
{
    char model[128] = "cpu";
    FILE *f = fopen("/proc/cpuinfo", "r");
    char line[256];
    while (f && fgets(line, sizeof(line), f))
        if (!strncmp(line, "model name", 10)) {
            char *c = strchr(line, ':');
            if (c) {
                c++;
                while (*c == ' ')
                    c++;
                c[strcspn(c, "\n\t")] = 0;
                snprintf(model, sizeof(model), "%s", c);
            }
            break;
        }
    if (f)
        fclose(f);
    snprintf(key, size, "cpu %s", model);
}

int stereo_dibr_time_ladder(double fps, stereo_dibr_time_way time, void *user, double *p95_ms, int n, int *timed)
{
    if (!time || !p95_ms || n != NB_WAYS || !(fps > 0.0) || fps > 1000.0)
        return -1;
    const double budget = 0.8 * 1000.0 / fps;
    int count = 0;
    for (int i = n - 1; i >= 0; i--) {
        if (p95_ms[i] < 0.0) {
            /* eight times a way that took over 125 ms is over a second: not run, it does not keep up */
            if (i < n - 1 && p95_ms[i + 1] * 8.0 > 1000.0)
                break;
            double t;
            const int r = time(user, i, &t);
            if (r < 0) {
                if (timed)
                    *timed = count;
                return -1;
            }
            if (r > 0) {
                /* not run (the GPU has not the memory for it): -2, and nothing dearer runs */
                p95_ms[i] = -2.0;
                break;
            }
            p95_ms[i] = t;
            count++;
        }
        if (p95_ms[i] > budget)
            break;
    }
    if (timed)
        *timed = count;
    return 0;
}

struct cpu_timing {
    stereo_dibr *d;
    const float *table;
    struct stereo_dibr_picture in, l, r;
    struct stereo_dibr_depth dep;
};

static int time_one_cpu(void *user, int rung, double *p95)
{
    struct cpu_timing *t = user;
    struct stereo_dibr_params p;
    double ms[STEREO_DIBR_TIMED_FRAMES];
    int count = 0;

    stereo_dibr_params_default(&p);
    p.warp = ladder[rung].warp;
    p.fill = ladder[rung].fill;
    /* a first frame to warm up; given up on at once when it is hopeless; then the timed frames, given up on after the first when it is */
    for (int f = 0; f < STEREO_DIBR_TIMED_FRAMES + 1; f++) {
        const double t0 = stereo_dibr_now_ms();
        if (stereo_dibr_render(t->d, &p, t->table, 256, &t->in, &t->dep, &t->l, &t->r) < 0)
            return -1;
        const double ft = stereo_dibr_now_ms() - t0;
        if (f == 0) {
            if (ft > 2 * STEREO_DIBR_GIVE_UP_MS) {
                ms[count++] = ft;
                break;
            }
            continue;
        }
        ms[count++] = ft;
        if (count == 1 && ft > STEREO_DIBR_GIVE_UP_MS)
            break;
    }
    *p95 = count ? stereo_dibr_p95(ms, count) : -1.0;
    return 0;
}

int stereo_dibr_measure_cpu(int width, int height, double fps, double *p95_ms, int n, int *measured)
{
    char key[160];
    if (measured)
        *measured = 0;
    if (n != NB_WAYS || !p95_ms || width < 2 || height < 2 || width > 16384 || height > 16384 || !(fps > 0.0))
        return -1;
    cpu_key(key, sizeof(key));
    for (int i = 0; i < n; i++)
        p95_ms[i] = -1.0;
    stereo_dibr_cache_load(key, width, height, p95_ms, n);

    const size_t luma = (size_t)width * height, chroma = (size_t)((width + 1) / 2) * ((height + 1) / 2);
    const int cw = (width + 1) / 2;
    unsigned char *rgba = malloc(luma * 4), *depth = malloc(luma), *planes = malloc(luma + 2 * chroma);
    unsigned char *outl = malloc(luma + 2 * chroma), *outr = malloc(luma + 2 * chroma);
    float *table = malloc(256 * sizeof(float));
    struct cpu_timing t = { stereo_dibr_alloc(), table };
    int ok = rgba && depth && planes && outl && outr && table && t.d;
    int timed = 0;
    if (ok) {
        stereo_dibr_synthetic(width, height, rgba, depth);
        /* the picture as a player has it: YUV 4:2:0 planes (the luma from the green, neutral chroma) */
        for (size_t i = 0; i < luma; i++)
            planes[i] = rgba[4 * i + 1];
        memset(planes + luma, 128, 2 * chroma);
        stereo_dibr_timing_table(width, table);
        t.dep = (struct stereo_dibr_depth){ depth, width, 1, width, height };
        t.in = (struct stereo_dibr_picture){ width, height, 3, { { planes, width, 1, 0, 0, 0 }, { planes + luma, cw, 1, 1, 1, 128 }, { planes + luma + chroma, cw, 1, 1, 1, 128 } } };
        t.l = t.r = t.in;
        t.l.plane[0].data = outl;
        t.l.plane[1].data = outl + luma;
        t.l.plane[2].data = outl + luma + chroma;
        t.r.plane[0].data = outr;
        t.r.plane[1].data = outr + luma;
        t.r.plane[2].data = outr + luma + chroma;
        ok = stereo_dibr_time_ladder(fps, time_one_cpu, &t, p95_ms, n, &timed) == 0;
    }
    stereo_dibr_free(t.d);
    free(rgba);
    free(depth);
    free(planes);
    free(table);
    free(outl);
    free(outr);
    if (!ok)
        return -1;
    if (timed)
        stereo_dibr_cache_store(key, width, height, p95_ms, n);
    if (measured)
        *measured = timed > 0;
    return 0;
}
