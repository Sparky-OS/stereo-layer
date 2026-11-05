/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/*
 * The writers of stereo-mark.h. The technique of the MP4 writer, the order of the checks, the reading back and the
 * putting back are those of sView's StVideoStereoMark (movie player, MIT license), moved into the shared library; the
 * Matroska reader that judges every write is written here from the file's bytes (RFC 8794, EBML; RFC 9559, Matroska),
 * so that the values 15 to 20, which FFmpeg does not read, are read back too.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "stereo-mark.h"
#include "stereo-policy.h"

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/stereo3d.h>

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/* ---- the values ------------------------------------------------------------------------------------------------- */

/* RFC 9559 5.1.4.1.28.3 StereoMode, and the depth layouts IANA assigned on 26 October 2026 (docs/depth-marks.md). */
static const struct { int layout, order, anaglyph; } k_matroska[21] = {
    { STEREO_POLICY_LAYOUT_NONE, STEREO_POLICY_LEFT_FIRST, STEREO_POLICY_ANAGLYPH_NONE },                      /* 0 mono */
    { STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, STEREO_POLICY_LEFT_FIRST, STEREO_POLICY_ANAGLYPH_NONE },              /* 1 */
    { STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, STEREO_POLICY_RIGHT_FIRST, STEREO_POLICY_ANAGLYPH_NONE },           /* 2 */
    { STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, STEREO_POLICY_LEFT_FIRST, STEREO_POLICY_ANAGLYPH_NONE },            /* 3 */
    { STEREO_POLICY_LAYOUT_CHECKERBOARD, STEREO_POLICY_RIGHT_FIRST, STEREO_POLICY_ANAGLYPH_NONE },             /* 4 */
    { STEREO_POLICY_LAYOUT_CHECKERBOARD, STEREO_POLICY_LEFT_FIRST, STEREO_POLICY_ANAGLYPH_NONE },              /* 5 */
    { STEREO_POLICY_LAYOUT_ROWS, STEREO_POLICY_RIGHT_FIRST, STEREO_POLICY_ANAGLYPH_NONE },                     /* 6 */
    { STEREO_POLICY_LAYOUT_ROWS, STEREO_POLICY_LEFT_FIRST, STEREO_POLICY_ANAGLYPH_NONE },                      /* 7 */
    { STEREO_POLICY_LAYOUT_COLUMNS, STEREO_POLICY_RIGHT_FIRST, STEREO_POLICY_ANAGLYPH_NONE },                  /* 8 */
    { STEREO_POLICY_LAYOUT_COLUMNS, STEREO_POLICY_LEFT_FIRST, STEREO_POLICY_ANAGLYPH_NONE },                   /* 9 */
    { STEREO_POLICY_LAYOUT_NONE, STEREO_POLICY_LEFT_FIRST, STEREO_POLICY_ANAGLYPH_RED_CYAN },                  /* 10 */
    { STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, STEREO_POLICY_RIGHT_FIRST, STEREO_POLICY_ANAGLYPH_NONE },            /* 11 */
    { STEREO_POLICY_LAYOUT_NONE, STEREO_POLICY_LEFT_FIRST, STEREO_POLICY_ANAGLYPH_GREEN_MAGENTA },            /* 12 */
    { STEREO_POLICY_LAYOUT_BLOCK_LACED, STEREO_POLICY_LEFT_FIRST, STEREO_POLICY_ANAGLYPH_NONE },              /* 13 */
    { STEREO_POLICY_LAYOUT_BLOCK_LACED, STEREO_POLICY_RIGHT_FIRST, STEREO_POLICY_ANAGLYPH_NONE },             /* 14 */
    { STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE, STEREO_POLICY_PICTURE_FIRST, STEREO_POLICY_ANAGLYPH_NONE },    /* 15 */
    { STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE, STEREO_POLICY_DEPTH_FIRST, STEREO_POLICY_ANAGLYPH_NONE },      /* 16 */
    { STEREO_POLICY_LAYOUT_DEPTH_TOP_AND_BOTTOM, STEREO_POLICY_PICTURE_FIRST, STEREO_POLICY_ANAGLYPH_NONE },  /* 17 */
    { STEREO_POLICY_LAYOUT_DEPTH_TOP_AND_BOTTOM, STEREO_POLICY_DEPTH_FIRST, STEREO_POLICY_ANAGLYPH_NONE },    /* 18 */
    { STEREO_POLICY_LAYOUT_DEPTH_GRAPHICS, STEREO_POLICY_PICTURE_FIRST, STEREO_POLICY_ANAGLYPH_NONE },        /* 19 */
    { STEREO_POLICY_LAYOUT_DEPTH_BACKGROUND, STEREO_POLICY_PICTURE_FIRST, STEREO_POLICY_ANAGLYPH_NONE },      /* 20 */
};

static int order_ok(int order)
{
    return order == STEREO_POLICY_LEFT_FIRST || order == STEREO_POLICY_RIGHT_FIRST;
}

int stereo_mark_matroska_value(int layout, int order, int anaglyph)
{
    if (anaglyph < STEREO_POLICY_ANAGLYPH_NONE || anaglyph > STEREO_POLICY_ANAGLYPH_AMBER_BLUE)
        return -1;
    if (layout == STEREO_POLICY_LAYOUT_NONE) {
        switch (anaglyph) {
        case STEREO_POLICY_ANAGLYPH_NONE: return 0;
        case STEREO_POLICY_ANAGLYPH_RED_CYAN: return 10;
        case STEREO_POLICY_ANAGLYPH_GREEN_MAGENTA: return 12;
        default: return -1;
        }
    }
    /* a packing and an anaglyph at once is no layout */
    if (anaglyph != STEREO_POLICY_ANAGLYPH_NONE || !order_ok(order))
        return -1;
    switch (layout) {
    case STEREO_POLICY_LAYOUT_SIDE_BY_SIDE: return order ? 11 : 1;
    case STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM: return order ? 2 : 3;
    case STEREO_POLICY_LAYOUT_CHECKERBOARD: return order ? 4 : 5;
    case STEREO_POLICY_LAYOUT_ROWS: return order ? 6 : 7;
    case STEREO_POLICY_LAYOUT_COLUMNS: return order ? 8 : 9;
    /* FFmpeg's matroskaenc.c writes 13 and 14 for its frame sequence; the registry calls them block lacing */
    case STEREO_POLICY_LAYOUT_BLOCK_LACED:
    case STEREO_POLICY_LAYOUT_FRAME_SEQUENTIAL: return order ? 14 : 13;
    case STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE: return order ? 16 : 15;
    case STEREO_POLICY_LAYOUT_DEPTH_TOP_AND_BOTTOM: return order ? 18 : 17;
    case STEREO_POLICY_LAYOUT_DEPTH_GRAPHICS: return 19;
    case STEREO_POLICY_LAYOUT_DEPTH_BACKGROUND: return 20;
    default: return -1;
    }
}

