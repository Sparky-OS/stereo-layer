/* SPDX-License-Identifier: GPL-2.0-or-later */
/* SPDX-FileCopyrightText: 2026 Daniel Campos Ramos <Capitain_Jack@yahoo.com> */

/*
 * The way a video is drawn with: the ladder of ways, the choice of the first that keeps up with the frame rate on a fixture of
 * times (a GPU's budget), the person's cache of timings, the person's own way against the automatic one in the options' text,
 * and the one default of the library.
 */

#define _GNU_SOURCE
#include "stereo-dibr.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int failures;
#define CHECK(cond)                                                                                                                                  \
    do {                                                                                                                                             \
        if (!(cond)) {                                                                                                                               \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                                                   \
            failures++;                                                                                                                              \
        }                                                                                                                                            \
    } while (0)

static void ladder_and_default(void)
{
    struct stereo_dibr_way ways[8];
    const int n = stereo_dibr_ways(ways, 8);
    CHECK(n == 4);
    struct stereo_dibr_options o;
    struct stereo_dibr_params p, q;
    stereo_dibr_options_default(&o);
    stereo_dibr_options_apply(&o, NULL, &q);
    stereo_dibr_params_default(&p);
    /* the first of the ladder is the best way, which is the one default of the library */
    CHECK(ways[0].warp == o.warp && ways[0].fill == o.fill);
    CHECK(p.view == q.view && p.warp == q.warp && p.fill == q.fill && p.smooth == q.smooth && p.radius == q.radius && p.sigma_h == q.sigma_h
        && p.sigma_v == q.sigma_v);
    CHECK(o.automatic == 1 && stereo_dibr_options_is_default(&o));
    /* every way of the ladder is a way the options can hold; none twice; Telea is not a way a video waits for */
    for (int i = 0; i < n; i++) {
        CHECK(ways[i].fill != STEREO_DIBR_FILL_TELEA && ways[i].fill != STEREO_DIBR_FILL_NONE);
        for (int j = i + 1; j < n; j++)
            CHECK(ways[i].warp != ways[j].warp || ways[i].fill != ways[j].fill);
    }
}

/* the GPU's budget as times of the ladder: layered depth-aided, layered pyramid, forward pyramid, forward background (ms a frame,
 * 95th percentile) */
static const double fast[4] = { 10.0, 4.0, 3.0, 2.0 };
static const double card[4] = { 59.5, 14.7, 7.6, 5.1 }; /* the RTX 3060 at 1080p, measured: the best does not fit a frame */
static const double igpu[4] = { 437.0, 100.0, 70.0, 30.2 };
static const double slow[4] = { 2583.0, 1500.0, 1200.0, 146.4 };

