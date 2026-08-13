/* Regression test for a use-after-free found by the fuzzer.
 *
 * ss_track_insert makes dest's nodes alias src's nodes rather than
 * copying samples -- that is the whole point of the design. But
 * ss_track_destroy used to free every node it owned without checking
 * whether anyone still referenced them, leaving the other track's
 * parent pointers dangling.
 *
 * ss_track_delete already refused this case (SS_ERR_ALIASED); destroy
 * had no equivalent check.
 */
#include "sound_seg/sound_seg.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    ss_ctx *ctx = NULL;
    assert(ss_ctx_create(&ctx) == SS_OK);

    ss_track *a = NULL, *b = NULL;
    assert(ss_track_create(ctx, &a) == SS_OK);
    assert(ss_track_create(ctx, &b) == SS_OK);

    const int16_t data[8] = {11, 22, 33, 44, 55, 66, 77, 88};
    assert(ss_track_write(a, 0, 8, data) == SS_OK);

    /* b now aliases a's samples -- no copy was made. */
    assert(ss_track_insert(b, 0, a, 0, 8) == SS_OK);

    /* Destroy the track that owns the samples b is pointing at. */
    ss_track_destroy(a);

    /* b must still be readable and still hold the right samples. */
    size_t len = 0;
    assert(ss_track_length(b, &len) == SS_OK);
    assert(len == 8);

    int16_t out[8] = {0};
    assert(ss_track_read(b, 0, 8, out) == SS_OK);
    for (int i = 0; i < 8; i++) {
        assert(out[i] == data[i]);
    }

    /* And b must still be editable: this path walks parent pointers. */
    assert(ss_track_delete(b, 2, 3) == SS_OK);
    assert(ss_track_length(b, &len) == SS_OK);
    assert(len == 5);

    ss_ctx_destroy(ctx);

    /* Chained aliases: c aliases b, b aliases a. Destroying the middle
     * one must not strand c either. */
    assert(ss_ctx_create(&ctx) == SS_OK);
    ss_track *x = NULL, *y = NULL, *z = NULL;
    assert(ss_track_create(ctx, &x) == SS_OK);
    assert(ss_track_create(ctx, &y) == SS_OK);
    assert(ss_track_create(ctx, &z) == SS_OK);
    assert(ss_track_write(x, 0, 8, data) == SS_OK);
    assert(ss_track_insert(y, 0, x, 0, 8) == SS_OK);
    assert(ss_track_insert(z, 0, y, 0, 8) == SS_OK);

    ss_track_destroy(y);               /* the middle link */
    ss_track_destroy(x);               /* the original owner */

    assert(ss_track_read(z, 0, 8, out) == SS_OK);
    for (int i = 0; i < 8; i++) {
        assert(out[i] == data[i]);
    }

    ss_ctx_destroy(ctx);
    printf("test_alias_destroy OK\n");
    return 0;
}
