/* libFuzzer entry point.
 *
 * libFuzzer hands us a random byte string. We interpret it as a script
 * of operations on a small set of tracks. If any sequence crashes,
 * leaks, or trips a sanitizer, the process aborts and libFuzzer saves
 * the exact bytes to a file named crash-<hash>.
 *
 * Two rules make the difference between guided fuzzing and random
 * noise:
 *   1. The library must be built with coverage instrumentation, or
 *      libFuzzer gets no feedback from inside it. See SOUND_SEG_FUZZ
 *      in the top-level CMakeLists.
 *   2. Random bytes must mostly produce *valid* calls. If nearly every
 *      operation is rejected by argument validation, the fuzzer never
 *      reaches the interesting code. We bias positions and lengths
 *      into the track's live range for that reason.
 */
#include "sound_seg/sound_seg.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NTRACKS  3
#define MAXCHUNK 32

/* Scratch file for the WAV round-trip op. A fixed name is fine: each
 * input runs to completion in one process before the next starts. */
#define WAV_PATH "fuzz_track_scratch.wav"

/* Read one byte, or 0 if the script is exhausted. */
static uint8_t take(const uint8_t **p, const uint8_t *end)
{
    return (*p < end) ? *(*p)++ : 0;
}

/* Map a byte onto [0, bound]. Biased, but the point is to land inside
 * the valid range most of the time, not to be uniform. */
