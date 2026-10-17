/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/*
 * The DIBR core (stereo-dibr.h). A view is drawn in three steps: a map that tells, for each pixel of the view on the
 * picture's first plane, which position of the picture it shows (or that it shows nothing, a hole) and the parallax of
 * what it shows; every plane sampled through the map at its own subsampling; the holes filled on every plane. Rows
 * are independent until the fill, so the GLSL version runs the same map per row.
 *
 * Positions are continuous: picture pixel x covers [x, x + 1), its centre at x + 0.5. A view of the right eye shows a
 * point of parallax p at x + p, the left eye's of a middle picture at x - p / 2: the shift s * p, s being 1, -1/2 or
 * 1/2. Of two points that land on the same pixel the nearer one, of smaller parallax, is kept (a Z-buffer).
 */

#include "stereo-dibr.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define HOLE (-1.0f)
/* A step of parallax larger than this between neighbours is a depth edge, for the layered warp (pixels of the view). */
#define EDGE_STEP 1.0f
/* The width of the boundary layer at a view 1920 wide, pixels. */
#define BOUNDARY_1920 3.0

/* Telea's states; BLOCKED is a known pixel nearer than the hole beside it, which neither feeds nor starts the march. */
enum { KNOWN = 0, BAND = 1, INSIDE = 2, BLOCKED = 3 };

struct stereo_dibr {
    size_t size, row_size, depth_size, plane_size;
    float max_stretch;          /* how much wider than a pixel its splat may be before the gap is a hole */
    float *par;                 /* the picture's parallax */
    float *tmp;                 /* the smoothing's scratch */
    float *map, *zt;            /* the view (or its main layer): position shown, parallax shown */
    float *bmap, *bzt, *balpha; /* the layered warp's boundary layer */
    float *s0, *s1, *mp, *alpha; /* per row: the main layer's spans and parallax, the boundary's opacity */
    uint8_t *band;
    int *dist;
    float *dpar;                /* the parallax at the depth map's size */
    float *val, *vz, *bval, *next, *tt, *bgz;
    uint8_t *hole, *state;
    float *lpar, *lmap, *lzt, *lval; /* the second layer: its parallax, its view, its samples */
    uint8_t *lhole;
    int *heap, *heap_pos, *list;
};

/* Grows d->field to need elements; 0, or -1 when out of memory (the old buffer kept, freed with the context). */
#define RESERVE(field, need) ((grown = realloc(d->field, (need) * sizeof(*d->field))) ? (d->field = grown, 0) : -1)

stereo_dibr *stereo_dibr_alloc(void)
{
    return calloc(1, sizeof(stereo_dibr));
}

void stereo_dibr_free(stereo_dibr *d)
{
    if (!d)
        return;
    free(d->par);
    free(d->tmp);
    free(d->map);
    free(d->zt);
    free(d->bmap);
    free(d->bzt);
    free(d->balpha);
    free(d->s0);
    free(d->s1);
    free(d->mp);
    free(d->alpha);
    free(d->band);
    free(d->dist);
    free(d->dpar);
    free(d->val);
    free(d->vz);
    free(d->bval);
    free(d->next);
    free(d->tt);
    free(d->bgz);
    free(d->hole);
    free(d->state);
    free(d->heap);
    free(d->heap_pos);
    free(d->list);
    free(d->lpar);
    free(d->lmap);
    free(d->lzt);
    free(d->lval);
    free(d->lhole);
    free(d);
}

void stereo_dibr_params_default(struct stereo_dibr_params *p)
{
    p->view = STEREO_DIBR_VIEW_LEFT;
    p->warp = STEREO_DIBR_WARP_FORWARD;
    p->fill = STEREO_DIBR_FILL_BACKGROUND;
    p->smooth = STEREO_DIBR_SMOOTH_OFF;
    p->sigma_h = 10.0;
    p->sigma_v = 30.0;
    p->radius = 5;
}

static int buffers(stereo_dibr *d, size_t size, size_t row, size_t depth, size_t plane)
{
    void *grown;

    if (size > d->size) {
        if (RESERVE(par, size) || RESERVE(tmp, size) || RESERVE(map, size) || RESERVE(zt, size) || RESERVE(bmap, size)
            || RESERVE(bzt, size) || RESERVE(balpha, size) || RESERVE(lpar, size) || RESERVE(lmap, size) || RESERVE(lzt, size))
            return -1;
        d->size = size;
    }
    if (row > d->row_size) {
        if (RESERVE(s0, row) || RESERVE(s1, row) || RESERVE(mp, row) || RESERVE(alpha, row) || RESERVE(band, row)
            || RESERVE(dist, row))
            return -1;
        d->row_size = row;
    }
    if (depth > d->depth_size) {
        if (RESERVE(dpar, depth))
            return -1;
        d->depth_size = depth;
    }
    if (plane > d->plane_size) {
        if (RESERVE(val, plane) || RESERVE(vz, plane) || RESERVE(bval, plane) || RESERVE(next, plane) || RESERVE(tt, plane) || RESERVE(bgz, plane)
            || RESERVE(hole, plane) || RESERVE(state, plane) || RESERVE(heap, plane) || RESERVE(heap_pos, plane)
            || RESERVE(list, plane) || RESERVE(lval, plane) || RESERVE(lhole, plane))
            return -1;
        d->plane_size = plane;
    }
    return 0;
}

