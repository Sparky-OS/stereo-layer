/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/*
 * The writers of the 3D layout into Matroska, WebM and MP4 (stereo-mark.h), against real files: ffmpeg makes them,
 * mkvmerge and libavformat read what was written (the oracles that are not our reader), and every refusal leaves the
 * file byte for byte as it was. Needs ffmpeg, mkvmerge and mkvpropedit in PATH (mkvtoolnix); a missing tool fails the test.
 */

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "stereo-mark.h"
#include "stereo-policy.h"

#include <libavformat/avformat.h>
#include <libavcodec/packet.h>
#include <libavutil/dict.h>
#include <libavutil/stereo3d.h>

#include <dirent.h>
#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int fails;
static int checks;
static char dir[512];

#define CHECK(cond, ...) \
    do { \
        ++checks; \
        if (!(cond)) { \
            ++fails; \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
            fprintf(stderr, __VA_ARGS__); \
            fputc('\n', stderr); \
        } \
    } while (0)

static int run(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static int run(const char *fmt, ...)
{
    char cmd[16384];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    return system(cmd);
}

static char *path_of(const char *name)
{
    static char bufs[64][700];
    static int next;
    char *b = bufs[next++ & 63];
    snprintf(b, 700, "%s/%s", dir, name);
    return b;
}

static unsigned char *slurp(const char *path, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        *len = 0;
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *b = malloc(n > 0 ? n : 1);
    size_t got = fread(b, 1, n, f);
    fclose(f);
    *len = got;
    return b;
}

/* The bytes of a file kept to compare it with the file after a refusal. */
typedef struct { unsigned char *data; size_t len; } Snapshot;
static Snapshot snap(const char *path)
{
    Snapshot s;
    s.data = slurp(path, &s.len);
    return s;
}
static int same_as(const Snapshot *s, const char *path)
{
    size_t len;
    unsigned char *now = slurp(path, &len);
    int same = now && len == s->len && memcmp(now, s->data, len) == 0;
    free(now);
    return same;
}
static void drop(Snapshot *s) { free(s->data); s->data = NULL; }

/* The StereoMode of the nth video track as mkvmerge reads it (the oracle for every value, 15 to 20 included). */
static int mkvmerge_stereo(const char *path, int nth)
{
    char cmd[900];
    snprintf(cmd, sizeof(cmd), "mkvmerge -J '%s' 2>/dev/null", path);
    FILE *p = popen(cmd, "r");
    if (!p)
        return -2;
    char *out = malloc(1 << 20);
    size_t n = fread(out, 1, (1 << 20) - 1, p);
    out[n] = 0;
    pclose(p);
    int seen = 0, value = -1;
    char *tracks = strstr(out, "\"tracks\"");
    char *at = tracks;
    while (at && (at = strstr(at, "\"codec\":"))) {
        char *next = strstr(at + 8, "\"codec\":");
        char saved = 0;
        if (next) {
            saved = *next;
            *next = 0;
        }
        if (strstr(at, "\"type\": \"video\"") && ++seen == nth) {
            char *s = strstr(at, "\"stereo_mode\":");
            value = s ? atoi(s + 14) : -1;
        }
        if (next)
            *next = saved;
        at = next;
    }
    free(out);
    return value;
}


/* The DisplayWidth (which 0) or DisplayHeight (1) of the nth video track as mkvmerge reads it ("display_dimensions": "WxH"), 0 when absent. */
static int mkvmerge_display(const char *path, int nth, int which)
{
    char cmd[900];
    snprintf(cmd, sizeof(cmd), "mkvmerge -J '%s' 2>/dev/null", path);
    FILE *p = popen(cmd, "r");
    if (!p)
        return -2;
    char *out = malloc(1 << 20);
    size_t n = fread(out, 1, (1 << 20) - 1, p);
    out[n] = 0;
    pclose(p);
    int seen = 0, value = 0;
    char *at = strstr(out, "\"tracks\"");
    while (at && (at = strstr(at, "\"codec\":"))) {
        char *next = strstr(at + 8, "\"codec\":");
        char saved = 0;
        if (next) {
            saved = *next;
            *next = 0;
        }
        if (strstr(at, "\"type\": \"video\"") && ++seen == nth) {
            char *s = strstr(at, "\"display_dimensions\": \"");
            int w = 0, h = 0;
            if (s && (s = strchr(s + 21, '"')) && sscanf(s + 1, "%dx%d", &w, &h) == 2)
                value = which ? h : w;
        }
        if (next)
            *next = saved;
        at = next;
    }
    free(out);
    return value;
}

static int video_stream_index(AVFormatContext *ctx, int nth)
{
    int seen = 0;
    for (unsigned i = 0; i < ctx->nb_streams; ++i)
        if (ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && !(ctx->streams[i]->disposition & AV_DISPOSITION_ATTACHED_PIC) && ++seen == nth)
            return i;
    return -1;
}

/* What FFmpeg reads for the nth video track: Matroska's stereo_mode tag, or st3d's side data. Returns 1 when it reads a mark. */
static int libav_mark(const char *path, int nth, char *tag, size_t tagsize, int *st3d_type, int *st3d_flags)
{
    AVFormatContext *ctx = NULL;
    tag[0] = 0;
    *st3d_type = -1;
    *st3d_flags = 0;
    if (avformat_open_input(&ctx, path, NULL, NULL) < 0)
        return -1;
    int idx = video_stream_index(ctx, nth);
    int found = 0;
    if (idx >= 0) {
        AVStream *st = ctx->streams[idx];
        const AVDictionaryEntry *e = av_dict_get(st->metadata, "stereo_mode", NULL, 0);
        if (e) {
            snprintf(tag, tagsize, "%s", e->value);
            found = 1;
        }
        const AVPacketSideData *sd = av_packet_side_data_get(st->codecpar->coded_side_data, st->codecpar->nb_coded_side_data, AV_PKT_DATA_STEREO3D);
        if (sd) {
            *st3d_type = ((const AVStereo3D *)sd->data)->type;
            *st3d_flags = ((const AVStereo3D *)sd->data)->flags;
            found = 1;
        }
    }
    avformat_close_input(&ctx);
    return found;
}

/* How many packets the file demuxes to (all streams): the media data is intact when it does not change. */
static int packet_count(const char *path)
{
    AVFormatContext *ctx = NULL;
    if (avformat_open_input(&ctx, path, NULL, NULL) < 0)
        return -1;
    int n = 0;
    AVPacket *pkt = av_packet_alloc();
    while (av_read_frame(ctx, pkt) >= 0) {
        ++n;
        av_packet_unref(pkt);
    }
    av_packet_free(&pkt);
    avformat_close_input(&ctx);
    return n;
}

/* The top-level boxes of an ISO base media file, as "ftyp moov mdat". */
static void boxes_of(const char *path, char *out, size_t outsize)
{
    size_t len;
    unsigned char *b = slurp(path, &len);
    out[0] = 0;
    for (size_t pos = 0; b && pos + 8 <= len;) {
        uint64_t size = (uint64_t)b[pos] << 24 | b[pos + 1] << 16 | b[pos + 2] << 8 | b[pos + 3];
        char type[5] = { b[pos + 4], b[pos + 5], b[pos + 6], b[pos + 7], 0 };
        if (size == 1 && pos + 16 <= len) {
            size = 0;
            for (int i = 0; i < 8; ++i)
                size = size << 8 | b[pos + 8 + i];
        } else if (size == 0) {
            size = len - pos;
        }
        if (strlen(out) + 6 < outsize) {
            strcat(out, out[0] ? " " : "");
            strcat(out, type);
        }
        if (size < 8)
            break;
        pos += size;
    }
    free(b);
}

static void make_mkv(const char *name, const char *extra)
{
    CHECK(run("ffmpeg -v error -y -f lavfi -i testsrc=size=160x90:rate=10:duration=1 -f lavfi -i smptebars=size=160x90:rate=10:duration=1 "
              "-map 0 -map 1 -c:v mpeg4 %s '%s'", extra, path_of(name)) == 0, "ffmpeg could not make %s", name);
}

/* Table: every packing and both eye orders against the container values */
static void test_values(void)
{
    struct { int layout, order, anaglyph, matroska, mp4; } t[] = {
        { STEREO_POLICY_LAYOUT_NONE, 0, STEREO_POLICY_ANAGLYPH_NONE, 0, 0 },
        { STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, STEREO_POLICY_LEFT_FIRST, 0, 1, 2 },
        { STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, STEREO_POLICY_RIGHT_FIRST, 0, 11, 4 },
        { STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, STEREO_POLICY_LEFT_FIRST, 0, 3, 1 },
        { STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, STEREO_POLICY_RIGHT_FIRST, 0, 2, -1 },
        { STEREO_POLICY_LAYOUT_CHECKERBOARD, STEREO_POLICY_LEFT_FIRST, 0, 5, -1 },
        { STEREO_POLICY_LAYOUT_CHECKERBOARD, STEREO_POLICY_RIGHT_FIRST, 0, 4, -1 },
        { STEREO_POLICY_LAYOUT_ROWS, STEREO_POLICY_LEFT_FIRST, 0, 7, -1 },
        { STEREO_POLICY_LAYOUT_ROWS, STEREO_POLICY_RIGHT_FIRST, 0, 6, -1 },
        { STEREO_POLICY_LAYOUT_COLUMNS, STEREO_POLICY_LEFT_FIRST, 0, 9, -1 },
        { STEREO_POLICY_LAYOUT_COLUMNS, STEREO_POLICY_RIGHT_FIRST, 0, 8, -1 },
        { STEREO_POLICY_LAYOUT_NONE, 0, STEREO_POLICY_ANAGLYPH_RED_CYAN, 10, -1 },
        { STEREO_POLICY_LAYOUT_NONE, 0, STEREO_POLICY_ANAGLYPH_GREEN_MAGENTA, 12, -1 },
        { STEREO_POLICY_LAYOUT_NONE, 0, STEREO_POLICY_ANAGLYPH_AMBER_BLUE, -1, -1 },
        { STEREO_POLICY_LAYOUT_BLOCK_LACED, STEREO_POLICY_LEFT_FIRST, 0, 13, -1 },
        { STEREO_POLICY_LAYOUT_BLOCK_LACED, STEREO_POLICY_RIGHT_FIRST, 0, 14, -1 },
        { STEREO_POLICY_LAYOUT_FRAME_SEQUENTIAL, STEREO_POLICY_LEFT_FIRST, 0, 13, -1 },
        { STEREO_POLICY_LAYOUT_FRAME_SEQUENTIAL, STEREO_POLICY_RIGHT_FIRST, 0, 14, -1 },
        { STEREO_POLICY_LAYOUT_SIDE_BY_SIDE_QUINCUNX, STEREO_POLICY_LEFT_FIRST, 0, -1, -1 },
        { STEREO_POLICY_LAYOUT_MULTI_VIEW, STEREO_POLICY_LEFT_FIRST, 0, -1, -1 },
        { STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE, STEREO_POLICY_PICTURE_FIRST, 0, 15, -1 },
        { STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE, STEREO_POLICY_DEPTH_FIRST, 0, 16, -1 },
        { STEREO_POLICY_LAYOUT_DEPTH_TOP_AND_BOTTOM, STEREO_POLICY_PICTURE_FIRST, 0, 17, -1 },
        { STEREO_POLICY_LAYOUT_DEPTH_TOP_AND_BOTTOM, STEREO_POLICY_DEPTH_FIRST, 0, 18, -1 },
        { STEREO_POLICY_LAYOUT_DEPTH_GRAPHICS, STEREO_POLICY_PICTURE_FIRST, 0, 19, -1 },
        { STEREO_POLICY_LAYOUT_DEPTH_BACKGROUND, STEREO_POLICY_PICTURE_FIRST, 0, 20, -1 },
        { STEREO_POLICY_LAYOUT_DEPTH_SEPARATE, STEREO_POLICY_PICTURE_FIRST, 0, -1, -1 },
        { 99, 0, 0, -1, -1 },
        { -1, 0, 0, -1, -1 },
    };
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); ++i) {
        CHECK(stereo_mark_matroska_value(t[i].layout, t[i].order, t[i].anaglyph) == t[i].matroska,
              "matroska value of layout %d order %d anaglyph %d: %d, wanted %d", t[i].layout, t[i].order, t[i].anaglyph,
              stereo_mark_matroska_value(t[i].layout, t[i].order, t[i].anaglyph), t[i].matroska);
        CHECK(stereo_mark_mp4_value(t[i].layout, t[i].order, t[i].anaglyph) == t[i].mp4,
              "mp4 value of layout %d order %d anaglyph %d: %d, wanted %d", t[i].layout, t[i].order, t[i].anaglyph,
              stereo_mark_mp4_value(t[i].layout, t[i].order, t[i].anaglyph), t[i].mp4);
    }
    /* WebM Container Guidelines, StereoMode: 0, 1, 2, 3 and 11 */
    for (int v = -1; v <= 21; ++v)
        CHECK(stereo_mark_webm_allows(v) == (v == 0 || v == 1 || v == 2 || v == 3 || v == 11), "webm allows %d", v);
    /* the reverse reading: every value of the registry stands for a layout that gives the value back */
    for (int v = 0; v <= 20; ++v) {
        int layout = -1, order = -1, anaglyph = -1;
        CHECK(stereo_mark_value_layout(STEREO_MARK_CONTAINER_MATROSKA, v, &layout, &order, &anaglyph) == 1, "matroska %d stands for a layout", v);
        CHECK(stereo_mark_matroska_value(layout, order, anaglyph) == v, "matroska %d round trip gives %d", v, stereo_mark_matroska_value(layout, order, anaglyph));
    }
    CHECK(stereo_mark_value_layout(STEREO_MARK_CONTAINER_MATROSKA, 21, NULL, NULL, NULL) == 0, "matroska 21 stands for none");
    for (int v = 0; v <= 5; ++v) {
        int layout = -1, order = -1, anaglyph = -1;
        int known = v == 0 || v == 1 || v == 2 || v == 4;
        CHECK(stereo_mark_value_layout(STEREO_MARK_CONTAINER_MP4, v, &layout, &order, &anaglyph) == known, "st3d %d known %d", v, known);
        if (known)
            CHECK(stereo_mark_mp4_value(layout, order, anaglyph) == v, "st3d %d round trip", v);
    }
}

