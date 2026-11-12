/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "stereo-jpeg.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned failures;
static void check(int ok, const char *name)
{
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    failures += !ok;
}
static unsigned char *read_file(const char *name, const char *suffix, size_t *size)
{
    char path[4096];
    snprintf(path, sizeof(path), "%s/%s%s", CORRECTION_DATA, name, suffix);
    FILE *f = fopen(path, "rb");
    if (!f || fseek(f, 0, SEEK_END))
        exit(2);
    long length = ftell(f);
    if (length < 0 || fseek(f, 0, SEEK_SET))
        exit(2);
    *size = (size_t)length;
    unsigned char *p = malloc(*size);
    if (!p || fread(p, 1, *size, f) != *size || fclose(f))
        exit(2);
    return p;
}
int main(void)
{
    static const char *names[] = { "pair.jpg", "progressive.jpg", "fields.jps", "pair.mpo", "big.mpo", "tail.jps", "tail.jpg" };
    for (unsigned i = 0; i < sizeof(names) / sizeof(*names); ++i) {
        size_t size, expected_size;
        unsigned char *input = read_file(names[i], "", &size), *expected = read_file(names[i], ".expected", &expected_size);
        size_t capacity = size + 65537, written = 0;
        unsigned char *output = malloc(capacity), *again = malloc(capacity);
        if (!output || !again)
            exit(2);
        memset(output, 0x5a, capacity);
        int layout = (i == 3 || i == 4) ? 5 : 2;
        check(stereo_jpeg_correct(input, size, layout, 1, output, 1, &written) == STEREO_JPEG_CAPACITY && output[0] == 0x5a,
            "capacity refusal untouched");
        check(stereo_jpeg_correct(input, size, layout, 1, output, capacity, &written) == STEREO_JPEG_OK, names[i]);
        check(written == expected_size && !memcmp(output, expected, written), "independent complete-byte oracle");
        struct stereo_jpeg_info info;
        check(
            stereo_jpeg_inspect(output, written, &info) == STEREO_JPEG_OK && info.layout == layout && info.right_first == 1, "inspect layout/order");
        size_t repeated = 0;
        check(stereo_jpeg_correct(output, written, layout, 1, again, capacity, &repeated) == STEREO_JPEG_OK && repeated == written
                && !memcmp(again, output, written),
            "absolute correction idempotent");
        input[0] = 0;
        memset(output, 0x5a, capacity);
        check(stereo_jpeg_correct(input, size, layout, 1, output, capacity, &written) == STEREO_JPEG_INVALID && output[0] == 0x5a,
            "invalid JPEG untouched");
        free(input);
        free(expected);
        free(output);
        free(again);
    }
    return failures ? 1 : 0;
}
