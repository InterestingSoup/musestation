// SPDX-License-Identifier: GPL-3.0-or-later
// Public libmtmd generation, following PSllama audio_service.cpp at
// d079717184d8153793789f13ed157c1c70390ba0 and llama.cpp's pinned tts example.
#include "muse_neural_tts.h"
#include "llama.h"
#include "mtmd.h"
#include "mtmd-helper.h"
#include "ggml-cpu.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <unistd.h>
#include <cerrno>
#include <sys/stat.h>
#include <fcntl.h>

extern "C" int muse_mtmd_generation_buffers(const mtmd_context *, muse_neural_tts_stats *);

namespace {
using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
uint32_t u32(const unsigned char *p) {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
void put16(unsigned char *p, unsigned value) { p[0] = value; p[1] = value >> 8; }
void put32(unsigned char *p, uint32_t value) {
    for (int i = 0; i < 4; ++i) p[i] = value >> (8*i);
}
int fail(char *error, size_t cap, const char *message) {
    if (error && cap) std::snprintf(error, cap, "%s", message);
    return -1;
}
struct Resources {
    llama_model *model = nullptr;
    llama_context *context = nullptr;
    mtmd_context *mtmd = nullptr;
    ggml_threadpool_t pool = nullptr;
    ~Resources() {
        if (mtmd) mtmd_free(mtmd);
        if (context) llama_free(context);
        if (pool) ggml_threadpool_free(pool);
        if (model) llama_model_free(model);
    }
};
struct Output {
    std::string temporary;
    FILE *file = nullptr;
    explicit Output(const char *path) : temporary(std::string(path) + ".neural.part") {
        file = std::fopen(temporary.c_str(), "wb+");
        if (!file) throw std::runtime_error("cannot create neural speech WAV");
        unsigned char header[44] = {};
        if (std::fwrite(header, 1, sizeof(header), file) != sizeof(header)) {
            std::fclose(file); file = nullptr; std::remove(temporary.c_str());
            throw std::runtime_error("neural speech WAV header write failed");
        }
    }
    ~Output() { if (file) std::fclose(file); std::remove(temporary.c_str()); }
    void write(const void *data, size_t size) {
        if (std::fwrite(data, 1, size, file) != size) throw std::runtime_error("neural speech WAV write failed");
    }
    void commit(const char *path, int rate, int64_t samples) {
        unsigned char h[44] = {};
        std::memcpy(h, "RIFF", 4); put32(h + 4, 36 + uint32_t(samples * 2));
        std::memcpy(h + 8, "WAVEfmt ", 8); put32(h + 16, 16);
        put16(h + 20, 1); put16(h + 22, 1); put32(h + 24, rate);
        put32(h + 28, rate * 2); put16(h + 32, 2); put16(h + 34, 16);
        std::memcpy(h + 36, "data", 4); put32(h + 40, uint32_t(samples * 2));
        if (std::fseek(file, 0, SEEK_SET)) throw std::runtime_error("neural WAV header seek failed");
        write(h, sizeof(h));
        bool ok = std::fflush(file) == 0 && fsync(fileno(file)) == 0;
        if (std::fclose(file)) ok = false;
        file = nullptr;
        if (!ok || std::rename(temporary.c_str(), path)) throw std::runtime_error("cannot commit neural speech WAV");
    }
};

// Exact-size read: on the PS5 title filesystem a stdio read to end-of-file
// reports an error, so never read "until EOF" there.
bool read_whole(const char *path, std::vector<unsigned char> &out) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    struct stat st{};
    bool ok = !fstat(fd, &st) && st.st_size > 44 && st.st_size < 16 * 1024 * 1024;
    out.assign(ok ? size_t(st.st_size) : 0, 0);
    size_t used = 0;
    while (ok && used < out.size()) {
        ssize_t got = read(fd, out.data() + used, out.size() - used);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) ok = false; else used += size_t(got);
    }
    close(fd);
    return ok;
}
bool write_whole(const std::string &path, const char *data, size_t bytes) {
    const std::string part = path + ".part";
    FILE *f = std::fopen(part.c_str(), "wb");
    if (!f) return false;
    bool ok = std::fwrite(data, 1, bytes, f) == bytes;
    if (std::fclose(f)) ok = false;
    if (!ok || std::rename(part.c_str(), path.c_str())) { std::remove(part.c_str()); return false; }
    return true;
}
using voice_ptr = std::unique_ptr<mtmd_bitmap, decltype(&mtmd_bitmap_free)>;
voice_ptr voice_from(const mtmd_context *ctx, const unsigned char *data, size_t bytes) {
    return voice_ptr(mtmd_helper_bitmap_init_from_buf(ctx, data, bytes, false, mtmd_helper_init_opt_default()).bitmap,
                     mtmd_bitmap_free);
}
std::vector<std::string> chunks(const char *input) {
    std::string text(input);
    if (text.empty() || text.size() > 2048) throw std::runtime_error("neural speech text must contain 1..2048 UTF-8 bytes");
    for (char &c : text) {
        if (c == '\n' || c == '\r' || c == '\t') c = ' ';
        else if (static_cast<unsigned char>(c) < 32) throw std::runtime_error("neural speech text contains control characters");
    }
    std::vector<std::string> out;
    size_t pos = 0;
    while (pos < text.size()) {
        while (pos < text.size() && text[pos] == ' ') ++pos;
        if (pos == text.size()) break;
        size_t end = std::min(pos + 240, text.size());
        if (end < text.size()) {
            size_t boundary = text.rfind(' ', end);
            if (boundary == std::string::npos || boundary <= pos)
                throw std::runtime_error("neural speech contains a word longer than the chunk limit");
            end = boundary;
            // Prefer a full sentence when it does not create tiny chunks.
            for (size_t i = end; i > pos + 80; --i)
                if (text[i-1] == '.' || text[i-1] == '!' || text[i-1] == '?') { end = i; break; }
        }
        out.emplace_back(text.substr(pos, end-pos));
        pos = end;
    }
    if (out.empty()) throw std::runtime_error("neural speech text is blank");
    return out;
}
void append_wav(Output &out, const char *wav, size_t bytes, int rate, int64_t samples) {
    const auto *p = reinterpret_cast<const unsigned char *>(wav);
    if (!p || bytes < 44 || std::memcmp(p, "RIFF", 4) || std::memcmp(p+8, "WAVEfmt ", 8)
        || u32(p+16) != 16 || p[20] != 1 || p[21] || p[22] != 1 || p[23]
        || p[34] != 16 || p[35] || u32(p+24) != uint32_t(rate)
        || std::memcmp(p+36, "data", 4) || samples < 1
        || u32(p+40) != uint64_t(samples)*2 || bytes != 44 + uint64_t(samples)*2)
        throw std::runtime_error("neural decoder returned invalid PCM16 mono WAV");
    out.write(p+44, bytes-44);
}
}

