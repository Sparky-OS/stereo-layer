/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _POSIX_C_SOURCE 200809L
#include "stereo-content.h"
#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <time.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    assert(argc == 2);
    int fd = open(argv[1], O_RDONLY | O_CLOEXEC);
    assert(fd >= 0);
    for (unsigned track = 1; track <= 2; ++track) {
        struct timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        StereoContentRequest request = {.size = sizeof(request), .abi = STEREO_CONTENT_ABI,
            .fd = fd, .deadline_us = (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000 + 1000000,
            .max_bytes = 4 * 1024 * 1024, .max_packets = 128, .video_track_plus_one = track};
        StereoContentResult result = {.size = sizeof(result)};
        assert(stereo_content_inspect(&request, &result) == 0);
        assert(result.status == STEREO_CONTENT_OK);
        assert(result.stream_index == (int)track - 1);
        assert(result.stereo == (track == 1 ? -1 : 1));
        assert(result.layout == (track == 1 ? 0 : 1));
        assert(result.width == (track == 1 ? 960 : 320));
        assert(result.height == (track == 1 ? 804 : 180));
        assert(result.sar_num == 1 && result.sar_den == 1);
        assert(lseek(fd, 0, SEEK_CUR) == 0);
    }
    close(fd);
    puts("selected mono and stereo tracks, native geometry, preserved FD offset: passed");
}