static inline unsigned get_sample(const void *row, int bytes, int x)
{
    return bytes == 1 ? ((const uint8_t *)row)[x] : ((const uint16_t *)row)[x];
}

static inline void put_sample(void *row, int bytes, int x, float v)
{
    const float top = bytes == 1 ? 255.0f : 65535.0f;
    v = v < 0.0f ? 0.0f : v > top ? top : v;
    if (bytes == 1)
        ((uint8_t *)row)[x] = (uint8_t)(v + 0.5f);
    else
        ((uint16_t *)row)[x] = (uint16_t)(v + 0.5f);
}

static inline int clampi(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

/* A sample with no depth (NaN in the table) takes the farther of its nearest valid neighbours along the row, the
 * background it most likely belongs to; a row with none takes the nearest row above with depth, else below, else the
 * screen (glsl/fill_row.frag and fill_column.frag). */
static void fill_invalid(float *par, int w, int h)
{
    for (int y = 0; y < h; y++) {
        float *row = par + y * w;
        for (int x = 0; x < w;) {
            if (!isnan(row[x])) {
                x++;
                continue;
            }
            const int a = x;
            while (x < w && isnan(row[x]))
                x++;
            const int l = a - 1, r = x;
            if (l < 0 && r >= w)
                continue;
            const float z = l < 0 ? row[r] : r >= w ? row[l] : fmaxf(row[l], row[r]);
            for (int t = a; t < x; t++)
                row[t] = z;
        }
    }
    for (int y = 0; y < h; y++) {
        float *row = par + y * w;
        if (!isnan(row[0]))
            continue;
        int from = -1;
        for (int k = y - 1; k >= 0 && from < 0; k--)
            if (!isnan(par[k * w]))
                from = k;
        for (int k = y + 1; k < h && from < 0; k++)
            if (!isnan(par[k * w]))
                from = k;
        for (int x = 0; x < w; x++)
            row[x] = from < 0 ? 0.0f : par[from * w + x];
    }
}

/* The parallax of every picture pixel into out: the table read at the depth map's size, then scaled bilinearly (the
 * nearest sample where a neighbour holds no depth). Samples with no depth are filled when fill is set, and stay NaN
 * otherwise. */
static void depth_to_parallax(stereo_dibr *d, const float *table, int table_size, const struct stereo_dibr_depth *depth, int w, int h,
    float *out, int fill)
{
    const int dw = depth->width, dh = depth->height;

    for (int y = 0; y < dh; y++) {
        const uint8_t *row = (const uint8_t *)depth->data + y * depth->stride;
        for (int x = 0; x < dw; x++) {
            unsigned s = get_sample(row, depth->bytes, x);
            d->dpar[y * dw + x] = table[s < (unsigned)table_size ? s : (unsigned)table_size - 1];
        }
    }
    if (fill)
        fill_invalid(d->dpar, dw, dh);

    if (dw == w && dh == h) {
        memcpy(out, d->dpar, sizeof(float) * w * h);
        return;
    }
    for (int y = 0; y < h; y++) {
        float fy = (y + 0.5f) * dh / h - 0.5f;
        int y0 = (int)floorf(fy);
        float wy = fy - y0;
        int ya = clampi(y0, 0, dh - 1), yb = clampi(y0 + 1, 0, dh - 1);
        for (int x = 0; x < w; x++) {
            float fx = (x + 0.5f) * dw / w - 0.5f;
            int x0 = (int)floorf(fx);
            float wx = fx - x0;
            int xa = clampi(x0, 0, dw - 1), xb = clampi(x0 + 1, 0, dw - 1);
            const float aa = d->dpar[ya * dw + xa], ab = d->dpar[ya * dw + xb], ba = d->dpar[yb * dw + xa], bb = d->dpar[yb * dw + xb];
            if (isnan(aa) || isnan(ab) || isnan(ba) || isnan(bb)) {
                out[y * w + x] = d->dpar[(wy < 0.5f ? ya : yb) * dw + (wx < 0.5f ? xa : xb)];
                continue;
            }
            const float top = aa * (1.0f - wx) + ab * wx, bottom = ba * (1.0f - wx) + bb * wx;
            out[y * w + x] = top * (1.0f - wy) + bottom * wy;
        }
    }
}

/* One direction of a separable Gaussian, from src to dst; step 1 runs along rows, step w along columns. */
static void gaussian_pass(const float *src, float *dst, int w, int h, double sigma, int along_rows)
{
    if (sigma < 0.01) {
        memcpy(dst, src, sizeof(float) * w * h);
        return;
    }
    const int r = (int)ceil(3.0 * sigma);
    float *k = malloc(sizeof(float) * (2 * r + 1));
    if (!k) {
        memcpy(dst, src, sizeof(float) * w * h);
        return;
    }
    double sum = 0.0;
    for (int i = -r; i <= r; i++)
        sum += k[i + r] = (float)exp(-0.5 * i * i / (sigma * sigma));
    for (int i = 0; i <= 2 * r; i++)
        k[i] = (float)(k[i] / sum);

    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float acc = 0.0f;
            for (int i = -r; i <= r; i++) {
                int sx = along_rows ? clampi(x + i, 0, w - 1) : x;
                int sy = along_rows ? y : clampi(y + i, 0, h - 1);
                acc += k[i + r] * src[sy * w + sx];
            }
            dst[y * w + x] = acc;
        }
    free(k);
}

