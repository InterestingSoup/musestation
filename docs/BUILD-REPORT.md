# MuseStation build report — 2026-10-19

Build output and log paths below refer to the developer’s retained verification
files. They are not downloads in this Git checkout; public binary release assets
have not been uploaded.

This is the frozen report for 01.002.000. The newer Hybrid/Opti3 relay candidate is recorded in [BUILD-REPORT-RELAY.md](BUILD-REPORT-RELAY.md).

MuseStation **01.002.000** builds in Docker on Opti3. The native title links GPU
inference, local voice, the offline Lua builder/player and the generated-package
installer. Host execution tests passed as described below. **No PS5 installation,
GPU execution, microphone, audio-output, performance, background-lifecycle or
firmware 12.40 compatibility test is claimed.**

## Release

Single installable package (`dist/pkg-v3/IV0000-PPSA99576_00-MUSEGADGET000001.pkg`) —
**2,101,900,140 bytes (2,004.53 MiB)**.

SHA-256: `c93036e41aa32edf655bedb2c53ed55993aa8d1ed351f78d1f3d73fff77ef56f`.

All **443 input files** passed full extraction/hash comparison;
all **8 structural checks** passed. An independent release
check confirmed package bytes/hash, title/version, exact locked weights, the raw
installer helper and Lua license. `HardwareVerified` remains false. Reports:
package report (`dist/pkg-v3/IV0000-PPSA99576_00-MUSEGADGET000001.pkg.report.json`),
independent check (`logs/musestation-release-check.json`),
package log (`packaging/package-musestation-build.log`).

The documented metadata transform changes zero SDK/firmware version fields to
the upstream 2.00 baseline. The independent verifier recomputed those exact
bytes; all other staged files are unchanged. This metadata does not establish
runtime firmware compatibility.

The four complete weights total **1,957,178,283 bytes**:
Qwen2.5-Coder 1.5B Q4_K_M, Whisper tiny.en, Qwen3-TTS 0.6B and its audio projector.
The old 0.8B model is excluded from this title. There is no first-run download.
Model cards/licenses, eSpeak data and runtime notices accompany the models.

Native eboot: **87,867,652 bytes**, SHA-256
`e9ff271cf5c886e2ae2dc50a9c6967c8b395f70b4c78dd7835fdc000915e16ae`. The raw installer helper is **210,528
bytes**, SHA-256 `20670117222c0144400a027299792cf77b0e4c5899d2e0d0e03c5e189606a7f3`. Native ELF/SELF artifacts are under
`dist/native-v3`. The ELF is an intermediate native title executable; do not send
it to elfldr. Use `dist/muse-hello.elf` for the independent payload-loader test.
Earlier releases and their reports remain under `dist/pkg-v2`, `dist/pkg`,
BUILD-REPORT-GPU-VOICE.md and BUILD-REPORT-BASELINE.md.

Corresponding source (`dist/pkg-v3/musestation-corresponding-source.tar.gz`)
includes full pinned Lua, engine, driver and runtime sources, graphics source
archives, eight packaging patches, model provenance, licenses and build recipes.
Every source-manifest entry is read back and hash-checked before publication.
Its adjacent checksum and release.json record its independent archive hash.
Weights are included in the package; verified download recipes reproduce them.
No credentials or device configuration are included in the source archive.

## Local and Hybrid behavior

All Local is the new-install default. Typed requests work without an IP, account,
server or microphone. Optional DualSense capture uses local Whisper. The bundled
model drafts a plan; explicit approval starts generation and real Lua validation.
Failures carry actual diagnostics into bounded repair attempts. A validated app
requires a separate Play action. Source is checked against its approved hash
before the player creates its own Lua VM. No app automatically starts on resume.

Generated Local apps run inside MuseStation with GPU-rendered 2D drawing and
controller input. This release does not port clang, the native converter or the
package writer to the console. Fully offline native packages are not implemented.
The restricted Lua API exposes no filesystem, network, process, native-code or
installation functions. Allocation/instruction/draw limits and bounded C helpers
keep scripts within the documented runtime contract; this is not an OS process
boundary. Blank-output checks do not prove readability or requested behavior.

Durable jobs record approval/revision, attempts, consumed active time, source,
diagnostics and seeds in two checksummed checkpoint slots. Pause/cancel is
cooperative between decode operations. Restart marks active work interrupted and
requires explicit resume; it does not restore model KV state. Budgets are
4 attempts/2 hours, 64/24 hours or 512/7 days, whichever limit is reached first.
Both slots' referenced sources survive pruning; retained source bytes are bounded
to 24 MiB across 64 projects. The 512-attempt restart/accounting fixture passed.
A multi-day console soak has not been performed.

Keep MuseStation open and the console awake. The worker runs separately from the
UI, but rest mode, another foreground game and a closed title are not supported
background execution. An in-progress GPU graph may delay cooperative pause.
Local journals remain private, including after mode switches. Tests observed
zero Local HTTP requests, including a first-task mode switch with an old Hybrid
upload spool. Local failures need an owner-authorized retrieval path for outside
analysis; no export UI is supplied. Users are not expected to retype logs.

Hybrid keeps voice local, sends the draft to Muse for final plan authority, and
uses Muse's existing API paths for native builds. Package size, hash and fresh
allowed identity are checked before the authenticated installer helper runs.
It refuses replacement of existing titles or MuseStation, caps generated packages
at 1 GiB, keeps durable handoff logs and waits for full installation before launch.
A launch call alone is insufficient: the matching running title must be observed.
A genuine generated package passed the actual C verifier and complete extraction.
The production Muse service is not supplied or deployed; its complete console
loop has not been exercised. Root Compose accepts Muse's separately supplied image.

