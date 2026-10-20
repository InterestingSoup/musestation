#ifndef MUSE_INFERENCE_H
#define MUSE_INFERENCE_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Call on one worker thread. No HTTP, embedded endpoint, or API key is used.
 * The title supplies absolute model/data paths read from install configuration.
 * Ordinary calls return 0 on success, -1 on error with error text. Codegen has
 * explicit stop/limit outcomes and retains unfinished source as documented.
 * The caller must never turn an inference failure into approval. Destroy only
 * after the worker has returned; concurrent/reentrant operations are rejected. */
typedef struct muse_inference muse_inference;
enum muse_backend_policy {
    MUSE_BACKEND_AUTO = 0,
    MUSE_BACKEND_CPU = 1,
    MUSE_BACKEND_VULKAN_REQUIRED = 2
};
typedef struct {
    const char *llm_model; /* NULL/empty: voice-only; draft/classify/codegen fail explicitly.
                           * A nonempty missing/invalid path still fails initialization. */
    const char *whisper_model;
    const char *espeak_data; /* parent of espeak-ng-data, e.g. /app0/inference */
    const char *language;   /* default en; bundled Whisper tiny.en is English */
    int threads;           /* clamped 1..2 until console profiling, default 2 */
    enum muse_backend_policy backend_policy;
    const char *cache_dir; /* writable absolute directory; optional */
    const char *neural_backbone; /* optional paired Qwen3-TTS files */
    const char *neural_projector;
} muse_inference_config;

enum muse_local_intent {
    MUSE_INTENT_UNKNOWN = 0,
    MUSE_INTENT_APPROVE = 1,
    MUSE_INTENT_DETAIL = 2,
    MUSE_INTENT_CANCEL = 3
};

/* Voice-only creation retains real Whisper and neural/eSpeak speech; it never
 * provides an implicit language model or a successful placeholder plan. */
muse_inference *muse_inference_create(const muse_inference_config *, char *error, size_t error_size);
void muse_inference_destroy(muse_inference *);
int muse_inference_draft(muse_inference *, const char *request, char *plan, size_t plan_size,
                         char *error, size_t error_size);
enum muse_codegen_result {
    MUSE_CODEGEN_ERROR = -1,
    MUSE_CODEGEN_COMPLETE = 0, /* model reached EOG; runtime validation still required */
    MUSE_CODEGEN_STOPPED = 1,  /* caller requested pause/cancel */
    MUSE_CODEGEN_LIMIT = 2     /* time, tokens, context or output capacity exhausted */
};
typedef struct {
    const char *system_prompt;   /* required runtime/API contract; at most 16384 bytes */
    const char *request;         /* required user request; at most 8192 bytes */
    const char *approved_plan;   /* optional; at most 8192 bytes */
    const char *previous_source; /* optional repair input; at most 65536 bytes */
    const char *diagnostics;     /* optional real validator errors; at most 8192 bytes */
    uint32_t context_tokens;     /* 0 -> 8192; accepted range 2048..8192 */
    uint32_t max_output_tokens;  /* 0 -> 3072; accepted range 1..3072 */
    uint32_t max_duration_ms;    /* 0 -> 120000; accepted range 1..1800000 */
    uint32_t seed;               /* record with job; UINT32_MAX maps to 0, never random */
    int (*should_stop)(void *);  /* nonzero -> stop; callback must not throw/block */
    void (*progress)(void *, const char *piece, size_t bytes, uint32_t generated_tokens);
    void *userdata;
} muse_codegen_request;
typedef struct {
    uint32_t prompt_tokens, generated_tokens, context_tokens;
    uint64_t elapsed_ms;
} muse_codegen_stats;
/* Serial inference-worker call. Progress contains raw byte fragments (possibly
 * split UTF-8), valid only during the callback. Pause/cancel is checked before
 * loading and between prompt/decode batches; an in-flight Vulkan graph cannot
 * be interrupted. max_duration_ms is a cooperative limit, not a hard timeout.
 * source is always terminated when capacity > 0, and preserves partial output
 * on STOPPED/LIMIT/ERROR for repair/checkpoint only. COMPLETE means EOG was seen,
 * never that generated code passed validation or is safe to execute. There is
 * no execution, filesystem write or network request in this call. */
int muse_inference_codegen(muse_inference *, const muse_codegen_request *,
                           char *source, size_t source_capacity, muse_codegen_stats *,
                           char *error, size_t error_capacity);
/* Model approval is vetoed unless the utterance is exactly approve,
 * approve this plan, or build it (case/whitespace/one trailing ./! normalized).
 * Questions, bare yes, negations and additional clauses cannot approve. */
int muse_inference_classify(muse_inference *, const char *utterance, const char *shown_plan,
                            enum muse_local_intent *, char *error, size_t error_size);
/* mono signed 16-bit capture; resamples 48kHz DualSense capture to 16kHz. */
int muse_inference_transcribe(muse_inference *, const int16_t *pcm, size_t frames, int sample_rate,
                              char *text, size_t text_size, char *error, size_t error_size);
/* Writes a RIFF/WAVE PCM16 mono file, atomically; caller plays it through AudioOut. */
int muse_inference_speak(muse_inference *, const char *text, const char *wav_path,
                        char *error, size_t error_size);
const char *muse_inference_backend(void); /* honest runtime capability string */
/* Serial worker use. Copies actual selected backends and any fallback reason.
 * Before inference succeeds, GPU selection is not hardware verification. */
int muse_inference_status(muse_inference *, char *status, size_t status_size);

#ifdef __cplusplus
}
#endif
#endif
