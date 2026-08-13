/* The whole point of error codes: every one of these used to be a
 * silent no-op, a wrong answer, or a crash. */
#include "sound_seg/sound_seg.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>

int main(void) {
    int16_t buf[4] = {0};
    ss_track *t_unused = NULL;

    ss_ctx *ctx = NULL;
    assert(ss_ctx_create(&ctx) == SS_OK);

    /* NULL handling */
    assert(ss_ctx_create(NULL)                == SS_ERR_INVALID_ARG);
    assert(ss_track_create(ctx, NULL)         == SS_ERR_INVALID_ARG);
    assert(ss_track_create(NULL, &t_unused)   == SS_ERR_INVALID_ARG);
    assert(ss_track_read(NULL, 0, 1, buf)     == SS_ERR_INVALID_ARG);
    assert(ss_track_length(NULL, NULL)        == SS_ERR_INVALID_ARG);
    ss_track_destroy(NULL);   /* must not crash */
    ss_ctx_destroy(NULL);     /* must not crash */

    ss_track *t = NULL;
    assert(ss_track_create(ctx, &t) == SS_OK);

    const int16_t in[4] = {1, 2, 3, 4};
    assert(ss_track_write(t, 0, 4, in) == SS_OK);

    /* Reading past the end is now reported, not silently ignored. */
    assert(ss_track_read(t, 2, 5, buf)  == SS_ERR_OUT_OF_RANGE);
    assert(ss_track_read(t, 99, 1, buf) == SS_ERR_OUT_OF_RANGE);

    /* Deleting past the end used to walk off the list. */
    assert(ss_track_delete(t, 3, 10) == SS_ERR_OUT_OF_RANGE);

    /* Overflow: pos + len wraps around size_t. Must not be accepted. */
    assert(ss_track_read(t, SIZE_MAX - 1, 4, buf) == SS_ERR_OUT_OF_RANGE);

    /* Zero length is a valid no-op, not an error. */
    assert(ss_track_read(t, 0, 0, NULL)  == SS_OK);
    assert(ss_track_write(t, 0, 0, NULL) == SS_OK);

    /* An ad longer than the target used to underflow and read wildly. */
    ss_track *ad = NULL;
    assert(ss_track_create(ctx, &ad) == SS_OK);
    const int16_t long_ad[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    assert(ss_track_write(ad, 0, 8, long_ad) == SS_OK);
    char *matches = NULL;
    assert(ss_track_identify(t, ad, &matches) == SS_ERR_OUT_OF_RANGE);
    assert(matches == NULL);

    /* I/O failures are reported instead of dereferencing a NULL FILE*. */
    int16_t *samples = NULL;
    size_t count = 0;
    assert(ss_wav_load("/nonexistent/file.wav", &samples, &count) == SS_ERR_IO);
    assert(samples == NULL);

    /* Every code has a message. */
    assert(ss_strerror(SS_ERR_ALIASED)[0] != '\0');

    ss_track_destroy(ad);
    ss_track_destroy(t);

    /* Creating tracks after a destroy must still work. */
    ss_track *again = NULL;
    assert(ss_track_create(ctx, &again) == SS_OK);
    ss_track_destroy(again);

    ss_ctx_destroy(ctx);

    printf("test_errors OK\n");
    return 0;
}