/* Spherical Video V2, Stereoscopic 3D Video Box: 0 monoscopic, 1 top-bottom, 2 left-right, 4 right-left. */
int stereo_mark_mp4_value(int layout, int order, int anaglyph)
{
    if (anaglyph != STEREO_POLICY_ANAGLYPH_NONE)
        return -1;
    if (layout == STEREO_POLICY_LAYOUT_NONE)
        return 0;
    if (!order_ok(order))
        return -1;
    if (layout == STEREO_POLICY_LAYOUT_SIDE_BY_SIDE)
        return order ? 4 : 2;
    if (layout == STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM && !order)
        return 1;
    return -1;
}

int stereo_mark_webm_allows(int value)
{
    return value == 0 || value == 1 || value == 2 || value == 3 || value == 11;
}

int stereo_mark_value_layout(int container, int value, int *layout, int *order, int *anaglyph)
{
    int l, o, a;
    if (container == STEREO_MARK_CONTAINER_MATROSKA || container == STEREO_MARK_CONTAINER_WEBM) {
        if (value < 0 || value > 20)
            return 0;
        l = k_matroska[value].layout;
        o = k_matroska[value].order;
        a = k_matroska[value].anaglyph;
    } else if (container == STEREO_MARK_CONTAINER_MP4) {
        a = STEREO_POLICY_ANAGLYPH_NONE;
        o = STEREO_POLICY_LEFT_FIRST;
        switch (value) {
        case 0: l = STEREO_POLICY_LAYOUT_NONE; break;
        case 1: l = STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM; break;
        case 2: l = STEREO_POLICY_LAYOUT_SIDE_BY_SIDE; break;
        case 4: l = STEREO_POLICY_LAYOUT_SIDE_BY_SIDE; o = STEREO_POLICY_RIGHT_FIRST; break;
        default: return 0;
        }
    } else {
        return 0;
    }
    if (layout)
        *layout = l;
    if (order)
        *order = o;
    if (anaglyph)
        *anaglyph = a;
    return 1;
}

const char *stereo_mark_status_name(int status)
{
    switch (status) {
    case STEREO_MARK_WRITTEN: return "written";
    case STEREO_MARK_UNCHANGED: return "unchanged";
    case STEREO_MARK_BAD_ARGUMENT: return "bad-argument";
    case STEREO_MARK_NOT_LOCAL: return "not-local";
    case STEREO_MARK_CONTAINER: return "container";
    case STEREO_MARK_NO_TRACK: return "no-track";
    case STEREO_MARK_LAYOUT: return "layout";
    case STEREO_MARK_WEBM_LAYOUT: return "webm-layout";
    case STEREO_MARK_MP4_DEPTH: return "mp4-depth";
    case STEREO_MARK_STREAM_DECLARES: return "stream-declares";
    case STEREO_MARK_MP4_NO_ROOM: return "mp4-no-room";
    case STEREO_MARK_MP4_ENTRIES: return "mp4-entries";
    case STEREO_MARK_BROKEN: return "broken";
    case STEREO_MARK_TOOL_MISSING: return "tool-missing";
    case STEREO_MARK_WRITE_FAILED: return "write-failed";
    case STEREO_MARK_READ_BACK: return "read-back";
    case STEREO_MARK_MP4_VIEWS: return "mp4-views";
    case STEREO_MARK_VIEWS_LAYOUT: return "views-layout";
    case STEREO_MARK_NOT_LACED: return "not-laced";
    default: return "unknown";
    }
}

/* ---- files ------------------------------------------------------------------------------------------------------ */

static int pread_all(int fd, uint64_t off, void *buf, size_t n)
{
    uint8_t *p = buf;
    size_t done = 0;
    while (done < n) {
        ssize_t r = pread(fd, p + done, n - done, (off_t)(off + done));
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return 0;
        done += (size_t)r;
    }
    return 1;
}

static int pwrite_all(int fd, uint64_t off, const void *buf, size_t n)
{
    const uint8_t *p = buf;
    size_t done = 0;
    while (done < n) {
        ssize_t r = pwrite(fd, p + done, n - done, (off_t)(off + done));
        if (r < 0 && errno == EINTR)
            continue;
        if (r <= 0)
            return 0;
        done += (size_t)r;
    }
    return 1;
}

/* ---- EBML (RFC 8794) and the Matroska tracks (RFC 9559) ---------------------------------------------------------- */

#define EBML_HEADER 0x1A45DFA3u
#define EBML_DOCTYPE 0x4282u
#define MKV_SEGMENT 0x18538067u
#define MKV_TRACKS 0x1654AE6Bu
#define MKV_TRACKENTRY 0xAEu
#define MKV_TRACKTYPE 0x83u
#define MKV_VIDEO 0xE0u
#define MKV_STEREOMODE 0x53B8u
#define MKV_TRACKNUMBER 0xD7u
#define MKV_PIXELWIDTH 0xB0u
#define MKV_PIXELHEIGHT 0xBAu
#define MKV_DISPLAYWIDTH 0x54B0u
#define MKV_DISPLAYHEIGHT 0x54BAu
#define MKV_CLUSTER 0x1F43B675u
#define MKV_SIMPLEBLOCK 0xA3u
#define MKV_BLOCKGROUP 0xA0u
#define MKV_BLOCK 0xA1u
#define MKV_MAX_VIDEO_TRACKS 256

typedef struct {
    uint64_t id, data, size;
    int unknown;
} ebml_el;

/* An element header at off inside the parent that ends at limit. A size that overruns the parent is not an element. */
static int ebml_read(int fd, uint64_t off, uint64_t limit, ebml_el *e)
{
    uint8_t b[12];
    if (off >= limit)
        return 0;
    size_t avail = limit - off < sizeof(b) ? (size_t)(limit - off) : sizeof(b);
    if (avail < 2 || !pread_all(fd, off, b, avail))
        return 0;
    if (b[0] == 0)
        return 0;
    int idlen = __builtin_clz((unsigned)b[0]) - 24 + 1;
    if (idlen > 4 || (size_t)idlen >= avail || b[idlen] == 0)
        return 0;
    int szlen = __builtin_clz((unsigned)b[idlen]) - 24 + 1;
    if ((size_t)(idlen + szlen) > avail)
        return 0;
    uint64_t id = 0;
    for (int i = 0; i < idlen; ++i)
        id = id << 8 | b[i];
    unsigned mask = 0xFFu >> szlen;
    uint64_t size = b[idlen] & mask;
    int all_ones = (b[idlen] & mask) == mask;
    for (int i = 1; i < szlen; ++i) {
        size = size << 8 | b[idlen + i];
        if (b[idlen + i] != 0xFF)
            all_ones = 0;
    }
    e->id = id;
    e->data = off + (uint64_t)idlen + (uint64_t)szlen;
    e->size = size;
    e->unknown = all_ones;
    if (!e->unknown && (e->data > limit || e->size > limit - e->data))
        return 0;
    return 1;
}

static int ebml_uint(int fd, const ebml_el *e, uint64_t *v)
{
    uint8_t b[8];
    if (e->unknown || e->size == 0 || e->size > 8 || !pread_all(fd, e->data, b, (size_t)e->size))
        return 0;
    uint64_t x = 0;
    for (size_t i = 0; i < e->size; ++i)
        x = x << 8 | b[i];
    *v = x;
    return 1;
}

