/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */
#include "stereo-jpeg.h"
#include <string.h>

struct jpeg_scan {
    size_t end, insertion, jps_begin, jps_payload, jps_end, mpf_base, mpf_size;
    unsigned width, height, jps_count, mpf_count;
    int lossless;
};
static unsigned big16(const unsigned char *p) { return (unsigned)p[0] * 256 + p[1]; }
/* ITU-T T.81 Annex B: length excludes marker, FF00 is stuffed scan data,
 * restart markers stay in scans, opaque segment payloads are never searched. */
static int scan_jpeg(const unsigned char *p, size_t n, size_t begin, struct jpeg_scan *out)
{
    if (!p || begin > n || n - begin < 2 || p[begin] != 255 || p[begin + 1] != 0xd8)
        return 0;
    memset(out, 0, sizeof(*out));
    out->insertion = begin + 2;
    size_t pos = begin + 2;
    int entropy = 0, frame = 0, scans = 0, initial = 1;
    while (pos < n) {
        if (entropy) {
            if (p[pos++] != 255)
                continue;
            size_t marker_start = pos - 1;
            while (pos < n && p[pos] == 255)
                ++pos;
            if (pos == n)
                return 0;
            if (!p[pos] || (p[pos] >= 0xd0 && p[pos] <= 0xd7)) {
                ++pos;
                continue;
            }
            pos = marker_start;
            entropy = 0;
        }
        if (p[pos++] != 255)
            return 0;
        while (pos < n && p[pos] == 255)
            ++pos;
        if (pos == n)
            return 0;
        size_t marker_begin = pos - 1;
        unsigned marker = p[pos++];
        if (marker == 0xd9) {
            out->end = pos;
            return frame && scans;
        }
        if (!marker || marker == 0xd8 || (marker >= 0xd0 && marker <= 0xd7))
            return 0;
        if (marker == 1) {
            initial = 0;
            continue;
        }
        if (n - pos < 2)
            return 0;
        unsigned length = big16(p + pos);
        if (length < 2 || length > n - pos)
            return 0;
        size_t payload = pos + 2, end = pos + length;
        if (initial && (marker == 0xe0 || marker == 0xe1))
            out->insertion = end;
        else
            initial = 0;
        if (marker >= 0xc0 && marker <= 0xcf && marker != 0xc4 && marker != 0xc8 && marker != 0xcc) {
            if (frame || length < 11 || !p[payload + 5] || length != 8u + 3u * p[payload + 5])
                return 0;
            out->width = big16(p + payload + 3);
            out->height = big16(p + payload + 1);
            if (!out->width || !out->height)
                return 0;
            out->lossless = marker == 0xc3 || marker == 0xc7 || marker == 0xcb || marker == 0xcf;
            frame = 1;
        }
        if (marker == 0xda) {
            if (!frame || length < 8 || length != 6u + 2u * p[payload])
                return 0;
            scans = entropy = 1;
        }
        if (marker == 0xe3 && length >= 10 && !memcmp(p + payload, "_JPSJPS_", 8)) {
            ++out->jps_count;
            out->jps_begin = marker_begin;
            out->jps_payload = payload;
            out->jps_end = end;
        }
        if (marker == 0xe2 && length >= 6 && !memcmp(p + payload, "MPF\0", 4)) {
            ++out->mpf_count;
            out->mpf_base = payload + 4;
            out->mpf_size = end - out->mpf_base;
        }
        pos = end;
    }
    return 0;
}
struct mpf_view {
    const unsigned char *tiff;
    size_t size, base, ordinal, reference;
};
static uint32_t val(const struct mpf_view *v, size_t offset) { return stereo_mpf_u32(v->tiff, v->size, offset); }
static size_t tag(const struct mpf_view *v, uint32_t ifd, uint16_t id, uint16_t type, uint32_t count)
{
    return stereo_mpf_field(v->tiff, v->size, ifd, id, type, count);
}
static size_t attribute(const struct mpf_view *v, uint16_t id)
{
    size_t found = 0;
    for (uint32_t ifd = val(v, 4); ifd; ifd = stereo_mpf_next(v->tiff, v->size, ifd)) {
        size_t field = tag(v, ifd, id, 4, 1);
        if (field) {
            if (found)
                return 0;
            found = field;
        }
    }
    return found;
}
static int mpf_view(const unsigned char *data, const struct jpeg_scan *j, struct mpf_view *v)
{
    if (j->mpf_count != 1)
        return 0;
    v->base = j->mpf_base;
    v->tiff = data + v->base;
    v->size = j->mpf_size;
    if (!stereo_mpf_valid(v->tiff, v->size))
        return 0;
    v->ordinal = attribute(v, 0xb101);
    v->reference = attribute(v, 0xb204);
    return v->ordinal && v->reference;
}
static int mpo_pair(
    const unsigned char *p, size_t n, const struct jpeg_scan *first, struct mpf_view views[2], size_t *second_start, size_t *second_size)
{
    struct mpf_view *a = views, *b = views + 1;
    if (!mpf_view(p, first, a))
        return STEREO_JPEG_MPO;
    uint32_t ifd = val(a, 4);
    size_t version = tag(a, ifd, 0xb000, 7, 4), number = tag(a, ifd, 0xb001, 4, 1), table = tag(a, ifd, 0xb002, 7, 32);
    if (!version || memcmp(a->tiff + version, "0100", 4) || !number || val(a, number) != 2 || !table)
        return STEREO_JPEG_MPO;
    size_t entries = val(a, table);
    if (entries > a->size || a->size - entries < 32)
        return STEREO_JPEG_MPO;
    size_t start = val(a, entries + 24), size = val(a, entries + 20);
    if ((val(a, entries) & 0x07ffffffu) != 0x020002 || (val(a, entries + 16) & 0x07ffffffu) != 0x020002 || val(a, entries + 8)
        || val(a, entries + 4) != first->end || start > n - a->base)
        return STEREO_JPEG_MPO;
    start += a->base;
    if (start < first->end || size != n - start)
        return STEREO_JPEG_MPO;
    struct jpeg_scan second;
    if (!scan_jpeg(p, n, start, &second) || second.end != n || !mpf_view(p, &second, b) || tag(b, val(b, 4), 0xb001, 4, 1))
        return STEREO_JPEG_MPO;
    if (second.lossless)
        return STEREO_JPEG_LOSSLESS;
    unsigned av = val(a, a->ordinal), bv = val(b, b->ordinal), ref = val(a, a->reference);
    if (!((av == 1 && bv == 2) || (av == 2 && bv == 1)) || (ref != 1 && ref != 2) || val(b, b->reference) != ref)
        return STEREO_JPEG_MPO;
    *second_start = start;
    *second_size = size;
    return STEREO_JPEG_OK;
}
int stereo_jpeg_inspect(const void *data, size_t size, struct stereo_jpeg_info *out)
{
    const unsigned char *p = data;
    struct jpeg_scan j;
    if (!out || !scan_jpeg(p, size, 0, &j))
        return STEREO_JPEG_INVALID;
    struct stereo_jpeg_info info = { 0 };
    info.width = j.width;
    info.height = j.height;
    info.lossless = j.lossless;
    info.layout = -1;
    info.image_count = 1;
    info.image_size[0] = j.end;
    if (j.mpf_count) {
        struct mpf_view views[2];
        int result = mpo_pair(p, size, &j, views, &info.image_offset[1], &info.image_size[1]);
        if (result)
            return result;
        info.layout = 5;
        info.right_first = val(views, views[0].ordinal) == 2;
        info.image_count = 2;
    } else {
        /* Keep opaque trailer bytes as the existing sView writer did. Another
         * JPEG needs an MPF index; never invent one or shift indexed pictures. */
        if (size - j.end >= 2 && p[j.end] == 255 && p[j.end + 1] == 0xd8)
            return STEREO_JPEG_TRAILING;
        if (j.jps_count > 1)
            return STEREO_JPEG_DESCRIPTOR;
        if (j.jps_count) {
            struct stereo_jps_descriptor d;
            if (!stereo_jps_decode(p + j.jps_payload, j.jps_end - j.jps_payload, &d))
                return STEREO_JPEG_DESCRIPTOR;
            info.descriptor = d.value;
            info.layout = (d.value & 255u) ? (int)((d.value >> 8) & 255u) : 0;
            info.right_first = !!(d.value & 255u) && !(d.value & 0x40000u);
        }
    }
    *out = info;
    return STEREO_JPEG_OK;
}
int stereo_jpeg_correct(const void *data, size_t size, int layout, int right_first, void *output, size_t capacity, size_t *output_size)
{
    const unsigned char *p = data;
    unsigned char *out = output;
    struct stereo_jpeg_info info;
    int result = stereo_jpeg_inspect(data, size, &info);
    if (result)
        return result;
    if (info.lossless)
        return STEREO_JPEG_LOSSLESS;
    if (layout < 0 || layout > 5 || (right_first != 0 && right_first != 1))
        return STEREO_JPEG_LAYOUT;
    struct jpeg_scan j;
    if (!scan_jpeg(p, size, 0, &j))
        return STEREO_JPEG_INVALID;
    if (info.layout == 5) {
        if (layout != 5)
            return STEREO_JPEG_LAYOUT;
        struct mpf_view v[2];
        size_t start, length;
        result = mpo_pair(p, size, &j, v, &start, &length);
        if (result)
            return result;
        if (!out || !output_size || capacity < size)
            return STEREO_JPEG_CAPACITY;
        memcpy(out, p, size);
        for (unsigned i = 0; i < 2; ++i) {
            uint32_t wanted = i == 0 ? (right_first ? 2 : 1) : (right_first ? 1 : 2);
            stereo_mpf_set_u32(out + v[i].base, v[i].size, v[i].ordinal, wanted);
            if (info.right_first != right_first)
                stereo_mpf_set_u32(out + v[i].base, v[i].size, v[i].reference, 3 - val(v + i, v[i].reference));
        }
        *output_size = size;
        return STEREO_JPEG_OK;
    }
    if (layout == 5)
        return STEREO_JPEG_LAYOUT;
    struct stereo_jps_descriptor descriptor = { 0 };
    if (j.jps_count && !stereo_jps_decode(p + j.jps_payload, j.jps_end - j.jps_payload, &descriptor))
        return STEREO_JPEG_DESCRIPTOR;
    uint32_t previous = descriptor.value;
    uint32_t half = previous & 0x30000u, gap = (layout == 2 || layout == 3) ? previous & 0xff000000u : 0;
    uint32_t display = !layout && !(previous & 255u) ? previous & 0xff00u : 0;
    descriptor.value = half | gap | display | (layout ? 1u | ((unsigned)layout << 8) | (!right_first ? 0x40000u : 0) : 0);
    unsigned separation = descriptor.value >> 24;
    if ((layout == 2 && (j.width < separation + 2 || (j.width - separation) % 2))
        || (layout == 3 && (j.height < separation + 2 || (j.height - separation) % 2)) || (layout == 1 && j.height % 2))
        return STEREO_JPEG_GEOMETRY;
    /* A fixed-size descriptor-word edit preserves even offsets in opaque trailing
     * data. Keep legacy padding, extension and comment bytes exactly as stored. */
    if (j.jps_count) {
        if (!out || !output_size || capacity < size)
            return STEREO_JPEG_CAPACITY;
        memcpy(out, p, size);
        out[j.jps_payload + 10] = (unsigned char)(descriptor.value >> 24);
        out[j.jps_payload + 11] = (unsigned char)(descriptor.value >> 16);
        out[j.jps_payload + 12] = (unsigned char)(descriptor.value >> 8);
        out[j.jps_payload + 13] = (unsigned char)descriptor.value;
        *output_size = size;
        return STEREO_JPEG_OK;
    }
    unsigned char payload[65533];
    size_t length = stereo_jps_encode(&descriptor, payload, sizeof(payload));
    if (!length)
        return STEREO_JPEG_DESCRIPTOR;
    size_t begin = j.jps_count ? j.jps_begin : j.insertion, end = j.jps_count ? j.jps_end : begin;
    if (length + 4 > SIZE_MAX - (size - (end - begin)))
        return STEREO_JPEG_CAPACITY;
    size_t wanted = size - (end - begin) + length + 4;
    if (!out || !output_size || capacity < wanted)
        return STEREO_JPEG_CAPACITY;
    memcpy(out, p, begin);
    out[begin] = 255;
    out[begin + 1] = 0xe3;
    out[begin + 2] = (unsigned char)((length + 2) >> 8);
    out[begin + 3] = (unsigned char)(length + 2);
    memcpy(out + begin + 4, payload, length);
    memcpy(out + begin + 4 + length, p + end, size - end);
    *output_size = wanted;
    return STEREO_JPEG_OK;
}
