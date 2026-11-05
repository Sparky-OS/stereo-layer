/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/*
 * A video's 3D layout written into its container's own mark, when the person asked for it.
 *
 * Matroska and WebM: the track's StereoMode (RFC 9559 section 5.1.4.1.28.3, Table of stereo modes; the values 15 to
 * 20 are the packed picture-and-depth layouts IANA assigned on 26 October 2026, docs/depth-marks.md), written with
 * MKVToolNix's mkvpropedit. WebM allows only 0, 1, 2, 3 and 11 (WebM Container Guidelines, StereoMode).
 * MP4, MOV and their relatives: the Stereoscopic3D box st3d of the visual sample entry (Spherical Video V2, section
 * "Stereoscopic 3D Video Box": 0 monoscopic, 1 top-bottom, 2 left-right, 4 right-left; the box goes before clap, pasp
 * and sv3d), written in place, so that no chunk offset of ISO/IEC 14496-12 section 8.7.5 moves. st3d has no depth
 * value: a depth layout into MP4 is refused.
 *
 * The technique is that of sView's StVideoStereoMark (movie player, MIT license), moved here so every program writes
 * the same way: sView, Dragon Player and the players after them.
 *
 * The program writes only after the person accepted ("save the correction into the file"): this library never
 * decides to touch a file, and a refusal never changes one. Every write is read back from the file's bytes by a reader
 * of its own; a write that does not read back is put back as it was and said (STEREO_MARK_READ_BACK).
 *
 * layout, order and anaglyph are the values of enum stereo_policy_layout, stereo_policy_order (or, for the side by
 * side and top and bottom depth layouts, STEREO_POLICY_PICTURE_FIRST and STEREO_POLICY_DEPTH_FIRST) and
 * stereo_policy_anaglyph of stereo-policy.h; "not 3D" is layout NONE with anaglyph NONE. video_track counts the
 * file's video tracks from 1, in the order of the file (mkvpropedit's track:v1, the moov box's video trak boxes).
 */

#ifndef STEREO_MARK_H
#define STEREO_MARK_H

#ifdef __cplusplus
extern "C" {
#endif

#ifndef STEREO_AV_EXPORT
#define STEREO_AV_EXPORT __attribute__((visibility("default")))
#endif

enum stereo_mark_container {
    STEREO_MARK_CONTAINER_OTHER = 0,    /* a container with no 3D mark this library writes */
    STEREO_MARK_CONTAINER_MATROSKA = 1,
    STEREO_MARK_CONTAINER_WEBM = 2,     /* Matroska with DocType "webm" (RFC 8794 section 11.2.6) */
    STEREO_MARK_CONTAINER_MP4 = 3,      /* ISO base media files: MP4, MOV, M4V and their relatives */
};

enum stereo_mark_status {
    STEREO_MARK_WRITTEN = 0,         /* the file carries the layout now, and it was read back */
    STEREO_MARK_UNCHANGED,           /* the file carried the layout already; nothing was written */
    STEREO_MARK_BAD_ARGUMENT,
    STEREO_MARK_NOT_LOCAL,           /* not a local regular file */
    STEREO_MARK_CONTAINER,           /* the container has no 3D mark this library writes */
    STEREO_MARK_NO_TRACK,            /* the file has no such video track */
    STEREO_MARK_LAYOUT,              /* the container's mark has no value for the packing or the eye order */
    STEREO_MARK_WEBM_LAYOUT,         /* WebM allows only StereoMode 0, 1, 2, 3 and 11 */
    STEREO_MARK_MP4_DEPTH,           /* st3d has no value for a picture packed with its depth map */
    STEREO_MARK_STREAM_DECLARES,     /* the video stream declares another layout itself (frame packing SEI), and it comes first */
    STEREO_MARK_MP4_NO_ROOM,         /* st3d cannot be added without moving the media data */
    STEREO_MARK_MP4_ENTRIES,         /* the track has several sample descriptions */
    STEREO_MARK_BROKEN,              /* the file's structure could not be read */
    STEREO_MARK_TOOL_MISSING,        /* mkvpropedit is not installed */
    STEREO_MARK_WRITE_FAILED,        /* writing failed; details says why */
    STEREO_MARK_READ_BACK,           /* the written file did not read back with the layout; it was put back as it was */
};

typedef struct stereo_mark_result {
    int container;       /* enum stereo_mark_container */
    int value;           /* the container's value asked for, -1 when it has none */
    int previous;        /* what the file carried before, -1 when it carried no mark */
    int now;             /* what the file carries when the call ends, -1 when it carries no mark */
    int declared_layout; /* STEREO_MARK_STREAM_DECLARES: the layout the stream declares (stereo_policy_layout) */
    int declared_order;  /* and its eye order (stereo_policy_order) */
    char details[320];   /* English, for the log: what the operating system or mkvpropedit said */
} stereo_mark_result;

/*
 * The container's value for a layout, -1 when it has none.
 * Matroska: RFC 9559 StereoMode; the library's reading of the table (stereo-content.c): 0 mono, 1 and 11 side by side
 * (left first, right first), 3 and 2 top and bottom, 5 and 4 checkerboard, 7 and 6 rows, 9 and 8 columns, 10 red and
 * cyan, 12 green and magenta, 13 and 14 block laced and frame sequential (FFmpeg's matroskaenc.c writes 13 and 14 for
 * its frame sequence), 15 to 20 the depth layouts. Quincunx, multiview and the other anaglyph pairs have no value.
 * MP4 (st3d): 0 mono, 2 and 4 side by side, 1 top and bottom left first; no value for the other packings, the eye
 * order of top and bottom, or a depth layout.
 */
STEREO_AV_EXPORT int stereo_mark_matroska_value(int layout, int order, int anaglyph);
STEREO_AV_EXPORT int stereo_mark_mp4_value(int layout, int order, int anaglyph);
/* Whether WebM allows a StereoMode value: 1, else 0. */
STEREO_AV_EXPORT int stereo_mark_webm_allows(int value);
/* The layout a container's value stands for (1), or 0 for a value that stands for none. Any output pointer may be null. */
STEREO_AV_EXPORT int stereo_mark_value_layout(int container, int value, int *layout, int *order, int *anaglyph);

/*
 * What the file carries now for the video track: the container, and its mark's value (-1 for none). Reads the file's
 * bytes only, never a decoder. Returns STEREO_MARK_WRITTEN when it could read (the name is for the success of a read),
 * or NOT_LOCAL, CONTAINER, NO_TRACK, BROKEN or BAD_ARGUMENT.
 */
STEREO_AV_EXPORT int stereo_mark_read(const char *path, int video_track, int *container, int *value);

/*
 * Write the layout into the file. result may be null. Returns the status; unless it is WRITTEN, the file is as it was
 * (a failed or unread-back write is put back), and the result says why.
 */
STEREO_AV_EXPORT int stereo_mark_save(const char *path, int video_track, int layout, int order, int anaglyph, stereo_mark_result *result);

/* The status as a word for logs and tests: "written", "unchanged", "no-room"... Never null. */
STEREO_AV_EXPORT const char *stereo_mark_status_name(int status);

#ifdef __cplusplus
}
#endif
#endif
