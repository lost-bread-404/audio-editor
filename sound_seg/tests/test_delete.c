#include "sound_seg/sound_seg.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    ss_ctx *ctx = NULL;
    assert(ss_ctx_create(&ctx) == SS_OK);

    ss_track *t = NULL;
    assert(ss_track_create(ctx, &t) == SS_OK);

    const int16_t in[6] = {10, 20, 30, 40, 50, 60};
    assert(ss_track_write(t, 0, 6, in) == SS_OK);
    assert(ss_track_delete(t, 2, 2) == SS_OK);

    size_t len = 0;
    ss_track_length(t, &len);
    assert(len == 4);

    int16_t out[4] = {0};
    assert(ss_track_read(t, 0, 4, out) == SS_OK);
    const int16_t want[4] = {10, 20, 50, 60};
    for (int i = 0; i < 4; i++) assert(out[i] == want[i]);

    ss_track_destroy(t);
    ss_ctx_destroy(ctx);
    printf("test_delete OK\n");
    return 0;
}
