// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef MUSE_NEURAL_TTS_H
#define MUSE_NEURAL_TTS_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *backbone;
    const char *projector;
    const char *language; /* default en; uses the Base model's unconditioned voice */
    int threads;         /* 1..2, default 2 */
    int use_gpu;         /* selects layers/mtmd GPU use; not hardware verification */
    int max_frames;      /* per text chunk, default 360 (30 seconds at 12 Hz) */
    int require_gpu;     /* reject before WAV commit unless live projector has GPU buffers */
} muse_neural_tts_config;
typedef struct {
    int sample_rate;
    int chunks;
    int frames;
    int use_gpu_requested;
    int64_t samples;
    double load_ms;
    double synthesis_ms;
    /* Live projector model/reserved-compute allocation accounting. This is
     * device placement, not a claim that every graph operation used the GPU. */
    uint64_t projector_cpu_bytes;
    uint64_t projector_gpu_bytes;
    uint64_t projector_other_bytes;
    char projector_backends[192];
} muse_neural_tts_stats;

/* Caller initializes the shared llama backend once and serializes inference.
 * Release draft/Whisper contexts first. This owns/releases its own TTS models,
 * leaving shared backend lifetime to the caller. No HTTP or voice reference.
 * Up to 2048 UTF-8 bytes are split into <=240-byte chunks, without truncation.
 * Writes PCM16 mono WAV atomically. On failure the previous output is preserved;
 * error text explains why the caller should use its eSpeak fallback. */
int muse_neural_tts_synthesize(const muse_neural_tts_config *, const char *text,
    const char *wav_path, muse_neural_tts_stats *, char *error, size_t error_size);
#ifdef __cplusplus
}
#endif
#endif