typedef struct {
    int webm;
    int is_matroska;
    int tracks;
    int values[MKV_MAX_VIDEO_TRACKS];
    /* of each video track: its TrackNumber, PixelWidth and PixelHeight, DisplayWidth and DisplayHeight (0 when absent) */
    int number[MKV_MAX_VIDEO_TRACKS], pw[MKV_MAX_VIDEO_TRACKS], ph[MKV_MAX_VIDEO_TRACKS], dw[MKV_MAX_VIDEO_TRACKS], dh[MKV_MAX_VIDEO_TRACKS];
    uint64_t seg_data, seg_end;
} mkv_info;

/* The StereoMode of the video tracks, in the order of the Tracks element. Returns 1, or 0 for a file this cannot walk. */
static int mkv_scan(int fd, uint64_t fsize, mkv_info *mi)
{
    memset(mi, 0, sizeof(*mi));
    ebml_el h;
    if (!ebml_read(fd, 0, fsize, &h) || h.id != EBML_HEADER || h.unknown)
        return 0;
    char doctype[32] = "";
    for (uint64_t off = h.data; off < h.data + h.size;) {
        ebml_el c;
        if (!ebml_read(fd, off, h.data + h.size, &c) || c.unknown)
            break;
        if (c.id == EBML_DOCTYPE && c.size > 0 && c.size < sizeof(doctype) && pread_all(fd, c.data, doctype, (size_t)c.size))
            doctype[c.size] = 0;
        off = c.data + c.size;
    }
    mi->webm = strcmp(doctype, "webm") == 0;
    mi->is_matroska = mi->webm || strcmp(doctype, "matroska") == 0;
    if (!mi->is_matroska)
        return 1;

    uint64_t pos = h.data + h.size;
    ebml_el seg;
    int found = 0;
    for (int i = 0; i < 64 && !found; ++i) {
        if (!ebml_read(fd, pos, fsize, &seg))
            return 0;
        if (seg.id == MKV_SEGMENT)
            found = 1;
        else if (seg.unknown)
            return 0;
        else
            pos = seg.data + seg.size;
    }
    if (!found)
        return 0;
    uint64_t end = seg.unknown ? fsize : seg.data + seg.size;
    mi->seg_data = seg.data;
    mi->seg_end = end;
    pos = seg.data;
    for (long iter = 0; iter < 4000000 && pos < end; ++iter) {
        ebml_el c;
        if (!ebml_read(fd, pos, end, &c))
            return 0;
        if (c.id == MKV_TRACKS) {
            if (c.unknown)
                return 0;
            for (uint64_t t = c.data; t < c.data + c.size;) {
                ebml_el te;
                if (!ebml_read(fd, t, c.data + c.size, &te) || te.unknown)
                    return 0;
                if (te.id == MKV_TRACKENTRY) {
                    uint64_t type = 0, number = 0, pixw = 0, pixh = 0, disw = 0, dish = 0;
                    int stereo = -1;
                    for (uint64_t f = te.data; f < te.data + te.size;) {
                        ebml_el fe;
                        if (!ebml_read(fd, f, te.data + te.size, &fe) || fe.unknown)
                            return 0;
                        if (fe.id == MKV_TRACKTYPE) {
                            ebml_uint(fd, &fe, &type);
                        } else if (fe.id == MKV_TRACKNUMBER) {
                            ebml_uint(fd, &fe, &number);
                        } else if (fe.id == MKV_VIDEO) {
                            for (uint64_t v = fe.data; v < fe.data + fe.size;) {
                                ebml_el ve;
                                if (!ebml_read(fd, v, fe.data + fe.size, &ve) || ve.unknown)
                                    return 0;
                                uint64_t sm;
                                if (ve.id == MKV_STEREOMODE && ebml_uint(fd, &ve, &sm))
                                    stereo = (int)sm;
                                else if (ve.id == MKV_PIXELWIDTH)
                                    ebml_uint(fd, &ve, &pixw);
                                else if (ve.id == MKV_PIXELHEIGHT)
                                    ebml_uint(fd, &ve, &pixh);
                                else if (ve.id == MKV_DISPLAYWIDTH)
                                    ebml_uint(fd, &ve, &disw);
                                else if (ve.id == MKV_DISPLAYHEIGHT)
                                    ebml_uint(fd, &ve, &dish);
                                v = ve.data + ve.size;
                            }
                        }
                        f = fe.data + fe.size;
                    }
                    if (type == 1 && mi->tracks < MKV_MAX_VIDEO_TRACKS) {
                        mi->number[mi->tracks] = (int)number;
                        mi->pw[mi->tracks] = (int)pixw;
                        mi->ph[mi->tracks] = (int)pixh;
                        mi->dw[mi->tracks] = (int)disw;
                        mi->dh[mi->tracks] = (int)dish;
                        mi->values[mi->tracks++] = stereo;
                    }
                }
                t = te.data + te.size;
            }
            return 1;
        }
        /* a cluster of unknown size (a live stream) cannot be skipped */
        if (c.unknown)
            return 0;
        pos = c.data + c.size;
    }
    return 0;
}


/*
 * Whether the blocks of the track lace two frames: "packing two frames together in a Block that uses lacing" (RFC 9559
 * section 18.10; the Block header of section 10.1: the track number, a 16-bit timestamp, the flags with the lacing in bits
 * 0x06, then the number of frames minus one for a laced block). A few blocks of the first clusters are sampled. Returns 1
 * when every sampled block of the track laces two frames, 0 when one does not, -1 when none could be read.
 */
static int mkv_blocks_laced(int fd, const mkv_info *mi, int track_number)
{
    int sampled = 0, laced = 1;
    uint64_t pos = mi->seg_data;
    for (int clusters = 0; clusters < 64 && pos < mi->seg_end && sampled < 8; ++clusters) {
        ebml_el c;
        if (!ebml_read(fd, pos, mi->seg_end, &c))
            break;
        pos = c.data + (c.unknown ? 0 : c.size);
        if (c.id != MKV_CLUSTER) {
            if (c.unknown)
                break;
            continue;
        }
        if (c.unknown)
            break;
        for (uint64_t b = c.data; b < c.data + c.size && sampled < 8;) {
            ebml_el be;
            if (!ebml_read(fd, b, c.data + c.size, &be) || be.unknown)
                break;
            b = be.data + be.size;
            ebml_el blk = be;
            if (be.id == MKV_BLOCKGROUP) {
                int found = 0;
                for (uint64_t g = be.data; g < be.data + be.size;) {
                    ebml_el ge;
                    if (!ebml_read(fd, g, be.data + be.size, &ge) || ge.unknown)
                        break;
                    if (ge.id == MKV_BLOCK) {
                        blk = ge;
                        found = 1;
                        break;
                    }
                    g = ge.data + ge.size;
                }
                if (!found)
                    continue;
            } else if (be.id != MKV_SIMPLEBLOCK) {
                continue;
            }
            uint8_t h[8];
            size_t n = blk.size < sizeof(h) ? (size_t)blk.size : sizeof(h);
            if (n < 4 || !pread_all(fd, blk.data, h, n) || h[0] == 0)
                continue;
            int tl = __builtin_clz((unsigned)h[0]) - 24 + 1;
            if ((size_t)tl + 3 > n || tl > 4)
                continue;
            int number = h[0] & (0xFF >> tl);
            for (int i = 1; i < tl; ++i)
                number = number << 8 | h[i];
            if (number != track_number)
                continue;
            int lacing = (h[tl + 2] >> 1) & 3;
            ++sampled;
            if (lacing == 0 || (size_t)tl + 4 > n || h[tl + 3] != 1)
                laced = 0;
        }
    }
    return sampled ? laced : -1;
}

