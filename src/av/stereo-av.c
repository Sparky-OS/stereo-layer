/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */
#include "stereo-av.h"

#include <errno.h>
#include <limits.h>
#include <string.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libavutil/stereo3d.h>

struct StereoAVPair {
    AVFrame *pending;
    int pending_eye;
    int pending_id;
    unsigned ids[2];
    int configured;
    int base_eye;
    int single_view;
};

StereoAVPair *stereo_av_pair_alloc(void)
{
    StereoAVPair *pair = av_mallocz(sizeof(*pair));
    if (pair && !(pair->pending = av_frame_alloc()))
        av_freep(&pair);
    return pair;
}

void stereo_av_pair_reset(StereoAVPair *pair)
{
    if (pair)
        av_frame_unref(pair->pending);
}

void stereo_av_pair_free(StereoAVPair **pair)
{
    if (pair && *pair) {
        av_frame_free(&(*pair)->pending);
        av_freep(pair);
    }
}

int stereo_av_decoder_views(AVCodecContext *decoder)
{
    if (decoder->codec_id != AV_CODEC_ID_HEVC)
        return 0;
    return av_opt_set(decoder, "view_ids", "-1", AV_OPT_SEARCH_CHILDREN);
}

static int view_ids(const AVCodecContext *decoder, unsigned ids[2])
{
    unsigned count = 0;
    if (av_opt_get_array_size((void *)decoder, "view_ids_available", AV_OPT_SEARCH_CHILDREN, &count) < 0 || count != 2)
        return 0;
    if (av_opt_get_array((void *)decoder, "view_ids_available", AV_OPT_SEARCH_CHILDREN,
                         0, 2, AV_OPT_TYPE_UINT, ids) < 0)
        return 0;
    return ids[0] != ids[1];
}

static int single_view(const AVCodecContext *decoder)
{
    unsigned count = 0;
    int requested = -1;
    if (av_opt_get_array_size((void *)decoder, "view_ids", AV_OPT_SEARCH_CHILDREN, &count) < 0)
        return 0;
    if (!count)
        return 1;
    return count == 1
        && av_opt_get_array((void *)decoder, "view_ids", AV_OPT_SEARCH_CHILDREN, 0, 1, AV_OPT_TYPE_INT, &requested) >= 0
        && requested >= 0;
}

void stereo_av_pair_configure(StereoAVPair *pair, const AVCodecContext *decoder)
{
    unsigned ids[2];
    int valid = view_ids(decoder, ids);
    pair->single_view = single_view(decoder);
    if (valid != pair->configured || (valid && memcmp(ids, pair->ids, sizeof(ids))))
        stereo_av_pair_reset(pair);
    if (valid != pair->configured || (valid && memcmp(ids, pair->ids, sizeof(ids))))
        pair->base_eye = AV_STEREO3D_VIEW_UNSPEC;
    pair->configured = valid;
    if (valid)
        memcpy(pair->ids, ids, sizeof(ids));
}

int stereo_av_pair_base_eye(const StereoAVPair *pair)
{
    return pair->base_eye;
}

int stereo_av_decoder_multiview(const AVCodecContext *decoder)
{
    unsigned ids[2];
    return decoder->codec_id == AV_CODEC_ID_HEVC && view_ids(decoder, ids) && !single_view(decoder);
}

static void make_mono(AVFrame *frame)
{
    av_frame_remove_side_data(frame, AV_FRAME_DATA_STEREO3D);
    av_frame_remove_side_data(frame, AV_FRAME_DATA_VIEW_ID);
    AVStereo3D *stereo = av_stereo3d_create_side_data(frame);
    if (stereo)
        stereo->type = AV_STEREO3D_2D;
}

