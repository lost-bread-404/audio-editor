#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sound_seg/sound_seg.h"
#include "sound_seg_extra.h"

#define WAV_HEADER_SIZE 44
#define PCM_SUBCHUNK_SIZE 16
#define BYTES_PER_SAMPLE_16BIT 2
#define SAMPLE_RATE 8000
#define CORRELATION_THRESHOLD 95.0
#define MATCH_INDEX_CHAR_SIZE 24
#define DEFAULT_BUFFER_SIZE 128

/* ---------------------------------------------------------------- *
 * Internal helpers
 * ---------------------------------------------------------------- */

/* Overflow-safe "is [pos, pos+len) inside [0, total)?".
 * Writing `pos + len > total` would wrap around on huge values and
 * silently report that an out-of-range request is fine. */
static bool range_ok(size_t total, size_t pos, size_t len)
{
    return len <= total && pos <= total - len;
}

/* Every track header stores its context in the union slot. */
static struct ss_ctx *owner_of(struct sound_seg *track)
{
    return track->owner;
}

static size_t track_len(struct sound_seg *seg)
{
    size_t count = 0;
    for (seg = seg->next; seg; seg = seg->next) {
        count += seg->len;
    }
    return count;
}

/* Flatten a whole track into a freshly allocated array. */
static ss_status track_to_array(struct sound_seg *track, int16_t **out,
                                size_t *out_len)
{
    size_t len = track_len(track);
    if (len == 0) {
        return SS_ERR_OUT_OF_RANGE;
    }
    int16_t *buf = calloc(len, sizeof(int16_t));
    if (!buf) {
        return SS_ERR_NO_MEMORY;
    }
    size_t scanned = 0;
    for (struct sound_seg *n = track->next; n && scanned < len; n = n->next) {
        memcpy(buf + scanned, tr_get_data(n), n->len * sizeof(int16_t));
        scanned += n->len;
    }
    *out = buf;
    *out_len = len;
    return SS_OK;
}

/* ---------------------------------------------------------------- *
 * Status strings
 * ---------------------------------------------------------------- */

const char *ss_strerror(ss_status status)
{
    switch (status) {
    case SS_OK:               return "success";
    case SS_ERR_INVALID_ARG:  return "invalid argument";
    case SS_ERR_OUT_OF_RANGE: return "position or length out of range";
    case SS_ERR_NO_MEMORY:    return "out of memory";
    case SS_ERR_IO:           return "I/O error";
    case SS_ERR_ALIASED:      return "range is referenced by another track";
    case SS_ERR_WRONG_CONTEXT:return "tracks belong to different contexts";
    }
    return "unknown error";
}

void ss_free(void *ptr)
{
    free(ptr);
}

/* ---------------------------------------------------------------- *
 * Lifecycle
 * ---------------------------------------------------------------- */

ss_status ss_ctx_create(ss_ctx **out)
{
    if (!out) {
        return SS_ERR_INVALID_ARG;
    }
    *out = calloc(1, sizeof(struct ss_ctx));
    return *out ? SS_OK : SS_ERR_NO_MEMORY;
}

void ss_ctx_destroy(ss_ctx *ctx)
{
    if (!ctx) {
        return;
    }
    /* Free tracks back to front: each destroy removes itself from the
     * registry by swapping the last entry down into its slot, so
     * walking backwards never skips one. */
    while (ctx->len > 0) {
        ss_track_destroy(ctx->headers[ctx->len - 1]);
    }
    free(ctx->headers);
    free(ctx);
}

size_t ss_ctx_track_count(const ss_ctx *ctx)
{
    return ctx ? ctx->len : 0;
}

ss_status ss_track_create(ss_ctx *ctx, ss_track **out)
{
    if (!ctx || !out) {
        return SS_ERR_INVALID_ARG;
    }
    *out = NULL;

    struct sound_seg *seg = calloc(1, sizeof(struct sound_seg));
    if (!seg) {
        return SS_ERR_NO_MEMORY;
    }
    seg->owner = ctx;                 /* header node: union holds the ctx */
    if (!tr_registry_add(ctx, seg)) {
        free(seg);
        return SS_ERR_NO_MEMORY;
    }
    *out = seg;
    return SS_OK;
}