/* ---- ISO base media files (ISO/IEC 14496-12) --------------------------------------------------------------------- */

#define FOURCC(a, b, c, d) (((uint32_t)(a) << 24) | ((uint32_t)(b) << 16) | ((uint32_t)(c) << 8) | (uint32_t)(d))

static uint32_t get32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static uint64_t get64(const uint8_t *p) { return (uint64_t)get32(p) << 32 | get32(p + 4); }
static void put32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }
static void put64(uint8_t *p, uint64_t v) { put32(p, (uint32_t)(v >> 32)); put32(p + 4, (uint32_t)v); }

/* One box: size, type, largesize when size is 1, size 0 for the last box of the file (ISO/IEC 14496-12 4.2). */
typedef struct {
    size_t off, hdr;
    uint64_t size;
    uint32_t type;
    int to_end;
} mp4box;

static int read_box(const uint8_t *d, size_t pos, size_t end, mp4box *b)
{
    if (pos > end || end - pos < 8)
        return 0;
    b->off = pos;
    b->hdr = 8;
    b->size = get32(d + pos);
    b->type = get32(d + pos + 4);
    b->to_end = 0;
    if (b->size == 1) {
        if (end - pos < 16)
            return 0;
        b->size = get64(d + pos + 8);
        b->hdr = 16;
    } else if (b->size == 0) {
        b->size = end - pos;
        b->to_end = 1;
    }
    return b->size >= b->hdr && b->size <= (uint64_t)(end - pos);
}

static int find_child(const uint8_t *d, const mp4box *parent, size_t start, uint32_t type, mp4box *child)
{
    size_t end = parent->off + (size_t)parent->size;
    for (size_t pos = start; pos < end;) {
        mp4box b;
        if (!read_box(d, pos, end, &b))
            return 0;
        if (b.type == type) {
            *child = b;
            return 1;
        }
        pos += (size_t)b.size;
    }
    return 0;
}

static int grow_box(uint8_t *d, const mp4box *b, uint64_t growth)
{
    if (b->to_end)
        return 1;
    if (b->hdr == 16) {
        put64(d + b->off + 8, b->size + growth);
        return 1;
    }
    if (b->size + growth > 0xFFFFFFFFu)
        return 0;
    put32(d + b->off, (uint32_t)(b->size + growth));
    return 1;
}

/* moov > the nth video trak (hdlr 'vide') > mdia > minf > stbl > stsd > its one visual sample entry */
static int mp4_locate(const uint8_t *d, size_t len, int nth, mp4box path[7])
{
    mp4box moov;
    if (!read_box(d, 0, len, &moov) || moov.type != FOURCC('m', 'o', 'o', 'v'))
        return STEREO_MARK_BROKEN;
    int seen = 0;
    size_t moov_end = moov.off + (size_t)moov.size;
    for (size_t pos = moov.off + moov.hdr; pos < moov_end;) {
        mp4box trak;
        if (!read_box(d, pos, moov_end, &trak))
            return STEREO_MARK_BROKEN;
        pos += (size_t)trak.size;
        mp4box mdia, hdlr;
        if (trak.type != FOURCC('t', 'r', 'a', 'k') || !find_child(d, &trak, trak.off + trak.hdr, FOURCC('m', 'd', 'i', 'a'), &mdia)
            || !find_child(d, &mdia, mdia.off + mdia.hdr, FOURCC('h', 'd', 'l', 'r'), &hdlr) || hdlr.size < hdlr.hdr + 12)
            continue;
        if (get32(d + hdlr.off + hdlr.hdr + 8) != FOURCC('v', 'i', 'd', 'e') || ++seen != nth)
            continue;
        mp4box minf, stbl, stsd, entry;
        if (!find_child(d, &mdia, mdia.off + mdia.hdr, FOURCC('m', 'i', 'n', 'f'), &minf)
            || !find_child(d, &minf, minf.off + minf.hdr, FOURCC('s', 't', 'b', 'l'), &stbl)
            || !find_child(d, &stbl, stbl.off + stbl.hdr, FOURCC('s', 't', 's', 'd'), &stsd) || stsd.size < stsd.hdr + 8)
            return STEREO_MARK_BROKEN;
        /* stsd: FullBox, then entry_count, then the sample entries */
        if (get32(d + stsd.off + stsd.hdr + 4) != 1)
            return STEREO_MARK_MP4_ENTRIES;
        if (!read_box(d, stsd.off + stsd.hdr + 8, stsd.off + (size_t)stsd.size, &entry))
            return STEREO_MARK_BROKEN;
        path[0] = moov;
        path[1] = trak;
        path[2] = mdia;
        path[3] = minf;
        path[4] = stbl;
        path[5] = stsd;
        path[6] = entry;
        return 0;
    }
    return STEREO_MARK_NO_TRACK;
}

typedef struct {
    int present;
    size_t box_off;   /* the st3d box, when present */
    uint8_t mode;
    size_t insert;    /* where st3d goes when absent */
} st3d_info;

/* The boxes of a VisualSampleEntry (its 78 bytes first): st3d, and where it would go ("before optional boxes at the
 * end of the VisualSampleEntry definition such as the CleanApertureBox and PixelAspectRatioBox", and before sv3d). */
static int entry_scan(const uint8_t *d, const mp4box *entry, st3d_info *out)
{
    memset(out, 0, sizeof(*out));
    size_t entry_end = entry->off + (size_t)entry->size;
    size_t pos = entry->off + entry->hdr + 78;
    if (pos > entry_end)
        return STEREO_MARK_BROKEN;
    for (;;) {
        if (entry_end - pos < 8) {
            /* nothing left, or QuickTime's 32-bit zero terminator: the box goes before it */
            for (size_t t = pos; t < entry_end; ++t)
                if (d[t] != 0)
                    return STEREO_MARK_BROKEN;
            if (!out->insert)
                out->insert = pos;
            return 0;
        }
        mp4box child;
        if (!read_box(d, pos, entry_end, &child) || child.to_end)
            return STEREO_MARK_BROKEN;
        if (child.type == FOURCC('s', 't', '3', 'd')) {
            /* Stereoscopic3D extends FullBox('st3d', 0, 0) { unsigned int(8) stereo_mode; } */
            if (child.size != 13 || child.hdr != 8 || d[child.off + 8] != 0)
                return STEREO_MARK_BROKEN;
            out->present = 1;
            out->box_off = child.off;
            out->mode = d[child.off + 12];
            return 0;
        }
        if (!out->insert && (child.type == FOURCC('c', 'l', 'a', 'p') || child.type == FOURCC('p', 'a', 's', 'p') || child.type == FOURCC('s', 'v', '3', 'd')))
            out->insert = pos;
        pos += (size_t)child.size;
    }
}

typedef struct {
    uint64_t moov_off, moov_size;
    uint64_t next_off, next_size;
    uint32_t next_type;
    size_t next_hdr;
    uint8_t *moov;
} mp4file;

