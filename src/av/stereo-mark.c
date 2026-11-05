/* SPDX-License-Identifier: LGPL-2.1-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/* The declarations of stereo-mark.h, before the writers: every call says the work is not done. */

#include "stereo-mark.h"

#include <stddef.h>
#include <string.h>

int stereo_mark_matroska_value(int layout, int order, int anaglyph) { (void)layout; (void)order; (void)anaglyph; return -1; }
int stereo_mark_mp4_value(int layout, int order, int anaglyph) { (void)layout; (void)order; (void)anaglyph; return -1; }
int stereo_mark_webm_allows(int value) { (void)value; return 0; }
int stereo_mark_value_layout(int container, int value, int *layout, int *order, int *anaglyph) { (void)container; (void)value; (void)layout; (void)order; (void)anaglyph; return 0; }
int stereo_mark_read(const char *path, int video_track, int *container, int *value) { (void)path; (void)video_track; (void)container; (void)value; return STEREO_MARK_BAD_ARGUMENT; }
int stereo_mark_save(const char *path, int video_track, int layout, int order, int anaglyph, stereo_mark_result *result)
{
    (void)path; (void)video_track; (void)layout; (void)order; (void)anaglyph;
    if (result)
        memset(result, 0, sizeof(*result));
    return STEREO_MARK_BAD_ARGUMENT;
}
const char *stereo_mark_status_name(int status) { (void)status; return "not-implemented"; }