/*
 * Splats a row of picture pixels into a row of the view. Pixel x, of parallax p[x], covers the picture's positions
 * [s0[x], s1[x]) (x and x + 1 when s0 is null) and lands on [x + s p[x], x + 1 + s p[x + 1]): spread over it while it
 * is no more than max_stretch times as wide as the pixel, beyond which the gap is a disocclusion and the pixel lands
 * on one view pixel. Only pixels with take[x] are splatted when take is not null; alpha_in, when not null, is carried
 * to alpha_out.
 */
static void splat_row(float max_stretch, int w, float s, const float *p, const float *s0, const float *s1, const uint8_t *take,
    const float *alpha_in, float *map, float *zt, float *alpha_out)
{
    for (int t = 0; t < w; t++) {
        map[t] = HOLE;
        zt[t] = FLT_MAX;
    }
    for (int x = 0; x < w; x++) {
        if ((take && !take[x]) || isnan(p[x]))
            continue;
        const float pn = x + 1 < w && (!take || take[x + 1]) && !isnan(p[x + 1]) ? p[x + 1] : p[x];
        const float a = x + s * p[x], b = x + 1 + s * pn;
        const float u0 = s0 ? s0[x] : (float)x, u1 = s1 ? s1[x] : (float)(x + 1);

        if (b > a && b - a <= max_stretch) {
            const int t0 = (int)ceilf(a - 0.5f), t1 = (int)ceilf(b - 0.5f);
            for (int t = t0 < 0 ? 0 : t0; t < t1 && t < w; t++) {
                const float f = (t + 0.5f - a) / (b - a);
                const float z = p[x] + f * (pn - p[x]);
                if (z < zt[t]) {
                    zt[t] = z;
                    map[t] = u0 + f * (u1 - u0);
                    if (alpha_out)
                        alpha_out[t] = alpha_in[x];
                }
            }
        } else {
            const int t = (int)floorf(a + 0.5f);
            if (t >= 0 && t < w && p[x] < zt[t]) {
                zt[t] = p[x];
                map[t] = 0.5f * (u0 + u1);
                if (alpha_out)
                    alpha_out[t] = alpha_in[x];
            }
        }
    }
}

/* The farther of the valid neighbours of each run of holes of a row, by parallax: what a hole would show. */
static void background_parallax_row(int w, const float *map, float *zt)
{
    for (int x = 0; x < w;) {
        if (map[x] >= 0.0f) {
            x++;
            continue;
        }
        const int a = x;
        while (x < w && map[x] < 0.0f)
            x++;
        const int l = a - 1, r = x;
        float z;
        if (l < 0 && r >= w)
            z = 0.0f;
        else if (l < 0)
            z = zt[r];
        else if (r >= w)
            z = zt[l];
        else
            z = fmaxf(zt[l], zt[r]);
        for (int t = a; t < x; t++)
            zt[t] = z;
    }
}

/* Forward warping: the picture splatted into the view. */
static void warp_forward_row(stereo_dibr *d, int w, float s, const float *p, float *map, float *zt)
{
    splat_row(d->max_stretch, w, s, p, NULL, NULL, NULL, NULL, map, zt, NULL);
    background_parallax_row(w, map, zt);
}

/* Backward warping: the parallax splatted into the view, and each view pixel then sampled from the picture where the
 * parallax it received says (McMillan's inverse mapping). */
static void warp_backward_row(stereo_dibr *d, int w, float s, const float *p, float *map, float *zt)
{
    splat_row(d->max_stretch, w, s, p, NULL, NULL, NULL, NULL, map, zt, NULL);
    background_parallax_row(w, map, zt);
    for (int t = 0; t < w; t++)
        if (map[t] >= 0.0f) {
            float u = t + 0.5f - s * zt[t];
            map[t] = u < 0.0f ? 0.0f : u > w ? (float)w : u;
        }
}

/*
 * Layered warping: at each depth edge the foreground's last pixels form a boundary layer, opaque inside and fading to
 * the edge, drawn over the main layer; in the main layer those pixels take the background beyond the edge, its
 * colour and parallax, so the background continues under the foreground's edge (Zitnick et al. 2004).
 */
