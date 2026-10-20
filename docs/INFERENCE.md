# Local inference in MuseStation

This implementation links real llama.cpp, whisper.cpp and eSpeak NG engines into
the native title behind `inference/muse_inference.h`. The title calls this C ABI
from one worker thread. It does not depend on a running PS5LM HTTP service and
does not compile any address, API key or Tailscale filter into the runtime.
PS5LM's optional endpoint remains an install-time external-provider setting.

The first packaged baseline used CPU AVX2 inference and eSpeak. The upgrade adds
a real Vulkan build target using pinned PS5 RADV, runtime backend selection and
CPU recovery, plus Qwen3-TTS neural speech with eSpeak fallback. See [GPU
integration](GPU.md) and [neural speech](../inference/NEURAL-SPEECH.md) for the implementations,
source pins and verification limits. Native title graphics remain OpenGL. No
console speed or firmware 12.40 runtime claim follows from these builds.

## Bundled defaults

`inference/lock.json` pins the sources and weights. The model download script
checks both byte count and SHA-256 before moving a file into the package inputs.

| Component | Revision / file | License |
|---|---|---|
| llama.cpp | `04f7226a2c12c7f382c545a6db5a034266aa53cb` (PS5LM's pinned submodule) | MIT |
| whisper.cpp | `d1be6fde11ac6e0407606b4e42fe72d34add8037` (PSllama's documented revision) | MIT |
| eSpeak NG | `4870adfa25b1a32b4361592f1be8a40337c58d6c` / 1.52.0 | GPL-3.0-or-later |
| Plan/intent/code model | `qwen2.5-coder-1.5b-instruct-q4_k_m.gguf`, 1,117,320,768 bytes | Apache-2.0 |
| Speech recognition | `ggml-tiny.en.bin`, 77,704,715 bytes | MIT |
| Neural voice | Qwen3-TTS 0.6B Q4_K_M + Q8 projector, 762,152,800 bytes | Apache-2.0 |

The four release weights total 1,957,178,283 bytes. The sole language model is
the official Qwen2.5-Coder-1.5B-Instruct GGUF, pinned at
`f86cb2c1fa58255f8052cc32aeede1b7482d4361`. Earlier v2 releases used Qwen3.5-0.8B;
that model is preserved for historical evidence but excluded from the current
release title. Neural weights are separately pinned in
`inference/neural-lock.json`. English is the bundled speech language;
other languages require a multilingual Whisper model and corresponding config.
eSpeak NG's voice data is generated from the same pinned source as its engine.
All model and engine licenses are in `inference/licenses` and are copied into
the package. GPL components require corresponding source distribution; the
package must carry the source/source-offer artifacts prepared by the package
build, not just a binary license notice.

Runtime model paths, voice-data parent directory, language and thread count are
passed by the title. A normal package maps these to files under its `/app0`
mount (currently `/app0/assets/inference`); writable recordings and generated
WAVs use the configured data directory.
The engine has no knowledge of a console LAN IP or user-specific paths.

## Real behavior and limits

- `create` selects the backend, initializes eSpeak and loads Qwen and Whisper.
  Those models are released before neural speech and reload lazily on later
  operations to bound concurrent residency. Missing/invalid models fail explicitly; no
  substitute transcript or plan is returned.
- `transcribe` accepts 0.1–60 seconds of mono PCM16, including 48 kHz DualSense
  capture. It rejects near-silence, converts to 16 kHz, and calls Whisper's
  greedy decoder. The application owns capture and fallback routing.
- `draft` generates a short plan with Qwen. The draft is advisory and must be
  submitted to Muse for authoritative review in Hybrid mode before the approval
  gate. In All Local mode the local draft is displayed for explicit user approval;
  there is no server authority or silent network fallback.
- `classify` grammar-constrains generation to approve/detail/cancel/unknown.
  Model approval is additionally vetoed unless the normalized spoken command is
  exactly `approve`, `approve this plan`, or `build it`. Case, whitespace and one
  final period/exclamation point are normalized; question marks, bare yes,
  negations and extra clauses cannot authorize a build. Errors and unrecognized
  output stay unknown. The title must tie the result to
  the exact displayed plan revision and must keep voice approval disabled until
  hardware testing verifies the gate. A language model is not a deterministic
  authorization mechanism; the button fallback remains available.
- `speak` uses the configured Qwen3-TTS backbone/projector when the neural
  component is compiled. It releases draft and recognizer contexts first, then
  reloads them lazily on subsequent requests. Neural failure is recorded before
  eSpeak fallback. Both paths write actual PCM16 mono WAV atomically; the title
  plays the file through AudioOut. No voice cloning is implemented.
- Planning starts with a 2,048-token context and is capped at 140 output
  tokens. Local code generation reserves a token-counted context of up to 8,192
  tokens and at most 3,072 output tokens; subsequent language-model operations
  reuse the most recently selected context size, including after lazy reload.
  Code generation requires an EOG token
  for completion, exposes partial-source checkpoints, and supports cooperative
  pause/cancel between decode batches. The real restricted Lua runtime must
  validate complete source before it becomes playable. See the
  [local-generation bounds and evidence](MUSESTATION-FEASIBILITY.md).
  At most two inference threads are used, and llama's worker pool does
  not spin while idle. No latency claim is made for the user's firmware.
- One inference instance is supported because eSpeak is process-global. Calls
  must be serialized by the UI's worker. An atomic operation guard rejects
  concurrent or reentrant inference/TTS calls. The C++ implementation catches engine
  exceptions before returning through the C ABI.

The original CPU build's libc adapter deliberately supports only C/POSIX locales.
The GPU title uses the pinned platform runtime described in GPU.md. The CPU
compatibility implementation remains available for rebuilding that baseline. It uses the
public SDK's rune table and conversion wrappers, makes C collation explicit,
initializes rune bounds, and returns errors for absent message catalogs and
unsupported locale names. The native engine selects C after eSpeak startup.
Optional subprocess backtraces and timestamp mutation return `ENOSYS`; model
loading itself uses real file reads. These adapters still require console
runtime verification. The bounded multibyte conversion routines are from the
public SDK/Musl with fixes for null state and incomplete conversion preservation.
They passed Linux Docker ASan/UBSan checks for buffer limits, pointer advancement,
null state and invalid characters (`logs/inference-locale-check.log`).

## Build and verification

The current GPU title is built through [the native build recipe](NATIVE-APP.md)
and [the GPU dependency recipe](GPU.md), including the final complete native
link. To rebuild the separate CPU compatibility archives, run these commands on
Opti3 inside the existing Docker toolchain image:

```sh
python3 scripts/inference-fetch-sources.py
python3 scripts/inference-fetch-models.py
MUSE_BUILD_JOBS=1 bash scripts/inference-build-data.sh
MUSE_BUILD_JOBS=1 \
PS5_PAYLOAD_SDK=/work/build/native/muse/.deps/native/ps5-payload-sdk \
  bash scripts/inference-build.sh
```

The native build writes an `archives.txt` list under `inference/build/ps5`.
The final title must link these in a linker archive group, with the native SDK's
C++ imports available. Engine compilation enables exceptions and RTTI; the UI
may retain its own different flags. Whisper deliberately uses llama's ggml
library, following PSllama's integration, instead of linking a second ggml.

`scripts/inference-check.sh` builds and runs a Linux Docker harness for the same
wrappers and model files. The **current sole coder model** passed with Docker
networking disabled and a 3 GiB/one-CPU allocation: actual 2–4 sentence plans,
eight approval/cancel/detail/question/negation cases, neural Qwen3-TTS synthesis,
Whisper transcription of that WAV, silent-input rejection and lazy coder reload
after speech released the language/recognition models. The transcript was
“Your plan is ready. Press cross to approve.” Evidence:
`logs/musestation-coder-voice-regression.log`.

Actual offline code generation, parser-driven repair, a real white-on-white
failure, its bounded repair attempts and independent app checks are recorded in
[MuseStation feasibility and tests](MUSESTATION-FEASIBILITY.md). Source completion,
safe execution and visual/behavioral correctness are separate properties.
Small-model failures remain visible and are never replaced by canned source.

### Historical CPU baseline

The original 0.8B/eSpeak CPU build passed native archive compilation and a Linux
harness on 2026-10-19. Its cold test (load, draft, eight intents, eSpeak, Whisper
and silence rejection) took 11.990 seconds and peaked at 845,284 KiB RSS with a
2 GB container limit. These are historical Opti3 measurements for different
weights and speech, not performance claims for the current release or the PS5.
See `logs/inference-host-metrics.json`, `logs/inference-compile.log`,
`logs/inference-data-build.log`, `logs/inference-model-download.log` and
`logs/inference-runtime-check.log`; its audio is `dist/inference-check.wav`.

That earlier model incorrectly classified “Add a two player mode” and “What
colors will it use?” as approval. This led to the deterministic three-command
veto and shared client guard, both retained with the coder model. Its Tetris
draft also misclassified the game as a platformer. Neither a small test corpus
nor swapping models establishes reliable general classification or planning.
In Hybrid mode Muse reviews plans; in All Local mode the user reviews them.

Native compilation and Linux execution are not console execution. Capture,
playback, GPU stability, memory pressure and firmware compatibility require the
owner's hardware tests.

## Upstream evidence and research verdict

[PS5LM](https://github.com/cobanov/PS5LM/tree/6323ecbf443e257932989a55cf535567697e2624)
provides separate payload and native-title recipes. Its native recipe builds
llama's Vulkan backend against PS5_Vulkan RADV and isolates the OpenGL renderer's
symbols before linking both drivers. Its advertised 207 tokens/s for Qwen0.8B
and 55 tokens/s for Qwen9B are upstream GPU measurements, not this build's speed.

[PSllama's speech implementation](https://github.com/mansanitizer/psllama/blob/d079717184d8153793789f13ed157c1c70390ba0/app/audio_service.cpp)
contains actual Whisper inference and mtmd Qwen3-TTS audio generation. Its
[build recipe](https://github.com/mansanitizer/psllama/blob/d079717184d8153793789f13ed157c1c70390ba0/app/build-gguf-vk-title.sh)
compiles Whisper against the same ggml as llama, with a native RADV driver.
This establishes that local speech is an implemented upstream path. It does
not establish the user's Slim/12.40 compatibility. The upgrade now implements
that mtmd speech path and bundles the 0.6B Base backbone and projector. Real
Linux Docker synthesis, non-silent WAV output, Whisper transcription, multiple
text chunks, and sequential model unload/reload have passed. The Base voice is
unconditioned; it is not voice cloning. See the neural speech report for hashes
and measured host memory.

eSpeak's native compilation is not risk-free simply because it is C. Its data
files, callback behavior, libc imports and the title's audio playback must all
be exercised by the user. CPU speech recognition is likewise compile-verifiable
here, with capture and real-time performance still requiring console tests.
