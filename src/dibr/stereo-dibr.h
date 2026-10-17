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
    /* Google's Dynamic Depth 1.0 (Depth Map, Format "RangeLinear") and GDepth: v uniform in distance, 0 for Near and
     * 1 for Far (z_near, z_far); 1/distance then reads as NEARNESS. Without Near and Far it reads as FARNESS. */
    STEREO_DEPTH_RANGE_LINEAR = 7,
    /* Dynamic Depth 1.0 and GDepth, Format "RangeInverse": v uniform in 1/distance, 0 for Near and 1 for Far, so
     * uniform in parallax: it reads as FARNESS. */
    STEREO_DEPTH_RANGE_INVERSE = 8,
    /* A depth sensor's distances: a sample is the distance itself, in the sensor's unit (RealSense Z16 with its depth
     * scale, ROS REP 118 16UC1 in millimetres, a 13-bit Android DEPTH16 range), and 0 is no measurement. z_near and
     * z_far, in the same unit, are the distances 1/distance is spread between as NEARNESS; without them the samples
     * read as FARNESS. A sample of 0 gets no parallax (NaN in the table): the render takes the farther neighbour's. */
    STEREO_DEPTH_METRIC = 9,
};

struct stereo_depth_convention {
    /* an enum stereo_depth_kind */
    int kind;
    /* bits per depth sample, 1 to 16 */
    int bits;
    /* NEARNESS and the kinds that read as it: where the screen is, 0 to 1 */
    double zero;
    /* the same: the largest parallax either side of the screen, in pixels of a view 1920 wide (any value: no limit) */
    double range;
    /* every kind: a parallax added to all, pixels of a view 1920 wide (a horizontal image translation, which moves the
     * whole scene behind or in front) */
    double shift;
    /* Z: ZNear and ZFar given */
    int z_present;
    double z_near, z_far;
    /* DISPARITY, NONLINEAR_DISPARITY: DMin and DMax given, in luma sample widths of a view d_width wide */
    int d_present;
    double d_min, d_max;
    int d_width;
    /* NONLINEAR_DISPARITY: DepthLUT[0] to DepthLUT[maxVal] */
    const uint16_t *lut;
    /* PARALLAX */
    double parallax_zero;
    double parallax_scale;
    double wref;
};

/*
 * Fills table[0] to table[(1 << c->bits) - 1] with the parallax of each sample value in pixels of a view width pixels
 * wide, positive behind the screen; NaN for a sample that holds no depth. Returns 0, or -1 for a bad argument.
 */
STEREO_DIBR_EXPORT int stereo_depth_parallax_table(const struct stereo_depth_convention *c, int width, float *table);

/*
 * DepthLUT of a non-linear disparity depth map (H.265 equation G-10; H.264 H.13.2.3 for 8 bits): model holds
 * depth_nonlinear_representation_model[0] to [depth_nonlinear_representation_num_minus1 + 2], nb_model values (3 to
 * 65), the first and the last 0; lut receives (1 << bits) values. Returns 0, or -1 for a bad argument.
 */
STEREO_DIBR_EXPORT int stereo_depth_nonlinear_lut(const uint16_t *model, int nb_model, int bits, uint16_t *lut);

/*
 * The depth mark of the edition (docs/depth-marks.md, "Stereo3DDepth"): a word for the kind, then key=value pairs,
 * separated by spaces, numbers in the C locale's notation:
 *   nearness | farness [zero= range=]              inverseZ | z [near= far= zero= range=]
 *   disparity | nonlinearDisparity [min= max= width= model=m0,m1,...,mN zero= range=]
 *   parallax [parallaxZero= parallaxScale= wref=]  rangeLinear | rangeInverse | metric [near= far= zero= range=]
 * and shift= on any of them. stereo_depth_read_mark() fills c (bits stay as they were: the depth map's own) and, when
 * model is not null, the model of a non-linear disparity into model (up to 65 values) and nb_model; it returns 1 for
 * a mark it reads, 0 for an unknown kind (c is then left as it was), -1 for a bad argument or a malformed pair.
 * stereo_depth_write_mark() writes c (and the model) as a mark into buf; it returns the length of the mark, or -1
 * when buf is too small or c is not a kind of the set.
 */
STEREO_DIBR_EXPORT int stereo_depth_read_mark(const char *mark, struct stereo_depth_convention *c, uint16_t *model, int *nb_model);
STEREO_DIBR_EXPORT int stereo_depth_write_mark(const struct stereo_depth_convention *c, const uint16_t *model, int nb_model, char *buf, size_t size);

enum stereo_dibr_view {
    STEREO_DIBR_VIEW_LEFT = 0, /* the picture is the left eye */
    STEREO_DIBR_VIEW_CENTER = 1, /* the picture is the middle */
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
    int view; /* an enum stereo_dibr_view */
    int warp; /* an enum stereo_dibr_warp */
    int fill; /* an enum stereo_dibr_fill */
    int smooth; /* an enum stereo_dibr_smooth */
    double sigma_h, sigma_v; /* the Gaussian's standard deviations, pixels of a view 1920 wide */
    int radius; /* depth-aided and Telea: the neighbourhood, pixels (Telea's epsilon) */
};