static void test_matroska(void)
{
    make_mkv("m.mkv", "");
    const char *f = path_of("m.mkv");
    int packets = packet_count(f);
    CHECK(packets > 0, "the fixture demuxes");
    int container = -1, value = -2;
    CHECK(stereo_mark_read(f, 1, &container, &value) == STEREO_MARK_WRITTEN && container == STEREO_MARK_CONTAINER_MATROSKA && value == -1,
          "a fresh track carries no mark (container %d value %d)", container, value);
    CHECK(stereo_mark_read(f, 2, &container, &value) == STEREO_MARK_WRITTEN && value == -1, "the second track carries no mark");
    CHECK(stereo_mark_read(f, 3, &container, &value) == STEREO_MARK_NO_TRACK, "there is no third video track");

    /* every value of the registry into track 2, read by mkvmerge, by libavformat (0 to 14) and by our reader; track 1 never changes */
    static const char *names[15] = { "mono", "left_right", "bottom_top", "top_bottom", "checkerboard_rl", "checkerboard_lr", "row_interleaved_rl",
        "row_interleaved_lr", "col_interleaved_rl", "col_interleaved_lr", "anaglyph_cyan_red", "right_left", "anaglyph_green_magenta", "block_lr", "block_rl" };
    for (int v = 0; v <= 20; ++v) {
        int layout, order, anaglyph;
        stereo_mark_value_layout(STEREO_MARK_CONTAINER_MATROSKA, v, &layout, &order, &anaglyph);
        stereo_mark_result r;
        if (v == 13 || v == 14) {
            /* both eyes laced in one Block: this file's blocks are not laced, so the mark is refused and the track keeps what it had */
            int before = mkvmerge_stereo(f, 2);
            int st13 = stereo_mark_save(f, 2, layout, order, anaglyph, &r);
            CHECK(st13 == STEREO_MARK_NOT_LACED && r.value == v, "StereoMode %d into blocks that are not laced: %s", v, stereo_mark_status_name(st13));
            CHECK(mkvmerge_stereo(f, 2) == before, "the track keeps StereoMode %d (%d)", before, mkvmerge_stereo(f, 2));
            continue;
        }
        int st = stereo_mark_save(f, 2, layout, order, anaglyph, &r);
        CHECK(st == STEREO_MARK_WRITTEN, "StereoMode %d written: %s (%s)", v, stereo_mark_status_name(st), r.details);
        CHECK(r.value == v && r.now == v, "result says value %d now %d, wanted %d", r.value, r.now, v);
        CHECK(mkvmerge_stereo(f, 2) == v, "mkvmerge reads StereoMode %d for track 2, wanted %d", mkvmerge_stereo(f, 2), v);
        CHECK(mkvmerge_stereo(f, 1) == -1, "track 1 stays unmarked (mkvmerge reads %d)", mkvmerge_stereo(f, 1));
        CHECK(stereo_mark_read(f, 2, &container, &value) == STEREO_MARK_WRITTEN && value == v, "our reader reads %d, wanted %d", value, v);
        CHECK(stereo_mark_read(f, 1, &container, &value) == STEREO_MARK_WRITTEN && value == -1, "our reader: track 1 unmarked");
        if (v >= 1 && v <= 14) {
            char tag[64];
            int type, flags;
            CHECK(libav_mark(f, 2, tag, sizeof(tag), &type, &flags) == 1 && strcmp(tag, names[v]) == 0, "libavformat reads stereo_mode \"%s\", wanted \"%s\"", tag, names[v]);
        }
        CHECK(packet_count(f) == packets, "the media is intact after StereoMode %d: %d packets of %d", v, packet_count(f), packets);
    }
    /* the same again: nothing is written */
    Snapshot s = snap(f);
    for (int v = 20; v >= 0; v -= 7) {
        int layout, order, anaglyph;
        stereo_mark_value_layout(STEREO_MARK_CONTAINER_MATROSKA, v, &layout, &order, &anaglyph);
        stereo_mark_save(f, 2, layout, order, anaglyph, NULL);
    }
    stereo_mark_result r;
    int st = stereo_mark_save(f, 2, STEREO_POLICY_LAYOUT_NONE, 0, STEREO_POLICY_ANAGLYPH_NONE, &r);
    drop(&s);
    s = snap(f);
    CHECK(st == STEREO_MARK_WRITTEN || st == STEREO_MARK_UNCHANGED, "mono again: %s", stereo_mark_status_name(st));
    st = stereo_mark_save(f, 2, STEREO_POLICY_LAYOUT_NONE, 0, STEREO_POLICY_ANAGLYPH_NONE, &r);
    CHECK(st == STEREO_MARK_UNCHANGED && r.previous == 0 && r.now == 0, "the same value again is %s", stereo_mark_status_name(st));
    CHECK(same_as(&s, f), "an unchanged save leaves the file byte for byte");
    drop(&s);

    /* the first track as well, and not the second */
    st = stereo_mark_save(f, 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, STEREO_POLICY_RIGHT_FIRST, 0, &r);
    CHECK(st == STEREO_MARK_WRITTEN && r.value == 11, "track 1 side by side right first: %s", stereo_mark_status_name(st));
    CHECK(mkvmerge_stereo(f, 1) == 11 && mkvmerge_stereo(f, 2) == 0, "mkvmerge: track 1 is %d, track 2 is %d", mkvmerge_stereo(f, 1), mkvmerge_stereo(f, 2));
}

