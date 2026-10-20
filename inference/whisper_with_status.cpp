// Compile the pinned engine unchanged and expose its actual selected backend.
// A request to use_gpu can silently fall back in upstream Whisper; reporting
// ctx->state's real backends prevents that request being mistaken for success.
#include "vendor/whisper.cpp/src/whisper.cpp"
extern "C" const char *muse_whisper_active_backend(whisper_context *ctx) {
    if (!ctx || !ctx->state) return "unloaded";
    for (auto backend : ctx->state->backends) {
        auto dev = ggml_backend_get_device(backend);
        auto type = ggml_backend_dev_type(dev);
        if (type == GGML_BACKEND_DEVICE_TYPE_GPU || type == GGML_BACKEND_DEVICE_TYPE_IGPU)
            return ggml_backend_name(backend);
    }
    return "CPU";
}
