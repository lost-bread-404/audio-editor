
/*
 * Latency benchmark harness for libsound_seg.
 *
 * Reports a distribution, not an average. See README section
 * "Why percentiles" for the reasoning.
 *
 * Output is JSON on stdout so CI can diff two runs mechanically.
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
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ *
 * Timing
 * ------------------------------------------------------------------ */

/* CLOCK_MONOTONIC never jumps backwards when the system clock is
 * adjusted (NTP, daylight saving). Wall-clock time can, which would
 * produce negative durations. */
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

/* 两次连续读时钟之间最小的非零间隔。如果某个操作的耗时不到它的
 * 几倍，那么所有测量值都是它的整数倍，数字是假的。
 * overhead 回答"读时钟多贵"，resolution 回答"时钟能分辨多细"。
 * 两者不同：一个 1 us 分辨率的时钟可以只要 20 ns 就读完。 */
static double clock_resolution_ns(void)
{
    double best = 1e18;
    for (int i = 0; i < 10000; i++) {
        double a = now_ns(), b = now_ns();
        if (b > a && b - a < best) best = b - a;
    }
    return best;
}

static int cmp_double(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return (x > y) - (x < y);
}

/* Nearest-rank percentile: the smallest sample that is >= p% of all
 * samples. No interpolation, so the number is always one we actually
 * observed. */
static double percentile(double *sorted, size_t n, double p)
{
    size_t rank = (size_t)(p / 100.0 * (double)n);
    if (rank >= n) {
        rank = n - 1;
    }
    return sorted[rank];
}

/* ------------------------------------------------------------------ *
 * Benchmark definition
 *
 * setup/teardown run OUTSIDE the timer. That matters: most of these
 * operations are destructive, so each sample needs a fresh track, and
 * building that track costs far more than the operation itself.
 * ------------------------------------------------------------------ */

typedef struct {
    ss_ctx   *ctx;
    ss_track *a;
    ss_track *b;
    int16_t  *buf;
    size_t    n;      /* size parameter for this case */
} fixture;

typedef struct {
    const char *name;
    size_t      n;
    void      (*setup)(fixture *);
    void      (*run)(fixture *);
    void      (*teardown)(fixture *);
} bench_case;

#define TRACK_SAMPLES 65536
#define OP_SAMPLES    1024

static void fill(int16_t *buf, size_t n)
{
    for (size_t i = 0; i < n; i++) {
        buf[i] = (int16_t)((i * 2654435761u) >> 16);   /* cheap spread */
    }
}

/* ---- fixtures ---------------------------------------------------- */

static void setup_empty(fixture *f)
{
    ss_ctx_create(&f->ctx);
    ss_track_create(f->ctx, &f->a);
    f->buf = malloc(f->n * sizeof(int16_t));
    fill(f->buf, f->n);
}

static void setup_filled(fixture *f)
{
    setup_empty(f);
    int16_t *big = malloc(TRACK_SAMPLES * sizeof(int16_t));
    fill(big, TRACK_SAMPLES);
    ss_track_write(f->a, 0, TRACK_SAMPLES, big);
    free(big);
}

static void setup_two_tracks(fixture *f)
{
    setup_filled(f);
    ss_track_create(f->ctx, &f->b);
}

static void teardown(fixture *f)
{
    ss_ctx_destroy(f->ctx);      /* frees every track in the context */
    free(f->buf);
    f->ctx = NULL;
    f->a = f->b = NULL;
    f->buf = NULL;
}

/* ---- the operations under test ----------------------------------- */

static void run_write(fixture *f)   { ss_track_write(f->a, 0, f->n, f->buf); }
static void run_read(fixture *f)    { ss_track_read(f->a, 0, f->n, f->buf); }
static void run_read_tail(fixture *f)
{
    ss_track_read(f->a, TRACK_SAMPLES - f->n, f->n, f->buf);
}
static void run_delete_mid(fixture *f)
{
    ss_track_delete(f->a, TRACK_SAMPLES / 2, f->n);
}
static void run_insert(fixture *f)
{
    ss_track_insert(f->b, 0, f->a, 0, f->n);
}
static void run_length(fixture *f)
{
    size_t len;
    ss_track_length(f->a, &len);
}

/* ------------------------------------------------------------------ *
 * Driver
 * ------------------------------------------------------------------ */

static void run_case(const bench_case *bc, size_t warmup, size_t iters,
                     int first)
{
    double *samples = malloc(iters * sizeof(double));
    fixture f = {0};
    f.n = bc->n;

    /* Warmup: the first calls pay for page faults, allocator arena
     * setup, and a cold instruction cache. Those costs are real but
     * they are not what we are measuring. */
    for (size_t i = 0; i < warmup; i++) {
        bc->setup(&f);
        bc->run(&f);
        bc->teardown(&f);
    }

    for (size_t i = 0; i < iters; i++) {
        bc->setup(&f);
        double t0 = now_ns();
        bc->run(&f);
        double t1 = now_ns();
        bc->teardown(&f);
        samples[i] = t1 - t0;
    }

    qsort(samples, iters, sizeof(double), cmp_double);

    double sum = 0.0;
    for (size_t i = 0; i < iters; i++) {
        sum += samples[i];
    }
    double mean = sum / (double)iters;

    printf("%s\n    {\"name\": \"%s\", \"n\": %zu, \"iters\": %zu, "
           "\"mean_ns\": %.1f, \"p50_ns\": %.1f, \"p90_ns\": %.1f, "
           "\"p99_ns\": %.1f, \"p999_ns\": %.1f, \"max_ns\": %.1f}",
           first ? "" : ",", bc->name, bc->n, iters, mean,
           percentile(samples, iters, 50.0),
           percentile(samples, iters, 90.0),
           percentile(samples, iters, 99.0),
           percentile(samples, iters, 99.9),
           samples[iters - 1]);
    fflush(stdout);
    free(samples);
}

int main(int argc, char **argv)
{
    size_t iters  = (argc > 1) ? strtoul(argv[1], NULL, 10) : 2000;
    size_t warmup = iters / 10 + 1;

    const bench_case cases[] = {
        {"track_length_64k", 0,          setup_filled,     run_length,     teardown},
        {"write_1k_empty",   OP_SAMPLES, setup_empty,      run_write,      teardown},
        {"read_1k_head",     OP_SAMPLES, setup_filled,     run_read,       teardown},
        {"read_1k_tail",     OP_SAMPLES, setup_filled,     run_read_tail,  teardown},
        {"delete_1k_mid",    OP_SAMPLES, setup_filled,     run_delete_mid, teardown},
        {"insert_1k_alias",  OP_SAMPLES, setup_two_tracks, run_insert,     teardown},
    };
    size_t ncases = sizeof(cases) / sizeof(cases[0]);

    /* Timer overhead: if an operation's p50 is close to this number,
     * the measurement is mostly noise and the result means nothing. */
    double t0 = now_ns();
    for (int i = 0; i < 1000; i++) {
        (void)now_ns();
    }
    double clock_ns = (now_ns() - t0) / 1000.0;
    printf("{\n  \"clock_overhead_ns\": %.1f,\n"
        "  \"clock_resolution_ns\": %.1f,\n  \"results\": [",
        clock_ns, clock_resolution_ns());

    for (size_t i = 0; i < ncases; i++) {
        run_case(&cases[i], warmup, iters, i == 0);
    }
    printf("\n  ]\n}\n");
    return 0;
}
