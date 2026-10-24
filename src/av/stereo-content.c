/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */
#define _POSIX_C_SOURCE 200809L
#include "stereo-content.h"
#include "stereo-av.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/intreadwrite.h>
#include <libavutil/spherical.h>
#include <libavutil/stereo3d.h>
#include <libavutil/opt.h>

typedef struct Reader {
    const StereoContentRequest *request;
    StereoContentResult *result;
    int64_t offset, length;
    int exhausted;
} Reader;

static int64_t monotonic_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

static int interrupted(void *opaque)
{
    Reader *r = opaque;
    return r->exhausted || monotonic_us() >= r->request->deadline_us;
}

static int read_at(Reader *r, int64_t offset, unsigned char *buffer, size_t length)
{
    if (interrupted(r))
        return AVERROR_EXIT;
    if (length > r->request->max_bytes - r->result->bytes)
        length = r->request->max_bytes - r->result->bytes;
    if (!length) {
        r->exhausted = 1;
        return AVERROR_EXIT;
    }
    ssize_t got = pread(r->request->fd, buffer, length, offset);
    if (got < 0)
        return AVERROR(errno);
    r->result->bytes += got;
    return got ? (int)got : AVERROR_EOF;
}

static int read_packet(void *opaque, uint8_t *buffer, int length)
{
    Reader *r = opaque;
    int got = read_at(r, r->offset, buffer, length);
    if (got > 0)
        r->offset += got;
    return got;
}

static int64_t seek_packet(void *opaque, int64_t offset, int whence)
{
    Reader *r = opaque;
    if (whence == AVSEEK_SIZE)
        return r->length;
    whence &= ~AVSEEK_FORCE;
    int64_t base = whence == SEEK_SET ? 0 : whence == SEEK_CUR ? r->offset : whence == SEEK_END ? r->length : -1;
    if (base < 0 || offset < -base || offset > INT64_MAX - base || base + offset > r->length)
        return AVERROR(EINVAL);
    return r->offset = base + offset;
}

static int deny_open(AVFormatContext *s, AVIOContext **pb, const char *url, int flags, AVDictionary **options)
{
    (void)s; (void)pb; (void)url; (void)flags; (void)options;
    return AVERROR(EACCES);
}

static void stereo_data(StereoContentResult *out, const AVStereo3D *s, unsigned evidence)
{
    /* AVStereo3D is extensible: the caller checks the minimum fields it reads. */
    static const int layouts[] = {0, 1, 2, 8, 6, 7, 4, 5, -1};
    if (s->type < 0 || (unsigned)s->type >= sizeof(layouts)/sizeof(layouts[0]))
        return;
    int stereo = s->type != AV_STEREO3D_2D;
    int layout = layouts[s->type];
    int order = !!(s->flags & AV_STEREO3D_FLAG_INVERT);
    if (out->stereo >= 0 && (out->stereo != stereo || out->layout != layout || out->order != order))
        out->conflicts |= evidence;
    out->stereo = stereo;
    out->layout = layout;
    out->order = order;
    out->evidence |= evidence;
}

static void spherical_data(StereoContentResult *out, const AVSphericalMapping *s, unsigned evidence)
{
    out->projection = s->projection;
    out->coverage = 0;
    memset(out->bounds, 0, sizeof(out->bounds));
    if (s->projection == AV_SPHERICAL_CUBEMAP || s->projection == AV_SPHERICAL_EQUIRECTANGULAR)
        out->coverage = 360;
    else if (s->projection == AV_SPHERICAL_HALF_EQUIRECTANGULAR)
        out->coverage = 180;
    else if (s->projection == AV_SPHERICAL_EQUIRECTANGULAR_TILE) {
        out->bounds[0] = s->bound_left;
        out->bounds[1] = s->bound_top;
        out->bounds[2] = s->bound_right;
        out->bounds[3] = s->bound_bottom;
        uint64_t horizontal = (uint64_t)s->bound_left + s->bound_right;
        uint64_t vertical = (uint64_t)s->bound_top + s->bound_bottom;
        if (horizontal >= UINT32_MAX || vertical >= UINT32_MAX)
            out->status = STEREO_CONTENT_INVALID;
        else if (!horizontal && !vertical)
            out->coverage = 360;
        else if ((horizontal == 0x7fffffff || horizontal == 0x80000000) && !vertical)
            out->coverage = 180;
    }
    out->evidence |= evidence;
}