static int join_views(AVFrame *out, const AVFrame *left, const AVFrame *right,
                       const AVCodecContext *decoder)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(left->format);
    if (!desc || desc->flags & (AV_PIX_FMT_FLAG_HWACCEL | AV_PIX_FMT_FLAG_PAL | AV_PIX_FMT_FLAG_BITSTREAM)
        || left->width > INT_MAX / 2 || left->width != right->width || left->height != right->height
        || left->format != right->format || left->color_range != right->color_range
        || left->color_primaries != right->color_primaries || left->color_trc != right->color_trc
        || left->colorspace != right->colorspace
        || av_cmp_q(left->sample_aspect_ratio, right->sample_aspect_ratio)
        || left->width % (1 << desc->log2_chroma_w))
        return AVERROR(EINVAL);

    AVFrame *l = av_frame_clone(left), *r = av_frame_clone(right);
    if (!l || !r) {
        av_frame_free(&l);
        av_frame_free(&r);
        return AVERROR(ENOMEM);
    }
    int ret = av_frame_apply_cropping(l, AV_FRAME_CROP_UNALIGNED);
    if (ret >= 0)
        ret = av_frame_apply_cropping(r, AV_FRAME_CROP_UNALIGNED);
    if (ret < 0 || l->width != r->width || l->height != r->height
        || l->width % (1 << desc->log2_chroma_w)) {
        ret = ret < 0 ? ret : AVERROR(EINVAL);
        goto done;
    }
    out->format = l->format;
    out->width = l->width * 2;
    out->height = l->height;
    if ((ret = av_frame_get_buffer(out, 32)) < 0 || (ret = av_frame_copy_props(out, l)) < 0)
        goto done;
    out->crop_left = out->crop_right = out->crop_top = out->crop_bottom = 0;
    out->opaque = NULL;
    int rowsize[4];
    ptrdiff_t strides[4];
    size_t sizes[4];
    if ((ret = av_image_fill_linesizes(rowsize, l->format, l->width)) < 0)
        goto done;
    for (int p = 0; p < 4; p++)
        strides[p] = rowsize[p];
    if ((ret = av_image_fill_plane_sizes(sizes, l->format, l->height, strides)) < 0)
        goto done;
    for (int p = 0; p < 4 && rowsize[p]; p++) {
        for (size_t y = 0; y < sizes[p] / rowsize[p]; y++) {
            memcpy(out->data[p] + y * out->linesize[p], l->data[p] + (ptrdiff_t)y * l->linesize[p], rowsize[p]);
            memcpy(out->data[p] + y * out->linesize[p] + rowsize[p], r->data[p] + (ptrdiff_t)y * r->linesize[p], rowsize[p]);
        }
    }
    av_frame_remove_side_data(out, AV_FRAME_DATA_VIEW_ID);
    av_frame_remove_side_data(out, AV_FRAME_DATA_STEREO3D);
    AVStereo3D *stereo = av_stereo3d_create_side_data(out);
    if (!stereo) {
        ret = AVERROR(ENOMEM);
        goto done;
    }
    const AVFrameSideData *source_stereo = av_frame_get_side_data(l, AV_FRAME_DATA_STEREO3D);
    if (source_stereo && source_stereo->size >= sizeof(*stereo))
        *stereo = *(const AVStereo3D *)source_stereo->data;
    /* MOV vexu's presentation fields complement the codec's view identities. */
    const AVPacketSideData *presentation = av_packet_side_data_get(decoder->coded_side_data,
                                                    decoder->nb_coded_side_data, AV_PKT_DATA_STEREO3D);
    if (presentation && presentation->size >= sizeof(*stereo))
        *stereo = *(const AVStereo3D *)presentation->data;
    stereo->type = AV_STEREO3D_SIDEBYSIDE;
    stereo->view = AV_STEREO3D_VIEW_PACKED;
    stereo->flags &= ~AV_STEREO3D_FLAG_INVERT;
    ret = 0;
done:
    av_frame_free(&l);
    av_frame_free(&r);
    return ret;
}

int stereo_av_pair_frame(StereoAVPair *pair, const AVCodecContext *decoder,
                         AVFrame *frame, int enabled)
{
    const unsigned *ids = pair->ids;
    const AVFrameSideData *id = av_frame_get_side_data(frame, AV_FRAME_DATA_VIEW_ID);
    if (!id || id->size < sizeof(int) || !pair->configured) {
        stereo_av_pair_reset(pair);
        return 1;
    }
    if (pair->single_view) {
        stereo_av_pair_reset(pair);
        make_mono(frame);
        return 1;
    }
    int view;
    memcpy(&view, id->data, sizeof(view));
    if ((unsigned)view != ids[0] && (unsigned)view != ids[1]) {
        stereo_av_pair_reset(pair);
        av_frame_unref(frame);
        return 0;
    }
    const AVFrameSideData *sd = av_frame_get_side_data(frame, AV_FRAME_DATA_STEREO3D);
    const AVStereo3D *stereo = sd && sd->size >= sizeof(*stereo) ? (const AVStereo3D *)sd->data : NULL;
    int eye = stereo ? stereo->view : AV_STEREO3D_VIEW_UNSPEC;
    if ((unsigned)view == ids[0])
        pair->base_eye = eye;
    if (!enabled || frame->pts == AV_NOPTS_VALUE
        || (eye != AV_STEREO3D_VIEW_LEFT && eye != AV_STEREO3D_VIEW_RIGHT)) {
        stereo_av_pair_reset(pair);
        if ((unsigned)view != ids[0]) {
            av_frame_unref(frame);
            return 0;
        }
        make_mono(frame);
        return 1;
    }
    AVFrame *pending = pair->pending;
    if (!pending->buf[0] || pair->pending_eye == eye || pair->pending_id == view || pending->pts != frame->pts) {
        av_frame_unref(pending);
        av_frame_move_ref(pending, frame);
        pair->pending_eye = eye;
        pair->pending_id = view;
        return 0;
    }
    AVFrame *out = av_frame_alloc();
    if (!out)
        return AVERROR(ENOMEM);
    int ret = join_views(out, eye == AV_STEREO3D_VIEW_LEFT ? frame : pending,
                              eye == AV_STEREO3D_VIEW_RIGHT ? frame : pending, decoder);
    av_frame_unref(pending);
    av_frame_unref(frame);
    if (ret >= 0)
        av_frame_move_ref(frame, out);
    av_frame_free(&out);
    return ret < 0 ? ret : 2;
}