void ss_track_destroy(ss_track *track)
{
    if (!track) {
        return;
    }
    struct ss_ctx *ctx = owner_of(track);

    /* Start at track->next: the header's union slot holds the context,
     * not a parent or a data pointer, so it must never be handled as an
     * ordinary node. */
    ss_track *cur = track->next;
    while (cur) {
        struct sound_seg *next = cur->next;
        tr_transfer_ownership(ctx, cur); // transfer all children of track to its heir/parent
        cur = next;
    }

    tr_registry_remove(owner_of(track), track);

    while (track){
        struct sound_seg *next = track->next;
        if (track->has_data) {
            free(track->data);
            track->has_data = 0;
        }
        free(track);
        track = next;
    }
}

/* ---------------------------------------------------------------- *
 * Queries and edits
 * ---------------------------------------------------------------- */

ss_status ss_track_length(ss_track *track, size_t *out_len)
{
    if (!track || !out_len) {
        return SS_ERR_INVALID_ARG;
    }
    *out_len = track_len(track);
    return SS_OK;
}

ss_status ss_track_read(ss_track *track, size_t pos, size_t len, int16_t *dest)
{
    if (!track || (!dest && len > 0)) {
        return SS_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return SS_OK;
    }
    if (!range_ok(track_len(track), pos, len)) {
        return SS_ERR_OUT_OF_RANGE;
    }

    struct sound_seg *node = track;
    int index = tr_find(&node, (int)pos);
    for (size_t i = 0; i < len; i++) {
        if ((unsigned long long)index >= node->len) {
            node = node->next;
            index = 0;
        }
        dest[i] = tr_get_data(node)[index++];
    }
    return SS_OK;
}

ss_status ss_track_write(ss_track *track, size_t pos, size_t len,
                         const int16_t *src)
{
    if (!track || (!src && len > 0)) {
        return SS_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return SS_OK;
    }

    size_t total = track_len(track);
    /* Guard the operands of the sum we are about to compute. Stating the
     * test over 'pos - total' instead shrinks the left-hand side by the
     * track's existing length, so a wrapping request slips through and
     * the blind spot widens as the track grows. */
    if (pos > SIZE_MAX - len) {
        return SS_ERR_OUT_OF_RANGE;      /* pos + len would overflow */
    }
    if (pos + len > total) {
        struct sound_seg *tail = track;
        while (tail->next) {
            tail = tail->next;
        }
        if (!tr_create_original_node(tail, pos + len - total)) {
            return SS_ERR_NO_MEMORY;
        }
    }

    struct sound_seg *node = track;
    int index = tr_find(&node, (int)pos);
    for (size_t i = 0; i < len; i++) {
        tr_get_data(node)[index++] = src[i];
        if ((unsigned long long)index >= node->len) {
            node = node->next;
            index = 0;
        }
    }
    return SS_OK;
}

ss_status ss_track_delete(ss_track *track, size_t pos, size_t len)
{
    if (!track) {
        return SS_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return SS_OK;
    }
    if (!range_ok(track_len(track), pos, len)) {
        return SS_ERR_OUT_OF_RANGE;
    }

    struct ss_ctx *ctx = owner_of(track);
    struct sound_seg *node = track;
    int prev_index = tr_find(&node, (int)pos - 1);
    struct sound_seg *prev_track = node;
    size_t index = (size_t)(prev_index + 1);
    if (index >= node->len) {
        node = node->next;
        index = 0;
    }

    /* Refuse if any node in the range is aliased by another track. */
    {
        size_t i = index, scanned = 0;
        struct sound_seg *probe = node;
        while (scanned < len && probe) {
            if (tr_find_child(ctx, probe)) {
                return SS_ERR_ALIASED;
            }
            scanned += probe->len - i;
            i = 0;
            probe = probe->next;
        }
    }

    size_t deleted = 0;
    while (deleted < len) {
        size_t movable;
        struct sound_seg *victim;

        if (index == 0 && (len - deleted) >= node->len) {
            movable = node->len;
            victim  = node;
        } else if (index != 0 && (len - deleted) < (node->len - index)) {
            movable = len - deleted;
            tr_split(ctx, node, (int)index, (int)movable);
            victim = node->next;
        } else if (index == 0 && (len - deleted) < node->len) {
            movable = node->len - (len - deleted);
            tr_split(ctx, node, (int)(len - deleted), (int)movable);
            movable = len - deleted;
            victim  = node;
        } else if (index != 0 && (len - deleted) >= (node->len - index)) {
            movable = node->len - index;
            tr_split(ctx, node, (int)index, (int)movable);
            victim = node->next;
        } else {
            break;
        }

        if (victim->has_data) {
            free(victim->data);
        }
        while (prev_track->next != victim) {
            prev_track = prev_track->next;
        }
        prev_track->next = victim->next;
        free(victim);
        node = prev_track->next;
        index = 0;
        deleted += movable;
    }
    return SS_OK;
}