static void container_data(StereoContentResult *out, const AVCodecParameters *par)
{
    const AVPacketSideData *s = av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data, AV_PKT_DATA_STEREO3D);
    if (s && s->size >= offsetof(AVStereo3D, view))
        stereo_data(out, (const AVStereo3D *)s->data, STEREO_CONTENT_CONTAINER);
    s = av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data, AV_PKT_DATA_SPHERICAL);
    if (s && s->size >= offsetof(AVSphericalMapping, padding))
        spherical_data(out, (const AVSphericalMapping *)s->data, STEREO_CONTENT_CONTAINER);
}

/* Matroska exports the original StereoMode as a stream tag, including the
 * two anaglyph values absent from AVStereo3D and block-laced identity. */
static void matroska_data(StereoContentResult *out, const AVStream *stream)
{
    static const struct { const char *name; int layout, order, anaglyph; } modes[] = {
        {"mono", 0, 0, 0}, {"left_right", 1, 0, 0}, {"bottom_top", 2, 1, 0},
        {"top_bottom", 2, 0, 0}, {"checkerboard_rl", 6, 1, 0}, {"checkerboard_lr", 6, 0, 0},
        {"row_interleaved_rl", 4, 1, 0}, {"row_interleaved_lr", 4, 0, 0},
        {"col_interleaved_rl", 5, 1, 0}, {"col_interleaved_lr", 5, 0, 0},
        {"anaglyph_cyan_red", 0, 0, 1}, {"right_left", 1, 1, 0},
        {"anaglyph_green_magenta", 0, 0, 2}, {"block_lr", 9, 0, 0}, {"block_rl", 9, 1, 0}
    };
    const AVDictionaryEntry *tag = av_dict_get(stream->metadata, "stereo_mode", NULL, 0);
    if (!tag)
        return;
    for (size_t i = 0; i < sizeof(modes)/sizeof(modes[0]); ++i)
        if (!strcmp(tag->value, modes[i].name)) {
            out->stereo = i != 0;
            out->layout = modes[i].layout;
            out->order = modes[i].order;
            out->anaglyph = modes[i].anaglyph;
            out->evidence |= STEREO_CONTENT_CONTAINER;
            return;
        }
}

static void frame_data(StereoContentResult *out, const AVFrame *frame)
{
    AVFrameSideData *s = av_frame_get_side_data(frame, AV_FRAME_DATA_STEREO3D);
    if (s && s->size >= offsetof(AVStereo3D, view)) {
        const AVStereo3D *stereo = (const AVStereo3D *)s->data;
        /* FFmpeg represents block-laced Matroska as frame sequence on frames. */
        if (!(out->layout == 9 && stereo->type == AV_STEREO3D_FRAMESEQUENCE))
            stereo_data(out, stereo, STEREO_CONTENT_FRAME);
    }
    s = av_frame_get_side_data(frame, AV_FRAME_DATA_SPHERICAL);
    if (s && s->size >= offsetof(AVSphericalMapping, padding))
        spherical_data(out, (const AVSphericalMapping *)s->data, STEREO_CONTENT_FRAME);
}

