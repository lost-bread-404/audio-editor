#ifndef SOUND_SEG_EXTRA_H
#define SOUND_SEG_EXTRA_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/*
 * Simple WAV header structure for PCM audio.
 */
typedef struct {
    char     riff[4];
    uint32_t size1;
    char     wave[4];
    char     fmt[4];
    uint32_t size2;
    uint16_t FormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nBytesPerSec;
    uint16_t nBytesPerSample;
    uint16_t nBitsPerSample;
    char     data[4];
    uint32_t size3;
} pcm_wav_header;

/*
 * Each audio segment is a singly linked list node. Exactly 24 bytes;
 * do not add fields.
 *
 * The union slot means three different things depending on which kind
 * of node this is:
 *   - header node (the pointer the user holds): 'owner' -> its context.
 *     A header has len == 0 and has_data == 0, so neither 'data' nor
 *     'parent' is ever read on it.
 *   - data node with has_data == 1: 'data' is an owned sample buffer.
 *   - data node with has_data == 0: 'parent' is the node it aliases.
 */
struct sound_seg {
    struct sound_seg *next;
    union {
        struct sound_seg *parent;   /* has_data == 0, non-header */
        int16_t *data;              /* has_data == 1              */
        struct ss_ctx *owner;       /* header node only           */
    };
    unsigned int has_data  : 1;
    unsigned long long len : 63;
};

/*
 * A context owns a set of tracks. Everything that used to be process
 * global lives here, so two independent users of the library in one
 * process cannot see or disturb each other's tracks.
 */
struct ss_ctx {
    struct sound_seg **headers;
    size_t len;
    size_t cap;        /* grow geometrically instead of on every add */
};

/* Registry maintenance. add() returns false if it could not grow. */
bool tr_registry_add(struct ss_ctx *ctx, struct sound_seg *seg);
void tr_registry_remove(struct ss_ctx *ctx, struct sound_seg *seg);

/* These functions are moved here for better modularity. */

/* Checks if 'parent' is referenced by any node in this context. */
bool tr_find_child(struct ss_ctx *ctx, struct sound_seg *parent);

/* Hand ownership of 'dying's samples to one of the nodes that alias it,
 * and re-point the remaining aliases at the new owner. Call before
 * freeing a data-owning node so references never dangle.
 * Returns true if ownership was transferred (so the caller must NOT
 * free the buffer), false if nobody referenced it. */
void tr_transfer_ownership(struct ss_ctx *ctx, struct sound_seg *dying);

/* Create a node that actually owns new data; link it after 'prev_seg'.
 * Returns NULL if allocation failed. */
struct sound_seg *tr_create_original_node(struct sound_seg *prev_seg,
                                          size_t length);

/* Find the node containing 'pos'; return index within that node and
 * update *header to point to that node. */
int tr_find(struct sound_seg **header, int pos);

/* Climb up to find data for nodes that do not own 'data' directly. */
int16_t *tr_get_data(struct sound_seg *seg);

/* Split one node at 'index' for 'length'. */
void tr_split_one(struct sound_seg *current, int index, int length);

/* Recursively split a node (and children) at 'index' for 'length'. */
void tr_split(struct ss_ctx *ctx, struct sound_seg *track,
              int index, int length);

#endif /* SOUND_SEG_EXTRA_H */