/* The top-level boxes: the moov box (read whole), and the box that follows it. */
static int mp4_load(int fd, uint64_t fsize, mp4file *f)
{
    memset(f, 0, sizeof(*f));
    int have_moov = 0, have_next = 0;
    for (uint64_t pos = 0; pos + 8 <= fsize;) {
        uint8_t h[16];
        if (!pread_all(fd, pos, h, 8))
            break;
        uint64_t size = get32(h);
        uint32_t type = get32(h + 4);
        size_t hdr = 8;
        if (size == 1) {
            if (fsize - pos < 16 || !pread_all(fd, pos + 8, h + 8, 8))
                break;
            size = get64(h + 8);
            hdr = 16;
        } else if (size == 0) {
            size = fsize - pos;
        }
        if (size < hdr || size > fsize - pos)
            break;
        if (have_moov && !have_next) {
            f->next_off = pos;
            f->next_size = size;
            f->next_type = type;
            f->next_hdr = hdr;
            have_next = 1;
        }
        if (type == FOURCC('m', 'o', 'o', 'v')) {
            if (have_moov)
                return STEREO_MARK_BROKEN;
            have_moov = 1;
            f->moov_off = pos;
            f->moov_size = size;
        }
        pos += size;
    }
    if (!have_moov || f->moov_size > 256u * 1024u * 1024u)
        return STEREO_MARK_BROKEN;
    f->moov = malloc((size_t)f->moov_size);
    if (!f->moov)
        return STEREO_MARK_WRITE_FAILED;
    if (!pread_all(fd, f->moov_off, f->moov, (size_t)f->moov_size)) {
        free(f->moov);
        f->moov = NULL;
        return STEREO_MARK_BROKEN;
    }
    return 0;
}

/* ---- what the file carries now ---------------------------------------------------------------------------------- */

static int detect(int fd, uint64_t fsize, int *container, mkv_info *mi)
{
    uint8_t b[12];
    *container = STEREO_MARK_CONTAINER_OTHER;
    memset(mi, 0, sizeof(*mi));
    if (fsize >= 12 && pread_all(fd, 0, b, sizeof(b))) {
        if (get32(b) == EBML_HEADER) {
            if (!mkv_scan(fd, fsize, mi)) {
                /* an EBML file whose tracks cannot be walked is not one to write into */
                *container = STEREO_MARK_CONTAINER_MATROSKA;
                return STEREO_MARK_BROKEN;
            }
            if (mi->is_matroska)
                *container = mi->webm ? STEREO_MARK_CONTAINER_WEBM : STEREO_MARK_CONTAINER_MATROSKA;
        } else if (get32(b + 4) == FOURCC('f', 't', 'y', 'p')) {
            *container = STEREO_MARK_CONTAINER_MP4;
        }
    }
    return 0;
}

static int read_state(int fd, uint64_t fsize, int video_track, int *container, int *value)
{
    mkv_info mi;
    *value = -1;
    int st = detect(fd, fsize, container, &mi);
    if (st)
        return st;
    switch (*container) {
    case STEREO_MARK_CONTAINER_MATROSKA:
    case STEREO_MARK_CONTAINER_WEBM:
        if (video_track > mi.tracks)
            return STEREO_MARK_NO_TRACK;
        *value = mi.values[video_track - 1];
        return 0;
    case STEREO_MARK_CONTAINER_MP4: {
        mp4file f;
        st = mp4_load(fd, fsize, &f);
        if (st)
            return st;
        mp4box path[7];
        st = mp4_locate(f.moov, (size_t)f.moov_size, video_track, path);
        if (st == 0) {
            st3d_info si;
            st = entry_scan(f.moov, &path[6], &si);
            if (st == 0 && si.present)
                *value = si.mode;
        }
        free(f.moov);
        return st;
    }
    default:
        return STEREO_MARK_CONTAINER;
    }
}

static int open_regular(const char *path, int flags, uint64_t *size)
{
    struct stat sb;
    if (!path || !path[0] || stat(path, &sb) != 0 || !S_ISREG(sb.st_mode))
        return -1;
    int fd = open(path, flags | O_CLOEXEC);
    if (fd < 0)
        return -2;
    *size = (uint64_t)sb.st_size;
    return fd;
}

int stereo_mark_read(const char *path, int video_track, int *container, int *value)
{
    int c = STEREO_MARK_CONTAINER_OTHER, v = -1;
    uint64_t size;
    if (!path || video_track < 1)
        return STEREO_MARK_BAD_ARGUMENT;
    int fd = open_regular(path, O_RDONLY, &size);
    if (fd < 0)
        return fd == -1 ? STEREO_MARK_NOT_LOCAL : STEREO_MARK_BROKEN;
    int st = read_state(fd, size, video_track, &c, &v);
    close(fd);
    if (container)
        *container = c;
    if (value)
        *value = v;
    return st;
}

/* ---- the video stream's own declaration (the frame packing SEI), which comes before the container's ----------------- */

static int declared_by_stream(const char *path, int nth, int *layout, int *order)
{
    AVFormatContext *ctx = NULL;
    int found = 0;
    if (avformat_open_input(&ctx, path, NULL, NULL) < 0)
        return 0;
    if (avformat_find_stream_info(ctx, NULL) < 0) {
        avformat_close_input(&ctx);
        return 0;
    }
    int idx = -1, seen = 0;
    for (unsigned i = 0; i < ctx->nb_streams; ++i) {
        AVStream *s = ctx->streams[i];
        if (s->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !(s->disposition & AV_DISPOSITION_ATTACHED_PIC) && ++seen == nth)
            idx = (int)i;
    }
    if (idx < 0) {
        avformat_close_input(&ctx);
        return 0;
    }
    for (unsigned i = 0; i < ctx->nb_streams; ++i)
        ctx->streams[i]->discard = (int)i == idx ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    AVStream *st = ctx->streams[idx];
    const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
    AVCodecContext *cc = codec ? avcodec_alloc_context3(codec) : NULL;
    if (cc && avcodec_parameters_to_context(cc, st->codecpar) >= 0) {
        /* the decoder adds the container's declaration to each picture: leave it out, the stream's own is asked */
        av_packet_side_data_remove(cc->coded_side_data, &cc->nb_coded_side_data, AV_PKT_DATA_STEREO3D);
        cc->thread_count = 1;
        if (avcodec_open2(cc, codec, NULL) >= 0) {
            AVPacket *pkt = av_packet_alloc();
            AVFrame *frame = av_frame_alloc();
            int eof = 0, done = 0;
            for (int i = 0; i < 1000 && !done && pkt && frame; ++i) {
                if (!eof && av_read_frame(ctx, pkt) < 0) {
                    eof = 1;
                    avcodec_send_packet(cc, NULL);
                } else if (!eof) {
                    int ours = pkt->stream_index == idx;
                    if (ours)
                        avcodec_send_packet(cc, pkt);
                    av_packet_unref(pkt);
                    if (!ours)
                        continue;
                }
                int r = avcodec_receive_frame(cc, frame);
                if (r == 0) {
                    done = 1;
                    const AVFrameSideData *sd = av_frame_get_side_data(frame, AV_FRAME_DATA_STEREO3D);
                    if (sd && sd->size >= sizeof(AVStereo3D)) {
                        const AVStereo3D *s3 = (const AVStereo3D *)sd->data;
                        int l = -1;
                        switch (s3->type) {
                        case AV_STEREO3D_2D: l = STEREO_POLICY_LAYOUT_NONE; break;
                        case AV_STEREO3D_SIDEBYSIDE: l = STEREO_POLICY_LAYOUT_SIDE_BY_SIDE; break;
                        case AV_STEREO3D_TOPBOTTOM: l = STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM; break;
                        case AV_STEREO3D_FRAMESEQUENCE: l = STEREO_POLICY_LAYOUT_FRAME_SEQUENTIAL; break;
                        case AV_STEREO3D_CHECKERBOARD: l = STEREO_POLICY_LAYOUT_CHECKERBOARD; break;
                        case AV_STEREO3D_SIDEBYSIDE_QUINCUNX: l = STEREO_POLICY_LAYOUT_SIDE_BY_SIDE_QUINCUNX; break;
                        case AV_STEREO3D_LINES: l = STEREO_POLICY_LAYOUT_ROWS; break;
                        case AV_STEREO3D_COLUMNS: l = STEREO_POLICY_LAYOUT_COLUMNS; break;
                        default: break;
                        }
                        if (l >= 0) {
                            *layout = l;
                            *order = (s3->flags & AV_STEREO3D_FLAG_INVERT) ? STEREO_POLICY_RIGHT_FIRST : STEREO_POLICY_LEFT_FIRST;
                            found = 1;
                        }
                    }
                } else if (r == AVERROR_EOF || (eof && r == AVERROR(EAGAIN))) {
                    break;
                }
            }
            av_frame_free(&frame);
            av_packet_free(&pkt);
        }
    }
    avcodec_free_context(&cc);
    avformat_close_input(&ctx);
    return found;
}