static void warp_layered_row(stereo_dibr *d, int w, float s, const float *p, float *map, float *zt, float *bmap,
    float *bzt, float *balpha)
{
    const int width = (int)lround(BOUNDARY_1920 * w / 1920.0);
    const int band_w = width < 1 ? 1 : width;
    int *dist = d->dist;

    for (int x = 0; x < w; x++) {
        d->band[x] = 0;
        d->s0[x] = (float)x;
        d->s1[x] = (float)(x + 1);
        d->mp[x] = p[x];
        d->alpha[x] = 1.0f;
        dist[x] = band_w;
    }
    for (int x = 0; x + 1 < w; x++) {
        const float step = p[x + 1] - p[x];
        if (fabsf(step) <= EDGE_STEP)
            continue;
        /* the foreground is the nearer side, of smaller parallax */
        const int fg = step > 0.0f ? x : x + 1, bg = step > 0.0f ? x + 1 : x, dir = step > 0.0f ? -1 : 1;
        for (int k = 0; k < band_w; k++) {
            const int px = fg + dir * k;
            if (px < 0 || px >= w || k >= dist[px])
                break;
            if (fabsf(p[px] - p[fg]) > EDGE_STEP)
                break;
            dist[px] = k;
            d->band[px] = 1;
            d->alpha[px] = (k + 1.0f) / (band_w + 1.0f);
            d->s0[px] = d->s1[px] = bg + 0.5f;
            d->mp[px] = p[bg];
        }
    }
    splat_row(d->max_stretch, w, s, d->mp, d->s0, d->s1, NULL, NULL, map, zt, NULL);
    background_parallax_row(w, map, zt);
    splat_row(d->max_stretch, w, s, p, NULL, NULL, d->band, d->alpha, bmap, bzt, balpha);
}

/* Samples a plane of the picture through a map into val (holes marked), with the parallax shown in vz. */
static void sample_plane(const struct stereo_dibr_plane *src, int pw, int ph, int w, int h, const float *map,
    const float *zt, float *val, float *vz, uint8_t *hole)
{
    const float scale = 1.0f / (1 << src->log2_w);

    for (int yp = 0; yp < ph; yp++) {
        const int y = clampi(yp << src->log2_h, 0, h - 1);
        const uint8_t *row = (const uint8_t *)src->data + yp * src->stride;
        for (int i = 0; i < pw; i++) {
            const int x = clampi(i << src->log2_w, 0, w - 1);
            const float u = map[y * w + x];
            const int o = yp * pw + i;
            if (vz)
                vz[o] = zt[y * w + x];
            if (u < 0.0f) {
                hole[o] = 1;
                val[o] = 0.0f;
                continue;
            }
            float c = (u - 0.5f) * scale;
            if (c < 0.0f)
                c = 0.0f;
            if (c > pw - 1)
                c = (float)(pw - 1);
            const int i0 = (int)c;
            const int i1 = i0 + 1 < pw ? i0 + 1 : i0;
            const float f = c - i0;
            hole[o] = 0;
            val[o] = get_sample(row, src->bytes, i0) * (1.0f - f) + get_sample(row, src->bytes, i1) * f;
        }
    }
}

/* Background extension: each run of holes of a row takes its farther valid neighbour. */
static void fill_background(float *val, float *vz, uint8_t *hole, int pw, int ph)
{
    for (int y = 0; y < ph; y++) {
        float *v = val + y * pw, *z = vz + y * pw;
        uint8_t *m = hole + y * pw;
        for (int x = 0; x < pw;) {
            if (!m[x]) {
                x++;
                continue;
            }
            const int a = x;
            while (x < pw && m[x])
                x++;
            const int l = a - 1, r = x;
            int c;
            if (l < 0 && r >= pw)
                continue;
            else if (l < 0)
                c = r;
            else if (r >= pw)
                c = l;
            else
                c = z[l] >= z[r] ? l : r;
            for (int t = a; t < x; t++) {
                v[t] = v[c];
                z[t] = z[c];
                m[t] = 0;
            }
        }
    }
}

/* Whatever no fill reached (a row with nothing to extend): the row above, else the value of a hole. */
static void fill_rest(float *val, uint8_t *hole, int pw, int ph, unsigned hole_value)
{
    for (int y = 0; y < ph; y++)
        for (int x = 0; x < pw; x++) {
            const int o = y * pw + x;
            if (!hole[o])
                continue;
            val[o] = y > 0 && !hole[o - pw] ? val[o - pw] : (float)hole_value;
            hole[o] = 0;
        }
}

/* The parallax of what each hole uncovers: the farther valid neighbour of its run along the row. Known pixels keep
 * their own. Returns the number of holes, listed in list. */
static int hole_background(const float *vz, const uint8_t *hole, int pw, int ph, float *bgz, int *list)
{
    int n = 0;

    for (int y = 0; y < ph; y++)
        for (int x = 0; x < pw;) {
            const int o = y * pw;
            if (!hole[o + x]) {
                bgz[o + x] = vz[o + x];
                x++;
                continue;
            }
            const int a = x;
            while (x < pw && hole[o + x])
                x++;
            const int l = a - 1, r = x;
            const float z = l < 0 && r >= pw ? 0.0f : l < 0 ? vz[o + r] : r >= pw ? vz[o + l] : fmaxf(vz[o + l], vz[o + r]);
            for (int t = a; t < x; t++) {
                bgz[o + t] = z;
                if (list)
                    list[n++] = o + t;
            }
        }
    return n;
}

