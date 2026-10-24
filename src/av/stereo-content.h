/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */
#ifndef STEREO_CONTENT_H
#define STEREO_CONTENT_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define STEREO_CONTENT_ABI 1
/* An absent declaration differs from explicit monoscopic content. */
enum StereoContentStatus { STEREO_CONTENT_OK, STEREO_CONTENT_INCOMPLETE, STEREO_CONTENT_INVALID, STEREO_CONTENT_UNSUPPORTED };
enum StereoContentEvidence { STEREO_CONTENT_CONTAINER = 1, STEREO_CONTENT_FRAME = 2, STEREO_CONTENT_PICTURE = 4 };
typedef struct StereoContentRequest {
    size_t size;
    unsigned abi;
    int fd;                      /* Borrowed read-only regular-file FD; offset is preserved. */
    int64_t deadline_us;          /* CLOCK_MONOTONIC absolute time, including client startup. */
    uint64_t max_bytes;
    unsigned max_packets;
    char base_mime[128];
} StereoContentRequest;
typedef struct StereoContentResult {
    size_t size;
    unsigned abi;
    int status;
    int stereo;                  /* -1 absent, 0 explicitly mono, 1 stereo. */
    int layout;                  /* stereo_policy_layout, without shape/name inference. */
    int order;                   /* 0 left first, 1 right first. */
    int anaglyph;                 /* stereo_policy_anaglyph */
    int projection;              /* -1 absent, otherwise AVSphericalProjection. */
    int coverage;                /* 0 unknown/partial, 180 or 360. */
    uint32_t bounds[4];           /* left, top, right, bottom, unsigned 0.32. */
    unsigned evidence;
    unsigned conflicts;
    int stream_index;
    uint64_t bytes;
    unsigned packets;
    int64_t elapsed_us;
    char base_mime[128];
    char refined_mime[128];
} StereoContentResult;
/* No path/URL is opened and no media bytes or attributes are written. */
__attribute__((visibility("default"))) int stereo_content_inspect(const StereoContentRequest *request, StereoContentResult *result);
#ifdef __cplusplus
}
#endif
#endif