/* The same packing and eye order: frame sequence and block lacing are one value of the registry, and a layout that has
 * no eye order (2D, graphics, background) has none to compare. */
static int same_layout(int l1, int o1, int l2, int o2)
{
    if (l1 == STEREO_POLICY_LAYOUT_FRAME_SEQUENTIAL)
        l1 = STEREO_POLICY_LAYOUT_BLOCK_LACED;
    if (l2 == STEREO_POLICY_LAYOUT_FRAME_SEQUENTIAL)
        l2 = STEREO_POLICY_LAYOUT_BLOCK_LACED;
    if (l1 != l2)
        return 0;
    if (l1 == STEREO_POLICY_LAYOUT_NONE || l1 == STEREO_POLICY_LAYOUT_DEPTH_GRAPHICS || l1 == STEREO_POLICY_LAYOUT_DEPTH_BACKGROUND)
        return 1;
    return o1 == o2;
}

/* ---- writing ---------------------------------------------------------------------------------------------------- */

/* Run mkvpropedit on the file without a shell. value below 0 deletes the element. Returns 0, or a status. */
static int run_mkvpropedit(const char *path, int video_track, int value, int dw, int dh, char *out, size_t outsize)
{
    char file[4096], track[32], set[48], setw[48], seth[48];
    if (path[0] == '-')
        snprintf(file, sizeof(file), "./%s", path);
    else
        snprintf(file, sizeof(file), "%s", path);
    snprintf(track, sizeof(track), "track:v%d", video_track);
    snprintf(set, sizeof(set), "stereo-mode=%d", value);
    snprintf(setw, sizeof(setw), "display-width=%d", dw);
    snprintf(seth, sizeof(seth), "display-height=%d", dh);
    /* dw below 0: the display size is left alone; 0: the display size is deleted (the file carried none); else it is set */
    const char *argv[16] = { "mkvpropedit", file, "--edit", track, value >= 0 ? "--set" : "--delete", value >= 0 ? set : "stereo-mode" };
    int ac = 6;
    if (dw > 0 && dh > 0) {
        argv[ac++] = "--set";
        argv[ac++] = setw;
        argv[ac++] = "--set";
        argv[ac++] = seth;
    } else if (dw == 0) {
        argv[ac++] = "--delete";
        argv[ac++] = "display-width";
        argv[ac++] = "--delete";
        argv[ac++] = "display-height";
    }
    argv[ac] = NULL;

    int po[2], pe[2];
    out[0] = 0;
    if (pipe2(po, O_CLOEXEC) != 0)
        return STEREO_MARK_WRITE_FAILED;
    if (pipe2(pe, O_CLOEXEC) != 0) {
        close(po[0]);
        close(po[1]);
        return STEREO_MARK_WRITE_FAILED;
    }
    pid_t pid = fork();
    if (pid < 0) {
        snprintf(out, outsize, "%s", strerror(errno));
        close(po[0]);
        close(po[1]);
        close(pe[0]);
        close(pe[1]);
        return STEREO_MARK_WRITE_FAILED;
    }
    if (pid == 0) {
        dup2(po[1], 1);
        dup2(po[1], 2);
        int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0)
            dup2(devnull, 0);
        setenv("LC_ALL", "C", 1);
        execvp("mkvpropedit", (char *const *)argv);
        int e = errno;
        ssize_t ignored = write(pe[1], &e, sizeof(e));
        (void)ignored;
        _exit(127);
    }
    close(po[1]);
    close(pe[1]);
    int exec_errno = 0;
    ssize_t got;
    do {
        got = read(pe[0], &exec_errno, sizeof(exec_errno));
    } while (got < 0 && errno == EINTR);
    close(pe[0]);
    if (got != (ssize_t)sizeof(exec_errno)) {
        size_t used = 0;
        exec_errno = 0;
        for (;;) {
            char buf[512];
            ssize_t n = read(po[0], buf, sizeof(buf));
            if (n < 0 && errno == EINTR)
                continue;
            if (n <= 0)
                break;
            if (used + 1 < outsize) {
                size_t take = (size_t)n < outsize - 1 - used ? (size_t)n : outsize - 1 - used;
                memcpy(out + used, buf, take);
                used += take;
            }
        }
        out[used] = 0;
    }
    close(po[0]);
    int status = 0;
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    if (exec_errno) {
        snprintf(out, outsize, "%s", strerror(exec_errno));
        return exec_errno == ENOENT ? STEREO_MARK_TOOL_MISSING : STEREO_MARK_WRITE_FAILED;
    }
    /* mkvpropedit exits with 0 on success, 1 with warnings, 2 on errors */
    if (WIFEXITED(status) && (WEXITSTATUS(status) == 0 || WEXITSTATUS(status) == 1))
        return 0;
    if (!out[0])
        snprintf(out, outsize, "mkvpropedit exited with status %d", WIFEXITED(status) ? WEXITSTATUS(status) : -1);
    return STEREO_MARK_WRITE_FAILED;
}

/* The bytes written over, kept for putting back. */
typedef struct {
    uint64_t offset, file_size;
    uint8_t *data;
    size_t len;
} backup;

/* st3d into the moov box in place: the box keeps its offset, so every chunk offset of the media data stays valid
 * (ISO/IEC 14496-12 8.7.5). A moov box that grows must be the file's last box or be followed by a free space box
 * ('free' or 'skip', 8.1.2) with room for the 13 bytes. */