ss_status ss_track_insert(ss_track *dest, size_t destpos,
                          ss_track *src, size_t srcpos, size_t len)
{
    if (!dest || !src) {
        return SS_ERR_INVALID_ARG;
    }
    if (len == 0) {
        return SS_OK;
    }
    if (!range_ok(track_len(src), srcpos, len)) {
        return SS_ERR_OUT_OF_RANGE;
    }
    if (destpos > track_len(dest)) {
        return SS_ERR_OUT_OF_RANGE;
    }
    /* Insert makes dest's nodes alias src's nodes. If they lived in
     * different contexts, neither context's aliasing check would see
     * the other side, and a later delete would free data still in use. */
    struct ss_ctx *ctx = owner_of(dest);
    if (ctx != owner_of(src)) {
        return SS_ERR_WRONG_CONTEXT;
    }

    struct sound_seg *src_node = src;
    int src_index = tr_find(&src_node, (int)srcpos);

    struct sound_seg *staging = NULL;
    ss_status rc = ss_track_create(ctx, &staging);
    if (rc != SS_OK) {
        return rc;
    }
    struct sound_seg *iter = staging;

    size_t scanned = 0;
    while (scanned < len) {
        struct sound_seg *next_src = src_node->next;
        struct sound_seg *copy = calloc(1, sizeof(struct sound_seg));
        if (!copy) {
            ss_track_destroy(staging);
            return SS_ERR_NO_MEMORY;
        }
        struct sound_seg *parent = src_node;
        copy->len  = src_node->len;
        copy->next = NULL;

        if (src_index != 0 &&
            (len - scanned) < (size_t)(src_node->len - src_index)) {
            tr_split(ctx, src_node, src_index, (int)(len - scanned));
            parent = src_node->next;
            copy->len = len - scanned;
        } else if (src_index == 0 && (len - scanned) < (size_t)src_node->len) {
            tr_split(ctx, src_node, (int)(len - scanned),
                     (int)(src_node->len - (len - scanned)));
            copy->len = len - scanned;
        } else if (src_index != 0 &&
                   (len - scanned) >= (size_t)(src_node->len - src_index)) {
            copy->len = src_node->len - src_index;
            tr_split(ctx, src_node, src_index, (int)copy->len);
            parent = src_node->next;
        }

        copy->parent = parent;
        while (iter->next) {
            iter = iter->next;
        }
        iter->next = copy;
        iter = copy;
        src_node = next_src;
        src_index = 0;
        scanned += copy->len;
    }

    struct sound_seg *chain = staging->next;

    struct sound_seg *dest_node = dest;
    int prev_dest_index = tr_find(&dest_node, (int)destpos - 1);
    int index = prev_dest_index + 1;
    if ((unsigned long long)index < dest_node->len && index != 0) {
        tr_split(ctx, dest_node, index, (int)(dest_node->len - index));
    }
    struct sound_seg *rest = dest_node->next;
    dest_node->next = chain;
    while (iter->next) {
        iter = iter->next;
    }
    iter->next = rest;

    /* The staging header did its job; unregister and free just it. */
    tr_registry_remove(ctx, staging);
    free(staging);
    return SS_OK;
}