/* The offset of the last occurrence of a 4-byte element ID: the element itself, behind its mention in the SeekHead. */
static long find_id(const char *path, const unsigned char id[4])
{
    size_t len;
    unsigned char *b = slurp(path, &len);
    long at = -1;
    for (size_t i = 0; b && i + 4 <= len; ++i)
        if (memcmp(b + i, id, 4) == 0)
            at = (long)i;
    free(b);
    return at;
}

/* A file whose Tracks element mkvpropedit has had to move behind the clusters: the writer's reader follows it there. */
static void test_tracks_moved(void)
{
    const unsigned char tracks_id[4] = { 0x16, 0x54, 0xAE, 0x6B }, cluster_id[4] = { 0x1F, 0x43, 0xB6, 0x75 };
    CHECK(run("mkvmerge -q -o '%s' '%s'", path_of("mm.mkv"), path_of("m.mkv")) <= 1, "mkvmerge remakes the file");
    const char *f = path_of("mm.mkv");
    char big[6001];
    memset(big, 'x', 6000);
    big[6000] = 0;
    CHECK(run("mkvpropedit '%s' --edit track:v1 --set name=%s >/dev/null", f, big) == 0, "a long track name makes Tracks grow");
    long t = find_id(f, tracks_id), c = find_id(f, cluster_id);
    CHECK(t > c && c > 0, "Tracks (%ld) now follows the last cluster (%ld)", t, c);
    int packets = packet_count(f);
    for (int v = 0; v <= 20; ++v) {
        int layout, order, anaglyph;
        stereo_mark_value_layout(STEREO_MARK_CONTAINER_MATROSKA, v, &layout, &order, &anaglyph);
        stereo_mark_result r;
        if (v == 13 || v == 14)
            continue;
        int st = stereo_mark_save(f, 2, layout, order, anaglyph, &r);
        CHECK(st == STEREO_MARK_WRITTEN || st == STEREO_MARK_UNCHANGED, "StereoMode %d into the file with Tracks last: %s (%s)", v, stereo_mark_status_name(st), r.details);
        CHECK(mkvmerge_stereo(f, 2) == v, "mkvmerge reads %d, wanted %d (Tracks last)", mkvmerge_stereo(f, 2), v);
        CHECK(packet_count(f) == packets, "the media is intact (Tracks last, value %d)", v);
    }
}


/* ---- a Matroska file made by hand: one video track whose blocks lace two frames ---- */

typedef struct { unsigned char *d; size_t n; } Buf;
static void bput(Buf *b, const void *p, size_t n) { b->d = realloc(b->d, b->n + n); memcpy(b->d + b->n, p, n); b->n += n; }
static void bbyte(Buf *b, int v) { unsigned char c = (unsigned char)v; bput(b, &c, 1); }
/* an element: its ID bytes, an 8 byte size, the content */
static void belem(Buf *out, const char *id, size_t idlen, const Buf *content)
{
    bput(out, id, idlen);
    unsigned char sz[8] = { 0x01 };
    for (int i = 0; i < 7; ++i)
        sz[1 + i] = (unsigned char)(content->n >> (8 * (6 - i)));
    bput(out, sz, 8);
    bput(out, content->d, content->n);
}
static void buint(Buf *out, const char *id, size_t idlen, unsigned v, int bytes)
{
    Buf c = { NULL, 0 };
    for (int i = bytes - 1; i >= 0; --i)
        bbyte(&c, (int)(v >> (8 * i)) & 0xff);
    belem(out, id, idlen, &c);
    free(c.d);
}
static void bstr(Buf *out, const char *id, size_t idlen, const char *s)
{
    Buf c = { NULL, 0 };
    bput(&c, s, strlen(s));
    belem(out, id, idlen, &c);
    free(c.d);
}