/* The default parameters: left eye, forward warping, background extension, no smoothing; sigma 10 and 30, radius 5. */
STEREO_DIBR_EXPORT void stereo_dibr_params_default(struct stereo_dibr_params *p);

/* One plane of a planar picture. The first plane is luma (or green, or the only one); the others may be subsampled. */
struct stereo_dibr_plane {
    void *data;
    ptrdiff_t stride; /* bytes from one row to the next */
    int bytes; /* bytes per sample: 1, or 2 for native-endian 16-bit samples */
    int log2_w, log2_h; /* subsampling against the first plane */
    unsigned hole; /* FILL_NONE: the value a hole gets (black) */
};

struct stereo_dibr_picture {
    int width, height; /* of the first plane */
    int nb_planes; /* 1 to 4 */
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

/* What the second layer of a picture is for stereo_dibr_render_layer(). */
enum stereo_dibr_layer_role {
    /* What the picture hides: the background behind its foreground, with its own depth, which fills the places the
     * picture does not show before any other fill (a layered depth picture, Shade et al. 1998). */
    STEREO_DIBR_LAYER_BACKGROUND = 0,
    /* Drawn over the picture, with its own depth: graphics such as HDMI 1.4a's "L + depth + graphics +
     * graphics-depth" (3D_Structure 0101, Figure H-6). Where the layer's depth holds no depth (NaN in the table) the
     * layer is transparent. */
    STEREO_DIBR_LAYER_OVERLAY = 1,
};

/*
 * stereo_dibr_render() with a second layer, layer with layer_depth read through layer_table: the background behind the
 * picture or graphics over it (an enum stereo_dibr_layer_role). layer has the picture's size and planes. The layer's
 * samples with no depth (NaN in its table) hold nothing; the layer is warped as the picture is (forward warping when
 * the warp is layered), and nothing fills its own holes. Returns as stereo_dibr_render().
 */
STEREO_DIBR_EXPORT int stereo_dibr_render_layer(stereo_dibr *dibr, const struct stereo_dibr_params *p, const float *table, int table_size,
    const struct stereo_dibr_picture *picture, const struct stereo_dibr_depth *depth, int role, const struct stereo_dibr_picture *layer,
    const float *layer_table, int layer_table_size, const struct stereo_dibr_depth *layer_depth, struct stereo_dibr_picture *left,
    struct stereo_dibr_picture *right);

/*
 * The packings of a picture with its depth map in one frame: the depth layouts of docs/depth-marks.md (Stereo3DLayout;
 * libstereo-policy's STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE and on, in the same order).
 */
enum stereo_dibr_packing {
    /* depthSideBySidePictureFirst and DepthFirst: the picture and its depth map in the halves */
    STEREO_DIBR_PACKING_SIDE_BY_SIDE = 0,
    /* depthTopBottomPictureFirst and DepthFirst: HDMI 1.4a "L + depth" (3D_Structure 0100, Figure H-5) */
    STEREO_DIBR_PACKING_TOP_BOTTOM = 1,
    /* depthGraphicsTopBottom: four rows from the top, the picture, its depth map, graphics and their depth map
     * (3D_Structure 0101, Figure H-6) */
    STEREO_DIBR_PACKING_GRAPHICS = 2,
    /* depthBackgroundQuarters: the picture (top left), its depth map (top right), the background it hides (bottom left),
     * the background's depth map (bottom right) */
    STEREO_DIBR_PACKING_BACKGROUND = 3,
};

/* The parts of a packed frame, as stereo_dibr_unpack() finds them. */
struct stereo_dibr_parts {
    struct stereo_dibr_picture picture;
    struct stereo_dibr_depth depth;
    /* the second layer, for GRAPHICS (an overlay) and BACKGROUND; width 0 for the others */
    struct stereo_dibr_picture layer;
    struct stereo_dibr_depth layer_depth;
    int layer_role; /* enum stereo_dibr_layer_role */
};

/*
 * The parts of frame, packed as packing says (depth_first for the side by side and top and bottom packings with the
 * depth map first): views of its planes, nothing copied. A depth map is the gray of its part of plane depth_plane (the
 * luma of a YUV frame; the plane of RGB that holds the gray, all three being equal). Each part is drawn with
 * stereo_dibr_render() or, with its layer, stereo_dibr_render_layer(), and has the size of the views. Returns 0, or -1
 * for a bad argument or a frame too small to be cut where its planes' subsampling allows.
 */
STEREO_DIBR_EXPORT int stereo_dibr_unpack(
    const struct stereo_dibr_picture *frame, int packing, int depth_first, int depth_plane, struct stereo_dibr_parts *parts);

#ifdef __cplusplus
}
#endif

#endif
