#include <sound_seg/sound_seg.h>
#include <stdio.h>
int main(void) {
    ss_ctx *ctx = NULL;
    if (ss_ctx_create(&ctx) != SS_OK) return 1;
    ss_track *t = NULL;
    ss_track_create(ctx, &t);
    const int16_t d[4] = {1,2,3,4};
    ss_track_write(t, 0, 4, d);
    size_t n = 0;
    ss_track_length(t, &n);
    printf("consumer sees length = %zu\n", n);
    int16_t out[1];
    ss_status rc = ss_track_read(t, 99, 1, out);
    printf("out-of-range read -> %s\n", ss_strerror(rc));
    ss_ctx_destroy(ctx);
    return 0;
}