static void make_laced_mkv(const char *path, int laced)
{
    Buf hdr = { NULL, 0 }, file = { NULL, 0 }, tracks = { NULL, 0 }, entry = { NULL, 0 }, video = { NULL, 0 }, info = { NULL, 0 }, seg = { NULL, 0 };
    buint(&hdr, "\x42\x86", 2, 1, 1);
    buint(&hdr, "\x42\xF7", 2, 1, 1);
    buint(&hdr, "\x42\xF2", 2, 4, 1);
    buint(&hdr, "\x42\xF3", 2, 8, 1);
    bstr(&hdr, "\x42\x82", 2, "matroska");
    buint(&hdr, "\x42\x87", 2, 4, 1);
    buint(&hdr, "\x42\x85", 2, 2, 1);
    belem(&file, "\x1A\x45\xDF\xA3", 4, &hdr);
    buint(&info, "\x2A\xD7\xB1", 3, 1000000, 4);
    belem(&seg, "\x15\x49\xA9\x66", 4, &info);
    buint(&video, "\xB0", 1, 320, 2);
    buint(&video, "\xBA", 1, 90, 1);
    buint(&entry, "\xD7", 1, 1, 1);
    buint(&entry, "\x73\xC5", 2, 1, 1);
    buint(&entry, "\x83", 1, 1, 1);
    bstr(&entry, "\x86", 1, "V_VP8");
    belem(&entry, "\xE0", 1, &video);
    belem(&tracks, "\xAE", 1, &entry);
    belem(&seg, "\x16\x54\xAE\x6B", 4, &tracks);
    for (int c = 0; c < 3; ++c) {
        Buf cluster = { NULL, 0 }, block = { NULL, 0 };
        buint(&cluster, "\xE7", 1, (unsigned)c * 40, 1);
        bbyte(&block, 0x81);                  /* track number 1 */
        bbyte(&block, 0); bbyte(&block, 0);   /* relative timestamp */
        if (laced) {
            bbyte(&block, 0x84);              /* keyframe, fixed-size lacing */
            bbyte(&block, 1);                 /* two frames */
            for (int i = 0; i < 8; ++i)
                bbyte(&block, 'A' + i);
        } else {
            bbyte(&block, 0x80);              /* keyframe, no lacing */
            for (int i = 0; i < 8; ++i)
                bbyte(&block, 'A' + i);
        }
        belem(&cluster, "\xA3", 1, &block);
        belem(&seg, "\x1F\x43\xB6\x75", 4, &cluster);
        free(cluster.d);
        free(block.d);
    }
    belem(&file, "\x18\x53\x80\x67", 4, &seg);
    FILE *f = fopen(path, "wb");
    fwrite(file.d, 1, file.n, f);
    fclose(f);
    free(hdr.d); free(file.d); free(tracks.d); free(entry.d); free(video.d); free(info.d); free(seg.d);
}

static void test_laced(void)
{
    make_laced_mkv(path_of("laced.mkv"), 1);
    make_laced_mkv(path_of("unlaced.mkv"), 0);
    const char *l = path_of("laced.mkv"), *u = path_of("unlaced.mkv");
    stereo_mark_detail d;
    CHECK(stereo_mark_read_detail(l, 1, &d) == STEREO_MARK_WRITTEN && d.laced == 1 && d.pixel_width == 320 && d.pixel_height == 90 && d.value == -1,
          "the laced fixture reads laced=%d, %dx%d", d.laced, d.pixel_width, d.pixel_height);
    CHECK(stereo_mark_read_detail(u, 1, &d) == STEREO_MARK_WRITTEN && d.laced == 0, "the other one reads laced=%d", d.laced);
    CHECK(mkvmerge_stereo(l, 1) == -1, "mkvmerge opens the hand-made file");
    for (int order = 0; order <= 1; ++order) {
        for (int k = 0; k < 2; ++k) {
            int layout = k ? STEREO_POLICY_LAYOUT_FRAME_SEQUENTIAL : STEREO_POLICY_LAYOUT_BLOCK_LACED;
            char name[32];
            snprintf(name, sizeof(name), "l%d%d.mkv", order, k);
            CHECK(run("cp '%s' '%s'", l, path_of(name)) == 0, "copy");
            stereo_mark_result r;
            int st = stereo_mark_save(path_of(name), 1, layout, order, 0, &r);
            CHECK(st == STEREO_MARK_WRITTEN && r.value == 13 + order, "laced blocks take StereoMode %d: %s (%s)", 13 + order, stereo_mark_status_name(st), r.details);
            CHECK(mkvmerge_stereo(path_of(name), 1) == 13 + order, "mkvmerge reads %d, wanted %d", mkvmerge_stereo(path_of(name), 1), 13 + order);
        }
    }
    Snapshot s = snap(u);
    for (int k = 0; k < 2; ++k) {
        stereo_mark_result r;
        int st = stereo_mark_save(u, 1, k ? STEREO_POLICY_LAYOUT_FRAME_SEQUENTIAL : STEREO_POLICY_LAYOUT_BLOCK_LACED, STEREO_POLICY_LEFT_FIRST, 0, &r);
        CHECK(st == STEREO_MARK_NOT_LACED, "blocks that are not laced refuse 13 (%s): %s", k ? "frame sequential" : "block laced", stereo_mark_status_name(st));
        CHECK(same_as(&s, u), "and the file is byte for byte");
    }
    drop(&s);
    /* the other layouts do not care about lacing */
    stereo_mark_result r;
    CHECK(stereo_mark_save(u, 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, 0, 0, &r) == STEREO_MARK_WRITTEN && mkvmerge_stereo(u, 1) == 1, "side by side into the unlaced file");
}

