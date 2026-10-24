/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */
#undef NDEBUG
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/stereo3d.h>
#include "stereo-av.h"

static enum AVPixelFormat get_format(AVCodecContext *ctx, const enum AVPixelFormat *formats)
{
    stereo_av_pair_configure(ctx->opaque, ctx);
    assert(stereo_av_decoder_multiview(ctx));
    return avcodec_default_get_format(ctx, formats);
}

static void equal_eye(const AVFrame *packed, const AVFrame *view, int eye)
{
    int row[4];
    ptrdiff_t strides[4];
    size_t sizes[4];
    assert(packed->width == view->width * 2 && packed->height == view->height);
    assert(av_image_fill_linesizes(row, view->format, view->width) >= 0);
    for (int p = 0; p < 4; p++) strides[p] = row[p];
    assert(av_image_fill_plane_sizes(sizes, view->format, view->height, strides) >= 0);
    for (int p = 0; p < 4 && row[p]; p++)
        for (size_t y = 0; y < sizes[p] / row[p]; y++)
            assert(!memcmp(packed->data[p] + y * packed->linesize[p] + eye * row[p],
                           view->data[p] + y * view->linesize[p], row[p]));
}

static int push(StereoAVPair *pair, AVCodecContext *ctx, const AVFrame *source, int enabled)
{
    AVFrame *copy = av_frame_clone(source);
    assert(copy);
    int result = stereo_av_pair_frame(pair, ctx, copy, enabled);
    av_frame_free(&copy);
    return result;
}

int main(void)
{
    AVFormatContext *format = NULL;
    assert(avformat_open_input(&format, FIXTURE, NULL, NULL) >= 0);
    assert(avformat_find_stream_info(format, NULL) >= 0);
    int track = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    assert(track >= 0);
    const AVCodec *codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    assert(avcodec_parameters_to_context(ctx, format->streams[track]->codecpar) >= 0);
    ctx->pkt_timebase = format->streams[track]->time_base;
    ctx->thread_count = 2;
    StereoAVPair *pair = stereo_av_pair_alloc();
    assert(pair);
    ctx->opaque = pair;
    ctx->get_format = get_format;
    assert(stereo_av_decoder_views(ctx) >= 0 && avcodec_open2(ctx, codec, NULL) >= 0);
    AVPacket *packet = av_packet_alloc();
    AVFrame *frame = av_frame_alloc(), *eyes[2] = { 0 }, *first[2] = { 0 };
    int end = 0, decoded = 0, packed = 0;
    int64_t last_pts = AV_NOPTS_VALUE;
    while (!end) {
        end = av_read_frame(format, packet) < 0;
        if (end || packet->stream_index == track) {
            assert(avcodec_send_packet(ctx, end ? NULL : packet) >= 0);
            while (avcodec_receive_frame(ctx, frame) >= 0) {
                const AVFrameSideData *sd = av_frame_get_side_data(frame, AV_FRAME_DATA_STEREO3D);
                assert(sd);
                int eye = ((AVStereo3D *)sd->data)->view == AV_STEREO3D_VIEW_LEFT ? 0 : 1;
                av_frame_free(&eyes[eye]);
                eyes[eye] = av_frame_clone(frame);
                if (!first[eye]) first[eye] = av_frame_clone(frame);
                decoded++;
                int result = stereo_av_pair_frame(pair, ctx, frame, 1);
                assert(result == 0 || result == 2);
                if (result == 2) {
                    assert(eyes[0]->pts == eyes[1]->pts && frame->pts == eyes[0]->pts);
                    assert(last_pts == AV_NOPTS_VALUE || frame->pts > last_pts);
                    last_pts = frame->pts;
                    equal_eye(frame, eyes[0], 0);
                    equal_eye(frame, eyes[1], 1);
                    sd = av_frame_get_side_data(frame, AV_FRAME_DATA_STEREO3D);
                    const AVStereo3D *s = (const AVStereo3D *)sd->data;
                    assert(s->type == AV_STEREO3D_SIDEBYSIDE && s->view == AV_STEREO3D_VIEW_PACKED);
                    assert(!(s->flags & AV_STEREO3D_FLAG_INVERT) && s->baseline == 63000);
                    assert(s->primary_eye == AV_PRIMARY_EYE_LEFT);
                    assert(frame->color_primaries == eyes[0]->color_primaries);
                    assert(frame->color_trc == eyes[0]->color_trc && frame->colorspace == eyes[0]->colorspace);
                    assert(!av_frame_get_side_data(frame, AV_FRAME_DATA_VIEW_ID));
                    packed++;
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(packet);
    }
    assert(decoded == 192 && packed == 96);
    stereo_av_pair_reset(pair);
    assert(push(pair, ctx, first[0], 0) == 1 && push(pair, ctx, first[1], 0) == 0);
    assert(push(pair, ctx, first[0], 1) == 0);
    assert(push(pair, ctx, first[0], 1) == 0); /* duplicate view cannot complete a pair */
    AVFrame *bad = av_frame_clone(first[1]);
    bad->pts++;
    assert(stereo_av_pair_frame(pair, ctx, bad, 1) == 0);
    av_frame_free(&bad);
    stereo_av_pair_reset(pair); /* a seek cannot retain an old half */
    assert(push(pair, ctx, first[1], 1) == 0);
    stereo_av_pair_reset(pair);
    assert(push(pair, ctx, first[1], 1) == 0); /* either eye may arrive first */
    assert(push(pair, ctx, first[0], 1) == 2);
    assert(push(pair, ctx, first[0], 1) == 0);
    bad = av_frame_clone(first[1]);
    bad->color_range = first[0]->color_range == AVCOL_RANGE_MPEG ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;
    assert(stereo_av_pair_frame(pair, ctx, bad, 1) < 0);
    av_frame_free(&bad);
    printf("192 decoded views, 96 sample-exact pairs, presentation metadata and reset/duplicate/timestamp/format controls passed\n");
    for (int i = 0; i < 2; i++) { av_frame_free(&first[i]); av_frame_free(&eyes[i]); }
    stereo_av_pair_free(&pair);
    av_frame_free(&frame); av_packet_free(&packet); avcodec_free_context(&ctx); avformat_close_input(&format);
    return 0;
}