/* PNG extension sTER: a typed, CRC-checked chunk, before IDAT, never a byte search. */
static int png_data(Reader *r)
{
    unsigned char head[33];
    if (read_at(r, 0, head, sizeof(head)) != sizeof(head) || memcmp(head, "\x89PNG\r\n\x1a\n", 8))
        return 0;
    if (AV_RB32(head + 8) != 13 || memcmp(head + 12, "IHDR", 4))
        return -1;
    int seen = 0;
    for (int64_t at = 33; at + 12 <= r->length;) {
        unsigned char h[13];
        if (read_at(r, at, h, 8) != 8)
            return -1;
        uint32_t length = AV_RB32(h);
        if (length > (uint64_t)(r->length - at - 12))
            return -1;
        if (!memcmp(h + 4, "IDAT", 4) || !memcmp(h + 4, "IEND", 4))
            return 1;
        if (!memcmp(h + 4, "sTER", 4)) {
            if (seen++ || length != 1 || read_at(r, at + 8, h + 8, 5) != 5 || h[8] > 1
                || crc32(0, h + 4, 5) != AV_RB32(h + 9))
                return -1;
            r->result->stereo = 1;
            r->result->layout = 1;
            r->result->order = h[8] == 0;
            r->result->evidence |= STEREO_CONTENT_PICTURE;
        }
        at += length + 12;
    }
    return -1;
}

static void refine_type(StereoContentResult *out)
{
    const char *stem = NULL;
    const struct { const char *base, *stem; } types[] = {
#include "content-mime-map.inc"
    };
    for (size_t i = 0; i < sizeof(types)/sizeof(types[0]); ++i)
        if (!strcmp(out->base_mime, types[i].base))
            stem = types[i].stem;
    snprintf(out->refined_mime, sizeof(out->refined_mime), "%s", out->base_mime);
    if (!stem || out->status != STEREO_CONTENT_OK)
        return;
    const char *suffix = out->projection < 0 ? "" : out->coverage == 180 ? "-180" : out->coverage == 360 ? "-360" : "-spherical";
    if (out->stereo == 1 || *suffix)
        snprintf(out->refined_mime, sizeof(out->refined_mime), "%s%s%s", stem, out->stereo == 1 ? "-3d" : "", suffix);
}