ss_status ss_track_identify(ss_track *target, ss_track *ad, char **out_matches)
{
    if (!target || !ad || !out_matches) {
        return SS_ERR_INVALID_ARG;
    }
    if (owner_of(target) != owner_of(ad)) {
        return SS_ERR_WRONG_CONTEXT;
    }
    *out_matches = NULL;

    int16_t *ad_values = NULL, *target_values = NULL;
    char *output = NULL;
    size_t ad_len = 0, target_len = 0;
    ss_status rc;

    if ((rc = track_to_array(ad, &ad_values, &ad_len)) != SS_OK) {
        return rc;
    }
    if ((rc = track_to_array(target, &target_values, &target_len)) != SS_OK) {
        free(ad_values);
        return rc;
    }
    /* The old code computed `target_len - ad_len` unsigned; a short
     * target wrapped it to a huge number and read far out of bounds. */
    if (target_len < ad_len) {
        rc = SS_ERR_OUT_OF_RANGE;
        goto done;
    }

    double ref = 0.0;
    for (size_t i = 0; i < ad_len; i++) {
        ref += (double)ad_values[i] * ad_values[i];
    }
    if (ref == 0.0) {
        rc = SS_ERR_INVALID_ARG;      /* silent ad: correlation undefined */
        goto done;
    }

    size_t cap = DEFAULT_BUFFER_SIZE, pos = 0;
    output = calloc(cap, 1);
    if (!output) {
        rc = SS_ERR_NO_MEMORY;
        goto done;
    }

    for (size_t i = 0; i <= target_len - ad_len; i++) {
        double cross = 0.0;
        for (size_t j = 0; j < ad_len; j++) {
            cross += (double)target_values[i + j] * ad_values[j];
        }
        if ((cross / ref) * 100.0 < CORRELATION_THRESHOLD) {
            continue;
        }
        if (cap - pos < MATCH_INDEX_CHAR_SIZE) {
            char *grown = realloc(output, cap * 2);
            if (!grown) {
                free(output);
                output = NULL;
                rc = SS_ERR_NO_MEMORY;
                goto done;
            }
            output = grown;
            cap *= 2;
        }
        pos += (size_t)snprintf(output + pos, cap - pos, "%zu,%zu\n",
                                i, i + ad_len - 1);
    }
    if (pos > 0) {
        output[pos - 1] = '\0';
    }
    *out_matches = output;
    output = NULL;
    rc = SS_OK;

done:
    free(output);
    free(ad_values);
    free(target_values);
    return rc;
}

/* ---------------------------------------------------------------- *
 * WAV I/O
 * ---------------------------------------------------------------- */

ss_status ss_wav_load(const char *path, int16_t **out_samples, size_t *out_count)
{
    if (!path || !out_samples || !out_count) {
        return SS_ERR_INVALID_ARG;
    }
    *out_samples = NULL;
    *out_count = 0;

    FILE *f = fopen(path, "rb");
    if (!f) {
        return SS_ERR_IO;                 /* the old code dereferenced NULL */
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return SS_ERR_IO;
    }
    long size = ftell(f);
    if (size < WAV_HEADER_SIZE) {
        fclose(f);
        return SS_ERR_IO;
    }
    size_t count = ((size_t)size - WAV_HEADER_SIZE) / sizeof(int16_t);
    if (count == 0) {
        fclose(f);
        return SS_ERR_IO;
    }
    if (fseek(f, WAV_HEADER_SIZE, SEEK_SET) != 0) {
        fclose(f);
        return SS_ERR_IO;
    }

    int16_t *buf = calloc(count, sizeof(int16_t));
    if (!buf) {
        fclose(f);
        return SS_ERR_NO_MEMORY;
    }
    size_t got = fread(buf, sizeof(int16_t), count, f);
    fclose(f);
    if (got != count) {
        free(buf);
        return SS_ERR_IO;
    }
    *out_samples = buf;
    *out_count = count;
    return SS_OK;
}

ss_status ss_wav_save(const char *path, const int16_t *samples, size_t count)
{
    if (!path || (!samples && count > 0)) {
        return SS_ERR_INVALID_ARG;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        return SS_ERR_IO;
    }
    pcm_wav_header header = {
        .riff = {'R', 'I', 'F', 'F'},
        .wave = {'W', 'A', 'V', 'E'},
        .fmt  = {'f', 'm', 't', ' '},
        .data = {'d', 'a', 't', 'a'}
    };
    header.size2           = PCM_SUBCHUNK_SIZE;
    header.FormatTag       = 1;
    header.nChannels       = 1;
    header.nSamplesPerSec  = SAMPLE_RATE;
    header.nBytesPerSample = BYTES_PER_SAMPLE_16BIT;
    header.nBytesPerSec    = header.nSamplesPerSec * header.nBytesPerSample;
    header.size3           = (uint32_t)(count * sizeof(int16_t));
    header.size1           = (uint32_t)(sizeof(pcm_wav_header) + header.size3 - 8);

    bool ok = fwrite(&header, sizeof(header), 1, f) == 1 &&
              fwrite(samples, sizeof(int16_t), count, f) == count;
    if (fclose(f) != 0) {                 /* fclose can fail: disk full */
        ok = false;
    }
    return ok ? SS_OK : SS_ERR_IO;
}

#ifdef SOUND_SEG_STANDALONE
int main(void)
{
    printf("%zu\n", sizeof(struct sound_seg));
    return 0;
}
#endif
