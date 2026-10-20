# Bundled neural speech

The native speech path uses Qwen3-TTS through the pinned public `llama.cpp`/`libmtmd` libraries. It owns its backbone, audio projector and generation contexts for one speech request; the caller owns shared backend initialization and serializes requests. No HTTP service, API key, prerecorded reply or separate app is involved. The previous eSpeak path remains the fallback when initialization, generation or output validation fails.

`neural-lock.json` pins the 0.6B Base Q4_K_M backbone and matching Q8 projector by repository revision, SHA-256 and byte length. Together they occupy 762,152,800 bytes. The quantized files are published by mradermacher from the official Apache-2.0 Qwen base model; this is distinct from the larger official ggml-org 1.7B conversion used by the upstream example. The bundled model cards preserve that provenance. `scripts/neural-fetch-models.py` verifies downloads before replacing files and stages the license texts and model manifest beside the weights.

Implementation references:

- [llama.cpp TTS example at the pinned revision](https://github.com/ggml-org/llama.cpp/blob/04f7226a2c12c7f382c545a6db5a034266aa53cb/tools/tts/tts.cpp)
- [PSllama speech service at the inspected revision](https://github.com/mansanitizer/psllama/blob/d079717184d8153793789f13ed157c1c70390ba0/app/audio_service.cpp)
- [Official Qwen model](https://huggingface.co/Qwen/Qwen3-TTS-12Hz-0.6B-Base/tree/5d83992436eae1d760afd27aff78a71d676296fc)
- [Exact converted weights](https://huggingface.co/mradermacher/Qwen3-TTS-12Hz-0.6B-Base-GGUF/tree/0762b31e9cd99274d49dca4e895bd81863134f08)

The default voice is the Base model's unconditioned speaker. There is no named-person voice reference or claim that CustomVoice speaker names are supported by this pipeline. The configured language defaults to English. A reference-voice cloning UI is outside this implementation.

Speech text is limited to 2,048 UTF-8 bytes and divided at spaces/sentence boundaries into chunks of at most 240 bytes. No text is silently truncated. Each chunk has a generation-frame limit, and the combined WAV has a 120-second limit. A chunk that fails to reach the model's end-of-speech token discards the incomplete output and returns an explicit error. Completed chunks are joined with 125 ms of silence into an atomically committed PCM16 mono WAV. Model output is validated before writing; an error preserves the previous destination file.

The caller must unload drafting and transcription models before neural synthesis. Libmtmd expands the token embedding table during generation; quantized file sizes alone underestimate peak memory. The wrapper frees the entire neural model set after each request. A small translation unit compiles pinned mtmd unchanged and reads its live generation-projector model/compute allocation accounting. Status reports actual buffer device names and CPU/GPU byte counts. Buffer placement does not claim that every operation runs on that device. Required-GPU mode rejects output before committing its WAV when no live GPU projector allocation exists.

Build with `MUSE_NEURAL_TTS=ON`. `scripts/neural-check.sh` builds and executes the same wrapper inside the Linux Docker toolchain with `MUSE_VULKAN=OFF`. Its checks synthesize real speech, validate PCM format and non-silent output, then release the TTS models and transcribe the resulting WAV with the bundled Whisper model. This establishes host execution and intelligibility evidence, not PS5 runtime performance or subjective voice-quality scores. Native compilation is a separate check. The owner's console remains the only hardware test loop.

## Measured host check

The real-model short check passed in Docker on Opti3 with a one-CPU limit and a 3 GiB memory cap. It generated 74,880 mono PCM16 samples at 24 kHz (3.120 seconds); Whisper transcribed “Your plan is ready. Press cross to approve.” exactly. Loading took 0.983 seconds and synthesis took 21.009 seconds under that CPU limit. Peak process RSS was 2,490,224 KiB (about 2.375 GiB). The earlier 2 GiB run was killed at the container limit while reserving the audio decoder graph; the later successful run is not described as fitting 2 GiB.

Evidence: `logs/neural-short-check.log`, `dist/neural-voice-preview.wav`, and `dist/neural-voice-preview.json`. These CPU figures are not PS5 timings. The native Vulkan path uses the platform allocator and serial model unloading; its actual speed, free-memory margin and sound output need the owner's console. Replies are fully synthesized into a WAV before playback; there is no claimed streaming first-audio latency.

The longer plan test passed with two chunks and 398,520 samples (16.605 seconds). Whisper recovered the complete plan through the final menu instruction. Synthesis took 90.663 seconds at the same one-CPU limit, with peak RSS 2,628,128 KiB (about 2.506 GiB). A forced 12-frame generation limit returned a specific incomplete-audio error and preserved the previous WAV byte-for-byte. Evidence: `logs/neural-long-check.log`.

The combined engine regression also passed: draft/classify, unload the draft and Whisper models, synthesize with Qwen3-TTS, reload Whisper and transcribe, then lazily reload Qwen and generate another draft. Status checks require the neural path and fail if it silently falls back to eSpeak. The separate eSpeak baseline still passes. Evidence: `logs/inference-v2-neural-integration.log` and `logs/inference-v2-baseline-check.log`.

CPU mode supplies an explicit CPU-only device list and disables operation and KV offload, including when it follows a failed Vulkan attempt in the same process. The short real-model regression passed again after this correction: identical 3.120-second output and transcript, 22.539 seconds synthesis, 2,491,184 KiB peak RSS. Evidence: `logs/neural-cpu-fallback-check.log`. That host run verifies the corrected CPU configuration; it does not simulate a PS5 GPU failure.

The live backend accounting check also passed on the CPU host: the projector reported 1,294,147,556 CPU bytes and zero GPU/other bytes, with the exact expected transcript. Requiring GPU allocation on that CPU-only build returned a clear error and preserved the prior WAV byte-for-byte. Evidence: `logs/neural-backend-status-check.log`. The GPU native library and title link/SELF inspection also passed; actual GPU allocation, throughput and audio on the PS5 remain to be measured there.

Build-time upstream check: PSllama HEAD matched the recorded `d079717...` revision. Llama.cpp HEAD was `1b29621bacc6a446bd523fc790316a24a06e886f`; this build deliberately retains the tested `04f7226...` revision from the engine lock. It does not claim to use the newest llama.cpp tip.
