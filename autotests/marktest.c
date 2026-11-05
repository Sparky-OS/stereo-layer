/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/*
 * The writers of the 3D layout into Matroska, WebM and MP4 (stereo-mark.h), against real files: ffmpeg makes them,
 * mkvmerge and libavformat read what was written (the oracles that are not our reader), and every refusal leaves the
 * file byte for byte as it was. Needs ffmpeg, mkvmerge and mkvpropedit in PATH (mkvtoolnix); a missing tool fails the test.
 */

#define _GNU_SOURCE
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

static int run(const char *fmt, ...)
{
    char cmd[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cmd, sizeof(cmd), fmt, ap);
    va_end(ap);
    return system(cmd);
}

static char *path_of(const char *name)
{
    static char bufs[8][700];
    static int next;
    char *b = bufs[next++ & 7];
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
    char body[900];
    snprintf(body, sizeof(body), "case \"$*\" in *--set*) exec '%s' \"$1\" --edit \"$3\" --set stereo-mode=7 ;; *) exec '%s' \"$@\" ;; esac\n", real, real);
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

    /* moov at the start with no free space after it: st3d would move the media data, so it is refused */
    CHECK(run("ffmpeg -v error -y -i '%s' -map 0:v:0 -c copy -movflags +faststart -f mp4 '%s'", path_of("src.mkv"), path_of("fs.mp4")) == 0, "ffmpeg makes the faststart mp4");
    boxes_of(path_of("fs.mp4"), boxes, sizeof(boxes));
    CHECK(strcmp(boxes, "ftyp moov mdat") == 0, "the fixture has its moov before mdat and no free box: %s", boxes);
    s = snap(path_of("fs.mp4"));
    st = stereo_mark_save(path_of("fs.mp4"), 1, STEREO_POLICY_LAYOUT_SIDE_BY_SIDE, 0, 0, &r);
    CHECK(st == STEREO_MARK_MP4_NO_ROOM, "moov before mdat with no room: %s", stereo_mark_status_name(st));
    CHECK(same_as(&s, path_of("fs.mp4")), "no room, the file is byte for byte");
    drop(&s);

    /* moov at the start with a free box after it: the free box gives the 13 bytes, mdat does not move */
    CHECK(run("ffmpeg -v error -y -i '%s' -map 0:v:0 -c copy -movflags +faststart -moov_size 4096 -f mp4 '%s'", path_of("src.mkv"), path_of("fr.mp4")) == 0, "ffmpeg makes the mp4 with reserved moov space");
    boxes_of(path_of("fr.mp4"), boxes, sizeof(boxes));
    if (strcmp(boxes, "ftyp moov free mdat") == 0) {
        size_t size_before;
        free(slurp(path_of("fr.mp4"), &size_before));
        int n = packet_count(path_of("fr.mp4"));
        st = stereo_mark_save(path_of("fr.mp4"), 1, STEREO_POLICY_LAYOUT_TOP_AND_BOTTOM, STEREO_POLICY_LEFT_FIRST, 0, &r);
        CHECK(st == STEREO_MARK_WRITTEN && r.now == 1, "st3d into the free space: %s (%s)", stereo_mark_status_name(st), r.details);
        size_t size_after;
        free(slurp(path_of("fr.mp4"), &size_after));
        CHECK(size_after == size_before, "the file keeps its size: %zu, %zu", size_before, size_after);
        boxes_of(path_of("fr.mp4"), boxes, sizeof(boxes));
        CHECK(strcmp(boxes, "ftyp moov free mdat") == 0, "the boxes: %s", boxes);
        CHECK(packet_count(path_of("fr.mp4")) == n, "the media of the free-space file is intact");
        CHECK(libav_mark(path_of("fr.mp4"), 1, tag, sizeof(tag), &type, &flags) == 1 && type == AV_STEREO3D_TOPBOTTOM, "libavformat reads top and bottom from it");
    } else {
        CHECK(0, "ffmpeg did not leave a free box after moov (%s): the free-space case has no fixture", boxes);
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
    test_refusals();
    test_webm();
    test_tool_failures();
    test_declared_by_stream();
    test_mp4();
    remove_tree();
    printf("marktest: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
