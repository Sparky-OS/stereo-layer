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

#include <stddef.h>

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
    /* A picture and its depth map in one frame (docs/depth-marks.md): next to each other; one above the other (HDMI
     * 1.4a's "L + depth", 3D_Structure 0100, Figure H-5, picture first); four rows, picture, depth, graphics and
     * graphics depth (HDMI 1.4a's 0101, Figure H-6); four quarters, picture, depth, background and background depth.
     * The libstereo-dibr core draws the two views from them. */
    STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE = 10,
    STEREO_POLICY_LAYOUT_DEPTH_TOP_AND_BOTTOM = 11,
    STEREO_POLICY_LAYOUT_DEPTH_GRAPHICS = 12,
    STEREO_POLICY_LAYOUT_DEPTH_BACKGROUND = 13,
    /* A picture whose depth map is an image or a track of its own (an HEIF auxiliary depth item, an MPEG-C Part 3
     * auxiliary video stream, an H.265 depth layer, a Dynamic Depth container item). */
    STEREO_POLICY_LAYOUT_DEPTH_SEPARATE = 14,
};

/* For the side by side and top and bottom depth layouts, which comes first: the picture (the value of LEFT_FIRST) or
 * the depth map (RIGHT_FIRST). */
#define STEREO_POLICY_PICTURE_FIRST 0
#define STEREO_POLICY_DEPTH_FIRST 1

/* Whether a layout holds a picture and its depth rather than two views: 1, else 0. */
STEREO_POLICY_EXPORT int stereo_policy_is_depth_layout(int layout);

/*
 * A site's mark for a video: the packing a video site declares in the page's keywords, for a video whose stream and
 * container say nothing (the YouTube convention of 2010, the video keywords yt3d:*). The one reader of the edition's
 * programs (the browser, PlasmaTube): it comes after the stream's own mark and before the shape rule, and the user's
 * choice comes before all of them. Case and spaces do not matter and a malformed value is read as far as it can be
 * (docs/yt3d.md lists every form):
 *   yt3d:enable=LR | RL | LonR | RonL   side by side, left eye first | right eye first; top and bottom, left eye on top |
 *                                      right eye on top (YouTube's help of 2010, "Upload 3D video")
 *   yt3d:enable=true (also 1, yes, on)  the first form: what yt3d:left and yt3d:right place, else side by side, right eye first
 *                                      (YouTube's player took it for cross-eyed; yt3d:swap=true says parallel, left eye first)
 *   yt3d:enable=false (also 0, no, off) the site says the video is not 3D
 *   yt3d:swap=true (also 1, yes, on, or the bare tag)   the eyes the other way round, whatever else decided (so the first form
 *                                      with it is left eye first)
 *   yt3d:left=l_t_r_b, yt3d:right=l_t_r_b   where each eye is, as the left, top, right and bottom fractions of the frame
 *                                      (the halves of a frame are the two supported arrangements; any other rectangle is
 *                                      not an arrangement of this policy and the keywords give no mark, return value 2)
 *   yt3d:aspect=W:H (also W/H, a number)  the shape of ONE view as the site shows it: a frame of that shape holds half
 *                                      views, one twice as wide (side by side) or as tall (top and bottom) full views
 * keywords is an array of count keywords, one tag each. Returns 1 when the keywords make a mark (layout NONE for "not 3D",
 * order LEFT_FIRST or RIGHT_FIRST, aspect_num and aspect_den the shape of one view, 0 and 0 when none is given), 0 when
 * they carry no yt3d tag, 2 when they carry yt3d tags that name no arrangement of this policy (an unsupported rectangle,
 * an unknown enable value), -1 for a bad argument. An output pointer may be null.
 */
STEREO_POLICY_EXPORT int stereo_policy_site_mark(
    const char *const *keywords, int count, int *layout, int *order, int *aspect_num, int *aspect_den);

/* The same for the keywords as a page hands them on in a list, tags separated by commas or line breaks (the keywords meta
 * tag of a watch page). The tags are read by stereo_policy_site_mark(): one reader. */
STEREO_POLICY_EXPORT int stereo_policy_site_mark_list(const char *keywords, int *layout, int *order, int *aspect_num, int *aspect_den);

/*
 * The mark as the Stereo3DLayout word stereo_policy_read_mark() reads ("sideBySideLeftFirst", "topBottomRightFirst", "mono"),
 * so a program that takes marks as words needs no table of its own. Returns 1, or -1 when the layout is not a packing of
 * a site mark. word receives a string of the library, valid for ever.
 */
STEREO_POLICY_EXPORT int stereo_policy_site_mark_word(int layout, int order, const char **word);

/*
 * The depth of a display: the one source of how much a stereo output may sink and pop, for every program that puts depth on it
 * (the desktop's windows, the browser's WebXR pages and, after them, the CSS depth levels sunk, screen and popped). Read in this
 * order of importance: (1) the person's choice, built on (2) the display. The display's default is what is comfortable on it:
 *   - an angular budget, the same on every display of one kind, so a framed screen of any size and any viewing distance gets the
 *     parallax that looks alike: the sunk limit 0.198 degrees and the popped limit 0.079 degrees of disparity at the eyes (the
 *     desktop's defaults of 10 and 4 pixels on a 24 inch monitor of 1920 pixels, seen from 0.8 m);
 *   - the viewing distance of the display, from its physical size: 0.6 m up to a diagonal of 0.5 m, then 1.8 m more for each
 *     metre of diagonal (a 24 inch monitor 0.8 m, a 46 inch television 1.8 m, a 65 inch one 2.7 m);
 *   - the kind: a framed screen seen through glasses 1.0; a screen shown in colours (anaglyph) 0.7, as ghosting grows with
 *     parallax; a headset (a flat headset, or a phone in one) 2.5, which shows the screen as a window in a virtual room and
 *     takes more separation, at a fixed window distance of 0.7 m whatever the size.
 * The limits are in pixels of a view 1920 pixels wide (the unit of the desktop's "3D Depth" settings, scaled to each screen by
 * its view's width). The person's choice is a strength in percent (100: the display's default) and, for each limit, an absolute
 * value in pixels that replaces it (0: none). A size that is not known (a width or height of 0 or less) is taken for the
 * 24 inch monitor and size_known says so, so a program can tell the person instead of guessing silently.
 */
