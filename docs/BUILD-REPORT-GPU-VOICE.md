# Upgrade build report — 2026-10-19

Built in Docker on Opti3 and retrieved to the Mac workspace. The upgraded native
application, real Vulkan driver/inference libraries, neural speech wrapper and
installer payload compile. No console installation, GPU execution, microphone,
audio playback, performance or firmware compatibility test is claimed.

## Published upgrade

`dist/pkg-v2/IV0000-PPSA99576_00-MUSEGADGET000001.pkg` — **1,515,774,886 bytes (1,445.56 MiB)**,
content version **01.001.000**.

SHA-256: `4ec570292eecb796d968e89c800956c643293bf8dd98f8249097cfbe4e7b0c8b`.

All **440 input files** passed complete extraction/hash comparison and all
eight structural checks passed. Independent release verification compared every
bundled model to its immutable lock file: Qwen3.5 0.8B, Whisper tiny.en, Qwen3-TTS
0.6B and its projector. Total model bytes: **1,372,374,635**. The installer remains
a raw `.payload` with identical bytes, not a payload disguised as a native module.
`HardwareVerified` is false. Exact file hashes, module inspections and upstream
warnings are in the adjacent `.pkg.report.json`; independent checks are in
`logs/upgrade-artifact-check.json`.

Native eboot: **87,553,044 bytes**, SHA-256
`65de971a0028731da7e6f9caf1059a338cf8eb614fe2b1e603838f49c0f0c938`. The upgraded ELF/SELF are under `dist/native-v2`.
The native ELF is an intermediate title executable and must not be sent to the
payload loader. Use the package for Muse and `dist/muse-hello.elf` for the loader
smoke test. The previous 693,367,714-byte CPU package and source archive remain
under `dist/pkg`; its original report is `BUILD-REPORT-BASELINE.md`.

## GPU implementation and verification

The native title links OpenGL and real source-built PS5 RADV, with Mesa internals
isolated and shared system-driver initialization. Real ggml SPIR-V shaders,
Qwen, Whisper and neural mtmd speech libraries compile into the executable.
Required upstream GPU buffer-lifetime patches are pinned and applied before the
final driver build. Known absent optional Mesa weak entrypoints use documented
ELF NULL semantics; missing strong/unknown imports are not hidden with fake stubs.
Native conversion and SELF integrity inspection pass. See `docs/GPU.md`,
`logs/inference-gpu-patched-driver.log`, `logs/native-gpu-final-build.log`,
and `logs/native-gpu-symbol-check.log`.

Automatic mode probes the actual Vulkan device, reports model/backend state and
can retry returned failures on CPU. Explicit CPU mode disables GPU offload;
required-Vulkan mode fails if its required backend is unavailable. Fatal driver
aborts/hangs require restart. Dynamic direct-memory allocation and serial model
unloading replace the earlier fixed heap; physical capacity and speed must be
measured on the owner's console. Backend status does not claim every operation
ran on the GPU merely because a device was selected.

## Voice and installer tests

- Real Qwen3-TTS synthesis generated 3.120 seconds of PCM16/24 kHz speech. Whisper
  recovered “Your plan is ready. Press cross to approve.” exactly. A longer
  two-chunk plan also round-tripped through Whisper. Forced truncation errors
  preserve the previous WAV. CPU peak RSS was about 2.4–2.5 GiB under a one-CPU,
  three-GiB container limit; these are host figures, not PS5 performance.
- A combined draft/classify → neural speech → Whisper → Qwen reload regression
  passed. Actual backend-allocation reporting and required-GPU rejection on the
  CPU-only host passed. eSpeak fallback remains. Complete WAV synthesis precedes
  playback; streaming first-audio latency is not claimed.
- The native WAV decoder/resampler passed ASan/UBSan tests for 24/48/96 kHz input,
  interpolation, channel handling and malformed/truncated data.
- Client tests cover approval/generation binding, package streaming/deadlines,
  exact size/SHA/identity, no replacement of existing titles, authenticated helper
  transport, installation progress, fresh readiness checks and observed-title
  launch confirmation. Playable state can precede completion; it never forces
  progress to 100 percent. Automatic launch waits for full completion so a title
  handoff cannot unmount the source sandbox while the OS is still copying.
- The installer resolves actual mount metadata and checks package access before
  submitting installation. A writable journal is passed to the helper using
  authenticated descriptor transfer so it can survive Muse's title handoff.
  Descriptor transfer has a verified shared writable-path fallback. If neither
  journal method works, installation is refused; explicit path overrides remain
  available. Host process tests exercise this
  boundary; actual PS5 behavior remains a smoke test. Closing Muse before install
  completion can interrupt source copying; reopening recovers retained logs,
  without promising automatic install resume or launch.
- A genuine model-free generated package passed complete extraction and the same
  C package verifier used by the gadget. The test-only fixture was removed.
  Generated games need no bundled AI weights; their `.pkg` limit is 1 GiB.
- The upgraded packager passed 512/1024-file fixtures and its existing format
  checks. Patch six removes a redundant full inner-image copy without changing
  decoded bytes. Patch seven streams exact-length source files into the inner
  image and reuses outer/finalization/extraction buffers. The prior full-bundle
  attempts exceeded four- and five-GiB memory limits; their failure logs are kept.
  The final run used a five-GiB container and a 90% managed-heap limit. Every
  final title file is still extracted and hash-checked.

Evidence: `tests/client-verification.log`, `logs/neural-*.log`,
`logs/inference-v2-neural-integration.log`, `logs/wav-upgrade-test.log`,
`packaging/package-generated-check.log`, `packaging/package-upgrade-fixtures.log`,
`packaging/package-patch7-checks.log`, `packaging/package-patch7-fixtures.log`,
`packaging/package-generated-patch7-check.log`
and `packaging/package-upgrade-build.log`. Desktop UI previews use the actual draw
code with Linux software rendering; displayed FPS/status values are fixtures.

## Source and operational handoff

The companion source archive includes the app, helper, engine sources, GPU driver
and platform sources, exact patches, graphics corresponding sources, runtime
sources, licenses and build recipes. Its per-file manifest is read back and
verified before publication. Runtime provenance distinguishes the supplied SDK
release libraries from source-built components. Model weights are in the package;
locked downloaders reproduce them without including weights in the source archive.

Muse must supply its production server and implement the proposed JSON schemas
in API-CONTRACT.md. Paths are unchanged; approval requests `artifact_kind: pkg`,
and completed builds provide exact size/hash/title/content identity. No server
install/launch endpoint is needed. Logs use the existing POST /logs. No production
Muse server or complete server-to-console loop has been tested here.

The first hardware gate is a confirmed 12.40-compatible native installation and
launch environment. Relapse at `dd8e4e0914c5e9d066ee1f9b056be76cb992c8e0` still
contains only its loader and kernel exploit, without etaHEN, kstuff or
ShadowMountPlus. Its exploit range is not proof of compatibility for those
separate components. The ready-to-post question is in reddit-draft.txt.

The public builder's static debug/SELF format checks do not reproduce console
cryptographic acceptance. Its opaque optional right.sprx remains omitted, with
that omission recorded in the report. Follow SMOKE-TEST.md on the Slim, keep
firmware 12.40, and diagnose from uploaded journals before asking for copied logs.
No script updates firmware, patches the console or deploys a persistent service.