/*
 * Depth-aided: every hole is filled from the pixels around it that are as far as its background (the farther valid
 * neighbour of its run), never from the foreground, nearest ring first; what has no background around it is extended.
 */
static void fill_depth_aided(stereo_dibr *d, float *val, float *vz, uint8_t *hole, int pw, int ph, int radius)
{
    float *target = d->bgz;
    uint8_t *commit = d->state;
    int *list = d->list, n = hole_background(vz, hole, pw, ph, target, list);

    while (n > 0) {
        int filled = 0;
        for (int j = 0; j < n; j++) {
            const int o = list[j], x = o % pw, y = o / pw;
            int edge = 0;
            commit[o] = 0;
            for (int dy = -1; dy <= 1 && !edge; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    const int yy = y + dy, xx = x + dx;
                    if (yy >= 0 && yy < ph && xx >= 0 && xx < pw && !hole[yy * pw + xx]) {
                        edge = 1;
                        break;
                    }
                }
            if (!edge)
                continue;
            double acc = 0.0, wsum = 0.0;
            for (int dy = -radius; dy <= radius; dy++)
                for (int dx = -radius; dx <= radius; dx++) {
                    const int yy = y + dy, xx = x + dx;
                    if (yy < 0 || yy >= ph || xx < 0 || xx >= pw || (!dx && !dy))
                        continue;
                    const int q = yy * pw + xx;
                    if (hole[q] || target[q] < target[o] - EDGE_STEP)
                        continue;
                    const double wq = 1.0 / (dx * dx + dy * dy);
                    acc += wq * val[q];
                    wsum += wq;
                }
            if (wsum > 0.0) {
                d->next[o] = (float)(acc / wsum);
                commit[o] = 1;
            }
        }
        int m = 0;
        for (int j = 0; j < n; j++) {
            const int o = list[j];
            if (commit[o]) {
                val[o] = d->next[o];
                vz[o] = target[o];
                hole[o] = 0;
                filled++;
            } else {
                list[m++] = o;
            }
        }
        n = m;
        if (!filled)
            break;
    }
    fill_background(val, vz, hole, pw, ph);
}

/* A binary heap of plane positions by Telea's T. */
static void heap_swap(int *heap, int *pos, int a, int b)
{
    int t = heap[a];
    heap[a] = heap[b];
    heap[b] = t;
    pos[heap[a]] = a;
    pos[heap[b]] = b;
}

static void heap_push(int *heap, int *pos, int *n, const float *key, int item)
{
    int i = (*n)++;
    heap[i] = item;
    pos[item] = i;
    while (i > 0 && key[heap[(i - 1) / 2]] > key[heap[i]]) {
        heap_swap(heap, pos, i, (i - 1) / 2);
        i = (i - 1) / 2;
    }
}

static int heap_pop(int *heap, int *pos, int *n, const float *key)
{
    const int top = heap[0];
    heap_swap(heap, pos, 0, --*n);
    for (int i = 0;;) {
        int l = 2 * i + 1, r = l + 1, m = i;
        if (l < *n && key[heap[l]] < key[heap[m]])
            m = l;
        if (r < *n && key[heap[r]] < key[heap[m]])
            m = r;
        if (m == i)
            break;
        heap_swap(heap, pos, i, m);
        i = m;
    }
    return top;
}

/* The eikonal step of the fast marching method from two neighbours (Telea 2004, section 3). */
static float solve(const float *t, const uint8_t *state, int pw, int ph, int x1, int y1, int x2, int y2)
{
    const int k1 = x1 >= 0 && x1 < pw && y1 >= 0 && y1 < ph && state[y1 * pw + x1] <= BAND;
    const int k2 = x2 >= 0 && x2 < pw && y2 >= 0 && y2 < ph && state[y2 * pw + x2] <= BAND;
    float sol = 1.0e6f;

    if (k1 && k2) {
        const float t1 = t[y1 * pw + x1], t2 = t[y2 * pw + x2];
        if (fabsf(t1 - t2) >= 1.0f) {
            sol = 1.0f + fminf(t1, t2);
        } else {
            const float sr = sqrtf(2.0f - (t1 - t2) * (t1 - t2));
            float s = (t1 + t2 - sr) / 2.0f;
            if (s >= t1 && s >= t2)
                sol = s;
            else if ((s += sr) >= t1 && s >= t2)
                sol = s;
        }
    } else if (k1) {
        sol = 1.0f + t[y1 * pw + x1];
    } else if (k2) {
        sol = 1.0f + t[y2 * pw + x2];
    }
    return sol;
}

/* The gradient of f at (x, y) from the neighbours that are known or in the band. */
static void gradient(const float *f, const uint8_t *state, int pw, int ph, int x, int y, float *gx, float *gy)
{
    const int o = y * pw + x;
    const int l = x > 0 && state[o - 1] <= BAND, r = x + 1 < pw && state[o + 1] <= BAND;
    const int u = y > 0 && state[o - pw] <= BAND, dn = y + 1 < ph && state[o + pw] <= BAND;

    *gx = l && r ? (f[o + 1] - f[o - 1]) * 0.5f : r ? f[o + 1] - f[o] : l ? f[o] - f[o - 1] : 0.0f;
    *gy = u && dn ? (f[o + pw] - f[o - pw]) * 0.5f : dn ? f[o + pw] - f[o] : u ? f[o] - f[o - pw] : 0.0f;
}

