/* SPDX-License-Identifier: LGPL-2.1-or-later */
#ifndef STEREO_CONTENT_CLIENT_H
#define STEREO_CONTENT_CLIENT_H
#ifdef __cplusplus
extern "C" {
#endif
/* Returns 1 only for a validated refinement, 0 for unchanged/unavailable.
 * The borrowed FD must be regular and read-only. No path is opened by the service.
 * timeout_ms 0 queries only the shared cache; positive values are capped at 400.
 * Does not load a decoder or Qt. The output is NUL terminated if capacity > 0. */
__attribute__((visibility("default"))) int stereo_content_refine_fd(int fd, const char *base,
    unsigned timeout_ms, char *mime, unsigned capacity);
#ifdef __cplusplus
}
#endif
#endif