static int write_mp4(int fd, uint64_t fsize, int video_track, int mode, backup *bk, char *details, size_t dsize)
{
    mp4file f;
    int st = mp4_load(fd, fsize, &f);
    if (st)
        return st;
    mp4box path[7];
    st3d_info si;
    memset(&si, 0, sizeof(si));
    st = mp4_locate(f.moov, (size_t)f.moov_size, video_track, path);
    if (st == 0)
        st = entry_scan(f.moov, &path[6], &si);
    if (st) {
        free(f.moov);
        return st;
    }
    size_t growth = 0;
    if (si.present) {
        f.moov[si.box_off + 12] = (uint8_t)mode;
    } else {
        growth = 13;
        for (int i = 0; i < 7; ++i)
            if (!grow_box(f.moov, &path[i], growth)) {
                free(f.moov);
                return STEREO_MARK_MP4_NO_ROOM;
            }
        const uint8_t box[13] = { 0, 0, 0, 13, 's', 't', '3', 'd', 0, 0, 0, 0, (uint8_t)mode };
        uint8_t *grown = realloc(f.moov, (size_t)f.moov_size + growth);
        if (!grown) {
            free(f.moov);
            return STEREO_MARK_WRITE_FAILED;
        }
        f.moov = grown;
        memmove(f.moov + si.insert + growth, f.moov + si.insert, (size_t)f.moov_size - si.insert);
        memcpy(f.moov + si.insert, box, growth);
    }
    size_t region_len = (size_t)f.moov_size + growth;
    uint8_t free_head[8];
    int with_free = 0;
    if (growth) {
        int is_last = f.moov_off + f.moov_size == fsize;
        int is_free = f.next_off == f.moov_off + f.moov_size && (f.next_type == FOURCC('f', 'r', 'e', 'e') || f.next_type == FOURCC('s', 'k', 'i', 'p'))
                      && f.next_hdr == 8 && (f.next_size == growth || f.next_size >= growth + 8);
        if (!is_last && !is_free) {
            free(f.moov);
            return STEREO_MARK_MP4_NO_ROOM;
        }
        if (!is_last && f.next_size != growth) {
            /* the free space box, shorter by the bytes st3d takes; its content is not read */
            put32(free_head, (uint32_t)(f.next_size - growth));
            memcpy(free_head + 4, "free", 4);
            with_free = 1;
        }
    }
    size_t total = region_len + (with_free ? 8 : 0);
    uint8_t *buf = malloc(total);
    if (!buf) {
        free(f.moov);
        return STEREO_MARK_WRITE_FAILED;
    }
    memcpy(buf, f.moov, region_len);
    if (with_free)
        memcpy(buf + region_len, free_head, 8);
    free(f.moov);

    bk->offset = f.moov_off;
    bk->file_size = fsize;
    bk->len = total < fsize - f.moov_off ? total : (size_t)(fsize - f.moov_off);
    bk->data = malloc(bk->len ? bk->len : 1);
    if (!bk->data || !pread_all(fd, f.moov_off, bk->data, bk->len)) {
        snprintf(details, dsize, "%s", strerror(errno));
        free(buf);
        return STEREO_MARK_WRITE_FAILED;
    }
    int ok = pwrite_all(fd, f.moov_off, buf, total) && fsync(fd) == 0;
    int e = errno;
    free(buf);
    if (!ok) {
        snprintf(details, dsize, "%s", strerror(e));
        return STEREO_MARK_WRITE_FAILED;
    }
    return STEREO_MARK_WRITTEN;
}

