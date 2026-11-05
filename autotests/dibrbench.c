/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/*
 * Which way of drawing the two views from a picture and its depth map is the best default? Measured here, on scenes whose
 * right answer is known: layered textured shapes in front of a sloped textured background, seen by two eyes. Every eye's
 * image is evaluated analytically (no resampling), so the two true views, and the exact depth map of the picture, are
 * known. Each way of drawing is then scored against the true views:
 *   all   the mean absolute error over every pixel of both eyes (0 to 255);
 *   holes the mean absolute error where the picture shows nothing (the places a plain forward warp leaves as holes),
 *         which is where the ways of filling differ.
 * Scenes: exact depth; depth with noise (an estimated or compressed depth map); several depth budgets.
 * The default of libstereo-dibr (stereo_dibr_options_default()) must be the best of the ways measured, or tie with it:
 * the program exits 1 when it is not, and prints the table either way. Given a folder, it writes the views of the
 * default and of every way as PPM files, the true views beside them.
 *
 *   dibrbench [FOLDER]
 */

#include "stereo-dibr.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define W 960
#define H 540
#define NLAYERS 5

struct layer {
    double cx, cy, rx, ry; /* an ellipse in the middle frame */
    double nearness; /* the depth sample as a fraction of the largest */
    unsigned seed;
    int ramp; /* the background: covers everything; its depth rises with y */
};

struct scene {
    struct layer layer[NLAYERS];
    int n;
    float table[256];
    int sample[NLAYERS]; /* the 8-bit depth sample of each layer (the ramp's at its top row) */
};

static double hash(unsigned x, unsigned y, unsigned s)
{
    unsigned h = x * 374761393u + y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (double)((h ^ (h >> 16)) & 0xffff) / 65535.0;
}

/* smooth value noise: a texture with detail at several scales, as a photograph has */
static double noise(double x, double y, unsigned seed)
{
    double sum = 0, amp = 0.5, norm = 0;
    double sx = x, sy = y;
    for (int o = 0; o < 4; ++o) {
        double fx = floor(sx), fy = floor(sy), tx = sx - fx, ty = sy - fy;
        tx = tx * tx * (3 - 2 * tx);
        ty = ty * ty * (3 - 2 * ty);
        double a = hash((unsigned)(int)fx, (unsigned)(int)fy, seed + o), b = hash((unsigned)(int)fx + 1, (unsigned)(int)fy, seed + o);
        double c = hash((unsigned)(int)fx, (unsigned)(int)fy + 1, seed + o), d = hash((unsigned)(int)fx + 1, (unsigned)(int)fy + 1, seed + o);
        sum += amp * ((a * (1 - tx) + b * tx) * (1 - ty) + (c * (1 - tx) + d * tx) * ty);
        norm += amp;
        amp *= 0.5;
        sx *= 2.3;
        sy *= 2.3;
    }
    return sum / norm;
}

static void colour(const struct layer *l, double x, double y, double *rgb)
{
    double s = l->ramp ? 1 / 40.0 : 1 / 14.0;
    double a = noise(x * s, y * s, l->seed), b = noise(x * s + 31.7, y * s - 12.3, l->seed + 7), c = noise(x * s - 8.1, y * s + 55.9, l->seed + 13);
    rgb[0] = 40 + 200 * a;
    rgb[1] = 40 + 200 * b;
    rgb[2] = 40 + 200 * c;
}

static int covers(const struct layer *l, double x, double y)
{
    if (l->ramp)
        return 1;
    double dx = (x - l->cx) / l->rx, dy = (y - l->cy) / l->ry;
    return dx * dx + dy * dy <= 1.0;
}

/* the sample of a layer at a row (the ramp's depth rises with y) */
static int layer_sample(const struct layer *l, double y)
{
    double n = l->ramp ? l->nearness * (0.4 + 0.6 * y / H) : l->nearness;
    int v = (int)lround(n * 255.0);
    return v < 0 ? 0 : v > 255 ? 255 : v;
}

