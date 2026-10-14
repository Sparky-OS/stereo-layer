/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/*
 * The stereo policy of Sparky Stereo OS for programs without Qt (a browser, a player): how a stereo mark reads, which
 * packing a video frame holds, whether its views are full or half, and the user's correction. libstereo-layer builds on
 * it, so every program decides alike. The marks are the Stereo3DLayout words (RFC 9559 StereoMode, section
 * 5.1.4.1.28.3; H.264 and H.265 Table D-8; the JPS and MPO marks; SVGStereo). H.264 V15 (08/2024) numbers the frame
 * packing table D-8, V16 (06/2026) D-9.
 */

#ifndef STEREO_POLICY_H
#define STEREO_POLICY_H

#ifdef __cplusplus
extern "C" {
#endif

#define STEREO_POLICY_EXPORT __attribute__((visibility("default")))

/* Every packing of RFC 9559 StereoMode and H.264/H.265 Table D-8 (H.264 V15, 08/2024; D-9 in H.264 V16, 06/2026);
 * the values of Stereo::Picture::Layout. */
enum stereo_policy_layout {
    STEREO_POLICY_LAYOUT_NONE = 0,
    STEREO_POLICY_LAYOUT_SIDE_BY_SIDE = 1,
    STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM = 2,
    STEREO_POLICY_LAYOUT_MULTI_VIEW = 3,
    STEREO_POLICY_LAYOUT_ROWS = 4,
    STEREO_POLICY_LAYOUT_COLUMNS = 5,
    STEREO_POLICY_LAYOUT_CHECKERBOARD = 6,
    STEREO_POLICY_LAYOUT_SIDE_BY_SIDE_QUINCUNX = 7,
    STEREO_POLICY_LAYOUT_FRAME_SEQUENTIAL = 8,
    STEREO_POLICY_LAYOUT_BLOCK_LACED = 9,
};

/* A value of user_layout for stereo_policy_video_layout(): the user says the video is not 3D, whatever its mark says. */
#define STEREO_POLICY_USER_FORCE_2D (-1)

/* Which view comes first in the packing; the values of Stereo::Picture::EyeOrder. */
enum stereo_policy_order {
    STEREO_POLICY_LEFT_FIRST = 0,
    STEREO_POLICY_RIGHT_FIRST = 1,
};

/* The colours of an anaglyph, one picture holding both views; the values of Stereo::Picture::AnaglyphColorPair. */
enum stereo_policy_anaglyph {
    STEREO_POLICY_ANAGLYPH_NONE = 0,
    STEREO_POLICY_ANAGLYPH_RED_CYAN = 1,
    STEREO_POLICY_ANAGLYPH_GREEN_MAGENTA = 2,
    STEREO_POLICY_ANAGLYPH_AMBER_BLUE = 3,
};

/* What decided; the values of Stereo::Picture::Source. */
enum stereo_policy_source {
    STEREO_POLICY_SOURCE_NONE = 0,
    STEREO_POLICY_SOURCE_FILE_MARK = 1,
    STEREO_POLICY_SOURCE_USER = 2,
    STEREO_POLICY_SOURCE_FORMAT = 3,
    STEREO_POLICY_SOURCE_DETECTION = 4,
    STEREO_POLICY_SOURCE_SHAPE = 5,
    STEREO_POLICY_SOURCE_NAME = 6,
};

/*
 * Reads a Stereo3DLayout value exactly: "mono"; sideBySide, sideBySideQuincunx, topBottom, checkerboard,
 * rowInterleaved, columnInterleaved, frameSequential, blockLaced and multiView, each followed by LeftFirst or
 * RightFirst; anaglyphCyanRed and anaglyphGreenMagenta (the red, or the green, view is the left eye's); JPS's
 * anaglyphRedLeft and anaglyphRedRight. Returns 1 for a value of the set, 0 for any other value (one picture: layout
 * NONE, order LEFT_FIRST, anaglyph NONE), -1 for a bad argument. An output pointer may be null.
 */
STEREO_POLICY_EXPORT int stereo_policy_read_mark(const char *value, int *layout, int *order, int *anaglyph);

/*
 * The packing of a video frame, in this order: the user's choice, when user_layout is not NONE (a packing, or
 * STEREO_POLICY_USER_FORCE_2D, which says the video is not 3D); then the stream's own mark, whatever the shape (an
 * anaglyph or "mono" holds no packing); then the shape: a frame at least three times as wide as high is side by side,
 * left first (two pictures next to each other; a scope film is 2.39:1). The user's explicit choice comes before the mark
 * because a mark can be wrong and every player lets the user say so. swap_eyes, the user's correction, reverses the
 * order of whatever packing was decided. width and height are the frame as shown, the sample aspect ratio applied.
 * Returns 0, or -1 for a bad argument.
 */
STEREO_POLICY_EXPORT int stereo_policy_video_layout(
    int width, int height, const char *mark, int user_layout, int user_order, int swap_eyes, int *layout, int *order, int *source);

/*
 * Whether the views of a frame of this packing are full, each at its own shape and shown as it is, or half, squeezed
 * along the packing and stretched back to the frame. Side by side is full when wider than a single picture can be and
 * top and bottom when narrower (STEREO_POLICY_SINGLE_PICTURE_*); interleaved,
 * checkerboard and quincunx views are always half; multiview, frame-sequential and block-laced views are whole frames,
 * full. Returns 1 for full, 0 for half, -1 for a bad argument or no packing.
 */
STEREO_POLICY_EXPORT int stereo_policy_video_full_views(int width, int height, int layout);

/*
 * Whether the views of a packed frame are full or half, from what the stream says, and only then from the shape.
 * coded_width and coded_height are the frame as stored, after any crop; sar_num and sar_den its sample aspect ratio (0 or less for
 * 1:1). plane_width and plane_height are Matroska's DisplayWidth and DisplayHeight when the track carries them (0 when
 * not): one plane's size (RFC 9559 section 18.10; elements 5.1.4.1.28.12 and .13), so a side by side frame whose half has the shape of
 * the plane holds full views, and one whose whole has it holds half views; a plane as large as the frame is the
 * elements' default (the pixel size after cropping) and says nothing. upsampled_aspect_ratio is the H.265 frame packing SEI's flag (D.3.16): 1 for half
 * views, 0 for full ones, -1 (STEREO_POLICY_UPSAMPLED_UNKNOWN) for H.264, which has none, or when it is not at hand.
 * Layouts other than side by side and top and bottom are decided as in stereo_policy_video_full_views(). When nothing
 * says, stereo_policy_video_views_by_shape() decides, with the frame as shown (the sample aspect ratio applied): a
 * marked frame of a single picture's shape carries half views, one twice as wide or as tall carries full views. decided_by, which may be null, receives
 * STEREO_POLICY_VIEWS_BY_MARK, _BY_SHAPE or _BY_LAYOUT. Returns 1 for full views, 0 for half, -1 for a bad argument or
 * no packing.
 */
#define STEREO_POLICY_UPSAMPLED_UNKNOWN (-1)
enum stereo_policy_views_source {
    STEREO_POLICY_VIEWS_BY_LAYOUT = 0,
    STEREO_POLICY_VIEWS_BY_MARK = 1,
    STEREO_POLICY_VIEWS_BY_SHAPE = 2,
};
STEREO_POLICY_EXPORT int stereo_policy_video_views(int coded_width, int coded_height, int sar_num, int sar_den, int plane_width, int plane_height,
    int upsampled_aspect_ratio, int layout, int *decided_by);

/*
 * The plane size (DisplayWidth and DisplayHeight) of a Matroska side by side or top and bottom track, from the frame and
 * the sample aspect ratio FFmpeg gives it: the Matroska demuxer multiplies the stacked axis of the display size by two
 * and folds the result into the sample aspect ratio (libavformat/matroskadec.c), so a player that reads the track
 * through FFmpeg has the plane size only in that form. Pass the result as plane_width and plane_height to
 * stereo_policy_video_views() for a stream that comes from Matroska (not for one whose ratio is the video's VUI).
 * Returns 0, or -1 for a bad argument or a layout other than side by side and top and bottom.
 */
STEREO_POLICY_EXPORT int stereo_policy_matroska_plane(int coded_width, int coded_height, int sar_num, int sar_den, int layout, int *plane_width, int *plane_height);

/*
 * The 3D tag of a file name. The name is cut into words at . _ - space [ ] ( ) and a word counts only whole and in any
 * case, so "3DMark" and "Blender3D" say nothing. The vocabulary, where each word comes from:
 *   3D                          the plain tag of 3D releases ("Name.3D.mkv", "Name [3D].mp4"): says only that the video is 3D
 *   SBS, TAB, TB, OU            side by side; top and bottom (TAB: top and bottom of the Blu-ray 3D scene, TB, OU:
 *                               over and under): the packing, left eye first
 *   HSBS, H-SBS, HALF-SBS, HTAB, H-TAB, HALF-TAB, HOU, H-OU, HALF-OU, HTB   the same with half views
 *   FSBS, F-SBS, FULL-SBS, FTAB, FULL-TAB, FOU, FULL-OU, FTB                the same with full views
 *   LR, RL                      the left eye first or the right eye first (the Bino 1.x and 3dtv.at names); alone they mean side by side
 * A packing word with half or full says both; a bare one leaves the views to stereo_policy_video_views() (an ordinary
 * single picture's size is half, twice that is full). A tag never overrides a stream's mark.
 * layout receives SIDE_BY_SIDE, TOP_AND_BOTTOM or NONE, order LEFT_FIRST or RIGHT_FIRST, views 1 for full, 0 for half and
 * -1 when the name does not say, three_d 1 when the name says the video is 3D (a bare "3D" or any packing word). Any
 * output pointer may be null. Returns 1 when the name carries a tag, 0 when not, -1 for a bad argument.
 */
STEREO_POLICY_EXPORT int stereo_policy_name_tag(const char *file_name, int *layout, int *order, int *views, int *three_d);

/*
 * What a frame whose name says only "3D" can still tell: the caller's own comparison of the halves of the picture (a
 * side by side pair has two similar halves left and right, a top and bottom pair above and below). STEREO_POLICY_HALVES_*.
 */
#define STEREO_POLICY_HALVES_UNKNOWN 0
#define STEREO_POLICY_HALVES_SIDE_BY_SIDE 1
#define STEREO_POLICY_HALVES_TOP_AND_BOTTOM 2

/*
 * stereo_policy_video_layout() with the file name's tag after the stream's mark: the user's explicit choice, then the
 * stream's mark, then the name (source STEREO_POLICY_SOURCE_NAME; views receives 1, 0 or -1 as for the tag, -1 when the
 * layout did not come from the name), then the shape. A name that only says "3D" gives side by side when the shape rule
 * does (three times as wide as high), else what halves says; if nothing decides, the video stays 2D and the user's menu
 * covers it. file_name may be null. Returns 0, or -1 for a bad argument.
 */
STEREO_POLICY_EXPORT int stereo_policy_video_layout_named(int width, int height, const char *mark, const char *file_name, int halves, int user_layout,
    int user_order, int swap_eyes, int *layout, int *order, int *source, int *views);

/*
 * The halves test, for a video whose name says only "3D" and a caller that can hand over one decoded frame: whether the
 * picture is two similar halves left and right (side by side) or above and below (top and bottom). luma is the
 * brightness plane (8 bits, stride bytes per row) of the frame as decoded. The halves of a stereo pair differ only by
 * a small horizontal shift and the other view's small differences, so each pair of halves is compared sampled on a grid,
 * allowing a shift of up to a thirtysecond of the width; the pair of halves that matches clearly better than the other
 * (STEREO_POLICY_HALVES_MARGIN_PERCENT of the other's difference) and is close in itself
 * (STEREO_POLICY_HALVES_CLOSE_PERCENT of the brightness range) is the packing. Returns STEREO_POLICY_HALVES_SIDE_BY_SIDE,
 * _TOP_AND_BOTTOM or _UNKNOWN (a flat picture, an ordinary picture, a bad argument). A still picture's content test, in
 * the photos lane's entry, may share this code later.
 */
#define STEREO_POLICY_HALVES_MARGIN_PERCENT 60
#define STEREO_POLICY_HALVES_CLOSE_PERCENT 12
STEREO_POLICY_EXPORT int stereo_policy_halves_test(const unsigned char *luma, int width, int height, int stride);

/*
 * The shapes of an ordinary single picture, from 5:4 to 12:5 (1.25 to 2.4 wide: 4:3, 16:10, 16:9, 1.85 and the 2.39
 * scope film are inside it). A frame that a stream marks as side by side or top and bottom and whose shape is inside the
 * range holds one picture's worth of shape, so its views are half, squeezed to fit; a side by side frame wider than the
 * range, or a top and bottom frame narrower than it, is two pictures and its views are full. The mark says the packing,
 * not the resolution of the views, and this is the only place the shape speaks for it: an explicit Matroska plane
 * size or the H.265 upsampled_aspect_ratio_flag decides first (stereo_policy_video_views()). Unmarked video is another
 * rule (stereo_policy_video_side_by_side_by_shape()).
 */
#define STEREO_POLICY_SINGLE_PICTURE_WIDEST_NUM 12
#define STEREO_POLICY_SINGLE_PICTURE_WIDEST_DEN 5
#define STEREO_POLICY_SINGLE_PICTURE_NARROWEST_NUM 5
#define STEREO_POLICY_SINGLE_PICTURE_NARROWEST_DEN 4

/*
 * A marked frame of a single picture's shape carries half views, a frame of two pictures' shape full ones: side by side
 * is full when wider than the widest single picture, top and bottom when narrower than the narrowest. The same as
 * stereo_policy_video_full_views(), which stays for programs of version 1.4.
 */
STEREO_POLICY_EXPORT int stereo_policy_video_views_by_shape(int width, int height, int layout);

/* The shape rule of video alone: 1 for a frame at least three times as wide as high, else 0. */
STEREO_POLICY_EXPORT int stereo_policy_video_side_by_side_by_shape(int width, int height);

/*
 * The shape rule of a still picture without a mark: 1 for a side-by-side pair, from 2:1 (two squares) to 3.6:1 (two
 * 16:9 pictures are 3.56:1); a wider one is a panorama, and the shape never says top and bottom. Else 0.
 */
STEREO_POLICY_EXPORT int stereo_policy_picture_pair_by_shape(int width, int height);

/*
 * Whether an unmarked still picture is a side-by-side pair by its pixels: its two halves must be views of one scene,
 * with strong correlation, block by block, after a small horizontal shift (the disparity) and next to no vertical one.
 * Shape says only that a picture may be a pair, so a caller runs this after the shape rule and never decides a pair
 * from shape alone. rgb holds the whole picture, both halves, as 8-bit RGB, three bytes a pixel, stride bytes a row;
 * hand it a copy scaled down to a few hundred pixels wide so a thumbnailer stays fast (a larger one is reduced here
 * to at most 192 pixels a view). A picture with too little texture to compare says nothing and is not a pair.
 * Returns 1 for a pair, 0 for one picture, -1 for a bad argument.
 */
STEREO_POLICY_EXPORT int stereo_policy_picture_pair_by_content(const unsigned char *rgb, int width, int height, int stride);

/*
 * The user's correction stored with a file, for a file whose mark has its eyes the wrong way round: the extended
 * attribute user.stereo-policy.swap-eyes, "1", which copies and moves that keep extended attributes keep. Returns 1
 * when the file carries it, 0 when it does not, -1 when the file system keeps no extended attributes, the file cannot
 * be read or the argument is bad.
 */
STEREO_POLICY_EXPORT int stereo_policy_file_swap_eyes(const char *path);

/* Stores the correction with the file (swap 1) or removes it (swap 0). Returns 0, or -1 with errno set. */
STEREO_POLICY_EXPORT int stereo_policy_set_file_swap_eyes(const char *path, int swap);

#ifdef __cplusplus
}
#endif

#endif
