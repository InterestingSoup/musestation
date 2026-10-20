# MuseStation local generation: engineering bounds

The offline route is a generated Lua application executed inside MuseStation's
native title. The title supplies GPU graphics, controller input and the runtime;
the local model produces application source. Source must pass the real runtime's
validation before it becomes playable. These applications are entries in
MuseStation's library, not independently compiled native PS5 packages.

The existing v2 binary, its GPU/voice verification logs and its corresponding
source release remain separate. New local-generation behavior needs its own
compile, execution and hardware evidence. Host tests never establish console
execution or sustained console performance.

## Why a scripting runtime

The public SDK is a cross-compilation toolchain using host clang/lld. Pacbrew's
PS5 LLVM recipe builds libraries with `LLVM_BUILD_TOOLS=NO` and no Clang project;
it is not an on-console compiler distribution. TinyCC supports x86-64 and an
embedding API, but an integrated PS5 compiler, executable-memory path, native
converter and package writer have not been implemented here. The existing
native converter and C# package builder run in the Docker build environment.
Waiting longer cannot supply these missing runtime components.

Primary source: [SDK requirements](https://github.com/ps5-payload-dev/sdk),
[Pacbrew LLVM recipe](https://github.com/ps5-payload-dev/pacbrew-repo/blob/722187f0ee246c0d7d57401c8a97de1c7f4e2ff0/llvm/PKGBUILD),
[TinyCC source](https://github.com/TinyCC/tinycc/blob/e23dabed154a8495f3ac1ff3cc9d04fa1c207893/README).

Pacbrew already contains PS5 Lua and LÖVE build recipes. A small embedded Lua
interpreter can reuse MuseStation's existing native draw/input stack and avoid
another graphics runtime. Lua provides a custom allocator, protected calls,
text-only loading and instruction-count hooks. Instruction hooks count Lua
instructions, not arbitrary time spent in C callbacks: every exposed native
function also needs explicit work and allocation limits. Restricted libraries
and quotas provide application containment; they are not an OS process boundary.

Primary source: [Lua PS5 recipe](https://github.com/ps5-payload-dev/pacbrew-repo/blob/722187f0ee246c0d7d57401c8a97de1c7f4e2ff0/lua/PKGBUILD),
[LÖVE PS5 recipe](https://github.com/ps5-payload-dev/pacbrew-repo/blob/722187f0ee246c0d7d57401c8a97de1c7f4e2ff0/love/PKGBUILD),
[Lua API reference](https://www.lua.org/manual/5.4/manual.html).

## Local model interface

`muse_inference_codegen` accepts the runtime API contract, user request, approved
plan and optional prior source plus actual diagnostics. It tokenizes the complete
prompt with the bundled model's tokenizer. The context is bounded to 2,048–8,192
tokens, output to 3,072 tokens and source to 64 KiB. Model-specific role tokens
are parsed only in the adapter's separators; task/source text cannot inject them.
Sampling records a seed. The release uses the official **Qwen2.5-Coder-1.5B-Instruct
Q4_K_M** GGUF for planning, approval classification and code generation. Its
immutable model revision, byte count and SHA-256 are in `inference/lock.json`
and `inference/codegen-lock.json`; its Apache-2.0 license and model card are
bundled. The original 0.8B model remains in the earlier v2 evidence, and is not
included in this release's title assets.

The adapter reads the pinned model's ChatML template metadata. Qwen2.5 receives
plain ChatML; a template with a thinking block receives the explicit empty
thinking prefix. Code generation with the coder uses temperature 0.7, top-p 0.8,
top-k 20, repetition penalty 1.05 and no presence penalty. These are selected
application settings, not a claim of universal best sampling parameters.
The narrow output grammar requires a letter or underscore after leading
whitespace and excludes literal backticks. This prevents Markdown fences; it
is not a Lua grammar and deliberately excludes leading comments/semicolons.
Every source byte still comes from the model. Four repeated identical long
comment lines produce an explicit repetition failure rather than a playable app.
[Official pinned model card](https://huggingface.co/Qwen/Qwen2.5-Coder-1.5B-Instruct-GGUF/blob/f86cb2c1fa58255f8052cc32aeede1b7482d4361/README.md)

The default context is 8,192 tokens; the requested
output reservation must fit alongside the actual prompt, without silent clipping.

Completion requires the model's end-of-generation token. Exhausting output space,
tokens, context or duration does not become a successful application. Partial
source remains available for a checkpoint or repair request. The adapter never
executes or writes source itself. Its progress callback receives raw byte pieces,
which may split a UTF-8 character. Consumers must assemble them before display.

Pause/cancel is cooperative. The callback is checked before loading and between
prompt/decode batches. Each call defaults to 120 seconds; the job scheduler can
explicitly reserve up to 30 minutes within the remaining durable job budget.
The duration uses a monotonic clock and includes model loading. A currently
running GPU graph or a model-loading call
may finish after that threshold. The pinned llama header explicitly documents
its abort callback as CPU-only, so this is not a promise to interrupt a wedged
GPU or bound wall time exactly. No worker thread is forcibly killed.

The adapter rejects concurrent/reentrant inference and speech operations. It
unloads Whisper while expanding Qwen's context; later voice tasks reload the
recognizer. Existing neural speech continues to release Qwen and Whisper before
loading its own models. Context and models remain owned by the one inference
worker, and destruction must happen only after that worker has stopped.

## Actual host evidence and limits

The earlier 0.8B model's planning/speech tests did not establish code-generation
quality. Real counter attempts exposed invalid Lua, attempts to implement the
supplied graphics API and repetition. Three generated/repair candidates reached
EOG in 26–35 seconds each on one host CPU allocation but failed syntax validation.
The API prompt was clarified, including removal of optional-argument notation
that the model had copied into source. Those failures are retained in the logs.

The 1.5B coder was tested with Docker networking disabled, a 3 GiB memory limit
and one host CPU allocation. The first counter completed in 47.091 seconds
(214 output tokens), and the model repaired an injected syntax fault from the
real Lua parser's diagnostic in 58.900 seconds (214 tokens). Runtime and exact
counter-value checks passed. The full boundary/generation/repair test took
180.818 seconds and peaked at 1,484,304 KiB RSS. This was CPU inference on Opti3;
it says nothing about PS5 throughput. The Lua source, every failed attempt and
the log are retained under `dist/musestation-coder-check` and
`logs/musestation-coder-model-check.log`.

The first generated counter had white text on a white panel. The runtime could
execute it, but actual LocalPlayer screenshots confirmed its number and controls
were invisible. This is a real model-quality defect, not a successful visible
application. A separate five-attempt model-driven drawing repair rejected four
complete candidates that repeated the bad colors. The fifth preserved counter
behavior (including held-button debouncing and reset) and changed to blue text
on a black panel. The exact-color equality test passed, but this does not imply
comfortable contrast or couch readability. The five-attempt run took 304.974
seconds and peaked at 1,487,120 KiB RSS. Every candidate and the original failure
remain in `dist/musestation-counter-visual-check` and
`logs/musestation-counter-visual-check.log`; actual render evidence is recorded
separately. The actual render confirms 0 → 1 → 0, but blue-on-black text is dim
and the instruction size is small; this intermediate candidate is not a
couch-readability pass.

One final bounded repair gave the model the measured 2.444:1 blue/black contrast
and observed 16-pixel instruction size, requesting at least 4.5:1 and 24 logical
pixels. The model—not a source rewrite—changed the text to white and enlarged
the instruction line. It passed actual Lua validation, exact counter/held/reset
behavior, the known-background contrast test and minimum text-size test on its
first attempt. Generation took 64.659 seconds; the whole check took 66.005
seconds and peaked at 1,478,288 KiB RSS. The actual source and log are in
`dist/musestation-counter-contrast-check` and
`logs/musestation-counter-contrast-check.log`. These tests inspect known draw
commands and do not prove arbitrary layout quality or readability on a TV.
A real LocalPlayer render is checked separately; no PS5 display was tested.
The successful counter required several earlier failed visual candidates and
specific feedback, which remains part of its evidence.

The independent D-pad movement request **failed** after three complete generated
candidates. Each declared local handlers instead of the required global
`update(dt,input)`/`draw()` and was rejected by the actual runtime. It therefore
never reached the movement/bounds checks and produced no playable validated
movement app. This run took 175.954 seconds and peaked at 1,485,084 KiB RSS;
`logs/musestation-movement-check.log` and `dist/musestation-movement-check` retain
all failures. The counter result does not generalize to different simple apps.
Runtime safety validation does not establish that an arbitrary app implements
its user's request or looks correct.

The same sole coding model passed the separate offline planning/classifier/voice
regression: complete 2–4 sentence plans before and after neural speech, all eight
approval/detail/cancel guard cases, real Qwen3-TTS audio, exact Whisper
transcription and silence rejection. Qwen/Whisper were unloaded before neural
speech and lazily reloaded afterward. The transcript was “Your plan is ready.
Press cross to approve.” See `logs/musestation-coder-voice-regression.log`.
A short generic plan passing these checks is not evidence of expert architecture.

The builder records actual generation, syntax/runtime validation and repair
results. A limited or failed attempt remains visible; it never substitutes a
canned application and describes that as generated source. Partial source is
checkpoint material only. Small models can fail repeatedly; a longer job budget
does not guarantee correctness or make complex games feasible.

## Long jobs and background execution

Multi-day job persistence is feasible through a durable queue, bounded generation
and repair attempts, and atomic checkpoints. That does not imply continuous
execution for days. Preserve the request, approval, source revisions, errors,
seed, consumed budget and completed stage; resume interrupted work after reopening.
Unattended foreground work requires the console to remain on and MuseStation to
remain active. Sony documents the setting that prevents automatic rest mode.
[Power settings](https://www.playstation.com/en-us/support/hardware/power-saving/)

Do not promise native GPU inference while playing another PS5 game or in Rest
Mode. Sony's documented lifecycle suspends a game for rest/media and closes the
first game when another PS5 game starts. A separate payload daemon is not an
equivalent execution environment: PS5LM measured paging of background payload
memory and explains that those processes lack the native title's direct-memory
budget. Its measurements were on its own 13.60 test console, not this user's
12.40 Slim. Behavior while the system UI is visible, suspend/resume, GPU recovery
and prolonged operation still require the owner's hardware tests.
[Sony lifecycle FAQ](https://blog.playstation.com/2020/11/09/ps5-the-ultimate-faq/),
[PS5LM console measurements](https://github.com/cobanov/PS5LM/blob/6323ecbf443e257932989a55cf535567697e2624/docs/CONSOLE.md)

Hybrid mode can retain local voice while Muse owns native code compilation and
package delivery. Offline mode must not silently send local jobs to that service.

## Reproduce the host model checks

Use the already built `ps5-muse-gpu` image and cached, hash-verified model assets
on Opti3. The harness is CPU-only despite the image name. Run one heavy model
check at a time; do not compare these timings with console or GPU measurements.

```sh
docker run --rm --network=none --memory=3g --memory-swap=3g --cpus=1 \
  -v "$PWD:/work" ps5-muse-gpu bash scripts/inference-codegen-check.sh
```

Pass mode/result/attempt settings through Docker `-e` arguments.
The default full check tests cancellation, token/context/output limits, reentrant
speech rejection, real generation, parser feedback and repair. A failed model
candidate makes the app-quality test fail; the test never fabricates success.
`MUSE_CODEGEN_MODE=movement` selects the independent movement request.
`MUSE_CODEGEN_MODE=repair-counter` requires `MUSE_CODEGEN_INPUT` pointing at an
actual generated candidate. `MUSE_CHECK_ATTEMPTS` bounds either loop to 1–5
attempts, and `MUSE_CODEGEN_RESULTS` selects a separate evidence directory.
The final harness additionally checks known-background text contrast and size;
earlier retained logs explicitly distinguish weaker checks from that final pass.