/* Telea's inpainting: the holes filled by fast marching inward from their boundary, each pixel from the known pixels
 * within the radius, weighted by direction, distance and level set distance (Telea 2004, equations 4 to 8). A hole
 * of a view uncovers background, so only pixels as far as the hole's background feed it, as in the depth-aided fill. */
static void fill_telea(stereo_dibr *d, float *val, float *vz, uint8_t *hole, int pw, int ph, int radius)
{
    float *t = d->tt, *target = d->bgz;
    uint8_t *state = d->state;
    int *heap = d->heap, *pos = d->heap_pos, n = 0;
    const int r2 = radius * radius;

    hole_background(vz, hole, pw, ph, target, NULL);
    for (int o = 0; o < pw * ph; o++) {
        state[o] = hole[o] ? INSIDE : KNOWN;
        t[o] = hole[o] ? 1.0e6f : 0.0f;
    }
    /* the march starts from the background side of each hole only */
    for (int y = 0; y < ph; y++)
        for (int x = 0; x < pw; x++) {
            const int o = y * pw + x;
            if (hole[o])
                continue;
            const int nb[4] = { x > 0 ? o - 1 : -1, x + 1 < pw ? o + 1 : -1, y > 0 ? o - pw : -1, y + 1 < ph ? o + pw : -1 };
            int beside = 0, nearer = 0;
            for (int k = 0; k < 4; k++)
                if (nb[k] >= 0 && hole[nb[k]]) {
                    beside = 1;
                    nearer |= vz[o] < target[nb[k]] - EDGE_STEP;
                }
            if (nearer)
                state[o] = BLOCKED;
            else if (beside)
                state[o] = BAND;
        }
    for (int o = 0; o < pw * ph; o++)
        if (state[o] == BAND)
            heap_push(heap, pos, &n, t, o);

    while (n > 0) {
        const int o = heap_pop(heap, pos, &n, t);
        const int x = o % pw, y = o / pw;
        state[o] = KNOWN;
        static const int nb[4][2] = { { -1, 0 }, { 1, 0 }, { 0, -1 }, { 0, 1 } };
        for (int k = 0; k < 4; k++) {
            const int qx = x + nb[k][0], qy = y + nb[k][1];
            if (qx < 0 || qx >= pw || qy < 0 || qy >= ph)
                continue;
            const int q = qy * pw + qx;
            if (state[q] != INSIDE)
                continue;
            t[q] = fminf(fminf(solve(t, state, pw, ph, qx - 1, qy, qx, qy - 1), solve(t, state, pw, ph, qx + 1, qy, qx, qy - 1)),
                fminf(solve(t, state, pw, ph, qx - 1, qy, qx, qy + 1), solve(t, state, pw, ph, qx + 1, qy, qx, qy + 1)));

            float tgx, tgy;
            gradient(t, state, pw, ph, qx, qy, &tgx, &tgy);
            double acc = 0.0, wsum = 0.0;
            float lo = FLT_MAX, hi = -FLT_MAX;
            for (int dy = -radius; dy <= radius; dy++)
                for (int dx = -radius; dx <= radius; dx++) {
                    const int kx = qx + dx, ky = qy + dy;
                    if ((!dx && !dy) || dx * dx + dy * dy > r2 || kx < 0 || kx >= pw || ky < 0 || ky >= ph)
                        continue;
                    const int p = ky * pw + kx;
                    if (state[p] > BAND || target[p] < target[q] - EDGE_STEP)
                        continue;
                    /* r = q - p: from the known pixel to the one being filled */
                    const float rx = (float)-dx, ry = (float)-dy, len2 = rx * rx + ry * ry, len = sqrtf(len2);
                    float dir = fabsf(rx * tgx + ry * tgy) / len;
                    if (dir < 1.0e-6f)
                        dir = 1.0e-6f;
                    const float dst = 1.0f / len2;
                    const float lev = 1.0f / (1.0f + fabsf(t[p] - t[q]));
                    float igx, igy;
                    gradient(val, state, pw, ph, kx, ky, &igx, &igy);
                    const double wgt = dir * dst * lev;
                    acc += wgt * (val[p] + igx * rx + igy * ry);
                    wsum += wgt;
                    lo = fminf(lo, val[p]);
                    hi = fmaxf(hi, val[p]);
                }
            if (wsum > 0.0) {
                /* the gradient term extrapolates; past a strong edge it would make values none of the sources
                 * have, so the estimate stays within theirs */
                val[q] = fminf(fmaxf((float)(acc / wsum), lo), hi);
            } else {
                /* nothing as far around it yet: the nearest known neighbour */
                val[q] = val[o];
            }
            vz[q] = target[q];
            hole[q] = 0;
            state[q] = BAND;
            heap_push(heap, pos, &n, t, q);
        }
    }
}