enum stereo_policy_display_kind {
    STEREO_POLICY_DISPLAY_FRAMED = 0,
    STEREO_POLICY_DISPLAY_ANAGLYPH = 1,
    STEREO_POLICY_DISPLAY_HEADSET = 2,
};

struct stereo_policy_depth {
    double sunk_px;            /* how far behind the screen, pixels at a view width of 1920 */
    double popped_px;          /* how far in front of it */
    int viewing_distance_mm;   /* where the viewer sits, the distance the screen is seen from */
    int size_known;            /* 1 when the physical size was given, 0 when the reference monitor stood in */
};

/* Returns 0, or -1 for a bad argument (a kind that is not listed, a strength under 0 or over 1000, a negative override). */
STEREO_POLICY_EXPORT int stereo_policy_display_depth(int kind, int width_mm, int height_mm, double strength_percent,
    double sunk_override_px, double popped_override_px, struct stereo_policy_depth *out);

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
 * anaglyphRedLeft and anaglyphRedRight; the depth layouts of docs/depth-marks.md, depthSideBySide and depthTopBottom
 * followed by PictureFirst or DepthFirst (the order PICTURE_FIRST or DEPTH_FIRST), depthGraphicsTopBottom,
 * depthBackgroundQuarters and depthSeparate. Returns 1 for a value of the set, 0 for any other value (one picture: layout
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
 * elements' default (the pixel size after cropping) and says nothing. upsampled_aspect_ratio is the H.265 frame packing SEI's flag (D.3.16): 1 for
 * half views, 0 for full ones, -1 (STEREO_POLICY_UPSAMPLED_UNKNOWN) for H.264, which has none, or when it is not at hand. Layouts other than side by
 * side and top and bottom are decided as in stereo_policy_video_full_views(). When nothing says, stereo_policy_video_views_by_shape() decides, with
 * the frame as shown (the sample aspect ratio applied): a marked frame of a single picture's shape carries half views, one twice as wide or as tall
 * carries full views. decided_by, which may be null, receives STEREO_POLICY_VIEWS_BY_MARK, _BY_SHAPE or _BY_LAYOUT. Returns 1 for full views, 0 for
 * half, -1 for a bad argument or no packing.
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
STEREO_POLICY_EXPORT int stereo_policy_matroska_plane(
    int coded_width, int coded_height, int sar_num, int sar_den, int layout, int *plane_width, int *plane_height);

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

/*
 * The person's per-file viewing choice: the input packing chosen by hand for a file (with its order, or the colours of
 * an anaglyph), the view (both views, one of them, or one flat picture, "Not 3D") and the swap of the eyes. It is kept
 * in the programs' own state, one small entry per file, and never in the file: $XDG_STATE_HOME/stereo-policy/files
 * ($HOME/.local/state when unset; XDG Base Directory Specification: "state data that should persist between
 * (application) restarts", "current state of the application that can be reused on a restart"), the entry named from
 * the file's real path, as a player names a resume position. One store for every program built on the policy, so a
 * file manager, its thumbnailer and a viewer show a file alike. A file that is moved or renamed loses its entry, as it
 * loses a resume position. The swap is over whatever decided the layout, the file's own mark included; a packing chosen
 * here is the person's correction of that file and comes before its mark (a declaration can be wrong), where a
 * caller's general choice comes after it. Nothing here reads or writes the file, its attributes or its times.
 */
enum stereo_policy_view {
    STEREO_POLICY_VIEW_STEREO = 0,
    STEREO_POLICY_VIEW_LEFT_ONLY = 1,
    STEREO_POLICY_VIEW_RIGHT_ONLY = 2,
    STEREO_POLICY_VIEW_MONO = 3,
};

struct stereo_policy_file_choice {
    int layout; /* enum stereo_policy_layout; NONE leaves the packing to the file's mark and the shape */
    int order; /* STEREO_POLICY_LEFT_FIRST or RIGHT_FIRST of that packing */
    int anaglyph; /* enum stereo_policy_anaglyph */
    int view; /* enum stereo_policy_view */
    int swap_eyes; /* 1 swaps the eyes of the file as stored */
};

/* Reads the entry of path into choice (zeroed when there is none). Returns 1 when the file has an entry, 0 when it has
 * none, -1 with errno set when the store cannot be read or the argument is bad. */
STEREO_POLICY_EXPORT int stereo_policy_file_choice(const char *path, struct stereo_policy_file_choice *choice);

/* Keeps choice for path, replacing its entry as a whole; a choice of all zeros, or null, removes the entry. Returns 0,
 * or -1 with errno set (the store cannot be written: say so to the person, the choice is not kept). */
STEREO_POLICY_EXPORT int stereo_policy_set_file_choice(const char *path, const struct stereo_policy_file_choice *choice);

/* Where the entry of path is, or would be, for a message or a test. Returns 0, or -1 with errno set. */
STEREO_POLICY_EXPORT int stereo_policy_file_choice_entry(const char *path, char *entry, size_t size);

#ifdef __cplusplus
}
#endif

#endif
