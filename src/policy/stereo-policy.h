/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/*
 * The stereo policy of Sparky Stereo OS for programs without Qt (a browser, a player): how a stereo mark reads, which
 * packing a video frame holds, whether its views are full or half, and the user's correction. libstereo-layer builds on
 * it, so every program decides alike. The marks are the Stereo3DLayout words (RFC 9559 StereoMode, section
 * 5.1.4.1.28.3; H.264 and H.265 Table D-8; the JPS and MPO marks; SVGStereo).
 */

#ifndef STEREO_POLICY_H
#define STEREO_POLICY_H

#ifdef __cplusplus
extern "C" {
#endif

#define STEREO_POLICY_EXPORT __attribute__((visibility("default")))

/* Every packing of RFC 9559 StereoMode and H.264/H.265 Table D-8; the values of Stereo::Picture::Layout. */
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
 * The packing of a video frame: the stream's own mark first, whatever the shape (an anaglyph or "mono" holds no
 * packing); then the user's choice, when user_layout is not NONE; then the shape: a frame at least three times as wide
 * as high is side by side, left first (two pictures next to each other; a scope film is 2.39:1). swap_eyes, the user's
 * correction, reverses the order of whatever packing was decided. width and height are the frame as shown, the sample
 * aspect ratio applied. Returns 0, or -1 for a bad argument.
 */
STEREO_POLICY_EXPORT int stereo_policy_video_layout(
    int width, int height, const char *mark, int user_layout, int user_order, int swap_eyes, int *layout, int *order, int *source);

/*
 * Whether the views of a frame of this packing are full, each at its own shape and shown as it is, or half, squeezed
 * along the packing and stretched back to the frame. Side by side is full at a display aspect of 2.5 or more and top
 * and bottom at 1.2 or less, the gap between one picture (1.25 to 2.39) and two (the edition's mpv); interleaved,
 * checkerboard and quincunx views are always half; multiview, frame-sequential and block-laced views are whole frames,
 * full. Returns 1 for full, 0 for half, -1 for a bad argument or no packing.
 */
STEREO_POLICY_EXPORT int stereo_policy_video_full_views(int width, int height, int layout);

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