static void copy_picture(const struct stereo_dibr_picture *src, struct stereo_dibr_picture *dst)
{
    for (int k = 0; k < src->nb_planes; k++) {
        const struct stereo_dibr_plane *s = &src->plane[k];
        const int pw = (src->width + (1 << s->log2_w) - 1) >> s->log2_w;
        const int ph = (src->height + (1 << s->log2_h) - 1) >> s->log2_h;
        for (int y = 0; y < ph; y++)
            memcpy((uint8_t *)dst->plane[k].data + y * dst->plane[k].stride, (const uint8_t *)s->data + y * s->stride,
                (size_t)pw * s->bytes);
    }
}

static int check_picture(const struct stereo_dibr_picture *p, const struct stereo_dibr_picture *ref)
{
    if (!p || p->nb_planes < 1 || p->nb_planes > 4 || p->width <= 0 || p->height <= 0)
        return -1;
    if (ref && (p->width != ref->width || p->height != ref->height || p->nb_planes != ref->nb_planes))
        return -1;
    for (int k = 0; k < p->nb_planes; k++) {
        const struct stereo_dibr_plane *s = &p->plane[k];
        if (!s->data || (s->bytes != 1 && s->bytes != 2) || s->log2_w < 0 || s->log2_w > 4 || s->log2_h < 0 || s->log2_h > 4)
            return -1;
        if (ref && (s->bytes != ref->plane[k].bytes || s->log2_w != ref->plane[k].log2_w || s->log2_h != ref->plane[k].log2_h))
            return -1;
    }
    return 0;
}

static void warp_view(stereo_dibr *d, const struct stereo_dibr_params *p, int w, int h, float sv, const float *par, float *map, float *zt,
    int allow_layered)
{
    const int layered = allow_layered && p->warp == STEREO_DIBR_WARP_LAYERED;

    for (int y = 0; y < h; y++) {
        const float *pr = par + y * w;
        if (layered)
            warp_layered_row(d, w, sv, pr, map + y * w, zt + y * w, d->bmap + y * w, d->bzt + y * w, d->balpha + y * w);
        else if (p->warp == STEREO_DIBR_WARP_BACKWARD)
            warp_backward_row(d, w, sv, pr, map + y * w, zt + y * w);
        else
            warp_forward_row(d, w, sv, pr, map + y * w, zt + y * w);
    }
}

