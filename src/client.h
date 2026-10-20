/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MUSE_CLIENT_H
#define MUSE_CLIENT_H
#include "config.h"
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define MUSE_MAX_ARTIFACT (128u * 1024u * 1024u)
#define MUSE_MAX_PACKAGE (1024ull * 1024ull * 1024ull)
#define MUSE_CLIENT_PENDING 1
#define MUSE_CLIENT_NO_PENDING 2
struct muse_client { struct muse_config config; char error[256]; int http_status; };
struct muse_plan { char id[129], revision[129], text[4097], transcript[4097], reply_wav[512], detail[1024]; };
struct muse_build_status {
    char state[32], detail[1024], sha256[65], kind[16];
    char title_id[10], content_id[48];
    uint64_t size;
};
void muse_client_init(struct muse_client *c, const struct muse_config *config);
/* Blocking bounded calls: run on a worker, never rendering/input thread.
 * Voice/transcript/plan return 0 for a ready plan, MUSE_CLIENT_PENDING for
 * HTTP 202 planning or an ambiguous POST with a durable request ID
 * (id/detail only, never approvable), or -1 on failure. */
int muse_client_voice(struct muse_client *c, const char *wav_path, struct muse_plan *plan);
/* Relay JSON contract: client_request_id + exact body checkpointed before POST. */
int muse_client_transcript(struct muse_client *c, const char *transcript, const char *draft, struct muse_plan *plan);
/* Both parent identifiers must be present or both empty. */
int muse_client_transcript_context(struct muse_client *c, const char *transcript, const char *draft,
    const char *parent_plan_id, const char *parent_revision, struct muse_plan *plan);
int muse_client_plan(struct muse_client *c, const char *id, struct muse_plan *plan);
/* GET first; replay only an exact, durable client-chosen request after HTTP 404. */
int muse_client_plan_resume(struct muse_client *c, const char *id, struct muse_plan *plan);
/* Single unapproved request recovery record, bound to server_url. No HTTP.
 * Load returns PENDING, NO_PENDING, or -1. Clear id="" clears the current record;
 * a nonempty id only clears that matching record. Local mode never reads it. */
int muse_client_pending_load(struct muse_client *c, struct muse_plan *plan);
int muse_client_pending_clear(struct muse_client *c, const char *id);
/* Call only once gate enters MUSE_APPROVING. Fetches a fresh, bounded console-local
 * ShadowMount inventory before every approval POST; unavailable/invalid inventory
 * fails closed. Stable revision acts as idempotency key. */
int muse_client_approve(struct muse_client *c, const char *id, const char *revision, char job_id[129]);
int muse_client_status(struct muse_client *c, const char *job_id, struct muse_build_status *status);
/* Requires done state + exact SHA256/length and ELF or package manifest; saves atomically in artifact_dir. */
int muse_client_artifact(struct muse_client *c, const char *job_id, const struct muse_build_status *manifest, char *out_path, size_t out_capacity);
/* FIH package verification binds the embedded CNT content identity to the manifest. */
int muse_client_verify_package(struct muse_client *c, const char *path, const struct muse_build_status *manifest);
/* Validate ELF again, send to configured console_ip:loader_port; no runtime acknowledgement exists. */
int muse_client_dispatch(struct muse_client *c, const char *elf_path);
int muse_client_logs(struct muse_client *c, const char *jsonl_path);
/* OpenAI-compatible local PS5LM server: responses are bounded, no UI/provider switching. */
int muse_client_draft(struct muse_client *c, const char *transcript, char draft[4097]);
int muse_client_classify(struct muse_client *c, const char *utterance, char intent[16]);
/* Local speech companion /inference multipart WAV (whisper.cpp-compatible). */
int muse_client_transcribe(struct muse_client *c, const char *wav_path, char transcript[4097]);
/* Shared relay transport: credentials only for the configured relay origin.
 * HTTP only, no redirects. Output malloc-owned. PS5LM/speech adapters use the
 * internal credential-free transport even when configured on the same origin. */
int muse_client_http(struct muse_client *c, const char *origin, const char *method, const char *path,
                     const char *content_type, const void *body, size_t length,
                     size_t max_response, unsigned char **response, size_t *response_length);
#ifdef __cplusplus
}
#endif
#endif
