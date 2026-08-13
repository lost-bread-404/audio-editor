#include <stdlib.h>
#include <string.h>
#include "sound_seg/sound_seg.h"
#include "sound_seg_extra.h"

bool tr_registry_add(struct ss_ctx *ctx, struct sound_seg *seg)
{
    if (ctx->len == ctx->cap) {
        size_t cap = ctx->cap ? ctx->cap * 2 : 4;
        struct sound_seg **grown = realloc(ctx->headers,
                                           cap * sizeof(struct sound_seg *));
        if (!grown) {
            return false;           /* caller keeps the old array intact */
        }
        ctx->headers = grown;
        ctx->cap = cap;
    }
    ctx->headers[ctx->len++] = seg;
    return true;
}

void tr_registry_remove(struct ss_ctx *ctx, struct sound_seg *seg)
{
    for (size_t i = 0; i < ctx->len; i++) {
        if (ctx->headers[i] != seg) {
            continue;
        }
        ctx->headers[i] = ctx->headers[ctx->len - 1];
        ctx->len--;
        return;
    }
}

bool tr_find_child(struct ss_ctx *ctx, struct sound_seg *parent)
{
    for (size_t i = 0; i < ctx->len; i++) {
        struct sound_seg *track = ctx->headers[i];
        while (track->next) {
            track = track->next;
            if (track->parent == parent) {
                return true;
            }
        }
    }
    return false;
}

void tr_transfer_ownership(struct ss_ctx *ctx, struct sound_seg *dying)
{
    struct sound_seg *heir = NULL;
    bool is_ancestor = dying->has_data;
 
    // find all children of dying and transfer their ownership
    for (size_t i = 0; i < ctx->len; i++) {
        struct sound_seg *node = ctx->headers[i];
        while (node->next) {
            node = node->next;
            if (node != dying && !node->has_data && node->parent == dying) {
                if (is_ancestor){
                    // transfer ownership to heir
                    if (!heir){
                        heir = node;
                        heir->has_data = 1;
                        heir->data     = dying->data;
                        dying->has_data = 0;
                        dying->data = NULL;
                    }else{
                        node->parent = heir;
                    }
                }else{
                    // 'dying' is an alias, so it has no samples of its own
                    // to give away; 'node' must be re-aimed at whatever
                    // node actually holds the samples. A single hop is not
                    // enough -- dying->parent may itself be another alias
                    // -- so climb until a data-owning node is reached,
                    // exactly as tr_get_data would.
                    //
                    // That owner may still be a node of the track being
                    // destroyed. That is fine here *only* because destroy
                    // runs this alias pass over the whole chain before the
                    // ownership pass: by the time the owning node is
                    // processed it will see 'node' among its referents and
                    // promote it to the real owner. Running the two passes
                    // in the other order would leave 'node' aimed at memory
                    // that is about to be freed.
                    struct sound_seg *owner = dying->parent;
                    while (owner && !owner->has_data) {
                        owner = owner->parent;
                    }
                    node->parent = owner;
                }
            }
        }
    }
    return;
}

struct sound_seg *tr_create_original_node(struct sound_seg *prev_seg,
                                          size_t length)
{
    /* Refuse a sample count that cannot be turned into a byte count.
     * Callers are expected to have range-checked already; this is here
     * so the multiply below can never overflow regardless of caller. */
    if (length > SIZE_MAX / sizeof(int16_t)) {
        return NULL;
    }
    struct sound_seg *new_node = calloc(1, sizeof(struct sound_seg));
    if (!new_node) {
        return NULL;
    }
    new_node->data = calloc(length, sizeof(int16_t));
    if (!new_node->data) {
        free(new_node);
        return NULL;
    }
    new_node->has_data = 1;
    new_node->len      = length;
    prev_seg->next     = new_node;
    return new_node;
}

int tr_find(struct sound_seg **header, int pos)
{
    if (!header || !(*header)) {
        return -1;
    }

    struct sound_seg *track = *header;
    int accumulated_len = 0;

    while (accumulated_len <= pos && track->next) {
        track = track->next;
        accumulated_len += track->len;
    }
    /* Index inside the node. */
    int index = pos - (accumulated_len - track->len);
    *header = track;
    return index;
}

int16_t *tr_get_data(struct sound_seg *seg)
{
    if (seg->has_data) {
        return seg->data;
    }
    while (!seg->has_data) {
        seg = seg->parent;
    }
    return seg->data;
}

void tr_split_one(struct sound_seg *current, int index, int length)
{
    struct sound_seg *new_node = calloc(1, sizeof(struct sound_seg));
    new_node->len = length;

    if ((unsigned long long)length < current->len - (unsigned long long)index) {
        struct sound_seg *back_node = calloc(1, sizeof(struct sound_seg));
        int back_node_index = index + length;
        back_node->len = current->len - back_node_index;

        if (current->has_data) {
            back_node->data = calloc(back_node->len, sizeof(int16_t));
            memcpy(back_node->data,
                   current->data + back_node_index,
                   back_node->len * sizeof(int16_t));
            back_node->has_data = 1;
        } else {
            back_node->parent   = current->parent->next->next;
            back_node->has_data = 0;
        }
        new_node->next  = back_node;
        back_node->next = current->next;
    } else {
        new_node->next = current->next;
    }

    if (current->has_data) {
        int16_t *orig = current->data;
        current->data = calloc(index, sizeof(int16_t));
        memcpy(current->data, orig, index * sizeof(int16_t));

        new_node->data = calloc(length, sizeof(int16_t));
        memcpy(new_node->data, orig + index, length * sizeof(int16_t));
        new_node->has_data = 1;
        free(orig);
    } else {
        new_node->parent   = current->parent->next;
        new_node->has_data = 0;
    }
    current->len  = index;
    current->next = new_node;
}

void tr_split(struct ss_ctx *ctx, struct sound_seg *track,
              int index, int length)
{
    if (!track || index < 0 || length <= 0) {
        return;
    }
    /* Find a parent node that actually owns data. */
    struct sound_seg *parent = track;
    while (!parent->has_data) {
        parent = parent->parent;
    }

    /* We'll track all relevant parents in a dynamic list. */
    size_t parent_list_len = 0;
    struct sound_seg **parent_list = calloc(1, sizeof(struct sound_seg *));
    parent_list[parent_list_len++] = parent;

    tr_split_one(parent, index, length);

    while (parent_list_len > 0) {
        parent = parent_list[0];
        parent_list_len--;
        memmove(parent_list, parent_list + 1,
                parent_list_len * sizeof(struct sound_seg *));

        for (size_t i = 0; i < ctx->len; i++) {
            struct sound_seg *seg = ctx->headers[i];
            while (seg->next) {
                seg = seg->next;
                if (seg->parent == parent) {
                    tr_split_one(seg, index, length);
                    parent_list_len++;
                    parent_list = realloc(parent_list,
                                          parent_list_len
                                          * sizeof(struct sound_seg *));
                    parent_list[parent_list_len - 1] = seg;
                }
            }
        }
    }
    free(parent_list);
}
