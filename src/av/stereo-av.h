/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */
#ifndef STEREO_AV_H
#define STEREO_AV_H

#ifdef __cplusplus
extern "C" {
#endif

struct AVCodecContext;
struct AVFrame;
typedef struct StereoAVPair StereoAVPair;
#define STEREO_AV_EXPORT __attribute__((visibility("default")))

STEREO_AV_EXPORT StereoAVPair *stereo_av_pair_alloc(void);
STEREO_AV_EXPORT void stereo_av_pair_reset(StereoAVPair *pair);
STEREO_AV_EXPORT void stereo_av_pair_free(StereoAVPair **pair);

/* Enable FFmpeg's native view selection before opening an HEVC decoder. */
STEREO_AV_EXPORT int stereo_av_decoder_views(struct AVCodecContext *decoder);
/* Valid in get_format, where FFmpeg exports the active VPS. */
STEREO_AV_EXPORT void stereo_av_pair_configure(StereoAVPair *pair, const struct AVCodecContext *decoder);
/* AVStereo3DView of the signalled VPS base eye, or UNSPEC when unknown. */
STEREO_AV_EXPORT int stereo_av_pair_base_eye(const StereoAVPair *pair);
STEREO_AV_EXPORT int stereo_av_decoder_multiview(const struct AVCodecContext *decoder);

/* Consume frame in place. Return 0 while waiting, 1 for an ordinary/base
 * picture, 2 for a left-first SBS pair, or a negative AVERROR.
 * Only distinct, signalled eyes at the same known timestamp are paired.
 * enabled=0 selects the VPS base view, with no stereo declaration.
 * Reset on seek, source/sequence replacement and decoder flush. */
STEREO_AV_EXPORT int stereo_av_pair_frame(StereoAVPair *pair, const struct AVCodecContext *decoder,
                         struct AVFrame *frame, int enabled);

#ifdef __cplusplus
}
#endif
#endif