/* what eye sees at (x, y) of its frame: eye -1 left, +1 right, 0 the middle; the layer, nearest first */
static int visible(const struct scene *sc, int eye, double x, double y, double *xm)
{
    int best = -1, bestv = -1;
    double bx = 0;
    for (int i = 0; i < sc->n; ++i) {
        int v = layer_sample(&sc->layer[i], y);
        double p = sc->table[v];
        /* x_left = x_middle - p / 2, x_right = x_middle + p / 2 */
        double m = x - eye * p / 2.0;
        if (covers(&sc->layer[i], m, y) && v > bestv) {
            best = i;
            bestv = v;
            bx = m;
        }
    }
    *xm = bx;
    return best;
}

static void make_scene(struct scene *sc, unsigned seed, double range)
{
    struct stereo_depth_convention c;
    memset(&c, 0, sizeof(c));
    c.kind = STEREO_DEPTH_NEARNESS;
    c.bits = 8;
    c.zero = 0.5;
    c.range = range;
    stereo_depth_parallax_table(&c, W, sc->table);
    sc->n = NLAYERS;
    sc->layer[0] = (struct layer){ 0, 0, 0, 0, 0.35, seed + 1, 1 };
    for (int i = 1; i < NLAYERS; ++i) {
        double r = hash(seed, i, 1);
        sc->layer[i].cx = W * (0.15 + 0.7 * hash(seed, i, 2));
        sc->layer[i].cy = H * (0.25 + 0.55 * hash(seed, i, 3));
        sc->layer[i].rx = 50 + 110 * r;
        sc->layer[i].ry = 45 + 80 * hash(seed, i, 4);
        sc->layer[i].nearness = 0.5 + 0.45 * (double)i / NLAYERS + 0.02 * hash(seed, i, 5);
        sc->layer[i].seed = seed * 17 + i * 101;
        sc->layer[i].ramp = 0;
    }
}

/* The picture of an eye as planar RGB, and its exact depth map. */
struct image {
    unsigned char *p[3];
    unsigned char *depth;
};

static struct image alloc_image(int depth)
{
    struct image im;
    for (int c = 0; c < 3; ++c)
        im.p[c] = calloc(W * H, 1);
    im.depth = depth ? calloc(W * H, 1) : NULL;
    return im;
}

static void free_image(struct image *im)
{
    for (int c = 0; c < 3; ++c)
        free(im->p[c]);
    free(im->depth);
}

static void render_eye(const struct scene *sc, int eye, struct image *im)
{
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            double xm, rgb[3] = { 0, 0, 0 };
            int i = visible(sc, eye, x + 0.5, y + 0.5, &xm);
            if (i >= 0) {
                colour(&sc->layer[i], xm, y + 0.5, rgb);
                if (im->depth)
                    im->depth[y * W + x] = (unsigned char)layer_sample(&sc->layer[i], y + 0.5);
            }
            for (int c = 0; c < 3; ++c)
                im->p[c][y * W + x] = (unsigned char)lround(rgb[c]);
        }
}

/* an estimated depth map is not exact: noise of a few levels, and edges that are a pixel or two off */
static void damage_depth(unsigned char *depth, unsigned seed, int amplitude)
{
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            int v = depth[y * W + x] + (int)lround((hash(x, y, seed) - 0.5) * 2 * amplitude);
            depth[y * W + x] = (unsigned char)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
}

static struct stereo_dibr_params *none_params(struct stereo_dibr_params *p)
{
    p->fill = STEREO_DIBR_FILL_NONE;
    return p;
}

struct way {
    int on_middle; /* the picture is the middle eye, drawn as if it were the left one (the wrong provenance) */
    const char *name;
    struct stereo_dibr_params p;
};

static void picture_of(struct image *im, struct stereo_dibr_picture *pic, int sentinel)
{
    memset(pic, 0, sizeof(*pic));
    pic->width = W;
    pic->height = H;
    pic->nb_planes = 3;
    for (int c = 0; c < 3; ++c) {
        pic->plane[c].data = im->p[c];
        pic->plane[c].stride = W;
        pic->plane[c].bytes = 1;
        pic->plane[c].hole = sentinel ? (c == 1 ? 0 : 255) : 0; /* magenta */
    }
}

