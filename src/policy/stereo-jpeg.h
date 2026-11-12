/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */
#ifndef STEREO_JPEG_H
#define STEREO_JPEG_H
#include "stereo-policy.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* VRex General Purpose Stereoscopic Data Descriptor (1997), pp.3-6.
 * Payload includes _JPSJPS_; borrowed slices remain valid while input lives.
 * Decode accepts sView's historical extra trailing NUL. Encode emits exact lengths.
 * Every reserved field is checked; future fields are refused, never discarded. */
struct stereo_jps_descriptor {
    uint32_t value;
    const unsigned char *extension, *comment;
    uint16_t extension_size, comment_size;
};
STEREO_POLICY_EXPORT int stereo_jps_decode(const void *payload, size_t size, struct stereo_jps_descriptor *result);
/* Returns payload size; zero for invalid fields, insufficient capacity or null output. */
STEREO_POLICY_EXPORT size_t stereo_jps_encode(const struct stereo_jps_descriptor *, void *output, size_t capacity);
/* CIPA DC-007-Translation-2009: TIFF data after MPF\0, both byte orders.
 * Field returns offset of inline value / external-value offset, zero on invalid/missing/duplicate.
 * All IFDs, types, counts, offsets and cycles are validated by stereo_mpf_valid(). */
STEREO_POLICY_EXPORT int stereo_mpf_valid(const void *tiff, size_t size);
STEREO_POLICY_EXPORT uint32_t stereo_mpf_u32(const void *, size_t, size_t offset);
STEREO_POLICY_EXPORT size_t stereo_mpf_field(const void *, size_t, uint32_t ifd, uint16_t tag, uint16_t type, uint32_t count);
STEREO_POLICY_EXPORT uint32_t stereo_mpf_next(const void *, size_t, uint32_t ifd);
STEREO_POLICY_EXPORT int stereo_mpf_set_u32(void *, size_t, size_t offset, uint32_t value);
struct stereo_mpf_entry {
    uint32_t attributes, size, offset;
};
/* Two-view writer's TIFF: entries non-null => index + attributes (viewpoint 1),
 * null => secondary attributes (viewpoint 2); B204 is viewpoint 1. */
STEREO_POLICY_EXPORT size_t stereo_mpf_encode_pair(const struct stereo_mpf_entry *entries, void *output, size_t capacity);
/* Existing-file metadata correction, without Qt or an image encoder.
 * Layout: -1 unmarked (inspect only), 0 mono, 1 rows, 2 side by side,
 * 3 top/bottom, 4 red/cyan, 5 indexed disparity MPO. right_first is 0 or 1.
 * JPEG scan bytes, marker fill and unrelated metadata are preserved.
 * Buffers must not overlap. On refusal output is untouched.
 * Capacity size + 65537 suffices; output_size is written only on success.
 * The caller must verify decoded eyes and perform a temporary-file transaction. */
enum stereo_jpeg_result {
    STEREO_JPEG_OK = 0,
    STEREO_JPEG_INVALID = 1,
    STEREO_JPEG_LOSSLESS = 2,
    STEREO_JPEG_DESCRIPTOR = 3,
    STEREO_JPEG_GEOMETRY = 4,
    STEREO_JPEG_MPO = 5,
    STEREO_JPEG_TRAILING = 6,
    STEREO_JPEG_CAPACITY = 7,
    STEREO_JPEG_LAYOUT = 8
};
struct stereo_jpeg_info {
    unsigned width, height;
    int layout, right_first, lossless;
    uint32_t descriptor;
    size_t image_count, image_offset[2], image_size[2];
};
STEREO_POLICY_EXPORT int stereo_jpeg_inspect(const void *, size_t, struct stereo_jpeg_info *);
STEREO_POLICY_EXPORT int stereo_jpeg_correct(const void *, size_t, int layout, int right_first, void *output, size_t capacity, size_t *output_size);
#ifdef __cplusplus
}
#endif
#endif
