/* Two contexts must behave as if the other did not exist. */
#include "sound_seg/sound_seg.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    ss_ctx *a = NULL, *b = NULL;
    assert(ss_ctx_create(&a) == SS_OK);
    assert(ss_ctx_create(&b) == SS_OK);

    ss_track *ta = NULL, *tb = NULL;
    assert(ss_track_create(a, &ta) == SS_OK);
    assert(ss_track_create(b, &tb) == SS_OK);

    /* Each context only counts its own tracks. */
    assert(ss_ctx_track_count(a) == 1);
    assert(ss_ctx_track_count(b) == 1);

    const int16_t data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    assert(ss_track_write(ta, 0, 8, data) == SS_OK);
    assert(ss_track_write(tb, 0, 8, data) == SS_OK);

    /* Aliasing inside context a: a copy of ta's samples lives in ta2,
     * so deleting from ta must be refused. */
    ss_track *ta2 = NULL;
    assert(ss_track_create(a, &ta2) == SS_OK);
    assert(ss_track_insert(ta2, 0, ta, 0, 8) == SS_OK);
    assert(ss_track_delete(ta, 0, 8) == SS_ERR_ALIASED);

    /* Context b knows nothing about any of that: its own delete works.
     * Under the old global registry, b's tracks were scanned too. */
    assert(ss_track_delete(tb, 0, 8) == SS_OK);

    /* Mixing tracks from different contexts is rejected, not silently
     * corrupted. */
    assert(ss_track_insert(tb, 0, ta, 0, 4) == SS_ERR_WRONG_CONTEXT);

    /* Destroying a context frees the tracks still inside it. */
    ss_ctx_destroy(a);
    ss_ctx_destroy(b);

    printf("test_context OK\n");
    return 0;
}
