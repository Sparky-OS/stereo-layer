/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/*
 * Depth-image-based rendering (DIBR) for Sparky Stereo OS, without Qt: a picture and its depth map become the two
 * views of full side by side, each at the picture's size, the left eye first. One core for every program that turns
 * depth into stereo (FFmpeg's dibr filter, players, KWin), so they all give the same views.
 *
 * Two steps. stereo_depth_parallax_table() reads what a depth sample means (the depth conventions of ITU-T H.265
 * Table G.3 and H.264 Table H-1, the depth and parallax maps of ISO/IEC 23002-3, or plain 2D-plus-depth) and gives
 * each sample value its parallax: the right eye's position of a point minus the left eye's, in pixels of the view,
 * positive behind the screen. stereo_dibr_render() then warps the picture by that parallax into the views, fills the
 * places the picture does not show (disocclusions) and smooths the depth first if asked:
 *
 *   view:   the picture is the left eye and only the right eye is drawn (default), or the picture is the middle and
 *           both eyes are drawn, half the parallax each way (Fehn 2004, doi:10.1117/12.524762);
 *   warp:   forward warping, each picture pixel splatted to where it goes, the nearest one kept (default); backward
 *           warping, the depth warped first and each view pixel sampled from the picture (McMillan's inverse
 *           mapping); or layered, a boundary layer of the foreground's edges drawn over the main layer with the
 *           background drawn under them (Zitnick et al. 2004, doi:10.1145/1015706.1015766; Shade et al. 1998,
 *           doi:10.1145/280814.280882), which keeps edges free of the background's colour;
 *   fill:   background extension, the farther neighbour of each hole repeated across it (default); depth-aided, only
 *           background pixels feeding the fill, nearest first (the depth-aided idea of Daribo and Saito 2010,
 *           doi:10.1109/TBC.2010.2044296); Telea's fast marching inpainting (Telea 2004,
 *           doi:10.1080/10867651.2004.10487596); or none, holes left at a given value;
 *   smooth: off (default), or an asymmetric Gaussian on the depth, stronger vertically than horizontally, which
 *           shrinks holes without bending vertical edges (Zhang and Tam 2005, doi:10.1109/TBC.2005.846190); the
 *           smoothed depth is then a rubber sheet, the picture stretched over every gap rather than leaving holes.
 */

#ifndef STEREO_DIBR_H
#define STEREO_DIBR_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define STEREO_DIBR_EXPORT __attribute__((visibility("default")))

/* What a depth sample means. v is a sample divided by its largest value, maxVal = (1 << bits) - 1 (H.265 G.14.3.4). */
enum stereo_depth_kind {
    /* Larger samples nearer, uniform in parallax: 2D-plus-depth (WOWvx, MPEG-C Part 3 depth maps). The screen is at
     * v = zero; the farthest and nearest samples are range pixels behind and in front of it at most. */
    STEREO_DEPTH_NEARNESS = 0,
    /* Larger samples farther, otherwise as NEARNESS (a distance map stored the other way round). */
    STEREO_DEPTH_FARNESS = 1,
    /* H.265 Table G.3 and H.264 Table H-1, type 0: v uniform in 1/Z, 0 for 1/ZFar and 1 for 1/ZNear. Uniform in
     * parallax too, so it reads as NEARNESS. */
    STEREO_DEPTH_INVERSE_Z = 2,
    /* Type 1: v uniform in disparity, 0 for DMin and 1 for DMax, in luma sample widths of a view d_width wide. The
     * parallax is the negated disparity (a point nearer than the cameras' convergence has a positive disparity and a
     * negative parallax), scaled to the view. Without DMin and DMax it reads as NEARNESS. */
    STEREO_DEPTH_DISPARITY = 3,
    /* Type 2: v uniform in Z, 0 for ZFar and 1 for ZNear; 1/Z, which parallax follows, then reads as NEARNESS.
     * Without ZNear and ZFar it reads as NEARNESS, as uniform in Z as it is. */
    STEREO_DEPTH_Z = 4,
    /* Type 3: v non-linear in disparity; lut, DepthLUT of H.265 equation G-10 (H.264 H.13.2.3), makes it uniform,
     * then as DISPARITY. */
    STEREO_DEPTH_NONLINEAR_DISPARITY = 5,
    /* An ISO/IEC 23002-3 parallax map (6.1.2.2, 6.2.2.2; HDMI 1.4a Table H-4): a sample m is a parallax of
     * (m - parallax_zero) / parallax_scale centimetres on a reference screen wref centimetres wide. Our reading of a
     * standard that is not freely available: UNVERIFIED. */
    STEREO_DEPTH_PARALLAX = 6,
};