static void test_views(void)
{
    CHECK(run("ffmpeg -v error -y -f lavfi -i testsrc=size=640x180:rate=10:duration=1 -c:v mpeg4 '%s'", path_of("v.mkv")) == 0, "ffmpeg makes the wide fixture");
    const char *f = path_of("v.mkv");
    stereo_mark_views_result r;
    stereo_mark_detail d;
    CHECK(stereo_mark_read_detail(f, 1, &d) == STEREO_MARK_WRITTEN && d.pixel_width == 640 && d.pixel_height == 180 && d.display_width == 0 && d.display_height == 0,
          "the fixture: %dx%d, display %dx%d", d.pixel_width, d.pixel_height, d.display_width, d.display_height);
    /* RFC 9559 section 18.10: DisplayWidth/DisplayHeight are the pixels of one plane */
    static const struct { int layout, views, dw, dh; const char *why; } t[] = {
        { STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, STEREO_MARK_VIEWS_FULL, 320, 180, "full side by side: one plane is half the width" },
        { STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, STEREO_MARK_VIEWS_HALF, 640, 180, "half side by side: one plane as large as the frame" },
        { STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, STEREO_MARK_VIEWS_FULL, 640, 90, "full top and bottom: one plane is half the height" },
        { STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, STEREO_MARK_VIEWS_HALF, 640, 180, "half top and bottom" },
        { STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, STEREO_MARK_VIEWS_FULL, 320, 180, "full again" },
    };
    for (size_t i = 0; i < sizeof(t) / sizeof(t[0]); ++i) {
        int st = stereo_mark_save_views(f, 1, t[i].layout, STEREO_POLICY_LEFT_FIRST, 0, t[i].views, &r);
        CHECK(st == STEREO_MARK_WRITTEN, "%s: %s (%s)", t[i].why, stereo_mark_status_name(st), r.base.details);
        CHECK(r.plane_width == t[i].dw && r.plane_height == t[i].dh, "%s: the result says %dx%d, wanted %dx%d", t[i].why, r.plane_width, r.plane_height, t[i].dw, t[i].dh);
        CHECK(mkvmerge_display(f, 1, 0) == t[i].dw && mkvmerge_display(f, 1, 1) == t[i].dh, "%s: mkvmerge reads display %dx%d, wanted %dx%d", t[i].why,
              mkvmerge_display(f, 1, 0), mkvmerge_display(f, 1, 1), t[i].dw, t[i].dh);
        CHECK(stereo_mark_read_detail(f, 1, &d) == STEREO_MARK_WRITTEN && d.display_width == t[i].dw && d.display_height == t[i].dh, "our reader agrees");
    }
    /* the same again: nothing is written; the mark alone differs: the display size stays */
    Snapshot s = snap(f);
    CHECK(stereo_mark_save_views(f, 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, STEREO_POLICY_LEFT_FIRST, 0, STEREO_MARK_VIEWS_FULL, &r) == STEREO_MARK_UNCHANGED, "the same mark and size again is unchanged");
    CHECK(same_as(&s, f), "and the file is byte for byte");
    drop(&s);
    CHECK(stereo_mark_save_views(f, 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, STEREO_POLICY_RIGHT_FIRST, 0, STEREO_MARK_VIEWS_UNSPECIFIED, &r) == STEREO_MARK_WRITTEN, "eye order only");
    CHECK(mkvmerge_stereo(f, 1) == 11 && mkvmerge_display(f, 1, 0) == 320, "the size of the views is left as the file has it (%d, display %d)", mkvmerge_stereo(f, 1), mkvmerge_display(f, 1, 0));
    /* the mark is the same and only the size changes: that is a write too */
    CHECK(stereo_mark_save_views(f, 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, STEREO_POLICY_RIGHT_FIRST, 0, STEREO_MARK_VIEWS_HALF, &r) == STEREO_MARK_WRITTEN && mkvmerge_display(f, 1, 0) == 640,
          "only the size of the views changes");
    /* refusals */
    s = snap(f);
    int st = stereo_mark_save_views(f, 1, STEREO_POLICY_LAYOUT_ROWS, STEREO_POLICY_LEFT_FIRST, 0, STEREO_MARK_VIEWS_FULL, &r);
    CHECK(st == STEREO_MARK_VIEWS_LAYOUT, "rows have no half or full: %s", stereo_mark_status_name(st));
    st = stereo_mark_save_views(f, 1, STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE, STEREO_POLICY_PICTURE_FIRST, 0, STEREO_MARK_VIEWS_FULL, &r);
    CHECK(st == STEREO_MARK_VIEWS_LAYOUT, "a depth layout has none either: %s", stereo_mark_status_name(st));
    CHECK(same_as(&s, f), "refused, byte for byte");
    drop(&s);
    /* WebM takes the size too; MP4 has no field for it */
    CHECK(run("ffmpeg -v error -y -f lavfi -i testsrc=size=640x180:rate=10:duration=1 -c:v libvpx -b:v 200k -f webm '%s'", path_of("v.webm")) == 0, "webm");
    CHECK(stereo_mark_save_views(path_of("v.webm"), 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, 0, 0, STEREO_MARK_VIEWS_FULL, &r) == STEREO_MARK_WRITTEN && mkvmerge_display(path_of("v.webm"), 1, 0) == 320,
          "WebM takes DisplayWidth too");
    CHECK(run("ffmpeg -v error -y -f lavfi -i testsrc=size=640x180:rate=10:duration=1 -c:v mpeg4 -f mp4 '%s'", path_of("v.mp4")) == 0, "mp4");
    s = snap(path_of("v.mp4"));
    st = stereo_mark_save_views(path_of("v.mp4"), 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, 0, 0, STEREO_MARK_VIEWS_FULL, &r);
    CHECK(st == STEREO_MARK_MP4_VIEWS, "st3d has no field for the size of the views: %s", stereo_mark_status_name(st));
    CHECK(same_as(&s, path_of("v.mp4")), "the mp4 is byte for byte");
    drop(&s);
    CHECK(stereo_mark_save_views(path_of("v.mp4"), 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, 0, 0, STEREO_MARK_VIEWS_UNSPECIFIED, &r) == STEREO_MARK_WRITTEN, "the packing alone goes into the mp4");
}

static void test_refusals(void)
{
    const char *f = path_of("m.mkv");
    Snapshot s = snap(f);
    stereo_mark_result r;
    int st = stereo_mark_save(f, 2, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE_QUINCUNX, 0, 0, &r);
    CHECK(st == STEREO_MARK_LAYOUT && r.value == -1, "quincunx has no StereoMode value: %s", stereo_mark_status_name(st));
    st = stereo_mark_save(f, 2, STEREO_POLICY_LAYOUT_NONE, 0, STEREO_POLICY_ANAGLYPH_AMBER_BLUE, &r);
    CHECK(st == STEREO_MARK_LAYOUT, "amber and blue has no StereoMode value: %s", stereo_mark_status_name(st));
    st = stereo_mark_save(f, 2, STEREO_POLICY_LAYOUT_DEPTH_SEPARATE, 0, 0, &r);
    CHECK(st == STEREO_MARK_LAYOUT, "a depth map of its own is not a packing: %s", stereo_mark_status_name(st));
    st = stereo_mark_save(f, 3, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, 0, 0, &r);
    CHECK(st == STEREO_MARK_NO_TRACK, "no third video track: %s", stereo_mark_status_name(st));
    st = stereo_mark_save(f, 0, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, 0, 0, &r);
    CHECK(st == STEREO_MARK_BAD_ARGUMENT, "track 0: %s", stereo_mark_status_name(st));
    st = stereo_mark_save(NULL, 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, 0, 0, &r);
    CHECK(st == STEREO_MARK_BAD_ARGUMENT, "no path: %s", stereo_mark_status_name(st));
    CHECK(same_as(&s, f), "refused saves leave the Matroska file byte for byte");
    drop(&s);

    st = stereo_mark_save(dir, 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, 0, 0, &r);
    CHECK(st == STEREO_MARK_NOT_LOCAL, "a folder is not a file: %s", stereo_mark_status_name(st));
    st = stereo_mark_save(path_of("missing.mkv"), 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, 0, 0, &r);
    CHECK(st == STEREO_MARK_NOT_LOCAL, "a missing file: %s", stereo_mark_status_name(st));

    /* a container with no mark to write */
    CHECK(run("ffmpeg -v error -y -f lavfi -i testsrc=size=160x90:rate=10:duration=1 -c:v mpeg4 -f avi '%s'", path_of("a.avi")) == 0, "ffmpeg makes the avi");
    s = snap(path_of("a.avi"));
    st = stereo_mark_save(path_of("a.avi"), 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, 0, 0, &r);
    CHECK(st == STEREO_MARK_CONTAINER && r.container == STEREO_MARK_CONTAINER_OTHER, "avi has no 3D mark: %s", stereo_mark_status_name(st));
    CHECK(same_as(&s, path_of("a.avi")), "the avi is untouched");
    drop(&s);
    FILE *t = fopen(path_of("t.txt"), "w");
    fputs("this is not a video\n", t);
    fclose(t);
    st = stereo_mark_save(path_of("t.txt"), 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, 0, 0, &r);
    CHECK(st == STEREO_MARK_CONTAINER, "a text file: %s", stereo_mark_status_name(st));

    /* a file cut short is said to be broken, and left alone */
    CHECK(run("head -c 200 '%s' > '%s'", f, path_of("cut.mkv")) == 0, "cut the mkv");
    s = snap(path_of("cut.mkv"));
    st = stereo_mark_save(path_of("cut.mkv"), 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, 0, 0, &r);
    CHECK(st == STEREO_MARK_BROKEN || st == STEREO_MARK_NO_TRACK, "a cut Matroska file: %s", stereo_mark_status_name(st));
    CHECK(same_as(&s, path_of("cut.mkv")), "the cut file is untouched");
    drop(&s);
}

