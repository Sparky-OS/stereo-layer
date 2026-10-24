/* SPDX-License-Identifier: LGPL-2.1-or-later */
#include "client.h"
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    if (argc != 5)
        return 2;
    int fd = open(argv[1], O_RDONLY | O_NONBLOCK);
    char mime[128];
    int result = stereo_content_refine_fd(fd, argv[2], 400, mime, sizeof(mime));
    close(fd);
    printf("%d %s\n", result, mime);
    return strcmp(mime, argv[3]) || result != (argv[4][0] == '1');
}
