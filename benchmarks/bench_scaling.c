

/*
 * Scaling benchmark: how does cost grow with input size?
 *
 * A single latency number tells you the cost today. A scaling curve
 * tells you what happens when a user's input is 100x bigger than
 * anything you tested. That is where libraries actually fail.
 */
/* macOS: CLOCK_MONOTONIC 的分辨率是 1 us，比本文件里任何一个操作都粗。
 * 而且在 Apple 上定义 _POSIX_C_SOURCE 会把 __DARWIN_C_LEVEL 降到
 * POSIX-only，高分辨率的那几个 clock id 会被头文件藏起来。 */
#ifndef __APPLE__
#define _POSIX_C_SOURCE 199309L
#endif

#include "sound_seg/sound_seg.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static double now_ns(void)
{
#ifdef __APPLE__
    return (double)clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1e9 + (double)ts.tv_nsec;
#endif
}

static int cmp(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

static double median_of(double *v, size_t n)
{
    qsort(v, n, sizeof(double), cmp);
    return v[n / 2];
}

/* How long does one read cost when the track is split into `nodes`
 * pieces? tr_find walks the linked list from the head, so we expect
 * cost to grow with the number of nodes, not with sample count. */
static double read_cost(size_t nodes, size_t reps)
{
    ss_ctx *ctx;
    ss_track *t;
    ss_ctx_create(&ctx);
    ss_track_create(ctx, &t);

    size_t chunk = 64;
    int16_t buf[64] = {0};

    /* Each write past the end appends a new node. */
    for (size_t i = 0; i < nodes; i++) {
        ss_track_write(t, i * chunk, chunk, buf);
    }

    size_t total = nodes * chunk;
    double *s = malloc(reps * sizeof(double));
    for (size_t i = 0; i < reps; i++) {
        double t0 = now_ns();
        ss_track_read(t, total - chunk, chunk, buf);   /* read the LAST chunk */
        s[i] = now_ns() - t0;
    }
    double m = median_of(s, reps);
    free(s);
    ss_ctx_destroy(ctx);
    return m;
}

/* How long does one delete cost when the context holds `tracks` tracks?
 * tr_find_child scans every track in the context looking for aliases. */
static double delete_cost(size_t tracks, size_t reps)
{
    int16_t buf[256] = {0};
    double *s = malloc(reps * sizeof(double));

    for (size_t i = 0; i < reps; i++) {
        ss_ctx *ctx;
        ss_ctx_create(&ctx);
        ss_track *victim;
        ss_track_create(ctx, &victim);
        ss_track_write(victim, 0, 256, buf);

        /* Unrelated tracks. They have nothing to do with `victim`. */
        for (size_t k = 0; k < tracks; k++) {
            ss_track *other;
            ss_track_create(ctx, &other);
            ss_track_write(other, 0, 64, buf);
        }

        double t0 = now_ns();
        ss_track_delete(victim, 0, 128);
        s[i] = now_ns() - t0;
        ss_ctx_destroy(ctx);
    }
    double m = median_of(s, reps);
    free(s);
    return m;
}

int main(void)
{
    printf("read of last 64 samples, vs node count\n");
    printf("%10s %12s %10s\n", "nodes", "p50_ns", "ns/node");
    for (size_t n = 32; n <= 8192; n *= 2) {
        double t = read_cost(n, 200);
        printf("%10zu %12.0f %10.2f\n", n, t, t / (double)n);
    }

    printf("\ndelete of 128 samples, vs unrelated tracks in the context\n");
    printf("%10s %12s %10s\n", "tracks", "p50_ns", "ns/track");
    for (size_t k = 0; k <= 512; k = k ? k * 2 : 1) {
        double t = delete_cost(k, 60);
        printf("%10zu %12.0f %10.2f\n", k, t, k ? t / (double)k : 0.0);
    }
    return 0;
}
