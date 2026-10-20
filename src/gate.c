/* SPDX-License-Identifier: GPL-3.0-or-later */
#include "gate.h"
#include <string.h>

static int token(const char *s) {
    size_t n = s ? strlen(s) : 0;
    if (!n || n > 128) return 0;
    for (size_t i = 0; i < n; ++i)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z') ||
              (s[i] >= '0' && s[i] <= '9') || s[i] == '_' || s[i] == '-')) return 0;
    return 1;
}
void muse_reset(struct muse_gate *g) {
    uint64_t next = g->generation + 1;
    int held = g->x_was_down;
    int voice_verified = g->voice_verified;
    memset(g, 0, sizeof(*g));
    g->generation = next;
    g->x_was_down = held;
    g->voice_verified = voice_verified;
}
int muse_set_voice_verified(struct muse_gate *g, int verified) {
    if (g->state != MUSE_IDLE) return -1;
    g->voice_verified = !!verified;
    ++g->generation;
    return 0;
}
int muse_set_plan(struct muse_gate *g, const char *id, const char *revision,
                  const char *text) {
    if (!token(id) || !token(revision) || !text || !*text || strlen(text) > 4096)
        return -1;
    if (g->state != MUSE_IDLE && g->state != MUSE_PLAN) return -1;
    muse_reset(g);
    strcpy(g->plan_id, id);
    strcpy(g->revision, revision);
    strcpy(g->text, text);
    g->state = MUSE_PLAN;
    /* Force release before approval, even if X opened this screen. */
    g->x_was_down = 1;
    return 0;
}
int muse_pad(struct muse_gate *g, int x_down) {
    int rising = x_down && !g->x_was_down;
    g->x_was_down = !!x_down;
    if (rising) return muse_input(g, MUSE_X);
    return 0;
}
int muse_input(struct muse_gate *g, enum muse_input input) {
    /* Async voice must use muse_voice so its generation is checked. */
    if (input == MUSE_VOICE_APPROVE || input == MUSE_VOICE_DETAIL ||
        input == MUSE_VOICE_CANCEL) return -1;
    if (input == MUSE_CIRCLE) {
        /* The stable API has no cancellation endpoint. Invalidate local results. */
        if (g->state == MUSE_DISPATCHED) return -1;
        muse_reset(g);
        return 0;
    }
    if (g->state != MUSE_PLAN) return -1;
    if (input == MUSE_SQUARE) {
        muse_reset(g);
        return 0;
    }
    if (input == MUSE_X) {
        g->state = MUSE_APPROVING;
        return 1; /* caller sends POST /plan/<id>/approve exactly once */
    }
    return -1;
}
int muse_voice(struct muse_gate *g, uint64_t generation, const char *intent) {
    if (generation != g->generation || g->state != MUSE_PLAN || !intent)
        return -1;
    enum muse_input action;
    if (!strcmp(intent, "approve")) action = MUSE_X;
    else if (!strcmp(intent, "add_detail")) action = MUSE_SQUARE;
    else if (!strcmp(intent, "cancel")) action = MUSE_CIRCLE;
    else return -1; /* No substring matches, explanations or unknown labels. */
    if (!g->voice_verified) return 0; /* advisory until owner hardware verification */
    return muse_input(g, action);
}
static int speech_space(unsigned char c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}
static int explicit_approval(const char *utterance) {
    if (!utterance) return 0;
    char normalized[65]; size_t n = 0; int pending_space = 0;
    for (const unsigned char *p = (const unsigned char *)utterance; *p; ++p) {
        unsigned char c = *p;
        if (speech_space(c)) { if (n) pending_space = 1; continue; }
        if (c >= 128 || c < 32) return 0;
        if (pending_space) { if (n + 1 >= sizeof(normalized)) return 0; normalized[n++] = ' '; pending_space = 0; }
        if (n + 1 >= sizeof(normalized)) return 0;
        normalized[n++] = (char)(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    }
    /* A single affirmative sentence terminator is allowed; '?' is not. */
    if (n && (normalized[n - 1] == '.' || normalized[n - 1] == '!')) --n;
    if (n && normalized[n - 1] == ' ') --n;
    normalized[n] = 0;
    return !strcmp(normalized, "approve") || !strcmp(normalized, "approve this plan") ||
           !strcmp(normalized, "build it");
}
int muse_voice_utterance(struct muse_gate *g, uint64_t generation,
                         const char *intent, const char *utterance) {
    if (generation != g->generation || g->state != MUSE_PLAN || !intent || !utterance)
        return -1;
    if (!strcmp(intent, "approve") && !explicit_approval(utterance)) return 0;
    return muse_voice(g, generation, intent);
}
int muse_job(struct muse_gate *g, uint64_t generation, const char *job) {
    if (generation != g->generation || g->state != MUSE_APPROVING || !token(job))
        return -1;
    strcpy(g->job_id, job);
    g->state = MUSE_BUILDING;
    return 0;
}
int muse_build_done(struct muse_gate *g, uint64_t generation, const char *job) {
    if (generation != g->generation || g->state != MUSE_BUILDING ||
        !job || strcmp(g->job_id, job)) return -1;
    g->state = MUSE_DOWNLOADING;
    return 0;
}
int muse_artifact_verified(struct muse_gate *g, uint64_t generation) {
    if (generation != g->generation || g->state != MUSE_DOWNLOADING) return -1;
    g->state = MUSE_READY;
    return 0;
}
int muse_dispatch(struct muse_gate *g) {
    if (g->state != MUSE_READY) return -1;
    g->state = MUSE_DISPATCHED;
    return 0;
}
void muse_fail(struct muse_gate *g) {
    ++g->generation;
    g->state = MUSE_ERROR;
    g->job_id[0] = 0;
}