struct stereo_depth_convention {
    int kind;                       /* an enum stereo_depth_kind */
    int bits;                       /* bits per depth sample, 1 to 16 */
    double zero;                    /* NEARNESS and the kinds that read as it: where the screen is, 0 to 1 */
    double range;                   /* the same: the largest parallax either side of the screen, in pixels of a view
                                       1920 wide (any value: no limit) */
    double shift;                   /* every kind: a parallax added to all, pixels of a view 1920 wide (a horizontal
                                       image translation, which moves the whole scene behind or in front) */
    int z_present;                  /* Z: ZNear and ZFar given */
    double z_near, z_far;
    int d_present;                  /* DISPARITY, NONLINEAR_DISPARITY: DMin and DMax given */
    double d_min, d_max;
    int d_width;                    /* the width of the view DMin and DMax are measured in */
    const uint16_t *lut;            /* NONLINEAR_DISPARITY: DepthLUT[0] to DepthLUT[maxVal] */
    double parallax_zero;           /* PARALLAX */
    double parallax_scale;
    double wref;
};

/*
 * Fills table[0] to table[(1 << c->bits) - 1] with the parallax of each sample value in pixels of a view width pixels
 * wide, positive behind the screen. Returns 0, or -1 for a bad argument.
 */
STEREO_DIBR_EXPORT int stereo_depth_parallax_table(const struct stereo_depth_convention *c, int width, float *table);

enum stereo_dibr_view {
    STEREO_DIBR_VIEW_LEFT = 0,      /* the picture is the left eye */
    STEREO_DIBR_VIEW_CENTER = 1,    /* the picture is the middle */
};

enum stereo_dibr_warp {
    STEREO_DIBR_WARP_FORWARD = 0,
    STEREO_DIBR_WARP_BACKWARD = 1,
    STEREO_DIBR_WARP_LAYERED = 2,
};

enum stereo_dibr_fill {
    STEREO_DIBR_FILL_BACKGROUND = 0,
    STEREO_DIBR_FILL_DEPTH_AIDED = 1,
    STEREO_DIBR_FILL_TELEA = 2,
    STEREO_DIBR_FILL_NONE = 3,
};

enum stereo_dibr_smooth {
    STEREO_DIBR_SMOOTH_OFF = 0,
    STEREO_DIBR_SMOOTH_ASYMMETRIC_GAUSSIAN = 1,
};

struct stereo_dibr_params {
    int view;                       /* an enum stereo_dibr_view */
    int warp;                       /* an enum stereo_dibr_warp */
    int fill;                       /* an enum stereo_dibr_fill */
    int smooth;                     /* an enum stereo_dibr_smooth */
    double sigma_h, sigma_v;        /* the Gaussian's standard deviations, pixels of a view 1920 wide */
    int radius;                     /* depth-aided and Telea: the neighbourhood, pixels (Telea's epsilon) */
};

/* The default parameters: left eye, forward warping, background extension, no smoothing; sigma 10 and 30, radius 5. */
STEREO_DIBR_EXPORT void stereo_dibr_params_default(struct stereo_dibr_params *p);

/* One plane of a planar picture. The first plane is luma (or green, or the only one); the others may be subsampled. */
struct stereo_dibr_plane {
    void *data;
    ptrdiff_t stride;               /* bytes from one row to the next */
    int bytes;                      /* bytes per sample: 1, or 2 for native-endian 16-bit samples */
    int log2_w, log2_h;             /* subsampling against the first plane */
    unsigned hole;                  /* FILL_NONE: the value a hole gets (black) */
};

struct stereo_dibr_picture {
    int width, height;              /* of the first plane */
    int nb_planes;                  /* 1 to 4 */
    struct stereo_dibr_plane plane[4];
};

/* A depth map: one plane of samples of c->bits, any size (it is scaled to the picture). */
struct stereo_dibr_depth {
    const void *data;
    ptrdiff_t stride;
    int bytes;
    int width, height;
};

typedef struct stereo_dibr stereo_dibr;

STEREO_DIBR_EXPORT stereo_dibr *stereo_dibr_alloc(void);
STEREO_DIBR_EXPORT void stereo_dibr_free(stereo_dibr *dibr);

/*
 * Draws the two views of picture with the parallax table (table_size entries, from stereo_depth_parallax_table() at
 * the picture's width) indexed by depth's samples. left and right have the picture's size and planes (they may be the
 * halves of one full side by side frame); with VIEW_LEFT, left receives the picture as it is. Returns 0, or -1 for a
 * bad argument, -2 when out of memory.
 */
STEREO_DIBR_EXPORT int stereo_dibr_render(stereo_dibr *dibr, const struct stereo_dibr_params *p, const float *table, int table_size,
    const struct stereo_dibr_picture *picture, const struct stereo_dibr_depth *depth, struct stereo_dibr_picture *left,
    struct stereo_dibr_picture *right);

#ifdef __cplusplus
}
#endif

#endif