## Verification evidence

- Final native GPU/SELF build and asset staging: `logs/native-musestation-final.log`.
  Real RADV, ggml Vulkan shaders, Qwen/Whisper/Qwen3-TTS, Lua and the installer link.
  Native conversion and SELF integrity checks pass. Backend CPU recovery and
  required-Vulkan modes exist; console driver stability/memory remain unverified.
- `logs/local-final-core-verification.log` covers worker state, actual Lua
  validation/repair, approval binding, pause/cancel, atomic partial checkpoints,
  mode changes, private logs, zero Local HTTP, source retention/recovery and
  512-attempt accounting across restart. Host/SDK compiles passed.
- `logs/musestation-hybrid-regression.log` repeats the complete Hybrid suite
  against the final sources in a network-disabled, 1 GiB/one-CPU container:
  API/config/logs, worker WAV fallback, phone upload, package stream/hash/identity,
  existing-title refusal, authenticated helper/journal descriptors, mount paths,
  full-install status and observed launch with mocked native APIs. Strict SDK
  core/helper compilation also passed; no console install was performed.
- `logs/local-app-final-check.log` and `logs/local-app-visibility-check.log` cover
  ASan/UBSan runtime execution, forbidden APIs, source and allocation limits,
  infinite loops, draw/state/input quotas, cancellation, deterministic seeds,
  uniform/covered/transparent output and strict host/SDK compilation.
- `logs/native-modes-preview-final.log` includes the real LocalPlayer ASan/UBSan
  tests: source hash, controls, save/reopen, corrupt saves, deterministic seed,
  and preservation of the prior save after runtime/write failures. Sixteen UI
  fixtures render through actual draw code with software OpenGL. Their displayed
  performance/status values are fixtures, not PS5 observations.
- `logs/musestation-coder-voice-regression.log` executes the sole bundled coder
  for complete short plans, eight guarded approval cases, real neural speech,
  exact Whisper transcription, silence rejection and serial model unload/reload.
  The transcript was “Your plan is ready. Press cross to approve.” Existing
  speech, resampling, CPU fallback and live backend-accounting regressions remain
  in the separate GPU/voice report. These are host CPU tests, not PS5 timings.
- Patch-eight packaging checks pass negative/format tests, complete 518/1030-file
  fixtures and the model-free generated package/client-verifier test. The final
  full bundle passes the same per-file extraction gate under a 5 GiB container
  limit and 90 percent managed-heap ceiling. No model or asset was omitted to
  satisfy that limit. See `packaging/package-patch8-*.log` and
  `packaging/package-generated-patch8-check.log`.

## Actual model-generated apps

The old 0.8B model repeatedly failed real Lua generation. The bundled 1.5B coder
produced a counter in 47.091 seconds (214 tokens) and repaired an injected syntax
fault from the real parser diagnostic in 58.900 seconds. The full boundary/
generation/repair check took 180.818 seconds, peak RSS 1,484,304 KiB under a
network-disabled, one-CPU, 3 GiB Docker limit. These are Opti3 CPU figures.

The first counter passed syntax/control checks but rendered white text on white.
Screenshots caught that failure. Four subsequent model repairs still failed;
the fifth produced a visible 0 → 1 → 0 counter, but its blue-on-black text still
had weak contrast and small instructions. A further model repair used those
measured diagnostics and passed the 4.5:1 contrast and 24-pixel instruction
checks, plus runtime and increment/held-button/reset tests. That final repair
took 64.659 seconds of generation, 66.005 seconds total, peak RSS 1,478,288 KiB.
The preserved screenshots use the actual LocalPlayer renderer and exact source.

A different D-pad movement request failed after three complete candidates
(175.954 seconds): the model repeatedly declared local handlers when global
update/draw functions were required. It was rejected, not promoted as playable.
These results establish limited small-program generation and actual diagnostic
repair, not a reliable designer or complex-game builder. All generated source
bytes came from the model; no template or manual source fix was substituted.
MUSESTATION-FEASIBILITY.md records the evidence and retained failed attempts.
Generic validation cannot guarantee that arbitrary generated apps are usable;
the user still tests the app and can request a freshly approved refinement.

## Remaining integration and hardware gates

Account linking is not implemented. The Codex screen states that explicitly.
Official device-code and Sign in with ChatGPT routes are researched in
ACCOUNT-CONNECTIONS.md; a secure scoped console/companion channel and Muse's
pairing contract are still needed. No QR, code, connected state or account login
has been fabricated. Account authorization is separate from build approval.

The current Relapse audit records
`dd8e4e0914c5e9d066ee1f9b056be76cb992c8e0`: its payloads directory contains the
loader and kernel exploit, without etaHEN, kstuff or ShadowMountPlus. DNS/hosting
instructions were removed. A separately compatible 12.40 native install/launch
combination has not been independently confirmed. Keep the intended Slim on
12.40. The [Reddit draft](reddit-draft.txt) asks for exact verified payload builds
and distinguishes ELF loading from native installation; it has not been posted.

The public debug-package builder's format/SELF checks do not reproduce console
acceptance. Its optional opaque right.sprx remains omitted and recorded in the
report. Follow SMOKE-TEST.md for native install/launch, controller/mic, actual GPU
allocations, audio playback, offline generation, persistence and Hybrid handoff.
No script updates firmware or installs jailbreak components.