static size_t scaled(uint8_t b, size_t bound)
{
    return bound ? (size_t)b * bound / 255u : 0;
}

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    /* A fresh context per input. Nothing carries over between runs,
     * which is what makes a saved crash file reproducible on its own. */
    ss_ctx *ctx = NULL;
    if (ss_ctx_create(&ctx) != SS_OK) {
        return 0;
    }

    ss_track *tracks[NTRACKS] = {0};
    for (int i = 0; i < NTRACKS; i++) {
        if (ss_track_create(ctx, &tracks[i]) != SS_OK) {
            ss_ctx_destroy(ctx);
            return 0;
        }
    }

    int16_t buf[MAXCHUNK];
    memset(buf, 0, sizeof(buf));

    const uint8_t *p = data, *end = data + size;

    while (p < end) {
        uint8_t op = take(&p, end);
        ss_track *t = tracks[take(&p, end) % NTRACKS];
        size_t live = 0;
        ss_track_length(t, &live);

        switch (op % 14) {
        case 0: {   /* append: the only way a track grows */
            size_t len = 1 + take(&p, end) % MAXCHUNK;
            ss_track_write(t, live, len, buf);
            break;
        }
        case 1: {   /* overwrite inside the live range */
            if (!live) break;
            size_t pos = scaled(take(&p, end), live - 1);
            size_t len = 1 + take(&p, end) % MAXCHUNK;
            ss_track_write(t, pos, len, buf);
            break;
        }
        case 2: {
            if (!live) break;
            size_t pos = scaled(take(&p, end), live - 1);
            size_t len = 1 + take(&p, end) % MAXCHUNK;
            if (pos + len > live) len = live - pos;
            ss_track_read(t, pos, len, buf);
            break;
        }
        case 3: {   /* delete: exercises tr_split and the alias check */
            if (!live) break;
            size_t pos = scaled(take(&p, end), live - 1);
            size_t len = 1 + take(&p, end) % MAXCHUNK;
            if (pos + len > live) len = live - pos;
            ss_track_delete(t, pos, len);
            break;
        }
        case 4: {   /* insert: creates aliases between tracks */
            ss_track *dst = tracks[take(&p, end) % NTRACKS];
            if (!live) break;
            size_t srcpos = scaled(take(&p, end), live - 1);
            size_t len = 1 + take(&p, end) % MAXCHUNK;
            if (srcpos + len > live) len = live - srcpos;
            size_t dlen = 0;
            ss_track_length(dst, &dlen);
            size_t destpos = scaled(take(&p, end), dlen);
            ss_track_insert(dst, destpos, t, srcpos, len);
            break;
        }
        case 5: {   /* destroy and replace: exercises registry churn */
            int idx = take(&p, end) % NTRACKS;
            ss_track_destroy(tracks[idx]);
            tracks[idx] = NULL;
            if (ss_track_create(ctx, &tracks[idx]) != SS_OK) {
                goto out;
            }
            break;
        }
        case 6: {
            ss_track *other = tracks[take(&p, end) % NTRACKS];
            char *matches = NULL;
            if (ss_track_identify(t, other, &matches) == SS_OK) {
                ss_free(matches);
            }
            break;
        }
        case 7: {   /* the read-only queries, including every error name */
            ss_ctx_track_count(ctx);
            ss_ctx_track_count(NULL);
            for (int code = 1; code >= -7; code--) {
                ss_strerror((ss_status)code);
            }
            break;
        }
        case 8: {   /* argument validation: the reject half of each guard */
            size_t n = 0;
            char *matches = NULL;
            ss_track_length(NULL, &n);
            ss_track_length(t, NULL);
            ss_track_read(NULL, 0, 1, buf);
            ss_track_read(t, 0, 1, NULL);
            ss_track_write(NULL, 0, 1, buf);
            ss_track_write(t, 0, 1, NULL);
            ss_track_delete(NULL, 0, 1);
            ss_track_insert(NULL, 0, t, 0, 1);
            ss_track_insert(t, 0, NULL, 0, 1);
            ss_track_identify(NULL, t, &matches);
            ss_track_identify(t, NULL, &matches);
            ss_track_identify(t, t, NULL);
            /* len == 0 takes the early-success path in each of these */
            ss_track_read(t, 0, 0, NULL);
            ss_track_write(t, 0, 0, NULL);
            ss_track_delete(t, 0, 0);
            ss_track_insert(t, 0, t, 0, 0);
            /* out-of-range positions, past the end of the live data */
            ss_track_read(t, live + 1, 1, buf);
            ss_track_delete(t, live + 1, 1);
            ss_track_insert(t, live + 1, t, 0, 1);
            ss_track_destroy(NULL);
            ss_free(NULL);
            break;
        }
        case 9: {   /* WAV round-trip, plus the files that must be rejected */
            size_t count = 1 + take(&p, end) % MAXCHUNK;
            ss_wav_save(WAV_PATH, buf, count);
            int16_t *loaded = NULL;
            size_t got = 0;
            if (ss_wav_load(WAV_PATH, &loaded, &got) == SS_OK) {
                ss_free(loaded);
            }
            /* A file shorter than a WAV header, and one that is exactly a
             * header with no samples, are both rejected -- different
             * branches from the successful load above. */
            FILE *f = fopen(WAV_PATH, "wb");
            if (f) {
                size_t stub = take(&p, end) % 48;   /* straddles 44 bytes */
                for (size_t i = 0; i < stub; i++) {
                    fputc('x', f);
                }
                fclose(f);
                loaded = NULL;
                if (ss_wav_load(WAV_PATH, &loaded, &got) == SS_OK) {
                    ss_free(loaded);
                }
            }
            remove(WAV_PATH);
            /* Missing file, and the NULL-argument guards. */
            loaded = NULL;
            if (ss_wav_load(WAV_PATH, &loaded, &got) == SS_OK) {
                ss_free(loaded);
            }
            ss_wav_load(NULL, &loaded, &got);
            ss_wav_load(WAV_PATH, NULL, &got);
            ss_wav_load(WAV_PATH, &loaded, NULL);
            ss_wav_save(NULL, buf, 1);
            ss_wav_save(WAV_PATH, NULL, 1);
            /* A directory that does not exist: fopen fails on both sides */
            ss_wav_save("no_such_dir/x.wav", buf, 1);
            loaded = NULL;
            ss_wav_load("no_such_dir/x.wav", &loaded, &got);
            break;
        }
        case 10: {  /* a second context: every cross-context call must fail */
            ss_ctx *other_ctx = NULL;
            if (ss_ctx_create(&other_ctx) != SS_OK) {
                break;
            }
            ss_track *foreign = NULL;
            if (ss_track_create(other_ctx, &foreign) == SS_OK) {
                ss_track_write(foreign, 0, 4, buf);
                ss_track_insert(t, 0, foreign, 0, 1);
                ss_track_insert(foreign, 0, t, 0, 1);
                char *matches = NULL;
                if (ss_track_identify(t, foreign, &matches) == SS_OK) {
                    ss_free(matches);
                }
            }
            ss_ctx_create(NULL);
            ss_track_create(NULL, &foreign);
            ss_track_create(other_ctx, NULL);
            ss_ctx_destroy(other_ctx);
            ss_ctx_destroy(NULL);
            break;
        }
        case 11: {  /* identify against a deliberately silent or long ad */
            ss_track *other = tracks[take(&p, end) % NTRACKS];
            char *matches = NULL;
            /* A track of zeros correlates with nothing: the reference
             * energy is zero and identify must refuse rather than divide
             * by it. buf is all zeros, so this is the silent-ad path. */
            if (ss_track_identify(t, other, &matches) == SS_OK) {
                ss_free(matches);
            }
            /* Non-zero samples give a real correlation to compute. */
            for (int i = 0; i < MAXCHUNK; i++) {
                buf[i] = (int16_t)(take(&p, end) * 257);
            }
            size_t len = 1 + take(&p, end) % MAXCHUNK;
            ss_track_write(t, live, len, buf);
            matches = NULL;
            if (ss_track_identify(t, other, &matches) == SS_OK) {
                ss_free(matches);
            }
            memset(buf, 0, sizeof(buf));
            break;
        }
        case 12: {  /* values near SIZE_MAX, where pos + len would wrap */
            static const size_t huge[] = {
                SIZE_MAX, SIZE_MAX - 1, SIZE_MAX / 2, SIZE_MAX - MAXCHUNK
            };
            size_t big = huge[take(&p, end) % 4];
            size_t len = 1 + take(&p, end) % MAXCHUNK;
            /* Each of these must be refused by the range check rather
             * than wrapping around into a range that looks valid. */
            ss_track_write(t, big, len, buf);
            ss_track_write(t, live, big, buf);
            ss_track_read(t, big, len, buf);
            ss_track_read(t, live, big, buf);
            ss_track_delete(t, big, len);
            ss_track_delete(t, live, big);
            ss_track_insert(t, big, t, 0, 1);
            ss_track_insert(t, 0, t, big, len);
            ss_track_insert(t, 0, t, 0, big);
            break;
        }
        case 13: {  /* hold more tracks than the registry's initial slots */
            size_t extra_n = 1 + take(&p, end) % 8;
            ss_track *extra[8] = {0};
            for (size_t i = 0; i < extra_n; i++) {
                if (ss_track_create(ctx, &extra[i]) != SS_OK) {
                    break;
                }
                ss_track_write(extra[i], 0, 1 + take(&p, end) % 4, buf);
            }
            ss_ctx_track_count(ctx);
            /* Drop them again so the registry shrinks back, exercising
             * the swap-down path in the remove routine repeatedly. */
            for (size_t i = 0; i < extra_n; i++) {
                ss_track_destroy(extra[i]);
            }
            break;
        }
        }
    }

out:
    ss_ctx_destroy(ctx);   /* destroys every track still alive */
    return 0;
}