static int restore_mp4(const char *path, const backup *bk)
{
    int fd = open(path, O_WRONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    int ok = pwrite_all(fd, bk->offset, bk->data, bk->len) && ftruncate(fd, (off_t)bk->file_size) == 0 && fsync(fd) == 0;
    return close(fd) == 0 && ok;
}

static int mkv_open_info(const char *path, mkv_info *mi)
{
    uint64_t size;
    int fd = open_regular(path, O_RDONLY, &size);
    if (fd < 0)
        return 0;
    int ok = mkv_scan(fd, size, mi);
    close(fd);
    return ok;
}

static int save_impl(const char *path, int video_track, int layout, int order, int anaglyph, int views, stereo_mark_views_result *ext)
{
    stereo_mark_result local;
    stereo_mark_result *r = ext ? &ext->base : &local;
    memset(r, 0, sizeof(*r));
    if (ext) {
        ext->plane_width = ext->plane_height = 0;
        ext->laced = -1;
    }
    r->value = r->previous = r->now = -1;
    r->declared_layout = r->declared_order = -1;
    if (!path || video_track < 1)
        return STEREO_MARK_BAD_ARGUMENT;

    uint64_t size;
    int fd = open_regular(path, O_RDONLY, &size);
    if (fd == -1)
        return STEREO_MARK_NOT_LOCAL;
    if (fd < 0) {
        snprintf(r->details, sizeof(r->details), "%s", strerror(errno));
        return STEREO_MARK_WRITE_FAILED;
    }
    int container, previous;
    int st = read_state(fd, size, video_track, &container, &previous);
    r->container = container;
    close(fd);
    if (container == STEREO_MARK_CONTAINER_OTHER)
        return STEREO_MARK_CONTAINER;

    /* what the container can name */
    int value;
    if (container == STEREO_MARK_CONTAINER_MP4) {
        if (layout >= STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE && layout <= STEREO_POLICY_LAYOUT_DEPTH_BACKGROUND)
            return STEREO_MARK_MP4_DEPTH;
        value = stereo_mark_mp4_value(layout, order, anaglyph);
    } else {
        value = stereo_mark_matroska_value(layout, order, anaglyph);
    }
    r->value = value;
    if (value < 0)
        return STEREO_MARK_LAYOUT;
    if (container == STEREO_MARK_CONTAINER_WEBM && !stereo_mark_webm_allows(value))
        return STEREO_MARK_WEBM_LAYOUT;
    if (views != STEREO_MARK_VIEWS_UNSPECIFIED) {
        if (views != STEREO_MARK_VIEWS_HALF && views != STEREO_MARK_VIEWS_FULL)
            return STEREO_MARK_BAD_ARGUMENT;
        if (layout != STEREO_POLICY_LAYOUT_SIDE_BY_SIDE && layout != STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM)
            return STEREO_MARK_VIEWS_LAYOUT;
        if (container == STEREO_MARK_CONTAINER_MP4)
            return STEREO_MARK_MP4_VIEWS;
    }
    if (st)
        return st;
    r->previous = r->now = previous;

    /* what the Matroska track carries besides its mark: the size of its planes and whether its blocks lace both eyes */
    mkv_info mi;
    int track_dw = 0, track_dh = 0, want_dw = -1, want_dh = -1;
    if (container != STEREO_MARK_CONTAINER_MP4) {
        if (!mkv_open_info(path, &mi) || video_track > mi.tracks)
            return STEREO_MARK_BROKEN;
        track_dw = mi.dw[video_track - 1];
        track_dh = mi.dh[video_track - 1];
        if (ext) {
            ext->plane_width = track_dw;
            ext->plane_height = track_dh;
        }
        if (value == 13 || value == 14) {
            int fd2 = open(path, O_RDONLY | O_CLOEXEC);
            int laced = fd2 >= 0 ? mkv_blocks_laced(fd2, &mi, mi.number[video_track - 1]) : -1;
            if (fd2 >= 0)
                close(fd2);
            if (ext)
                ext->laced = laced;
            if (laced != 1) {
                snprintf(r->details, sizeof(r->details), laced == 0 ? "the blocks of the track do not lace two frames" : "no block of the track could be read");
                return STEREO_MARK_NOT_LACED;
            }
        }
        if (views != STEREO_MARK_VIEWS_UNSPECIFIED) {
            int pw = mi.pw[video_track - 1], ph = mi.ph[video_track - 1];
            int side = layout == STEREO_POLICY_LAYOUT_SIDE_BY_SIDE;
            if (pw <= 0 || ph <= 0 || (views == STEREO_MARK_VIEWS_FULL && ((side && (pw & 1)) || (!side && (ph & 1)))))
                return STEREO_MARK_VIEWS_LAYOUT;
            want_dw = views == STEREO_MARK_VIEWS_FULL && side ? pw / 2 : pw;
            want_dh = views == STEREO_MARK_VIEWS_FULL && !side ? ph / 2 : ph;
        }
    }

    /* a declaration in the video stream itself (the frame packing SEI) comes before the container's */
    int dl, dord;
    if (declared_by_stream(path, video_track, &dl, &dord)) {
        int al = layout;
        if (layout == STEREO_POLICY_LAYOUT_NONE && anaglyph != STEREO_POLICY_ANAGLYPH_NONE)
            al = -2;
        if (same_layout(dl, dord, al, order) && (want_dw < 0 || (track_dw == want_dw && track_dh == want_dh)))
            return STEREO_MARK_UNCHANGED;
        if (same_layout(dl, dord, al, order))
            goto write;
        r->declared_layout = dl;
        r->declared_order = dord;
        return STEREO_MARK_STREAM_DECLARES;
    }
    if (previous == value && (want_dw < 0 || (track_dw == want_dw && track_dh == want_dh)))
        return STEREO_MARK_UNCHANGED;

write:;
    /* write, read back, put back what does not read back */
    int c2, v2;
    if (container == STEREO_MARK_CONTAINER_MP4) {
        fd = open(path, O_RDWR | O_CLOEXEC);
        if (fd < 0) {
            snprintf(r->details, sizeof(r->details), "%s", strerror(errno));
            return STEREO_MARK_WRITE_FAILED;
        }
        backup bk = { 0, 0, NULL, 0 };
        st = write_mp4(fd, size, video_track, value, &bk, r->details, sizeof(r->details));
        int e = errno;
        int closed = close(fd) == 0;
        if (st != STEREO_MARK_WRITTEN) {
            free(bk.data);
            return st;
        }
        if (!closed) {
            snprintf(r->details, sizeof(r->details), "%s", strerror(e));
            restore_mp4(path, &bk);
            free(bk.data);
            return STEREO_MARK_WRITE_FAILED;
        }
        int rb = stereo_mark_read(path, video_track, &c2, &v2);
        r->now = rb == 0 ? v2 : -1;
        if (rb != 0 || v2 != value) {
            int back = restore_mp4(path, &bk);
            int c3, v3;
            r->now = stereo_mark_read(path, video_track, &c3, &v3) == 0 ? v3 : -1;
            snprintf(r->details, sizeof(r->details), "the file read back st3d %d after the write; %s", rb == 0 ? v2 : -1,
                     back ? "put back as it was" : "putting it back failed");
            free(bk.data);
            return STEREO_MARK_READ_BACK;
        }
        free(bk.data);
        return STEREO_MARK_WRITTEN;
    }

    st = run_mkvpropedit(path, video_track, value, want_dw, want_dh, r->details, sizeof(r->details));
    if (st) {
        r->now = stereo_mark_read(path, video_track, &c2, &v2) == 0 ? v2 : -1;
        return st;
    }
    int rb = stereo_mark_read(path, video_track, &c2, &v2);
    r->now = rb == 0 ? v2 : -1;
    mkv_info after;
    int plane_ok = 1;
    if (want_dw >= 0) {
        plane_ok = mkv_open_info(path, &after) && video_track <= after.tracks && after.dw[video_track - 1] == want_dw && after.dh[video_track - 1] == want_dh;
    }
    if (ext && mkv_open_info(path, &after) && video_track <= after.tracks) {
        ext->plane_width = after.dw[video_track - 1];
        ext->plane_height = after.dh[video_track - 1];
    }
    if (rb != 0 || v2 != value || !plane_ok) {
        char ignored[64];
        run_mkvpropedit(path, video_track, previous, want_dw < 0 ? -1 : track_dw, want_dw < 0 ? -1 : track_dh, ignored, sizeof(ignored));
        if (ext && mkv_open_info(path, &after) && video_track <= after.tracks) {
            ext->plane_width = after.dw[video_track - 1];
            ext->plane_height = after.dh[video_track - 1];
        }
        int c3, v3;
        r->now = stereo_mark_read(path, video_track, &c3, &v3) == 0 ? v3 : -1;
        snprintf(r->details, sizeof(r->details), "the file read back StereoMode %d after the write; %s", rb == 0 ? v2 : -1,
                 r->now == previous ? "put back as it was" : "putting it back failed");
        return STEREO_MARK_READ_BACK;
    }
    return STEREO_MARK_WRITTEN;
}

int stereo_mark_save(const char *path, int video_track, int layout, int order, int anaglyph, stereo_mark_result *result)
{
    stereo_mark_views_result ext;
    int st = save_impl(path, video_track, layout, order, anaglyph, STEREO_MARK_VIEWS_UNSPECIFIED, &ext);
    if (result)
        *result = ext.base;
    return st;
}

int stereo_mark_save_views(const char *path, int video_track, int layout, int order, int anaglyph, int views, stereo_mark_views_result *result)
{
    stereo_mark_views_result local;
    return save_impl(path, video_track, layout, order, anaglyph, views, result ? result : &local);
}

int stereo_mark_read_detail(const char *path, int video_track, stereo_mark_detail *detail)
{
    stereo_mark_detail local;
    stereo_mark_detail *d = detail ? detail : &local;
    memset(d, 0, sizeof(*d));
    d->value = -1;
    d->laced = -1;
    int st = stereo_mark_read(path, video_track, &d->container, &d->value);
    if (st != STEREO_MARK_WRITTEN)
        return st;
    if (d->container == STEREO_MARK_CONTAINER_MATROSKA || d->container == STEREO_MARK_CONTAINER_WEBM) {
        mkv_info mi;
        if (!mkv_open_info(path, &mi) || video_track > mi.tracks)
            return STEREO_MARK_BROKEN;
        d->pixel_width = mi.pw[video_track - 1];
        d->pixel_height = mi.ph[video_track - 1];
        d->display_width = mi.dw[video_track - 1];
        d->display_height = mi.dh[video_track - 1];
        int fd = open(path, O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            d->laced = mkv_blocks_laced(fd, &mi, mi.number[video_track - 1]);
            close(fd);
        }
    }
    return STEREO_MARK_WRITTEN;
}
