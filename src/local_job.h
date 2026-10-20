/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MUSE_LOCAL_JOB_H
#define MUSE_LOCAL_JOB_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifndef MUSE_LOCAL_SOURCE_MAX
#define MUSE_LOCAL_SOURCE_MAX 65536u
#endif
#define MUSE_LOCAL_JOBS_MAX 64
#define MUSE_LOCAL_ATTEMPTS_MAX 512u
enum muse_local_state {
    MUSE_LOCAL_AWAITING_APPROVAL, MUSE_LOCAL_QUEUED, MUSE_LOCAL_GENERATING,
    MUSE_LOCAL_VALIDATING, MUSE_LOCAL_READY, MUSE_LOCAL_PAUSED,
    MUSE_LOCAL_INTERRUPTED, MUSE_LOCAL_FAILED, MUSE_LOCAL_CANCELLED
};
enum muse_local_phase { MUSE_LOCAL_GENERATE, MUSE_LOCAL_VALIDATE };
struct muse_local_job {
    char id[33], title[96], request[4097], plan[4097];
    char revision[65], approved_revision[65], candidate_sha[65], validated_sha[65];
    char detail[1024];
    enum muse_local_state state;
    enum muse_local_phase next_phase;
    uint64_t sequence, created_at, updated_at;
    uint32_t attempts, max_attempts, seconds_used, max_seconds, reserved_seconds;
};
struct muse_local_store { char root[1024], error[256]; int lock_fd; };
/* All calls serialized on the worker. Checkpoints use two checksum-protected
 * atomic slots. Source revisions are separate, immutable, hash-checked files.
 * Only revisions referenced by either slot are retained (up to4 canonical files,
 * plus one in flight); unreferenced source/temp files are pruned before work. */
int muse_local_open(struct muse_local_store *, const char *root);
void muse_local_close(struct muse_local_store *);
int muse_local_create(struct muse_local_store *, const char *title,
    const char *request, const char *plan, unsigned max_attempts,
    unsigned max_seconds, struct muse_local_job *);
int muse_local_list(struct muse_local_store *, struct muse_local_job *, size_t capacity,
                    size_t *count);
/* recover=1 converts queued/active work into INTERRUPTED and persists it.
 * Reserved step time stays charged after a crash. Never runs or approves work. */
int muse_local_load(struct muse_local_store *, const char *id,
                    struct muse_local_job *, int recover);
int muse_local_approve(struct muse_local_store *, struct muse_local_job *,
                       const char *displayed_revision);
int muse_local_resume(struct muse_local_store *, struct muse_local_job *);
int muse_local_pause(struct muse_local_store *, struct muse_local_job *, int cancel);
/* Charge a bounded step before performing it; max1800seconds. The caller must
 * honor that deadline and cooperatively observe its independent pause flag. */
int muse_local_begin_step(struct muse_local_store *, struct muse_local_job *,
                          unsigned requested_seconds, unsigned *allowed_seconds);
/* Save partial generation without refunding its durable budget reservation. */
int muse_local_checkpoint_source(struct muse_local_store *, struct muse_local_job *,
    const char *source, size_t length);
/* Finishes one active step; optional source is saved as an immutable candidate.
 * READY is permitted only after VALIDATING the current candidate. No auto-run. */
int muse_local_finish_step(struct muse_local_store *, struct muse_local_job *,
    enum muse_local_state state, enum muse_local_phase next_phase,
    const char *source, size_t source_length, const char *detail,
    uint64_t elapsed_ms);
int muse_local_read_source(struct muse_local_store *, const struct muse_local_job *,
    int validated_only, char *source, size_t capacity, size_t *length);
int muse_local_source_path(struct muse_local_store *, const struct muse_local_job *,
                          int validated_only, char *out, size_t capacity);
const char *muse_local_state_name(enum muse_local_state);
#ifdef __cplusplus
}
#endif
#endif