static void choice(void)
{
    struct stereo_dibr_choice c;
    struct stereo_dibr_way ways[4];
    stereo_dibr_ways(ways, 4);

    /* a GPU that keeps up with everything: the best way, nothing skipped */
    CHECK(stereo_dibr_choose_from_times(fast, 4, 60.0, &c) == 0);
    CHECK(c.rank == 0 && c.keeps_up && c.warp == ways[0].warp && c.fill == ways[0].fill && c.skipped_warp == -1 && c.skipped_fill == -1);
    CHECK(fabs(c.budget_ms - 13.333) < 0.01); /* four fifths of 16.7 ms */

    /* the card: 24 and 30 fps leave 33.3 and 26.7 ms: the layered pyramid (14.7) fits, the best (59.5) does not */
    for (int k = 0; k < 2; k++) {
        const double fps[2] = { 24, 30 };
        CHECK(stereo_dibr_choose_from_times(card, 4, fps[k], &c) == 0);
        CHECK(c.rank == 1 && c.keeps_up && c.fill == STEREO_DIBR_FILL_PYRAMID && c.warp == STEREO_DIBR_WARP_LAYERED);
        CHECK(c.skipped_warp == ways[0].warp && c.skipped_fill == ways[0].fill && c.skipped_ms == 59.5);
    }
    /* 60 fps leaves 13.3 ms: the layered pyramid (14.7) does not fit, the forward pyramid (7.6) does */
    CHECK(stereo_dibr_choose_from_times(card, 4, 60.0, &c) == 0);
    CHECK(c.rank == 2 && c.keeps_up && c.fill == STEREO_DIBR_FILL_PYRAMID && c.warp == STEREO_DIBR_WARP_FORWARD && c.ms == 7.6);
    /* 120 fps leaves 6.67 ms: only the forward background (5.1) fits */
    CHECK(stereo_dibr_choose_from_times(card, 4, 120.0, &c) == 0);
    CHECK(c.rank == 3 && c.keeps_up && c.fill == STEREO_DIBR_FILL_BACKGROUND && c.warp == STEREO_DIBR_WARP_FORWARD);
    /* 240 fps leaves 3.3 ms: nothing fits, the quickest is chosen and it says it does not keep up */
    CHECK(stereo_dibr_choose_from_times(card, 4, 240.0, &c) == 0);
    CHECK(!c.keeps_up && c.rank == 3 && c.ms == 5.1);

    /* the integrated GPU: 24 fps leaves 33.3 ms: only the forward background (30.2) fits; 30 fps leaves 26.7: nothing does, and
     * the quickest is chosen, said */
    CHECK(stereo_dibr_choose_from_times(igpu, 4, 24.0, &c) == 0);
    CHECK(c.keeps_up && c.rank == 3 && c.ms == 30.2);
    CHECK(stereo_dibr_choose_from_times(igpu, 4, 30.0, &c) == 0);
    CHECK(!c.keeps_up && c.rank == 3 && c.fill == STEREO_DIBR_FILL_BACKGROUND && c.warp == STEREO_DIBR_WARP_FORWARD && c.ms == 30.2);
    CHECK(stereo_dibr_choose_from_times(igpu, 4, 15.0, &c) == 0); /* 53.3 ms: the forward pyramid (70) is over */
    CHECK(c.keeps_up && c.rank == 3);
    CHECK(stereo_dibr_choose_from_times(igpu, 4, 12.0, &c) == 0); /* 66.7 ms: the forward pyramid is still over, 70 */
    CHECK(c.keeps_up && c.rank == 3);
    CHECK(stereo_dibr_choose_from_times(igpu, 4, 10.0, &c) == 0); /* 80 ms: the forward pyramid fits */
    CHECK(c.keeps_up && c.rank == 2);

    /* the slow GPU: nothing keeps up, the quickest is the forward background and the choice says it does not keep up */
    CHECK(stereo_dibr_choose_from_times(slow, 4, 24.0, &c) == 0);
    CHECK(!c.keeps_up && c.rank == 3 && c.ms == 146.4 && c.skipped_ms == 2583.0);

    /* ways that were not timed (negative) never keep up, and are not the quickest */
    const double part[4] = { -1.0, -1.0, 12.4, 4.2 };
    CHECK(stereo_dibr_choose_from_times(part, 4, 30.0, &c) == 0);
    CHECK(c.rank == 2 && c.keeps_up);
    const double none[4] = { -1, -1, -1, -1 };
    CHECK(stereo_dibr_choose_from_times(none, 4, 30.0, &c) == -1);

    /* bad arguments */
    CHECK(stereo_dibr_choose_from_times(NULL, 4, 30.0, &c) == -1);
    CHECK(stereo_dibr_choose_from_times(card, 5, 30.0, &c) == -1);
    CHECK(stereo_dibr_choose_from_times(card, 4, 0.0, &c) == -1);
    CHECK(stereo_dibr_choose_from_times(card, 4, NAN, &c) == -1);
    CHECK(stereo_dibr_choose_from_times(card, 4, 30.0, NULL) == -1);
}

static void cache(void)
{
    char dir[] = "/tmp/dibrchoosetest-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    setenv("XDG_CACHE_HOME", dir, 1);
    double got[4];
    CHECK(!stereo_dibr_cache_load("gl A", 1920, 1080, got, 4));
    CHECK(stereo_dibr_cache_store("gl A", 1920, 1080, card, 4) == 0);
    CHECK(stereo_dibr_cache_load("gl A", 1920, 1080, got, 4));
    for (int i = 0; i < 4; i++)
        CHECK(fabs(got[i] - card[i]) < 0.001);
    /* per GPU and per size */
    CHECK(!stereo_dibr_cache_load("gl B", 1920, 1080, got, 4));
    CHECK(!stereo_dibr_cache_load("gl A", 1280, 720, got, 4));
    CHECK(stereo_dibr_cache_store("gl B", 1920, 1080, slow, 4) == 0);
    CHECK(stereo_dibr_cache_store("gl A", 1280, 720, fast, 4) == 0);
    CHECK(stereo_dibr_cache_load("gl A", 1920, 1080, got, 4) && fabs(got[0] - 59.5) < 0.001);
    CHECK(stereo_dibr_cache_load("gl B", 1920, 1080, got, 4) && fabs(got[0] - 2583.0) < 0.001);
    CHECK(stereo_dibr_cache_load("gl A", 1280, 720, got, 4) && fabs(got[0] - 10.0) < 0.001);
    /* a new measurement replaces the line, it does not add one */
    CHECK(stereo_dibr_cache_store("gl A", 1920, 1080, igpu, 4) == 0);
    CHECK(stereo_dibr_cache_load("gl A", 1920, 1080, got, 4) && fabs(got[0] - 437.0) < 0.001);
    char path[512], cmd[640];
    snprintf(path, sizeof(path), "%s/stereo-dibr/timings", dir);
    snprintf(cmd, sizeof(cmd), "test \"$(grep -c '^gl A' %s)\" = 2", path);
    CHECK(system(cmd) == 0);
    /* a file that is not ours, or damaged, is a miss, not a crash; a store then replaces it */
    FILE *f = fopen(path, "w");
    fputs("not a cache\n\x01\x02", f);
    fclose(f);
    CHECK(!stereo_dibr_cache_load("gl A", 1920, 1080, got, 4));
    CHECK(stereo_dibr_cache_store("gl A", 1920, 1080, card, 4) == 0 && stereo_dibr_cache_load("gl A", 1920, 1080, got, 4));
    /* a key with a tab or a bad count is refused */
    CHECK(stereo_dibr_cache_store("gl\tA", 1920, 1080, card, 4) == -1);
    CHECK(stereo_dibr_cache_store("gl A", 1920, 1080, card, 5) == -1);
    /* the cache is the person's: directory 0700 */
    snprintf(cmd, sizeof(cmd), "test \"$(stat -c %%a %s/stereo-dibr)\" = 700", dir);
    CHECK(system(cmd) == 0);
    /* unwritable: said by the return value */
    snprintf(cmd, sizeof(cmd), "chmod 500 %s/stereo-dibr", dir);
    CHECK(system(cmd) == 0);
    if (geteuid())
        CHECK(stereo_dibr_cache_store("gl C", 1920, 1080, card, 4) == -1);
    snprintf(cmd, sizeof(cmd), "chmod 700 %s/stereo-dibr; rm -rf %s", dir, dir);
    CHECK(system(cmd) == 0);
}

