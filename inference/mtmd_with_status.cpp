// SPDX-License-Identifier: GPL-3.0-or-later
// Compile the pinned MIT mtmd source unchanged, then expose live generation
// projector allocation accounting. The public no-alloc fit probe constructs
// a different context and omits ctx_gen_a, so it cannot provide this evidence.
#include "vendor/llama.cpp/tools/mtmd/mtmd.cpp"
#include "muse_neural_tts.h"
#include <cstdio>

extern "C" int muse_mtmd_generation_buffers(
    const mtmd_context *ctx, muse_neural_tts_stats *stats) {
    if (!ctx || !ctx->ctx_gen_a || !stats) return -1;
    stats->projector_cpu_bytes = stats->projector_gpu_bytes = stats->projector_other_bytes = 0;
    stats->projector_backends[0] = 0;
    size_t used = 0;
    for (const auto &[device, bytes] : clip_get_mem_usage(ctx->ctx_gen_a)) {
        if (!bytes) continue;
        const auto type = device ? ggml_backend_dev_type(device) : GGML_BACKEND_DEVICE_TYPE_ACCEL;
        if (type == GGML_BACKEND_DEVICE_TYPE_GPU || type == GGML_BACKEND_DEVICE_TYPE_IGPU)
            stats->projector_gpu_bytes += bytes;
        else if (type == GGML_BACKEND_DEVICE_TYPE_CPU)
            stats->projector_cpu_bytes += bytes;
        else
            stats->projector_other_bytes += bytes;
        const char *name = device ? ggml_backend_dev_name(device) : "unknown";
        const size_t remaining = sizeof(stats->projector_backends) - used;
        const int n = std::snprintf(stats->projector_backends + used, remaining,
            "%s%s", used ? ", " : "", name ? name : "unknown");
        if (n > 0) used += std::min(size_t(n), remaining - 1);
    }
    return stats->projector_cpu_bytes || stats->projector_gpu_bytes || stats->projector_other_bytes ? 0 : -1;
}
