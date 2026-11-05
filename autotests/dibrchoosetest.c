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


/* the timing of the ladder: from the cheapest way up, each way at most once, stopping at the first that misses the budget; the dearer ways
 * never run and stay -1; a way that is already known is not run again; a way that would take over a second (eight times a cheaper
 * one's 125 ms) is not run */
struct fixture {
    const double *times;
    int calls[4];
    int order[8];
    int nb;
};

static int fixture_time(void *user, int rung, double *p95)
{
    struct fixture *f = user;
    f->calls[rung]++;
    f->order[f->nb++] = rung;
    *p95 = f->times[rung];
    return 0;
}

static int fixture_time_memory(void *user, int rung, double *p95)
{
    struct fixture *f = user;
    if (rung <= 1)
        return 1; /* the two dearest ways do not fit the memory */
    f->calls[rung]++;
    *p95 = f->times[rung];
    return 0;
}

static void ladder_timing(void)
{
    double p[4];
    int timed;
    /* the card at 24 fps (budget 33.3 ms): 4.5 fits, 6.2 fits, 15.9 fits, 77 misses: all four run, the cheapest first */
    static const double card_times[4] = { 77.0, 15.9, 6.2, 4.5 };
    struct fixture f = { card_times, { 0 }, { 0 }, 0 };
    memset(p, 0xff, sizeof(p));
    for (int i = 0; i < 4; i++)
        p[i] = -1.0;
    CHECK(stereo_dibr_time_ladder(24.0, fixture_time, &f, p, 4, &timed) == 0 && timed == 4);
    CHECK(f.order[0] == 3 && f.order[1] == 2 && f.order[2] == 1 && f.order[3] == 0 && f.nb == 4);
    CHECK(p[0] == 77.0 && p[3] == 4.5);
    /* at 60 fps (budget 13.3 ms): 4.5 fits, 6.2 fits, 15.9 misses: the best way never runs and stays untimed */
    struct fixture g = { card_times, { 0 }, { 0 }, 0 };
    for (int i = 0; i < 4; i++)
        p[i] = -1.0;
    CHECK(stereo_dibr_time_ladder(60.0, fixture_time, &g, p, 4, &timed) == 0 && timed == 3);
    CHECK(g.calls[0] == 0 && g.calls[1] == 1 && p[0] < 0.0 && p[1] == 15.9);
    /* a slow GPU: the cheapest misses, nothing dearer runs: one way timed */
    static const double slow_times[4] = { 2583.0, 686.0, 217.0, 130.0 };
    struct fixture s = { slow_times, { 0 }, { 0 }, 0 };
    for (int i = 0; i < 4; i++)
        p[i] = -1.0;
    CHECK(stereo_dibr_time_ladder(24.0, fixture_time, &s, p, 4, &timed) == 0 && timed == 1 && s.nb == 1 && s.order[0] == 3 && p[2] < 0.0);
    /* what is known is not run again, and a lower rate that has room goes on from where it stopped */
    for (int i = 0; i < 4; i++)
        p[i] = i == 3 ? 4.5 : i == 2 ? 6.2 : i == 1 ? 15.9 : -1.0;
    struct fixture k = { card_times, { 0 }, { 0 }, 0 };
    CHECK(stereo_dibr_time_ladder(24.0, fixture_time, &k, p, 4, &timed) == 0 && timed == 1 && k.calls[0] == 1 && k.calls[1] == 0 && k.calls[3] == 0);
    /* a way that would take over a second is not run: the cheaper one took 130 ms, which the budget of a 5 fps video (160 ms) allows */
    static const double heavy[4] = { 99999.0, 1040.0, 130.0, 100.0 };
    struct fixture h = { heavy, { 0 }, { 0 }, 0 };
    for (int i = 0; i < 4; i++)
        p[i] = -1.0;
    CHECK(stereo_dibr_time_ladder(5.0, fixture_time, &h, p, 4, &timed) == 0 && h.calls[0] == 0 && h.calls[1] == 0 && p[1] < 0.0);
    /* a way the GPU has not the memory for is not run (the callback says 1): -2, and nothing dearer runs */
    struct fixture m = { card_times, { 0 }, { 0 }, 0 };
    for (int i = 0; i < 4; i++)
        p[i] = -1.0;
    CHECK(stereo_dibr_time_ladder(24.0, fixture_time_memory, &m, p, 4, &timed) == 0 && p[3] == 4.5 && p[2] == 6.2 && p[1] == -2.0 && m.calls[0] == 0 && p[0] == -1.0);
    CHECK(timed == 2);
    /* the memory a way needs grows with the picture and with the way, in the order of the ladder's cost */
    struct stereo_dibr_way w[4];
    stereo_dibr_ways(w, 4);
    /* (the layered pyramid needs more than the layered depth-aided, though it costs less time: the order that matters is the pyramid's) */
    CHECK(stereo_dibr_way_memory(w[0].warp, w[0].fill, 1920, 1080, 8) > stereo_dibr_way_memory(w[3].warp, w[3].fill, 1920, 1080, 8));
    for (int i = 1; i < 3; i++)
        CHECK(stereo_dibr_way_memory(w[i].warp, w[i].fill, 1920, 1080, 8) > stereo_dibr_way_memory(w[i + 1].warp, w[i + 1].fill, 1920, 1080, 8));
    CHECK(stereo_dibr_way_memory(w[0].warp, w[0].fill, 1920, 1080, 8) > 150e6 && stereo_dibr_way_memory(w[0].warp, w[0].fill, 1920, 1080, 8) < 289e6 /* beside a desktop on a 512 MB carve-out */);
    CHECK(stereo_dibr_way_memory(w[0].warp, w[0].fill, 3840, 2160, 8) > 3.9 * stereo_dibr_way_memory(w[0].warp, w[0].fill, 1920, 1080, 8));
    /* the guard and the controls' sentence are one rule: the layered depth-aided way fits beside a two-screen desktop on a 512 MB carve-out (289 MB free),
     * the layered pyramid does not; a GPU that does not say (negative) holds nothing back; the driver's 4K takes far more */
    CHECK(stereo_dibr_way_fits(STEREO_DIBR_WARP_LAYERED, STEREO_DIBR_FILL_DEPTH_AIDED, 1920, 1080, 8, 289e6));
    CHECK(!stereo_dibr_way_fits(STEREO_DIBR_WARP_LAYERED, STEREO_DIBR_FILL_PYRAMID, 1920, 1080, 8, 289e6));
    CHECK(stereo_dibr_way_fits(STEREO_DIBR_WARP_LAYERED, STEREO_DIBR_FILL_PYRAMID, 1920, 1080, 8, -1.0));
    CHECK(stereo_dibr_way_fits(STEREO_DIBR_WARP_FORWARD, STEREO_DIBR_FILL_BACKGROUND, 1920, 1080, 8, 289e6));
    CHECK(!stereo_dibr_way_fits(STEREO_DIBR_WARP_FORWARD, STEREO_DIBR_FILL_BACKGROUND, 3840, 2160, 8, 289e6));
    /* 16 bit content holds float colour: a way needs more of it */
    CHECK(stereo_dibr_way_memory(STEREO_DIBR_WARP_LAYERED, STEREO_DIBR_FILL_DEPTH_AIDED, 1920, 1080, 16) > stereo_dibr_way_memory(STEREO_DIBR_WARP_LAYERED, STEREO_DIBR_FILL_DEPTH_AIDED, 1920, 1080, 8));
    /* a failing timer ends it, a bad argument is refused */
    CHECK(stereo_dibr_time_ladder(24.0, NULL, &f, p, 4, &timed) == -1);
    CHECK(stereo_dibr_time_ladder(0.0, fixture_time, &f, p, 4, &timed) == -1);
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
    /* the cache is the person's: directory 0700, file 0600 */
    snprintf(cmd, sizeof(cmd), "test \"$(stat -c %%a %s/stereo-dibr/timings)\" = 600", dir);
    CHECK(system(cmd) == 0);
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
    CHECK(stereo_dibr_measure_cpu(96, 54, 24.0, first, 4, &measured) == 0 && measured == 1);
    for (int i = 0; i < 4; i++)
        CHECK(first[i] > 0.0 && first[i] < 5000.0);
    /* the second time is the cache's: the same numbers, nothing timed */
    CHECK(stereo_dibr_measure_cpu(96, 54, 24.0, second, 4, &measured) == 0 && measured == 0);
    for (int i = 0; i < 4; i++)
        CHECK(fabs(first[i] - second[i]) < 0.001);
    /* another size is timed */
    CHECK(stereo_dibr_measure_cpu(64, 36, 24.0, second, 4, &measured) == 0 && measured == 1);
    CHECK(stereo_dibr_measure_cpu(0, 36, 24.0, second, 4, &measured) == -1);
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
    ladder_timing();
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
