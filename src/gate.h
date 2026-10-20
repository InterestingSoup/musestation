/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MUSE_GATE_H
#define MUSE_GATE_H
#include <stddef.h>
#include <stdint.h>

enum muse_state { MUSE_IDLE, MUSE_PLAN, MUSE_APPROVING, MUSE_BUILDING,
                  MUSE_DOWNLOADING, MUSE_READY, MUSE_DISPATCHED, MUSE_ERROR };
enum muse_input { MUSE_X, MUSE_SQUARE, MUSE_CIRCLE, MUSE_VOICE_APPROVE,
                  MUSE_VOICE_DETAIL, MUSE_VOICE_CANCEL };
struct muse_gate {
    enum muse_state state;
    char plan_id[129], revision[129], job_id[129];
    char text[4097];
    uint64_t generation;
    int x_was_down;
    int voice_verified;
};
/* Zero-initialize before first use. Serialize all calls on the application thread. */
void muse_reset(struct muse_gate *g);
/* Set only from hardware-validation configuration, never from model output. */
int muse_set_voice_verified(struct muse_gate *g, int verified);
int muse_set_plan(struct muse_gate *g, const char *id, const char *revision,
                  const char *text);
/* Physical approval requires a fresh X rising edge after plan display. */
int muse_pad(struct muse_gate *g, int x_down);
int muse_input(struct muse_gate *g, enum muse_input input);
/* Trusted intent dispatch only. Model output MUST use muse_voice_utterance below.
 * Speech results carry the generation captured when recording began. */
int muse_voice(struct muse_gate *g, uint64_t generation, const char *intent);
/* A model's approve result is vetoed unless the actual spoken transcript is an
 * exact explicit command: approve / approve this plan / build it. Returns 0
 * (advisory, no transition) on an approval veto. Never treats bare yes as approval. */
int muse_voice_utterance(struct muse_gate *g, uint64_t generation,
                         const char *intent, const char *utterance);
/* Every async result is bound to the generation captured when it was sent. */
int muse_job(struct muse_gate *g, uint64_t generation, const char *job);
int muse_build_done(struct muse_gate *g, uint64_t generation, const char *job);
int muse_artifact_verified(struct muse_gate *g, uint64_t generation);
int muse_dispatch(struct muse_gate *g);
void muse_fail(struct muse_gate *g);
#endif
