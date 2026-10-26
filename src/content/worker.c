/* SPDX-License-Identifier: LGPL-2.1-or-later */
#define _POSIX_C_SOURCE 200809L
#include "stereo-content.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/prctl.h>
#include <signal.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    if (argc != 3 || strlen(argv[1]) >= 128)
        return 2;
    struct rlimit memory = {1024UL * 1024 * 1024, 1024UL * 1024 * 1024};
    struct rlimit cpu = {1, 1};
    setrlimit(RLIMIT_AS, &memory);
    setrlimit(RLIMIT_CPU, &cpu);
    prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0);
    prctl(PR_SET_PDEATHSIG, SIGKILL);
    StereoContentRequest request = {.size = sizeof(request), .abi = STEREO_CONTENT_ABI,
        .fd = 3, .deadline_us = strtoll(argv[2], NULL, 10), .max_bytes = 4 * 1024 * 1024, .max_packets = 128};
    strcpy(request.base_mime, argv[1]);
    StereoContentResult result = {.size = sizeof(result)};
    if (stereo_content_inspect(&request, &result) < 0)
        return 1;
    return fwrite(&result, sizeof(result), 1, stdout) == 1 ? 0 : 1;
}
