#ifndef SOUND_SEG_H
#define SOUND_SEG_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ *
 * Opaque handle.
 *
 * Users can hold and pass around an ss_track*, but the struct body is
 * only defined inside src/. They cannot read fields, cannot take
 * sizeof(), cannot allocate one on the stack. That is deliberate: it
 * means we can change the internal layout in any future release
 * without breaking a single line of their code.
 * ------------------------------------------------------------------ */
typedef struct sound_seg ss_track;

/* ------------------------------------------------------------------ *
 * Context.
 *
 * A context owns a set of tracks. Two contexts are completely
 * independent: a track in one is invisible to the other, and aliasing
 * checks never cross the boundary. Create one per subsystem, or one
 * per test, so unrelated parts of a program cannot disturb each other.
 *
 * A track remembers which context created it, so only ss_track_create
 * takes a context explicitly. Every other function finds it from the
 * track, which makes it impossible to pass the wrong one.
 * ------------------------------------------------------------------ */
typedef struct ss_ctx ss_ctx;

/* ------------------------------------------------------------------ *
 * Error codes.
 *
 * SS_OK is 0 so `if (rc != SS_OK)` and `if (rc)` both read correctly.
 * Errors are negative so they can never be confused with a valid
 * count. Values are fixed forever; new codes are appended.
 * ------------------------------------------------------------------ */
typedef enum {
    SS_OK               =  0,  /* success                                   */
    SS_ERR_INVALID_ARG  = -1,  /* NULL pointer, or nonsensical argument     */
    SS_ERR_OUT_OF_RANGE = -2,  /* pos/len fall outside the track            */
    SS_ERR_NO_MEMORY    = -3,  /* an allocation failed                      */
    SS_ERR_IO           = -4,  /* file could not be opened / read / written */
    SS_ERR_ALIASED      = -5,  /* range is referenced by another track      */
    SS_ERR_WRONG_CONTEXT= -6   /* the two tracks belong to different ctxs   */
} ss_status;

/* Human-readable name for a status. Never NULL, never needs freeing. */
const char *ss_strerror(ss_status status);

/* ------------------------------------------------------------------ *
 * Lifecycle
 * ------------------------------------------------------------------ */

ss_status ss_ctx_create(ss_ctx **out);

/* Destroys the context and every track still alive inside it, so a
 * leaked track cannot outlive its owner. Safe to call with NULL. */
void ss_ctx_destroy(ss_ctx *ctx);

/* Number of tracks currently alive in this context. */
size_t ss_ctx_track_count(const ss_ctx *ctx);

/* On success writes a new track to *out and returns SS_OK.
 * On failure *out is set to NULL. */
ss_status ss_track_create(ss_ctx *ctx, ss_track **out);

/* Safe to call with NULL. Cannot fail, so it returns void. */
void ss_track_destroy(ss_track *track);

/* Release a buffer the library allocated for you (e.g. from
 * ss_track_identify or ss_wav_load). Do not call free() directly:
 * the library may not share your allocator. */
void ss_free(void *ptr);

/* ------------------------------------------------------------------ *
 * Queries and edits
 *
 * Convention: the object comes first, out-parameters come last.
 * Every fallible function returns ss_status and nothing else.
 * ------------------------------------------------------------------ */

ss_status ss_track_length(ss_track *track, size_t *out_len);

ss_status ss_track_read(ss_track *track, size_t pos, size_t len,
                        int16_t *dest);

ss_status ss_track_write(ss_track *track, size_t pos, size_t len,
                         const int16_t *src);

// refuse if the track to be deleted is a parent and has child
ss_status ss_track_delete(ss_track *track, size_t pos, size_t len);

ss_status ss_track_insert(ss_track *dest, size_t destpos,
                          ss_track *src, size_t srcpos, size_t len);

/* Finds every span of `target` that correlates >= 95% with `ad`.
 * Writes a newly allocated "start,end\n..." string to *out_matches
 * (empty string if there are no matches). Release it with ss_free. */
ss_status ss_track_identify(ss_track *target, ss_track *ad,
                            char **out_matches);

/* ------------------------------------------------------------------ *
 * WAV I/O (16-bit mono PCM)
 * ------------------------------------------------------------------ */

/* Reads the whole file. Writes a newly allocated sample buffer to
 * *out_samples and its length to *out_count. Release with ss_free. */
ss_status ss_wav_load(const char *path, int16_t **out_samples,
                      size_t *out_count);

ss_status ss_wav_save(const char *path, const int16_t *samples,
                      size_t count);

#ifdef __cplusplus
}
#endif

#endif /* SOUND_SEG_H */
