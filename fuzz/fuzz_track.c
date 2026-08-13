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
#include <stdlib.h>
#include <string.h>

#define NTRACKS  3
#define MAXCHUNK 32

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

        switch (op % 7) {
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
        }
    }

out:
    ss_ctx_destroy(ctx);   /* destroys every track still alive */
    return 0;
}
