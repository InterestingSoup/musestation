# Build report — 2026-10-19

This is a native homebrew candidate built in Docker on Opti3, with source and
retrieved artifacts in the Mac workspace. No PS5 installation, microphone,
rendering, performance or firmware compatibility test has been performed.

## Published package

`dist/pkg/IV0000-PPSA99576_00-MUSEGADGET000001.pkg` is a finalized native PS5
FullDebug FIH package: **693,367,714 bytes (661.25 MiB)**.

SHA-256: `78e1bb310e10e1a7f3d9c1a3d406ff128bc95e9c4b973050e6fbecef8b083cfc`.

All **411 source files** passed complete extraction/hash comparison, with the
packager's explicitly checked metadata normalization. The bundled model bytes and
frozen native executable are unchanged. All eight structural checks and direct
serialized-index checks passed. Native eboot FE10 and runtime FE18 inspections
passed. Exact warnings and all file hashes are in the adjacent `.pkg.report.json`;
its `HardwareVerified` field is false. The package remains a hardware-test candidate.

The public packager needed maintained fixes for raw storage, multiple inode
blocks, NAPS index encoding/padding, and double-indirect outer-file addressing.
Stress packages and an unchanged generic PFS reader verify those changes; this
is separate from console cryptographic/installation verification. See PACKAGING.md.

## What compiled

| Deliverable | Evidence |
|---|---|
| Public SDK Docker image and upstream hello-world | logs/docker-build.log, logs/compile.log, logs/artifacts.log |
| Muse hello-world notification and AudioIn probe | dist/muse-hello.elf, dist/mic-probe.elf |
| Native graphics/controller/config/voice/network/logging application | logs/native-build-final.log; native SELF integrity valid |
| llama.cpp, Whisper, ggml, eSpeak NG and native locale adapters | logs/inference-compile.log; included in final native link |
| Real native package tooling | packaging/package-image-build.log and package-negative-checks.log |

Final native eboot.bin: 30,542,914 bytes,
SHA-256 `c0b9231a71f2401bda58a1e1961cdebe4495e9c8475c80ce12448781ac6efdc5`.
The separate native eboot.elf is an intermediate native-title executable; it must
not be sent through the payload loader. Use the installable package for the gadget
and the separately named Muse hello-world ELF for the loader smoke test.

## Tests actually run

- Linux AddressSanitizer/UndefinedBehaviorSanitizer fixtures cover strict JSON,
  HTTP deadlines/framing, configuration, SHA-256, malformed/corrupt artifacts,
  approval/generation guards, exact loopback loader delivery, durable logs and
  phone pairing/upload. Six core translation units also compile against the PS5
  SDK under -Wall -Wextra -Werror. Evidence: tests/client-verification.log.
- The actual native worker was compiled for Linux with inference disabled to
  exercise the server-WAV fallback, audio validation, saved path, log flush and
  joined shutdown. This found and verifies the fix for a null Content-Type argument.
- Actual bundled Qwen and Whisper weights were loaded in Docker. eSpeak synthesis
  transcribed back through Whisper, silence was rejected, a draft was generated,
  and eight intent cases were checked. The real model falsely approved unrelated
  questions before a separate exact-command veto was added. Both engine and gate
  now require explicit approval wording; voice approval still defaults disabled.
- The complete cold host inference regression used 11.990 seconds wall time and
  845,284 KiB peak RSS under a two-CPU/two-GiB container limit. Those values cover
  the whole harness, not one response, and are not console latency/memory figures.
  Evidence: logs/inference-host-metrics.json and INFERENCE.md.
