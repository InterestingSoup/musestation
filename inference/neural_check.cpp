// SPDX-License-Identifier: GPL-3.0-or-later
// Linux Docker execution evidence only; uses the exact native TTS wrapper.
#include "muse_neural_tts.h"
#include "llama.h"
#include "whisper.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <cmath>
#include <algorithm>
#include <sys/resource.h>
#include <cstdlib>
#include <fstream>
#include <iterator>
static unsigned u32(const unsigned char *p) {
    return unsigned(p[0]) | unsigned(p[1])<<8 | unsigned(p[2])<<16 | unsigned(p[3])<<24;
}
int main(int argc, char **argv) {
    if (argc < 5 || argc > 6) {
        std::fprintf(stderr, "usage: neural-check backbone.gguf projector.gguf whisper.bin out.wav [text]\n"); return 2;
    }
    const char *text = argc == 6 ? argv[5] : "Your plan is ready. Press cross to approve.";
    muse_neural_tts_config config{argv[1], argv[2], "en", 2, 0, 360};
    muse_neural_tts_stats stats{}; char error[512]{};
    // Invalid inputs fail before touching models/output; backend is not initialized.
    std::string oversized(2049, 'x');
    if (!muse_neural_tts_synthesize(&config, oversized.c_str(), argv[4], &stats, error, sizeof(error))
        || !std::strstr(error, "2048")) return 3;
    if (!muse_neural_tts_synthesize(nullptr, text, argv[4], &stats, error, sizeof(error))) return 3;
    llama_backend_init();
    int rc = muse_neural_tts_synthesize(&config, text, argv[4], &stats, error, sizeof(error));
    llama_backend_free();
    if (rc) { std::fprintf(stderr, "NEURAL ERROR: %s\n", error); return 1; }
    rusage usage{}; getrusage(RUSAGE_SELF, &usage);
    std::printf("NEURAL_PEAK_RSS_KIB: %ld\n", usage.ru_maxrss);
    std::printf("NEURAL: rate=%d chunks=%d frames=%d samples=%lld load_ms=%.3f synthesis_ms=%.3f gpu_requested=%d\n",
        stats.sample_rate, stats.chunks, stats.frames, (long long)stats.samples,
        stats.load_ms, stats.synthesis_ms, stats.use_gpu_requested);
    std::printf("PROJECTOR BUFFERS: backends=%s cpu=%llu gpu=%llu other=%llu bytes\n",
        stats.projector_backends, (unsigned long long)stats.projector_cpu_bytes,
        (unsigned long long)stats.projector_gpu_bytes, (unsigned long long)stats.projector_other_bytes);
    if (!stats.projector_cpu_bytes || stats.projector_gpu_bytes || stats.projector_other_bytes
        || !std::strstr(stats.projector_backends, "CPU")) return 8;
    FILE *file = std::fopen(argv[4], "rb");
    unsigned char h[44];
    if (!file || std::fread(h,1,44,file) != 44) return 4;
    const unsigned rate = u32(h+24), bytes = u32(h+40);
    if (rate != unsigned(stats.sample_rate) || bytes != stats.samples*2 || bytes > 48000*120*2) return 4;
    std::vector<int16_t> pcm(bytes/2);
    if (std::fread(pcm.data(),2,pcm.size(),file) != pcm.size()) return 4;
    std::fclose(file);
    double squares = 0; for (auto x : pcm) squares += double(x)*x;
    double rms = std::sqrt(squares/pcm.size());
    if (!std::isfinite(rms) || rms < 20) { std::fprintf(stderr, "silent neural speech\n"); return 4; }
    std::printf("WAV: duration=%.3f rms=%.3f\n", pcm.size()/double(rate), rms);
    // TTS has released its models before Whisper loads, matching native serial scheduling.
    auto wp = whisper_context_default_params(); wp.use_gpu = false;
    auto *whisper = whisper_init_from_file_with_params(argv[3], wp);
    if (!whisper) return 5;
    std::vector<float> input(pcm.size()*16000/rate);
    for (size_t i=0;i<input.size();++i) {
        double pos=i*double(rate)/16000; size_t j=size_t(pos); float f=pos-j;
        input[i]=(pcm[j]*(1-f)+pcm[std::min(j+1,pcm.size()-1)]*f)/32768.0f;
    }
    auto params=whisper_full_default_params(WHISPER_SAMPLING_GREEDY);
    params.n_threads=2; params.language="en"; params.no_context=true;
    params.print_progress=params.print_realtime=params.print_timestamps=false;
    rc=whisper_full(whisper,params,input.data(),int(input.size()));
    std::string transcript;
    if (!rc) for(int i=0;i<whisper_full_n_segments(whisper);++i) transcript+=whisper_full_get_segment_text(whisper,i);
    whisper_free(whisper);
    std::printf("TRANSCRIPT: %s\n",transcript.c_str());
    if (rc || transcript.empty()) return 5;
    if (argc==5 && (transcript.find("plan")==std::string::npos || transcript.find("approve")==std::string::npos)) return 6;
    if (std::getenv("MUSE_NEURAL_FAILURE_CHECK")) {
        auto read = [&]() {
            std::ifstream f(argv[4], std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        };
        const std::string before = read();
        config.max_frames = 12;
        llama_backend_init();
        rc = muse_neural_tts_synthesize(&config, "Your plan is ready. Press cross to approve.",
            argv[4], &stats, error, sizeof(error));
        llama_backend_free();
        if (!rc || !std::strstr(error, "frame limit") || before.empty() || before != read()) {
            std::fprintf(stderr, "cap failure did not preserve previous output: %s\n", error); return 7;
        }
        std::printf("FAILURE PATH: %s; previous WAV unchanged\n", error);
    }
    if (std::getenv("MUSE_NEURAL_REQUIRE_GPU_CHECK")) {
        auto read = [&]() {
            std::ifstream f(argv[4], std::ios::binary);
            return std::string(std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>());
        };
        const std::string before = read();
        config.max_frames = 360;
        config.require_gpu = 1;
        llama_backend_init();
        rc = muse_neural_tts_synthesize(&config, "Your plan is ready. Press cross to approve.",
            argv[4], &stats, error, sizeof(error));
        llama_backend_free();
        if (!rc || !std::strstr(error, "no live GPU projector buffers") || before.empty() || before != read()) {
            std::fprintf(stderr, "GPU requirement did not preserve previous output: %s\n", error); return 9;
        }
        std::printf("REQUIRED GPU: %s; previous WAV unchanged\n", error);
    }
    std::puts("PASS: actual neural synthesis, non-silent PCM WAV and local Whisper transcription");
    return 0;
}