static void cpu(void)
{
    char dir[] = "/tmp/dibrchoosetest-XXXXXX";
    CHECK(mkdtemp(dir) != NULL);
    setenv("XDG_CACHE_HOME", dir, 1);
    double first[4], second[4];
    int measured = 0;
    CHECK(stereo_dibr_measure_cpu(96, 54, first, 4, &measured) == 0 && measured == 1);
    for (int i = 0; i < 4; i++)
        CHECK(first[i] > 0.0 && first[i] < 5000.0);
    /* the second time is the cache's: the same numbers, nothing timed */
    CHECK(stereo_dibr_measure_cpu(96, 54, second, 4, &measured) == 0 && measured == 0);
    for (int i = 0; i < 4; i++)
        CHECK(fabs(first[i] - second[i]) < 0.001);
    /* another size is timed */
    CHECK(stereo_dibr_measure_cpu(64, 36, second, 4, &measured) == 0 && measured == 1);
    CHECK(stereo_dibr_measure_cpu(0, 36, second, 4, &measured) == -1);
    char cmd[640];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", dir);
    CHECK(system(cmd) == 0);
}

static void options_text(void)
{
    struct stereo_dibr_options o, r, def;
    char buf[512];
    stereo_dibr_options_default(&def);
    /* the default is automatic; the text says so, and reads back */
    CHECK(stereo_dibr_options_write(&def, buf, sizeof(buf)) > 0 && strstr(buf, "way=auto") && strstr(buf, "warp=layered") && strstr(buf, "fill=depthAided"));
    CHECK(stereo_dibr_options_read(buf, &r) == 1 && stereo_dibr_options_is_default(&r) && r.automatic == 1);
    /* the person's own way: the text says so, and reads back as theirs, and is not the default */
    o = def;
    o.automatic = 0;
    CHECK(!stereo_dibr_options_is_default(&o));
    CHECK(stereo_dibr_options_write(&o, buf, sizeof(buf)) > 0 && strstr(buf, "way=own"));
    CHECK(stereo_dibr_options_read(buf, &r) == 1 && r.automatic == 0 && r.warp == def.warp && r.fill == def.fill);
    /* the pyramid is a word of the options */
    o.fill = STEREO_DIBR_FILL_PYRAMID;
    CHECK(stereo_dibr_options_write(&o, buf, sizeof(buf)) > 0 && strstr(buf, "fill=pyramid"));
    CHECK(stereo_dibr_options_read(buf, &r) == 1 && r.fill == STEREO_DIBR_FILL_PYRAMID && r.automatic == 0);
    /* a text of before, with a way that is not the default's and no word for who chose it, is the person's */
    CHECK(stereo_dibr_options_read("warp=forward fill=background strength=40", &r) == 1 && r.automatic == 0 && r.warp == STEREO_DIBR_WARP_FORWARD);
    /* a text with other things only keeps the way automatic */
    CHECK(stereo_dibr_options_read("strength=40 zero=0.4", &r) == 1 && r.automatic == 1 && r.strength == 40.0);
    CHECK(stereo_dibr_options_read("way=auto warp=forward", &r) == 1 && r.automatic == 1);
    CHECK(stereo_dibr_options_read("way=sometimes", &r) == -1);
    /* the limits */
    o = def;
    o.automatic = 7;
    CHECK(stereo_dibr_options_clamp(&o) == 1 && o.automatic == 1);
    o = def;
    o.fill = 9;
    CHECK(stereo_dibr_options_clamp(&o) == 1 && o.fill == STEREO_DIBR_FILL_BACKGROUND);
}

int main(void)
{
    ladder_and_default();
    choice();
    cache();
    cpu();
    options_text();
    if (failures) {
        printf("%d failures\n", failures);
        return 1;
    }
    printf("dibrchoosetest: ok\n");
    return 0;
}
