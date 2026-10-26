/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _POSIX_C_SOURCE 200809L
#include "stereo-content.h"
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Arguments describe independently authored fixtures, not detector output. */
int main(int argc, char **argv)
{
    if (argc != 6) return 2;
    int marked = atoi(argv[2]), spherical = atoi(argv[3]);
    int width = atoi(argv[4]), height = atoi(argv[5]);
    int fd = open(argv[1], O_RDONLY | O_CLOEXEC), failed = 0;
    if (fd < 0) return 2;
    for (unsigned track = 0; track <= 2; ++track) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        StereoContentRequest request = {.size = sizeof(request), .abi = STEREO_CONTENT_ABI,
            .fd = fd, .deadline_us = (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000 + 5000000,
            .max_bytes = 16 * 1024 * 1024, .max_packets = 128, .video_track_plus_one = track};
        snprintf(request.base_mime, sizeof(request.base_mime), "%s", spherical ? "video/mp4" : "video/x-matroska");
        StereoContentResult result = {.size = sizeof(result)};
        int ret = stereo_content_inspect(&request, &result);
        int selected = track ? (int)track - 1 : marked;
        int has_mark = selected == marked;
        int expected_width = spherical || has_mark ? width : 960;
        int expected_height = spherical || has_mark ? height : 804;
        int ok = !ret && result.status == STEREO_CONTENT_OK
            && result.stream_index == selected
            && result.stereo == (!spherical && has_mark ? 1 : -1)
            && result.layout == (!spherical && has_mark ? 1 : 0)
            && result.order == 0 && result.anaglyph == 0 && result.conflicts == 0
            && result.projection == (spherical && has_mark ? 0 : -1)
            && result.coverage == (spherical && has_mark ? 360 : 0)
            && !result.bounds[0] && !result.bounds[1] && !result.bounds[2] && !result.bounds[3]
            && (has_mark ? result.evidence != 0 : result.evidence == 0)
            && result.width == expected_width && result.height == expected_height
            && result.sar_num == 1 && result.sar_den == 1
            && result.bytes > 0 && result.bytes <= request.max_bytes
            && result.packets > 0 && result.packets <= request.max_packets
            && !strcmp(result.base_mime, request.base_mime)
            && lseek(fd, 0, SEEK_CUR) == 0;
        printf("%s track=%u expected_stream=%d actual_stream=%d stereo=%d layout=%d projection=%d coverage=%d raster=%dx%d evidence=%u conflicts=%u bytes=%llu packets=%u: %s\n",
            argv[1], track, selected, result.stream_index, result.stereo, result.layout,
            result.projection, result.coverage, result.width, result.height, result.evidence,
            result.conflicts, (unsigned long long)result.bytes, result.packets, ok ? "PASS" : "FAIL");
        failed |= !ok;
    }
    close(fd);
    return failed;
}