static void test_webm(void)
{
    CHECK(run("ffmpeg -v error -y -f lavfi -i testsrc=size=160x90:rate=10:duration=1 -c:v libvpx -b:v 200k -f webm '%s'", path_of("w.webm")) == 0, "ffmpeg makes the webm");
    const char *f = path_of("w.webm");
    int container = -1, value = -2;
    CHECK(stereo_mark_read(f, 1, &container, &value) == STEREO_MARK_WRITTEN && container == STEREO_MARK_CONTAINER_WEBM, "DocType webm is told from matroska (container %d)", container);
    stereo_mark_result r;
    Snapshot s = snap(f);
    for (int v = 4; v <= 20; ++v) {
        if (v == 11)
            continue;
        int layout, order, anaglyph;
        stereo_mark_value_layout(STEREO_MARK_CONTAINER_MATROSKA, v, &layout, &order, &anaglyph);
        int st = stereo_mark_save(f, 1, layout, order, anaglyph, &r);
        CHECK(st == STEREO_MARK_WEBM_LAYOUT && r.container == STEREO_MARK_CONTAINER_WEBM && r.value == v, "WebM refuses StereoMode %d: %s", v, stereo_mark_status_name(st));
    }
    CHECK(same_as(&s, f), "the refusals leave the WebM file byte for byte");
    drop(&s);
    static const int allowed[5][3] = { { STEREO_POLICY_LAYOUT_NONE, 0, 0 }, { STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, STEREO_POLICY_LEFT_FIRST, 1 },
        { STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, STEREO_POLICY_RIGHT_FIRST, 2 }, { STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, STEREO_POLICY_LEFT_FIRST, 3 },
        { STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, STEREO_POLICY_RIGHT_FIRST, 11 } };
    for (int i = 0; i < 5; ++i) {
        int st = stereo_mark_save(f, 1, allowed[i][0], allowed[i][1], 0, &r);
        CHECK(st == STEREO_MARK_WRITTEN, "WebM takes StereoMode %d: %s (%s)", allowed[i][2], stereo_mark_status_name(st), r.details);
        CHECK(mkvmerge_stereo(f, 1) == allowed[i][2], "mkvmerge reads %d from the webm, wanted %d", mkvmerge_stereo(f, 1), allowed[i][2]);
    }
    CHECK(packet_count(f) > 0, "the webm still demuxes");
}

static int stub_tool(const char *name, const char *body)
{
    char sub[600];
    snprintf(sub, sizeof(sub), "%s/%s", dir, name);
    mkdir(sub, 0755);
    char file[700];
    snprintf(file, sizeof(file), "%s/mkvpropedit", sub);
    FILE *f = fopen(file, "w");
    if (!f)
        return -1;
    fputs("#!/bin/sh\n", f);
    fputs(body, f);
    fclose(f);
    chmod(file, 0755);
    return 0;
}

static void test_tool_failures(void)
{
    const char *f = path_of("m.mkv");
    char *oldpath = getenv("PATH") ? strdup(getenv("PATH")) : strdup("/usr/bin:/bin");
    stereo_mark_result r;
    int st;
    char real[600] = "mkvpropedit";
    FILE *p = popen("command -v mkvpropedit", "r");
    if (p) {
        if (fgets(real, sizeof(real), p))
            real[strcspn(real, "\n")] = 0;
        pclose(p);
    }

    /* the tool is missing */
    mkdir(path_of("empty"), 0755);
    setenv("PATH", path_of("empty"), 1);
    Snapshot s = snap(f);
    st = stereo_mark_save(f, 2, STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, 0, 0, &r);
    CHECK(st == STEREO_MARK_TOOL_MISSING, "mkvpropedit missing: %s", stereo_mark_status_name(st));
    CHECK(same_as(&s, f), "no tool, the file is untouched");
    drop(&s);

    /* the tool fails */
    stub_tool("fail", "echo 'stub: cannot write' ; exit 2\n");
    setenv("PATH", path_of("fail"), 1);
    s = snap(f);
    st = stereo_mark_save(f, 2, STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, 0, 0, &r);
    CHECK(st == STEREO_MARK_WRITE_FAILED && strstr(r.details, "cannot write"), "mkvpropedit fails: %s (%s)", stereo_mark_status_name(st), r.details);
    CHECK(same_as(&s, f), "a failed write leaves the file");
    drop(&s);

    /* the tool says it wrote and did not */
    stub_tool("nothing", "exit 0\n");
    setenv("PATH", path_of("nothing"), 1);
    s = snap(f);
    st = stereo_mark_save(f, 2, STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, 0, 0, &r);
    CHECK(st == STEREO_MARK_READ_BACK, "a write that is not there: %s", stereo_mark_status_name(st));
    CHECK(same_as(&s, f), "a write that does not read back leaves the file");
    drop(&s);

    /* the tool writes another value: the file is put back as it was */
    char body[2200];
    snprintf(body, sizeof(body), "flag='%s/wrong.flag'\ncase \"$*\" in *--set*) if [ ! -e \"$flag\" ]; then : > \"$flag\"; exec '%s' \"$1\" --edit \"$3\" --set stereo-mode=7; fi ;; esac\nexec '%s' \"$@\"\n", dir, real, real);
    stub_tool("wrong", body);
    setenv("PATH", path_of("wrong"), 1);
    st = stereo_mark_save(f, 2, STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, 0, 0, &r);
    setenv("PATH", oldpath, 1);
    CHECK(st == STEREO_MARK_READ_BACK && r.previous == 0, "a write that reads back wrong: %s (previous %d)", stereo_mark_status_name(st), r.previous);
    CHECK(mkvmerge_stereo(f, 2) == 0 && r.now == 0, "the track is put back to what it carried (mkvmerge reads %d, result says %d)", mkvmerge_stereo(f, 2), r.now);
    free(oldpath);

    /* a file the person cannot write */
    if (geteuid() != 0) {
        CHECK(run("cp '%s' '%s' && chmod 444 '%s'", f, path_of("ro.mkv"), path_of("ro.mkv")) == 0, "copy the readonly fixture");
        s = snap(path_of("ro.mkv"));
        st = stereo_mark_save(path_of("ro.mkv"), 2, STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, 0, 0, &r);
        CHECK(st == STEREO_MARK_WRITE_FAILED && r.details[0], "a read-only Matroska file: %s (%s)", stereo_mark_status_name(st), r.details);
        CHECK(same_as(&s, path_of("ro.mkv")), "the read-only file is untouched");
        drop(&s);
    } else {
        fprintf(stderr, "NOTE: running as root, the read-only file case is not run\n");
    }
}

static void test_declared_by_stream(void)
{
    CHECK(run("ffmpeg -v error -y -f lavfi -i testsrc=size=320x90:rate=10:duration=1 -c:v libx264 -x264-params frame-packing=3 '%s'", path_of("fp.mkv")) == 0,
          "ffmpeg makes a stream with the frame packing SEI");
    const char *f = path_of("fp.mkv");
    Snapshot s = snap(f);
    stereo_mark_result r;
    int st = stereo_mark_save(f, 1, STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, STEREO_POLICY_LEFT_FIRST, 0, &r);
    CHECK(st == STEREO_MARK_STREAM_DECLARES && r.declared_layout == STEREO_POLICY_LAYOUT_SIDE_BY_SIDE && r.declared_order == STEREO_POLICY_LEFT_FIRST,
          "the stream's own side by side comes first: %s (declared %d/%d)", stereo_mark_status_name(st), r.declared_layout, r.declared_order);
    CHECK(same_as(&s, f), "refused for the stream's declaration, the file is untouched");
    st = stereo_mark_save(f, 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, STEREO_POLICY_LEFT_FIRST, 0, &r);
    CHECK(st == STEREO_MARK_UNCHANGED, "the stream declares what is asked: %s", stereo_mark_status_name(st));
    CHECK(same_as(&s, f), "nothing is written for what the stream declares");
    drop(&s);
}