/* Draw the views of picture with depth and read them into images. */
static void draw(const struct scene *sc, const struct stereo_dibr_params *p, struct image *pic, const unsigned char *depth, struct image *left, struct image *right,
    int sentinel)
{
    struct stereo_dibr_picture in, l, r;
    picture_of(pic, &in, 0);
    picture_of(left, &l, sentinel);
    picture_of(right, &r, sentinel);
    struct stereo_dibr_depth d = { depth, W, 1, W, H };
    stereo_dibr *dibr = stereo_dibr_alloc();
    stereo_dibr_render(dibr, p, sc->table, 256, &in, &d, &l, &r);
    stereo_dibr_free(dibr);
}

static double mae(const struct image *a, const struct image *b, const unsigned char *mask)
{
    double sum = 0;
    long n = 0;
    for (long i = 0; i < (long)W * H; ++i) {
        if (mask && !mask[i])
            continue;
        for (int c = 0; c < 3; ++c)
            sum += abs((int)a->p[c][i] - (int)b->p[c][i]);
        n += 3;
    }
    return n ? sum / n : 0;
}

static void write_ppm(const char *dir, const char *name, const struct image *l, const struct image *r)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    if (!f)
        return;
    fprintf(f, "P6\n%d %d\n255\n", W * 2, H);
    for (int y = 0; y < H; ++y) {
        for (int e = 0; e < 2; ++e) {
            const struct image *im = e ? r : l;
            for (int x = 0; x < W; ++x)
                for (int c = 0; c < 3; ++c)
                    fputc(im->p[c][y * W + x], f);
        }
    }
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : NULL;
    struct way ways[26];
    int nways = 0;
    static const char *warp_name[] = { "forward", "backward", "layered" };
    static const char *fill_name[] = { "background", "depth-aided", "telea", "none", "pyramid" };
    static const char *view_name[] = { "left", "centre" };
    for (int view = 0; view < 2; ++view)
        for (int warp = 0; warp < 3; ++warp)
            for (int fill = 0; fill < 5; ++fill)
                for (int smooth = 0; smooth < 2; ++smooth) {
                    if (view == 1 && warp != 0)
                        continue; /* the picture as the middle with the other warps is a matrix of its own: forward only */
                    if (fill == 3 && (smooth || view || warp))
                        continue; /* holes left black: no real way, a control */
                    if (smooth && (warp || fill))
                        continue; /* smoothing is measured on the forward warp with the default fill */
                    struct way *w = &ways[nways++];
                    memset(w, 0, sizeof(*w));
                    stereo_dibr_params_default(&w->p);
                    w->p.view = view;
                    w->p.warp = warp;
                    w->p.fill = fill;
                    w->p.smooth = smooth;
                    static char names[26][80];
                    snprintf(names[nways - 1], 80, "%s/%s/%s%s", view_name[view], warp_name[warp], fill_name[fill], smooth ? "/smooth" : "");
                    w->name = names[nways - 1];
                }
    /* what a wrong "which eye is the picture" costs: the middle picture taken for the left eye */
    {
        struct way *w = &ways[nways++];
        memset(w, 0, sizeof(*w));
        stereo_dibr_params_default(&w->p);
        w->p.warp = STEREO_DIBR_WARP_LAYERED;
        w->p.fill = STEREO_DIBR_FILL_DEPTH_AIDED;
        w->on_middle = 1;
        w->name = "left/layered/depth-aided, picture is the middle";
    }
    /* the default of the library */
    struct stereo_dibr_options def;
    stereo_dibr_options_default(&def);
    struct stereo_depth_convention conv;
    struct stereo_dibr_params defp;
    stereo_dibr_options_apply(&def, &conv, &defp);

    /* the scenes: three seeds, three depth budgets (pixels of a view 1920 wide), exact depth and damaged depth */
    static const struct { unsigned seed; double range; int damage; const char *name; } scenes[] = {
        { 11, 58, 0, "A exact, 58" },     { 23, 58, 0, "B exact, 58" },   { 37, 58, 0, "C exact, 58" },
        { 11, 30, 0, "A exact, 30" },     { 23, 90, 0, "B exact, 90" },
        { 11, 58, 6, "A noisy, 58" },     { 23, 58, 6, "B noisy, 58" },   { 37, 58, 10, "C noisier, 58" },
    };
    int nscenes = (int)(sizeof(scenes) / sizeof(scenes[0]));
    double all[26][9], holes[26][9], ms[26] = { 0 };
    struct image truthL = alloc_image(0), truthR = alloc_image(0), truthM = alloc_image(1), picL = alloc_image(1), holeL = alloc_image(0), holeR = alloc_image(0),
                 outL = alloc_image(0), outR = alloc_image(0);
    unsigned char *mask = malloc(W * H);
    for (int s = 0; s < nscenes; ++s) {
        struct scene sc;
        make_scene(&sc, scenes[s].seed, scenes[s].range);
        render_eye(&sc, -1, &truthL);
        render_eye(&sc, +1, &truthR);
        render_eye(&sc, 0, &truthM);
        render_eye(&sc, -1, &picL);
        if (scenes[s].damage) {
            damage_depth(picL.depth, scenes[s].seed, scenes[s].damage);
            damage_depth(truthM.depth, scenes[s].seed, scenes[s].damage);
        }
        /* the places the picture shows nothing of: the holes of a plain forward warp of the left eye */
        struct stereo_dibr_params none;
        stereo_dibr_params_default(&none);
        none.fill = STEREO_DIBR_FILL_NONE;
        draw(&sc, &none, &picL, picL.depth, &holeL, &holeR, 1);
        for (long i = 0; i < (long)W * H; ++i)
            mask[i] = holeR.p[0][i] == 255 && holeR.p[1][i] == 0 && holeR.p[2][i] == 255;
        for (int k = 0; k < nways; ++k) {
            struct image *pic = ways[k].p.view == STEREO_DIBR_VIEW_CENTER || ways[k].on_middle ? &truthM : &picL;
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0);
            draw(&sc, &ways[k].p, pic, pic->depth, &outL, &outR, 0);
            clock_gettime(CLOCK_MONOTONIC, &t1);
            ms[k] += ((t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6) / nscenes;
            all[k][s] = (mae(&outL, &truthL, NULL) + mae(&outR, &truthR, NULL)) / 2;
            holes[k][s] = (mae(&outL, &truthL, mask) + mae(&outR, &truthR, mask)) / 2;
            if (dir && s == 0) {
                char name[96];
                snprintf(name, sizeof(name), "way-%02d", k);
                write_ppm(dir, name, &outL, &outR);
            }
        }
        if (dir && s == 0)
            write_ppm(dir, "truth", &truthL, &truthR);
    }
    printf("%-30s", "way (mean absolute error: all / holes)");
    for (int s = 0; s < nscenes; ++s)
        printf(" %14s", scenes[s].name);
    printf(" %9s %9s %9s\n", "mean all", "mean hole", "per frame");
    double best_all = 1e9, best_hole = 1e9;
    int is_default[26] = { 0 };
    int default_index = -1;
    for (int k = 0; k < nways; ++k) {
        double ma = 0, mh = 0;
        printf("%-30s", ways[k].name);
        for (int s = 0; s < nscenes; ++s) {
            printf("  %5.2f /%6.2f", all[k][s], holes[k][s]);
            ma += all[k][s];
            mh += holes[k][s];
        }
        ma /= nscenes;
        mh /= nscenes;
        printf("  %8.3f  %8.3f  %6.1f ms", ma, mh, ms[k]);
        is_default[k] = !ways[k].on_middle && ways[k].p.view == defp.view && ways[k].p.warp == defp.warp && ways[k].p.fill == defp.fill && ways[k].p.smooth == defp.smooth;
        if (is_default[k]) {
            default_index = k;
            printf("  <- default");
        }
        printf("\n");
        all[k][nscenes] = ma;
        holes[k][nscenes] = mh;
        if (ways[k].p.fill != STEREO_DIBR_FILL_NONE && !ways[k].on_middle && ways[k].p.view == STEREO_DIBR_VIEW_LEFT) {
            if (ma < best_all)
                best_all = ma;
            if (mh < best_hole)
                best_hole = mh;
        }
    }
    /* the strength: the picture's parallax budget. The cost of a deeper picture is the area the picture does not show; the limit
     * of comfort is about one degree of visual angle (a viewer three picture heights from the screen): the default's parallax
     * must be within five hundredths of a degree of it. Pixels of a view 1920 wide, 1080 high. */
    printf("\nstrength (largest parallax either side, pixels of a view 1920 wide): visual angle at 3 picture heights, area not shown\n");
    {
        struct scene sc;
        struct stereo_dibr_params best = defp;
        double angle_default = atan(def.strength / (3.0 * 1080.0)) * 180.0 / M_PI;
        static const double sweep[] = { 20, 40, 58, 80, 100, 120 };
        for (int i = 0; i < 6; ++i) {
            make_scene(&sc, 11, sweep[i]);
            render_eye(&sc, -1, &picL);
            draw(&sc, none_params(&best), &picL, picL.depth, &holeL, &holeR, 1);
            long n = 0;
            for (long j = 0; j < (long)W * H; ++j)
                n += holeR.p[0][j] == 255 && holeR.p[1][j] == 0 && holeR.p[2][j] == 255;
            printf("  %5.0f px  %.2f degrees  %5.2f %% of the right eye is not shown by the picture\n", sweep[i], atan(sweep[i] / (3.0 * 1080.0)) * 180.0 / M_PI, 100.0 * n / (W * H));
        }
        printf("  default %.0f px = %.3f degrees\n", def.strength, angle_default);
        if (fabs(angle_default - 1.0) > 0.05) {
            printf("the default strength is not about one degree\n");
            return 1;
        }
    }
    if (default_index < 0) {
        printf("the library's default is not among the ways measured\n");
        return 1;
    }
    /* the ladder of the ways a video chooses from (stereo_dibr_ways()): the best first, the error not decreasing from one way to the
     * next beyond the noise of the bench, and the first of them the default */
    {
        struct stereo_dibr_way ladder[8];
        const int nl = stereo_dibr_ways(ladder, 8);
        double previous = -1.0;
        int ranking_ok = ladder[0].warp == defp.warp && ladder[0].fill == defp.fill;
        printf("\nthe ladder of a video (best first): mean error over both eyes\n");
        for (int i = 0; i < nl; ++i) {
            int found = -1;
            for (int k = 0; k < nways; ++k)
                if (!ways[k].on_middle && ways[k].p.view == STEREO_DIBR_VIEW_LEFT && ways[k].p.smooth == STEREO_DIBR_SMOOTH_OFF && ways[k].p.warp == ladder[i].warp
                    && ways[k].p.fill == ladder[i].fill)
                    found = k;
            if (found < 0) {
                printf("  rung %d is not a way measured\n", i);
                ranking_ok = 0;
                continue;
            }
            printf("  %d  %-28s %.3f  holes %.3f\n", i, ways[found].name, all[found][nscenes], holes[found][nscenes]);
            if (all[found][nscenes] < previous - 0.02) {
                printf("  rung %d has a lower error than the rung before it: the ladder is not in order\n", i);
                ranking_ok = 0;
            }
            previous = all[found][nscenes] > previous ? all[found][nscenes] : previous;
        }
        /* every way that costs no less than a rung and has more error than it is left out on purpose; the best way of all is the first rung */
        if (!ranking_ok) {
            printf("the ladder does not follow the measured ranking\n");
            return 1;
        }
    }
    int ok = all[default_index][nscenes] <= best_all + 0.02 && holes[default_index][nscenes] <= best_hole + 0.05;
    printf("default: mean all %.3f (best %.3f), mean holes %.3f (best %.3f): %s\n", all[default_index][nscenes], best_all, holes[default_index][nscenes], best_hole,
        ok ? "the best, or within the noise of it" : "NOT the best");
    return ok ? 0 : 1;
}
