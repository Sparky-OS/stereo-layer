/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */
#define _POSIX_C_SOURCE 200809L
#include "stereo-content.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 5 || strcmp(argv[1], "--base") || strcmp(argv[3], "--json")) {
        fprintf(stderr, "Usage: stereo-content-probe --base MIME --json FILE\n");
        return 2;
    }
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    StereoContentRequest request = {.size = sizeof(request), .abi = STEREO_CONTENT_ABI,
        .fd = open(argv[4], O_RDONLY | O_CLOEXEC | O_NONBLOCK),
        .deadline_us = (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000 + 400000,
        .max_bytes = 4 * 1024 * 1024, .max_packets = 128};
    snprintf(request.base_mime, sizeof(request.base_mime), "%s", argv[2]);
    StereoContentResult result = {.size = sizeof(result)};
    int error = stereo_content_inspect(&request, &result);
    close(request.fd);
    if (error < 0) {
        fprintf(stderr, "Inspection failed: %d\n", error);
        return 1;
    }
    printf("{\"stereo\":%d,\"layout\":%d,\"order\":%d,\"anaglyph\":%d,\"projection\":%d,\"coverage\":%d,"
           "\"status\":%d,\"evidence\":%u,\"conflicts\":%u,\"stream\":%d,\"bytes\":%llu,\"packets\":%u,\"elapsed_us\":%lld}\n",
           result.stereo, result.layout, result.order, result.anaglyph, result.projection, result.coverage,
           result.status, result.evidence, result.conflicts, result.stream_index,
           (unsigned long long)result.bytes, result.packets, (long long)result.elapsed_us);
    return 0;
}