/* Rewrite an ISO base media file of the shape ftyp, moov, free (8 bytes), mdat so that its free box has free_size bytes, and
 * move every chunk offset (stco and co64, ISO/IEC 14496-12 8.7.5) by the difference, so the file plays as before. */
static int tail_same(const char *a, const char *b, size_t n)
{
    size_t la, lb;
    unsigned char *da = slurp(a, &la), *db = slurp(b, &lb);
    int same = da && db && la >= n && lb >= n && memcmp(da + la - n, db + lb - n, n) == 0;
    free(da);
    free(db);
    return same;
}

static int make_free_room(const char *src, const char *dst, int free_size)
{
    size_t len;
    unsigned char *b = slurp(src, &len);
    if (!b)
        return -1;
    size_t pos = 0, moov = 0, moov_size = 0, mdat = 0;
    uint32_t sizes[4], types[4];
    int n = 0;
    while (pos + 8 <= len && n < 4) {
        sizes[n] = (uint32_t)b[pos] << 24 | b[pos + 1] << 16 | b[pos + 2] << 8 | b[pos + 3];
        types[n] = (uint32_t)b[pos + 4] << 24 | b[pos + 5] << 16 | b[pos + 6] << 8 | b[pos + 7];
        if (types[n] == 0x6d6f6f76) {
            moov = pos;
            moov_size = sizes[n];
        }
        if (types[n] == 0x6d646174)
            mdat = pos;
        pos += sizes[n];
        ++n;
    }
    if (n != 4 || !moov || !mdat || types[2] != 0x66726565 || sizes[2] != 8) {
        free(b);
        return -2;
    }
    long delta = (long)free_size - 8;
    for (size_t p = moov; p + 16 < moov + moov_size; ++p) {
        int is32 = memcmp(b + p, "stco", 4) == 0, is64 = memcmp(b + p, "co64", 4) == 0;
        if (!is32 && !is64)
            continue;
        uint32_t count = (uint32_t)b[p + 8] << 24 | b[p + 9] << 16 | b[p + 10] << 8 | b[p + 11];
        for (uint32_t i = 0; i < count; ++i) {
            unsigned char *e = b + p + 12 + (size_t)i * (is64 ? 8 : 4);
            if (is64) {
                uint64_t v = 0;
                for (int k = 0; k < 8; ++k)
                    v = v << 8 | e[k];
                v += delta;
                for (int k = 7; k >= 0; --k, v >>= 8)
                    e[k] = v & 0xff;
            } else {
                uint32_t v = (uint32_t)e[0] << 24 | e[1] << 16 | e[2] << 8 | e[3];
                v += delta;
                e[0] = v >> 24; e[1] = v >> 16; e[2] = v >> 8; e[3] = v;
            }
        }
    }
    FILE *f = fopen(dst, "wb");
    if (!f) {
        free(b);
        return -3;
    }
    size_t head = moov + moov_size;
    fwrite(b, 1, head, f);
    if (free_size >= 8) {
        unsigned char fh[8] = { (unsigned char)(free_size >> 24), (unsigned char)(free_size >> 16), (unsigned char)(free_size >> 8), (unsigned char)free_size, 'f', 'r', 'e', 'e' };
        fwrite(fh, 1, 8, f);
        for (int i = 8; i < free_size; ++i)
            fputc(0, f);
    } else {
        /* a gap smaller than a box header cannot exist: the case with no free box at all is not made */
        fclose(f);
        free(b);
        return -4;
    }
    fwrite(b + mdat, 1, len - mdat, f);
    fclose(f);
    free(b);
    return 0;
}

