#include "sound_seg/sound_seg.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    ss_ctx *ctx = NULL;
    assert(ss_ctx_create(&ctx) == SS_OK);

    ss_track *t = NULL;
    assert(ss_track_create(ctx, &t) == SS_OK);

    const int16_t in[5] = {1, 2, 3, 4, 5};
    assert(ss_track_write(t, 0, 5, in) == SS_OK);

    size_t len = 0;
    assert(ss_track_length(t, &len) == SS_OK);
    assert(len == 5);

    int16_t out[5] = {0};
    assert(ss_track_read(t, 0, 5, out) == SS_OK);
    for (int i = 0; i < 5; i++) assert(out[i] == in[i]);

    ss_track_destroy(t);
    ss_ctx_destroy(ctx);
    printf("test_read_write OK\n");
    return 0;
}