int stereo_content_inspect(const StereoContentRequest *request, StereoContentResult *out)
{
    struct stat initial, final;
    if (!request || !out || request->size < sizeof(*request) || out->size < sizeof(*out)
        || request->abi != STEREO_CONTENT_ABI || !memchr(request->base_mime, 0, sizeof(request->base_mime))
        || !request->max_bytes || !request->max_packets || fstat(request->fd, &initial) || !S_ISREG(initial.st_mode))
        return AVERROR(EINVAL);
    int64_t started = monotonic_us();
    memset(out, 0, sizeof(*out));
    out->size = sizeof(*out);
    out->abi = STEREO_CONTENT_ABI;
    out->stereo = -1;
    out->projection = -1;
    out->stream_index = -1;
    snprintf(out->base_mime, sizeof(out->base_mime), "%s", request->base_mime);
    Reader reader = {request, out, 0, initial.st_size, 0};
    int png = png_data(&reader);
    if (png) {
        if (png < 0)
            out->status = interrupted(&reader) ? STEREO_CONTENT_INCOMPLETE : STEREO_CONTENT_INVALID;
        goto done;
    }
    AVFormatContext *format = avformat_alloc_context();
    unsigned char *buffer = av_malloc(32768);
    AVIOContext *io = buffer ? avio_alloc_context(buffer, 32768, 0, &reader, read_packet, NULL, seek_packet) : NULL;
    if (!format || !io) {
        avformat_free_context(format);
        av_free(buffer);
        return AVERROR(ENOMEM);
    }
    format->pb = io;
    format->flags |= AVFMT_FLAG_CUSTOM_IO;
    format->interrupt_callback = (AVIOInterruptCB){interrupted, &reader};
    format->io_open = deny_open;
    format->probesize = request->max_bytes;
    AVDictionary *options = NULL;
    av_dict_set(&options, "protocol_whitelist", "", 0);
    int ret = avformat_open_input(&format, NULL, NULL, &options);
    av_dict_free(&options);
    AVCodecContext *decoder = NULL;
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (ret < 0 || !packet || !frame) {
        out->status = interrupted(&reader) ? STEREO_CONTENT_INCOMPLETE : STEREO_CONTENT_UNSUPPORTED;
        goto close;
    }
    int video = -1;
    unsigned video_track = 0;
    for (unsigned i = 0; i < format->nb_streams; ++i) {
        AVStream *stream = format->streams[i];
        if (stream->codecpar->codec_type != AVMEDIA_TYPE_VIDEO || stream->disposition & AV_DISPOSITION_ATTACHED_PIC)
            continue;
        ++video_track;
        if (request->video_track_plus_one && video_track != request->video_track_plus_one)
            continue;
        StereoContentResult candidate = *out;
        container_data(&candidate, stream->codecpar);
        if (strstr(format->iformat->name, "matroska"))
            matroska_data(&candidate, stream);
        if (video < 0 || candidate.stereo == 1 || candidate.projection >= 0) {
            *out = candidate;
            out->stream_index = video = i;
        }
    }
    if (video < 0)
        goto close;
    const AVCodecParameters *par = format->streams[video]->codecpar;
    out->width = par->width;
    out->height = par->height;
    AVRational sar = format->streams[video]->sample_aspect_ratio;
    if (sar.num <= 0 || sar.den <= 0)
        sar = par->sample_aspect_ratio;
    out->sar_num = sar.num > 0 && sar.den > 0 ? sar.num : 1;
    out->sar_den = sar.num > 0 && sar.den > 0 ? sar.den : 1;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    decoder = codec ? avcodec_alloc_context3(codec) : NULL;
    if (!decoder || avcodec_parameters_to_context(decoder, par) < 0)
        goto close;
    decoder->thread_count = 1;
    decoder->max_pixels = 32 * 1024 * 1024;
    stereo_av_decoder_views(decoder);
    if (avcodec_open2(decoder, codec, NULL) < 0)
        goto close;
    unsigned frames = 0;
    while (!interrupted(&reader) && out->packets < request->max_packets && frames < 8 && av_read_frame(format, packet) >= 0) {
        ++out->packets;
        if (packet->stream_index == video && avcodec_send_packet(decoder, packet) >= 0) {
            while (!interrupted(&reader) && avcodec_receive_frame(decoder, frame) >= 0) {
                frame_data(out, frame);
                ++frames;
                av_frame_unref(frame);
            }
        }
        av_packet_unref(packet);
    }
    if (!interrupted(&reader) && avcodec_send_packet(decoder, NULL) >= 0)
        while (!interrupted(&reader) && avcodec_receive_frame(decoder, frame) >= 0) {
            frame_data(out, frame);
            av_frame_unref(frame);
        }
    if (out->stereo == 1 && stereo_av_decoder_multiview(decoder)) {
        out->stereo = 1;
        out->layout = 3;
        out->evidence |= STEREO_CONTENT_FRAME;
    }
close:
    avcodec_free_context(&decoder);
    av_frame_free(&frame);
    av_packet_free(&packet);
    avformat_close_input(&format);
    av_freep(&io->buffer);
    avio_context_free(&io);
done:
    if (interrupted(&reader))
        out->status = STEREO_CONTENT_INCOMPLETE;
    if (fstat(request->fd, &final) || initial.st_dev != final.st_dev || initial.st_ino != final.st_ino
        || initial.st_size != final.st_size || initial.st_mtim.tv_sec != final.st_mtim.tv_sec
        || initial.st_mtim.tv_nsec != final.st_mtim.tv_nsec || initial.st_ctim.tv_sec != final.st_ctim.tv_sec
        || initial.st_ctim.tv_nsec != final.st_ctim.tv_nsec)
        out->status = STEREO_CONTENT_INVALID;
    out->elapsed_us = monotonic_us() - started;
    refine_type(out);
    return 0;
}