static void test_mp4(void)
{
    /* moov at the end of the file: st3d is added in place by growing the file's last box */
    make_mkv("src.mkv", "");
    CHECK(run("ffmpeg -v error -y -i '%s' -map 0:v:0 -c copy -f mp4 '%s'", path_of("src.mkv"), path_of("e.mp4")) == 0, "ffmpeg makes the mp4");
    const char *f = path_of("e.mp4");
    char boxes[128];
    boxes_of(f, boxes, sizeof(boxes));
    CHECK(strcmp(boxes, "ftyp mdat moov") == 0 || strcmp(boxes, "ftyp free mdat moov") == 0, "the fixture has its moov last: %s", boxes);
    int packets = packet_count(f);
    CHECK(packets > 0, "the mp4 demuxes");
    int container, value;
    CHECK(stereo_mark_read(f, 1, &container, &value) == STEREO_MARK_WRITTEN && container == STEREO_MARK_CONTAINER_MP4 && value == -1, "no st3d yet");
    size_t before;
    free(slurp(f, &before));
    stereo_mark_result r;
    int st = stereo_mark_save(f, 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, STEREO_POLICY_LEFT_FIRST, 0, &r);
    CHECK(st == STEREO_MARK_WRITTEN && r.value == 2 && r.previous == -1 && r.now == 2, "st3d added: %s (%s)", stereo_mark_status_name(st), r.details);
    size_t after;
    free(slurp(f, &after));
    CHECK(after == before + 13, "the file grows by the 13 bytes of st3d: %zu to %zu", before, after);
    char tag[64];
    int type, flags;
    CHECK(libav_mark(f, 1, tag, sizeof(tag), &type, &flags) == 1 && type == AV_STEREO3D_SIDEBYSIDE && !(flags & AV_STEREO3D_FLAG_INVERT), "libavformat reads st3d as side by side (type %d)", type);
    CHECK(packet_count(f) == packets, "every chunk offset still points at its samples: %d packets of %d", packet_count(f), packets);
    boxes_of(f, boxes, sizeof(boxes));
    CHECK(strstr(boxes, "mdat moov") != NULL, "the boxes keep their order: %s", boxes);

    /* the mode byte is replaced in place when st3d exists: top and bottom, then right-left, then mono, then the same again */
    static const struct { int layout, order, anaglyph, mode; } modes[] = {
        { STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, STEREO_POLICY_LEFT_FIRST, 0, 1 },
        { STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, STEREO_POLICY_RIGHT_FIRST, 0, 4 },
        { STEREO_POLICY_LAYOUT_NONE, 0, 0, 0 },
        { STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, STEREO_POLICY_LEFT_FIRST, 0, 2 },
    };
    for (int i = 0; i < 4; ++i) {
        st = stereo_mark_save(f, 1, modes[i].layout, modes[i].order, modes[i].anaglyph, &r);
        CHECK(st == STEREO_MARK_WRITTEN && r.now == modes[i].mode, "st3d mode %d: %s", modes[i].mode, stereo_mark_status_name(st));
        free(slurp(f, &after));
        CHECK(after == before + 13, "replacing the mode byte keeps the size (%zu)", after);
        CHECK(packet_count(f) == packets, "media intact after mode %d", modes[i].mode);
        if (modes[i].mode == 1)
            CHECK(libav_mark(f, 1, tag, sizeof(tag), &type, &flags) == 1 && type == AV_STEREO3D_TOPBOTTOM, "libavformat reads top and bottom");
        if (modes[i].mode == 0)
            CHECK(libav_mark(f, 1, tag, sizeof(tag), &type, &flags) == 1 && type == AV_STEREO3D_2D, "libavformat reads 2D");
    }
    Snapshot s = snap(f);
    st = stereo_mark_save(f, 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, STEREO_POLICY_LEFT_FIRST, 0, &r);
    CHECK(st == STEREO_MARK_UNCHANGED, "the same st3d again: %s", stereo_mark_status_name(st));
    CHECK(same_as(&s, f), "an unchanged save leaves the mp4 byte for byte");

    /* what st3d cannot name is refused, with the file untouched */
    static const struct { int layout, order, anaglyph, status; const char *why; } refused[] = {
        { STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, STEREO_POLICY_RIGHT_FIRST, 0, STEREO_MARK_LAYOUT, "top and bottom, right first" },
        { STEREO_POLICY_LAYOUT_ROWS, STEREO_POLICY_LEFT_FIRST, 0, STEREO_MARK_LAYOUT, "rows" },
        { STEREO_POLICY_LAYOUT_NONE, 0, STEREO_POLICY_ANAGLYPH_RED_CYAN, STEREO_MARK_LAYOUT, "red and cyan" },
        { STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE, STEREO_POLICY_PICTURE_FIRST, 0, STEREO_MARK_MP4_DEPTH, "depth beside picture" },
        { STEREO_POLICY_LAYOUT_DEPTH_SIDE_BY_SIDE, STEREO_POLICY_DEPTH_FIRST, 0, STEREO_MARK_MP4_DEPTH, "depth first" },
        { STEREO_POLICY_LAYOUT_DEPTH_TOP_AND_BOTTOM, STEREO_POLICY_PICTURE_FIRST, 0, STEREO_MARK_MP4_DEPTH, "depth above" },
        { STEREO_POLICY_LAYOUT_DEPTH_TOP_AND_BOTTOM, STEREO_POLICY_DEPTH_FIRST, 0, STEREO_MARK_MP4_DEPTH, "depth above, depth first" },
        { STEREO_POLICY_LAYOUT_DEPTH_GRAPHICS, STEREO_POLICY_PICTURE_FIRST, 0, STEREO_MARK_MP4_DEPTH, "graphics" },
        { STEREO_POLICY_LAYOUT_DEPTH_BACKGROUND, STEREO_POLICY_PICTURE_FIRST, 0, STEREO_MARK_MP4_DEPTH, "background" },
        { STEREO_POLICY_LAYOUT_DEPTH_SEPARATE, STEREO_POLICY_PICTURE_FIRST, 0, STEREO_MARK_LAYOUT, "separate depth" },
    };
    for (size_t i = 0; i < sizeof(refused) / sizeof(refused[0]); ++i) {
        st = stereo_mark_save(f, 1, refused[i].layout, refused[i].order, refused[i].anaglyph, &r);
        CHECK(st == refused[i].status && r.value == -1, "MP4 refuses %s: %s", refused[i].why, stereo_mark_status_name(st));
        CHECK(same_as(&s, f), "refused (%s), the mp4 is byte for byte", refused[i].why);
    }
    st = stereo_mark_save(f, 2, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, 0, 0, &r);
    CHECK(st == STEREO_MARK_NO_TRACK && same_as(&s, f), "no second video track in the mp4: %s", stereo_mark_status_name(st));
    drop(&s);

    /* moov at the start with a free box of 8 bytes after it (what ffmpeg's faststart leaves): too small, st3d would move the media data */
    CHECK(run("ffmpeg -v error -y -i '%s' -map 0:v:0 -c copy -movflags +faststart -f mp4 '%s'", path_of("src.mkv"), path_of("fs.mp4")) == 0, "ffmpeg makes the faststart mp4");
    boxes_of(path_of("fs.mp4"), boxes, sizeof(boxes));
    CHECK(strcmp(boxes, "ftyp moov free mdat") == 0, "the fixture has its moov before mdat and a free box of 8 bytes: %s", boxes);
    s = snap(path_of("fs.mp4"));
    st = stereo_mark_save(path_of("fs.mp4"), 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, 0, 0, &r);
    CHECK(st == STEREO_MARK_MP4_NO_ROOM, "moov before mdat with no room: %s", stereo_mark_status_name(st));
    CHECK(same_as(&s, path_of("fs.mp4")), "no room, the file is byte for byte");
    drop(&s);

    /* the free box of other sizes: 13 bytes (all taken, the box goes), 4104 (shortened), 20 (too small by one byte) */
    static const struct { int free_size; int fits; } rooms[] = { { 13, 1 }, { 4104, 1 }, { 20, 0 }, { 21, 1 }, { 12, 0 } };
    for (size_t i = 0; i < sizeof(rooms) / sizeof(rooms[0]); ++i) {
        char name[32];
        snprintf(name, sizeof(name), "room%d.mp4", rooms[i].free_size);
        CHECK(make_free_room(path_of("fs.mp4"), path_of(name), rooms[i].free_size) == 0, "make the file with a free box of %d bytes", rooms[i].free_size);
        const char *g = path_of(name);
        int n = packet_count(g);
        CHECK(n == packets, "the rebuilt file (free box %d) demuxes like the original: %d, %d", rooms[i].free_size, n, packets);
        size_t size_before;
        free(slurp(g, &size_before));
        Snapshot sg = snap(g);
        st = stereo_mark_save(g, 1, STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, STEREO_POLICY_LEFT_FIRST, 0, &r);
        if (rooms[i].fits) {
            CHECK(st == STEREO_MARK_WRITTEN && r.now == 1, "st3d into a free box of %d bytes: %s (%s)", rooms[i].free_size, stereo_mark_status_name(st), r.details);
            size_t size_after;
            free(slurp(g, &size_after));
            CHECK(size_after == size_before, "the file keeps its size: %zu, %zu", size_before, size_after);
            boxes_of(g, boxes, sizeof(boxes));
            CHECK(strcmp(boxes, rooms[i].free_size == 13 ? "ftyp moov mdat" : "ftyp moov free mdat") == 0, "the boxes after a free box of %d: %s", rooms[i].free_size, boxes);
            CHECK(packet_count(g) == n, "the media is intact after st3d into a free box of %d: %d packets of %d", rooms[i].free_size, packet_count(g), n);
            CHECK(libav_mark(g, 1, tag, sizeof(tag), &type, &flags) == 1 && type == AV_STEREO3D_TOPBOTTOM, "libavformat reads top and bottom (free box %d)", rooms[i].free_size);
            /* nothing else moved: the bytes of mdat are the same */
            CHECK(tail_same(g, path_of("fs.mp4"), 11000), "the media data is where and as it was (free box %d)", rooms[i].free_size);
        } else {
            CHECK(st == STEREO_MARK_MP4_NO_ROOM, "a free box of %d bytes is too small: %s", rooms[i].free_size, stereo_mark_status_name(st));
            CHECK(same_as(&sg, g), "no room (free box %d), the file is byte for byte", rooms[i].free_size);
        }
        drop(&sg);
    }
}

static void remove_tree(void)
{
    run("chmod -R u+w '%s' 2>/dev/null; rm -rf '%s'", dir, dir);
}

int main(void)
{
    const char *base = getenv("TMPDIR") ? getenv("TMPDIR") : "/tmp";
    snprintf(dir, sizeof(dir), "%s/stereo-mark-test-XXXXXX", base);
    if (!mkdtemp(dir)) {
        perror("mkdtemp");
        return 2;
    }
    av_log_set_level(AV_LOG_ERROR);
    test_values();
    test_matroska();
    test_tracks_moved();
    test_laced();
    test_views();
    test_refusals();
    test_webm();
    test_tool_failures();
    test_declared_by_stream();
    test_mp4();
    remove_tree();
    printf("marktest: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