extern "C" int muse_neural_tts_synthesize(const muse_neural_tts_config *cfg, const char *text,
    const char *path, muse_neural_tts_stats *stats, char *error, size_t cap) {
    if (error && cap) error[0] = 0;
    if (stats) *stats = {};
    if (!cfg || !cfg->backbone || !*cfg->backbone || !cfg->projector || !*cfg->projector
        || !text || !path || !*path) return fail(error, cap, "neural speech configuration is incomplete");
    try {
        auto parts = chunks(text);
        Resources r;
        muse_neural_tts_stats result{};
        result.use_gpu_requested = !!cfg->use_gpu;
        const auto started = Clock::now();
        const int threads = cfg->threads > 0 ? std::clamp(cfg->threads, 1, 2) : 2;
        const int limit = cfg->max_frames > 0 ? std::clamp(cfg->max_frames, 12, 480) : 360;
        auto mp = llama_model_default_params();
        // A CPU retry may follow an initialized Vulkan backend. Zero GPU layers
        // alone does not disable opportunistic operation/KV offload.
        ggml_backend_dev_t cpu_only[] = { nullptr };
        if (!cfg->use_gpu) mp.devices = cpu_only;
        mp.n_gpu_layers = cfg->use_gpu ? 999 : 0;
        mp.load_mode = LLAMA_LOAD_MODE_NONE;
        r.model = llama_model_load_from_file(cfg->backbone, mp);
        if (!r.model) throw std::runtime_error("neural speech backbone could not be loaded");
        auto cp = llama_context_default_params();
        cp.n_ctx = 1024; cp.n_batch = cp.n_ubatch = 64;
        cp.n_threads = cp.n_threads_batch = threads;
        cp.embeddings = true;
        cp.offload_kqv = cp.op_offload = !!cfg->use_gpu;
        r.context = llama_init_from_model(r.model, cp);
        if (!r.context) throw std::runtime_error("neural speech backbone context could not be created");
        auto tp = ggml_threadpool_params_default(threads); tp.poll = 0;
        r.pool = ggml_threadpool_new(&tp);
        if (!r.pool) throw std::runtime_error("neural speech thread pool could not be created");
        llama_attach_threadpool(r.context, r.pool, r.pool);
        auto pp = mtmd_context_params_default();
        pp.use_gpu = !!cfg->use_gpu; pp.n_threads = threads;
        pp.print_timings = false; pp.warmup = false;
        r.mtmd = mtmd_init_from_file(cfg->projector, r.model, pp);
        if (!r.mtmd || mtmd_gen_audio_get_info(r.mtmd).type != MTMD_GEN_AUDIO_TYPE_QWEN3TTS)
            throw std::runtime_error("neural speech projector is missing or not Qwen3-TTS");
        result.load_ms = elapsed(started);
        const auto synthesis = Clock::now();
        Output output(path);
        const bool want_voice = cfg->voice_ref && *cfg->voice_ref;
        voice_ptr voice(nullptr, mtmd_bitmap_free);
        bool voice_unsupported = false;
        if (want_voice) {
            std::vector<unsigned char> saved;
            if (read_whole(cfg->voice_ref, saved)) voice = voice_from(r.mtmd, saved.data(), saved.size());
            std::fprintf(stderr, "muse_tts voice_ref=%s loaded=%d\n", cfg->voice_ref, voice ? 1 : 0);
        }
        for (const auto &part : parts) {
            mtmd_helper_gen_audio_inp input{};
            input.prompt = part.c_str(); input.prompt_len = part.size();
            input.lang = cfg->language && *cfg->language ? cfg->language : "en";
            input.top_k = 50; input.top_p = 0.95f; input.seed = 1234;
            input.out_type = MTMD_HELPER_GEN_AUDIO_OUTTYPE_WAV;
            input.speaker_ref = voice.get();
            std::unique_ptr<mtmd_helper::gen_audio> generator;
            for (;;) {
                llama_memory_clear(llama_get_memory(r.context), true);
                generator = std::make_unique<mtmd_helper::gen_audio>(r.context, r.mtmd);
                if (!generator->set_input(&input)) break;
                if (!input.speaker_ref) throw std::runtime_error("neural speech input/language rejected");
                // This projector cannot encode a reference voice: continue unconditioned.
                std::fprintf(stderr, "muse_tts voice_ref rejected by projector; unconditioned voice\n");
                voice.reset(); voice_unsupported = true; input.speaker_ref = nullptr;
            }
            auto &gen = *generator;
            for (;;) {
                int left = gen.step_prompt(64);
                if (left < 0) throw std::runtime_error("neural speech prompt evaluation failed");
                if (!left) break;
            }
            std::unique_ptr<llama_sampler, decltype(&llama_sampler_free)> sampler(
                llama_sampler_chain_init(llama_sampler_chain_default_params()), llama_sampler_free);
            if (!sampler) throw std::runtime_error("neural speech sampler allocation failed");
            llama_sampler_chain_add(sampler.get(), llama_sampler_init_top_k(50));
            llama_sampler_chain_add(sampler.get(), llama_sampler_init_top_p(0.95f, 1));
            llama_sampler_chain_add(sampler.get(), llama_sampler_init_temp(0.9f));
            llama_sampler_chain_add(sampler.get(), llama_sampler_init_dist(1234));
            llama_token token = llama_sampler_sample(sampler.get(), r.context, -1);
            const float *hidden = llama_get_embeddings_ith(r.context, -1);
            if (!hidden) throw std::runtime_error("neural backbone returned no hidden state");
            bool stopped = false;
            int frames = 0;
            while (!stopped && frames < limit) {
                const float *next = nullptr;
                if (gen.step_gen(token, hidden, &next, &stopped))
                    throw std::runtime_error("neural speech frame generation failed");
                if (!next) break;
                ++frames;
                hidden = next;
                token = llama_sampler_sample(sampler.get(), r.context, -1);
            }
            if (!stopped) throw std::runtime_error("neural speech reached its frame limit; incomplete audio discarded");
            int32_t rate = 0; const char *wav = nullptr; size_t bytes = 0; int64_t samples = 0;
            if (gen.get_output(&rate, &wav, &bytes, &samples) || rate < 8000 || rate > 48000)
                throw std::runtime_error("neural speech decoder produced no valid audio");
            if (result.sample_rate && result.sample_rate != rate) throw std::runtime_error("neural speech sample rate changed between chunks");
            result.sample_rate = rate;
            if (result.samples + samples + (result.chunks ? rate/8 : 0) > int64_t(rate)*120)
                throw std::runtime_error("neural speech exceeds 120-second output limit");
            if (result.chunks) {
                std::vector<int16_t> gap(size_t(rate)/8, 0);
                output.write(gap.data(), gap.size()*2); result.samples += gap.size();
            }
            append_wav(output, wav, bytes, rate, samples);
            result.samples += samples; result.frames += frames; ++result.chunks;
            if (want_voice && !voice && !voice_unsupported && samples >= int64_t(rate) * 3 / 2) {
                // Keep this voice for the rest of the reply and for later replies.
                if (write_whole(cfg->voice_ref, wav, bytes))
                    voice = voice_from(r.mtmd, reinterpret_cast<const unsigned char *>(wav), bytes);
                std::fprintf(stderr, "muse_tts voice_ref saved=%d\n", voice ? 1 : 0);
            }
            if (cfg->on_chunk) {
                const std::string piece = std::string(path) + ".chunk" + std::to_string(result.chunks) + ".wav";
                if (write_whole(piece, wav, bytes)) cfg->on_chunk(cfg->chunk_context, piece.c_str(), result.chunks);
            }
        }
        if (muse_mtmd_generation_buffers(r.mtmd, &result))
            std::snprintf(result.projector_backends, sizeof(result.projector_backends), "unreported");
        if (cfg->require_gpu && !result.projector_gpu_bytes)
            throw std::runtime_error("Vulkan speech required but no live GPU projector buffers were allocated");
        output.commit(path, result.sample_rate, result.samples);
        result.synthesis_ms = elapsed(synthesis);
        if (stats) *stats = result;
        return 0;
    } catch (const std::exception &ex) { return fail(error, cap, ex.what()); }
      catch (...) { return fail(error, cap, "neural speech synthesis exception"); }
}