static int render(stereo_dibr *d, const struct stereo_dibr_params *p, const float *table, int table_size,
    const struct stereo_dibr_picture *picture, const struct stereo_dibr_depth *depth, int role, const struct stereo_dibr_picture *layer,
    const struct stereo_dibr_depth *layer_depth, struct stereo_dibr_picture *left, struct stereo_dibr_picture *right)
{
    if (!d || !p || !table || table_size < 1 || !depth || !depth->data || depth->width <= 0 || depth->height <= 0
        || (depth->bytes != 1 && depth->bytes != 2))
        return -1;
    if (check_picture(picture, NULL) || check_picture(left, picture) || check_picture(right, picture))
        return -1;
    if (layer
        && (check_picture(layer, picture) || !layer_depth || !layer_depth->data || layer_depth->width <= 0 || layer_depth->height <= 0
            || (layer_depth->bytes != 1 && layer_depth->bytes != 2) || (role != STEREO_DIBR_LAYER_BACKGROUND && role != STEREO_DIBR_LAYER_OVERLAY)))
        return -1;

    const int w = picture->width, h = picture->height;
    size_t depth_size = (size_t)depth->width * depth->height;
    if (layer && (size_t)layer_depth->width * layer_depth->height > depth_size)
        depth_size = (size_t)layer_depth->width * layer_depth->height;
    if (buffers(d, (size_t)w * h, (size_t)w + 1, depth_size, (size_t)w * h))
        return -2;

    depth_to_parallax(d, table, table_size, depth, w, h, d->par, 1);
    /* a smoothed depth is a rubber sheet: the picture stretches over every gap (Zhang and Tam 2005); otherwise a
     * pixel spread over more than two is a disocclusion */
    d->max_stretch = p->smooth == STEREO_DIBR_SMOOTH_ASYMMETRIC_GAUSSIAN ? FLT_MAX : 2.0f;
    if (p->smooth == STEREO_DIBR_SMOOTH_ASYMMETRIC_GAUSSIAN) {
        const double per_1920 = w / 1920.0;
        gaussian_pass(d->par, d->tmp, w, h, p->sigma_h * per_1920, 1);
        gaussian_pass(d->tmp, d->par, w, h, p->sigma_v * per_1920, 0);
    }
    /* the second layer keeps its places with no depth: a background with nothing there, graphics that are not there */
    if (layer)
        depth_to_parallax(d, table, table_size, layer_depth, w, h, d->lpar, 0);

    for (int eye = 0; eye < 2; eye++) {
        struct stereo_dibr_picture *out = eye ? right : left;
        if (!eye && p->view == STEREO_DIBR_VIEW_LEFT && (!layer || role == STEREO_DIBR_LAYER_BACKGROUND)) {
            copy_picture(picture, left);
            continue;
        }
        const float sv = p->view == STEREO_DIBR_VIEW_LEFT ? (eye ? 1.0f : 0.0f) : eye ? 0.5f : -0.5f;
        const int layered = p->warp == STEREO_DIBR_WARP_LAYERED;

        warp_view(d, p, w, h, sv, d->par, d->map, d->zt, 1);
        if (layer) {
            /* the layer is splatted as the picture is, without the layered warp's boundary layer */
            const float stretch = d->max_stretch;
            d->max_stretch = 2.0f;
            warp_view(d, p, w, h, sv, d->lpar, d->lmap, d->lzt, 0);
            d->max_stretch = stretch;
        }

        for (int k = 0; k < picture->nb_planes; k++) {
            const struct stereo_dibr_plane *src = &picture->plane[k];
            const int pw = (w + (1 << src->log2_w) - 1) >> src->log2_w;
            const int ph = (h + (1 << src->log2_h) - 1) >> src->log2_h;

            sample_plane(src, pw, ph, w, h, d->map, d->zt, d->val, d->vz, d->hole);
            if (layer) {
                sample_plane(&layer->plane[k], pw, ph, w, h, d->lmap, d->lzt, d->lval, NULL, d->lhole);
                if (role == STEREO_DIBR_LAYER_BACKGROUND) {
                    /* what the picture uncovers, the background layer shows */
                    for (int yp = 0; yp < ph; yp++)
                        for (int i = 0; i < pw; i++) {
                            const int o = yp * pw + i;
                            const int lo = clampi(yp << src->log2_h, 0, h - 1) * w + clampi(i << src->log2_w, 0, w - 1);
                            if (d->hole[o] && !d->lhole[o]) {
                                d->val[o] = d->lval[o];
                                d->vz[o] = d->lzt[lo];
                                d->hole[o] = 0;
                            }
                        }
                }
            }

            switch (p->fill) {
            case STEREO_DIBR_FILL_DEPTH_AIDED:
                fill_depth_aided(d, d->val, d->vz, d->hole, pw, ph, p->radius < 1 ? 1 : p->radius);
                break;
            case STEREO_DIBR_FILL_TELEA:
                fill_telea(d, d->val, d->vz, d->hole, pw, ph, p->radius < 1 ? 1 : p->radius);
                fill_background(d->val, d->vz, d->hole, pw, ph);
                break;
            case STEREO_DIBR_FILL_NONE:
                for (int o = 0; o < pw * ph; o++)
                    if (d->hole[o])
                        d->val[o] = (float)out->plane[k].hole;
                break;
            default:
                fill_background(d->val, d->vz, d->hole, pw, ph);
                break;
            }
            if (p->fill != STEREO_DIBR_FILL_NONE)
                fill_rest(d->val, d->hole, pw, ph, out->plane[k].hole);

            if (layered) {
                /* the boundary layer over the filled main layer, unless the main layer shows something nearer
                 * there */
                uint8_t *bhole = d->state;
                sample_plane(src, pw, ph, w, h, d->bmap, d->bzt, d->bval, NULL, bhole);
                for (int yp = 0; yp < ph; yp++)
                    for (int i = 0; i < pw; i++) {
                        const int o = yp * pw + i;
                        const int lo = clampi(yp << src->log2_h, 0, h - 1) * w + clampi(i << src->log2_w, 0, w - 1);
                        if (bhole[o] || (d->map[lo] >= 0.0f && d->zt[lo] < d->bzt[lo] - EDGE_STEP))
                            continue;
                        const float a = d->balpha[lo];
                        d->val[o] = a * d->bval[o] + (1.0f - a) * d->val[o];
                    }
            }

            if (layer && role == STEREO_DIBR_LAYER_OVERLAY) {
                /* the graphics over the picture, where they are */
                for (int o = 0; o < pw * ph; o++)
                    if (!d->lhole[o])
                        d->val[o] = d->lval[o];
            }

            for (int yp = 0; yp < ph; yp++) {
                uint8_t *row = (uint8_t *)out->plane[k].data + yp * out->plane[k].stride;
                for (int i = 0; i < pw; i++)
                    put_sample(row, src->bytes, i, d->val[yp * pw + i]);
            }
        }
    }
    return 0;
}

int stereo_dibr_render(stereo_dibr *d, const struct stereo_dibr_params *p, const float *table, int table_size,
    const struct stereo_dibr_picture *picture, const struct stereo_dibr_depth *depth, struct stereo_dibr_picture *left,
    struct stereo_dibr_picture *right)
{
    return render(d, p, table, table_size, picture, depth, 0, NULL, NULL, left, right);
}

int stereo_dibr_render_layer(stereo_dibr *d, const struct stereo_dibr_params *p, const float *table, int table_size,
    const struct stereo_dibr_picture *picture, const struct stereo_dibr_depth *depth, int role, const struct stereo_dibr_picture *layer,
    const struct stereo_dibr_depth *layer_depth, struct stereo_dibr_picture *left, struct stereo_dibr_picture *right)
{
    if (!layer)
        return -1;
    return render(d, p, table, table_size, picture, depth, role, layer, layer_depth, left, right);
}
