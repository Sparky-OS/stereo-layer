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
    /* depth-aided in a number of steps that grows with the logarithm of the hole: the background around it, by halves */
    STEREO_DIBR_FILL_PYRAMID = 4,
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

/* The default parameters: those of stereo_dibr_options_default(), the one default of the library (left eye, layered warping,
 * depth-aided fill, no smoothing; sigma 10 and 30, radius 5). */
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

/*
 * The options of drawing the views from a picture and its depth map, as one set for every program (Daniel, 2026-11-05: picture
 * plus depth is assembled into full side by side by one common library; where it has options the library carries the best
 * default and the person's choice, and every player shows the same controls). stereo_dibr_options_default() is the best
 * way measured on scenes whose true views are known (autotests/dibrbench.c prints the table and fails when it is not the
 * best); a program keeps the person's choice in its own store as the text of stereo_dibr_options_write() and gives it back
 * with stereo_dibr_options_read().
 *
 *   view, warp, fill, smooth, sigma_h, sigma_v, radius   as stereo_dibr_params;
 *   strength  the largest parallax either side of the screen, in pixels of a view 1920 wide, for the depth that carries no
 *             absolute parallax (the convention's range): how deep the picture looks;
 *   zero      where the screen is, 0 the farthest samples to 1 the nearest: what is in front of the screen and what behind;
 *   shift     a parallax added to every point, pixels of a view 1920 wide;
 *   z_near, z_far  the depth range of a depth map whose samples are distances, in the unit the map has (0 for both: what the
 *             file says);
 *   automatic the way of drawing (warp and fill) is not the person's: a still picture and the saving into a file use the best
 *             measured way (warp and fill as stereo_dibr_options_default() holds them), a video the best way that keeps its
 *             frame rate on the GPU that draws it (stereo_dibr_gl_choose(), stereo_dibr_choose_from_times()); 0 when the
 *             person picked a way: it is then the way of everything, the slowest included.
 */
struct stereo_dibr_options {
    int view, warp, fill, smooth;
    double sigma_h, sigma_v;
    int radius;
    double strength, zero, shift;
    double z_near, z_far;
    int automatic;
};

/* The limits a control offers; stereo_dibr_options_clamp() holds a set within them (returns 1 when it changed one). */
#define STEREO_DIBR_STRENGTH_MAX 300.0
#define STEREO_DIBR_SHIFT_MAX 100.0
#define STEREO_DIBR_SIGMA_MAX 100.0
#define STEREO_DIBR_RADIUS_MAX 30

STEREO_DIBR_EXPORT void stereo_dibr_options_default(struct stereo_dibr_options *o);
STEREO_DIBR_EXPORT int stereo_dibr_options_clamp(struct stereo_dibr_options *o);
/* 1 when o is the default set. */
STEREO_DIBR_EXPORT int stereo_dibr_options_is_default(const struct stereo_dibr_options *o);
/* The options into the drawing's two structures: params whole, and convention's zero, range, shift (and z_near and z_far when
 * both are given); its kind, bits and the rest stay as the caller (the depth map's mark) set them. */
STEREO_DIBR_EXPORT void stereo_dibr_options_apply(const struct stereo_dibr_options *o, struct stereo_depth_convention *c, struct stereo_dibr_params *p);
/*
 * The text of a set: "view=left warp=layered fill=depthAided smooth=off sigmaH=10 sigmaV=30 radius=5 strength=58 zero=0.5
 * shift=0 near=0 far=0 way=auto" (way=own for a way the person picked; a text without the key and with a warp or a fill other
 * than the default's is the person's), numbers in the C locale's notation. stereo_dibr_options_read() starts from the default, takes the
 * keys it knows, skips the others, and returns 1, or -1 for a malformed pair or a word that is not one of the key's;
 * stereo_dibr_options_write() returns the length, or -1 when buf is too small.
 */
STEREO_DIBR_EXPORT int stereo_dibr_options_read(const char *text, struct stereo_dibr_options *o);
STEREO_DIBR_EXPORT int stereo_dibr_options_write(const struct stereo_dibr_options *o, char *buf, size_t size);


/*
 * The ways of drawing a video that can keep up with its frame rate, best first (what dibrbench measures: the mean error against
 * the true views of the ways of the ladder is not decreasing from one to the next). A video is drawn with the first way of the
 * ladder that the GPU (or the CPU) draws within the frame's time, measured on that GPU at that size; the best, the first, is
 * the options' default. The person's own way, and a still picture or the saving into a file, need no ladder.
 */
struct stereo_dibr_way {
    int warp, fill; /* an enum stereo_dibr_warp and an enum stereo_dibr_fill */
};

/* The ladder, into ways (up to max); returns how many ways it has. */
STEREO_DIBR_EXPORT int stereo_dibr_ways(struct stereo_dibr_way *ways, int max);

struct stereo_dibr_choice {
    int warp, fill; /* the way chosen */
    int rank; /* its place in the ladder, 0 the best */
    int keeps_up; /* 1 when it is drawn within the budget; 0 when no way is: the least costly is chosen, and the program says so */
    double ms, budget_ms; /* its 95th percentile (the slowest of twelve frames), and the time a frame of the video may take */
    /* when the way chosen is not the first of the ladder, the best way that does not keep up and its time; else -1 */
    int skipped_warp, skipped_fill;
    double skipped_ms;
};

/*
 * The way for a video of fps frames a second from the 95th percentile times p95_ms[i] of the n ways of stereo_dibr_ways(),
 * each in milliseconds a frame drawn (a negative time is a way that was not timed: it does not keep up): the first way whose
 * time is within four fifths of the frame's time (a fifth is left for the rest of the player), else the quickest. Returns 0,
 * or -1 for a bad argument.
 */
STEREO_DIBR_EXPORT int stereo_dibr_choose_from_times(const double *p95_ms, int n, double fps, struct stereo_dibr_choice *out);

/*
 * The times of the ways on this CPU (stereo_dibr_render(), one thread) at width x height, into p95_ms (n values), kept in the
 * person's cache (XDG_CACHE_HOME/stereo-dibr/timings, else ~/.cache/stereo-dibr/timings: never in a media file) by the
 * processor and the size: the cache is read first, and *measured is 1 when this call timed the ways. Returns 0, or -1.
 */
STEREO_DIBR_EXPORT int stereo_dibr_measure_cpu(int width, int height, double *p95_ms, int n, int *measured);

/* The cache by hand (for a GPU's key, and for tests): 1 when key has a line for width x height with n times, into p95_ms. */
STEREO_DIBR_EXPORT int stereo_dibr_cache_load(const char *key, int width, int height, double *p95_ms, int n);
/* Sets the line of key for width x height; 0, or -1 when the cache cannot be written (the caller says so). */
STEREO_DIBR_EXPORT int stereo_dibr_cache_store(const char *key, int width, int height, const double *p95_ms, int n);

#ifdef __cplusplus
}
#endif

#endif