- The same GPU UI draw code rendered four desktop preview scenes with Linux Mesa
  software rendering. The home/plan/setup images were visually checked. Their FPS
  and status are fixture values. Evidence: logs/native-preview.log and
  build/native/preview/*.png.
- The package tool rejects an SDK payload renamed eboot and a title containing a
  symlink. A full release is published only after structure, full extraction,
  every-file hash comparison and native module inspections pass. Its report lives
  beside the final package; see PACKAGING.md for the actual publication result.
- Real FIH stress packages with 518 and 1,030 input files pass extraction/hash
  comparison. Direct tests cover inode-table boundaries, serialized NAPS indexes
  and padding. A 597 MB outer image passes readback through the unchanged generic
  PFS reader, including single/double-indirect boundaries and pointer signatures.
  These tests exposed and verify fixes for the public packager's small-title limits.

## Included native behavior

GPU-rendered 1080p interface, animated backdrop, couch-sized plan pages, DualSense
input/light-bar/rumble, R2 microphone capture, first-launch setup, local English
Whisper, Qwen drafts and intent classification, eSpeak speech, paired phone WAV
input, Muse approval/refinement, validated ELF download/dispatch and JSONL upload.
PS5LM/speech service addresses may stay blank when using bundled engines. Actual
console/server addresses, paths and payload reports remain install-time inputs.

The package bundles the 532,517,120-byte Qwen3.5 0.8B Q4_K_M model and the
77,704,715-byte Whisper tiny.en model, generated eSpeak voice data and licenses.
Model SHA-256 values and immutable origins are in inference/lock.json. A companion
corresponding-source archive contains app, engines, native kit and graphics sources,
patches, build recipes, license notices and a verified per-file manifest. It is a
source/rebuild deliverable; the console still installs just one .pkg.

## Remaining requirements and hardware gates

1. No independently confirmed etaHEN/native install-launch combination for firmware
   12.40 was found. Relapse's advertised exploit range does not prove those separate
   components. The owner must identify the compatible environment before installing.
2. Inference is CPU AVX2; the graphics use the GPU. GPU LLM inference and neural
   Qwen3-TTS are not implemented. eSpeak speech is functional but robotic.
3. Generated builds still use ELF artifacts. The gadget's own native .pkg does not
   provide automatic installation/launch of generated native packages. Public
   installer APIs exist; the privileged helper/launch context remains unimplemented.
4. Muse's production server was not supplied or tested. The stable endpoint paths
   are implemented against the proposed response schemas in API-CONTRACT.md,
   including parent revision context and optional reply_wav. Compose has a Muse
   image integration slot, not an operational server deployment.
5. Browser microphone capture needs a trusted HTTPS context; LAN HTTP offers WAV
   upload. Handled failures upload immediately, but a dead process/kernel panic
   can only report through next-launch recovery or an external supervisor. No
   separate supervisor is bundled.

## Current upstream drift

Relapse at dd8e4e0 contains its ELF loader and kernel exploit, without bundled
etaHEN, kstuff or ShadowMountPlus; DNS/hosting directions are absent. The payload
SDK wrapper now targets x86_64-sie-ps5. PSBox uses AudioIn, not SDL capture. PSllama's
reported speech numbers are from a base CFI-1208B on 13.60, not the user's Slim on
12.40. Primary source links and exact pins are in RESEARCH.md and upstream-lock.json.

Use SMOKE-TEST.md for the owner's hardware test sequence. Preserve firmware 12.40
and never update above 13.60 for this project. No build command updates the console
or installs jailbreak components.

## Recorded Docker images

- Toolchain: `sha256:a4869a303e3ea1b54ba1de6cae8d43e5c77cca2dac775b66557f6ba61afa5b34`
- Native build: `sha256:053920f5f907dd622b08e0e57a0691bced2946e7c2abf2fcfba420e9cb2b50a7`

Base image digests and upstream revisions are pinned; apt package versions are
recorded in logs/toolchain-packages.tsv. A future rebuild can pick up changed apt
packages unless their recorded versions are restored. This delivery is a recorded,
repeatable build recipe, not a claim of bit-for-bit reproducibility across time.
The local artifact digests are recorded in logs/local-artifact-manifest.json.
