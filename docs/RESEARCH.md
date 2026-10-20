# Upstream verification — 2026-10-19

Build output and log paths below refer to the developer’s retained verification
files. They are not downloads in this Git checkout; public binary release assets
have not been uploaded.

Exact checked-out revisions are in ../upstream-lock.json. Upstream claims below are
not Codex hardware measurements. Recheck versions again when the console arrives.

## Inventory/runtime-loading update audit

The current SDK and Relapse READMEs and Relapse payload listing were fetched again
for 01.005.000; the audit (`logs/title-inventory-upstream-audit.json`) records
retrieval time, URLs and hashes. Relapse still lists only
`elfldr-ps5-1360.elf` and `kexp_2026_05_25.bin` in `payloads/`; this does not bundle
etaHEN, kstuff or ShadowMountPlus or establish their separate 12.40 compatibility.

The official [ShadowMountPlus 1.7beta4 release](https://github.com/drakmor/ShadowMountPlus/releases/tag/1.7beta4)
publishes ELF and ZIP assets. Its pinned source includes a 12.40 registration path,
but its release notes do not establish a 12.40 install/update/launch test. The ZIP
route remains deferred pending confirmation of the required 12.40 environment.
The inventory API and its limitations are recorded in
[the exchange contract](MUSE-EXCHANGE.md#title-inventory-before-approval).

ProsperoStore commit `e8b9b09fcd54cce27ea1bcda99a57af9df0fb4b4` reports that directly
importing AppInstUtil prevents payload startup on 12.70. Its
[payload helper](https://github.com/darkxex/ProsperoStore/blob/e8b9b09fcd54cce27ea1bcda99a57af9df0fb4b4/helper/main.cpp#L152-L174)
loads the module by path and resolves functions through the SDK. MuseStation now
uses that loading pattern preemptively; this is not hardware confirmation on 12.40.

## Firmware and payload drift

[Relapse's current README](https://github.com/ntfargo/Relapse-Exploit) advertises
7.00–13.60 and port 9021. Its current payload directory has only
`elfldr-ps5-1360.elf` and `kexp_2026_05_25.bin`. etaHEN, kstuff and ShadowMountPlus are
absent, and the README has no DNS/hosting procedure. Thus the brief's warning about
drift is confirmed. The loader filename alone is not a separate 12.40 compatibility
test; the README's range covers the exploit as a whole.

The [SDK](https://github.com/ps5-payload-dev/sdk) uses prospero-clang and contains a
12.40 case (`0x12400000`) in crt/kernel.c. Its hello_world sample uses a system
notification, and prospero-deploy uses socat over TCP. The C runtime supports
dlopen/dlsym, used by the microphone probe to avoid inventing a link stub.

[elfldr](https://github.com/ps5-payload-dev/elfldr)'s README documents port 9021 and
separate payload processes, but does not provide an explicit 12.40 release matrix.
[etaHEN's latest-release API](https://api.github.com/repos/etaHEN/etaHEN/releases/latest)
returned 2.5B dated 2025-12-25; its notes do not establish Relapse/12.40 compatibility.
**A compatible etaHEN release has not been independently confirmed.** Do not relabel
that release as compatible simply because PS5LM's install instructions mention it.

The user must source/identify kstuff-lite and ShadowMountPlus separately, plus a
native installer and loader/FTP/kernel-log builds as needed. Require etaHEN only
if the chosen route actually depends on it. Check exact releases
and digests at install time. etaHEN's README describes its FTP on 1337 and klog on
9081; standalone ftpsrv is a separate service, commonly 2121. These are not mutually
interchangeable service/version checks. No console is available to query now.

Arrival-guide recheck: official [kstuff-lite v1.11 Beta](https://github.com/EchoStretch/kstuff-lite/releases/tag/v1.11)
explicitly lists firmware 1.00–13.60. This establishes an upstream support claim,
not a complete native-install test. The current SMP default branch is `1.7`;
use its [1.7beta4 documentation](https://github.com/drakmor/ShadowMountPlus/blob/1.7beta4/README.md),
not the stale `main` README.
[PKG Manager v1.5.1](https://github.com/itsPLK/ps5-pkg-manager/releases/tag/v1.5.1)
is a separate initial-installer candidate whose tagged README documents ELF
loading and browser Direct Install on port 8844. No explicit 12.40 qualification
was found. See [the arrival-day guide](FIRST-DAY.md) for the remaining gates.

## PS5LM

[PS5LM v0.9.0](https://github.com/cobanov/PS5LM/releases/tag/v0.9.0) is a native
ShadowMountPlus title, distributed as a title-folder zip. Its current build docs
require PS5_Vulkan/RADV and a native runtime beyond the payload SDK's hello-world
toolchain. The release calls for kstuff-lite, etaHEN and ShadowMountPlus; loader
and standalone FTP are listed as recommended in the current README.

The project reports GPU rates of 55 tokens/s for Qwen3.5 9B Q4_K_M and 207 tokens/s
for 0.8B. These are upstream measurements, not guarantees on the user's firmware.
The small default model selection uses the upstream catalog's size and SHA-256,
with an immutable Hugging Face repository revision and Apache-2.0 metadata. The selected weights have now been downloaded, hash-verified and bundled; see
INFERENCE.md for actual Linux model tests. Decision-model accuracy, latency and
coexistence with speech need measurement; “fraction of a second” is not verified.

## Microphone verdict: concrete API found; payload probe compiled on Opti3

[PSBox's capture implementation](https://github.com/RafaelNGP/xbox-cloud-gaming-ps5/blob/3cc78a1e0d3bf63e31beb1d212365d1b4370a227/src/media/audio_in_ps5.cpp)
uses sceAudioInInit, sceAudioInOpen, sceAudioInInput, sceAudioInClose and
sceAudioInGetSilentState. It requests 48 kHz S16 mono in 256-sample grains; type 0
is attempted before type 1. Hardware mute/no-device state is handled. This is
libSceAudioIn, not SDL capture on PS5. Its desktop backend is separate.

prototypes/mic_probe.c follows that sequence, selects the initial user, resolves
the AudioIn symbols dynamically, respects mute and writes a WAV to a supplied path.
The SDK checkout lacks an AudioIn stub. Native-title evidence does not establish
permissions or routing in payload context; a successful compile cannot establish
those either. The probe now compiles successfully with the pinned SDK; next use the owner's hardware loop.

## Speech verdict: existing port verified; neural upgrade integrated

[PSllama](https://github.com/mansanitizer/psllama) contains an actual speech integration
in app/audio_service.cpp and app/http_server.cpp. Its results are from a base
CFI-1208B on 13.60, not a Slim/12.40 test. It reports 0.79 s for an 8 s Whisper input,
about 2× real-time Qwen3-TTS, and 2.3–2.7 s warm voice response. It is experimental
and supplies no prebuilt package.

The build shares ggml across llama.cpp, whisper.cpp and the other engines and sets
GGML_MAX_NAME=160 consistently. Whisper accepts resampled 16 kHz float audio. The
HTTP transcription interface expects base64 WAV in JSON, not multipart upload.
The server's Tailscale-only address filter, compiled API key and app-root model
paths need adaptation for the gadget's runtime configuration. Its serialized
inference scheduling is useful evidence for avoiding concurrent GPU memory pressure.

[whisper.cpp's FreeBSD CI](https://github.com/ggml-org/whisper.cpp/blob/master/.github/workflows/build-freebsd.yml)
exists, but full FreeBSD compatibility is not proof of compatibility with the PS5
SDK's smaller libc. The CPU tiny.en engine now compiles into the native app and passes actual-model
Linux tests. The complete cold host harness peaked at about 825 MiB; neither
latency nor the available memory budget on the PS5 has been measured here. See
INFERENCE.md for the scope and evidence.

[espeak-ng's build](https://github.com/espeak-ng/espeak-ng/blob/master/CMakeLists.txt)
has optional audio/dependency switches and cross-build data-generation requirements.
The native build now includes its generated voice data and synthesis callback,
with WAV playback through PS5 AudioOut. eSpeak and Whisper archives compile and
link into the native executable, and their host audio round-trip test passes.
Console playback remains untested. The upgraded title now includes the actual
Qwen3-TTS/mtmd path, paired 0.6B model and projector, CPU retry and eSpeak fallback.
Short and multi-chunk host synthesis, Whisper transcription, bounded-output errors
and Qwen/Whisper reload were tested with real weights. No evidence
found establishes that a PS5 ONNX Runtime port is already available; the FreeBSD
port alone would not do so.

## Packaging route: public builder with native validation

[LibProsperoPkg](https://github.com/drakmor/LibProsperoPkg) exposes
ProsperoHomebrewPackager and structural/launch-readiness validators, currently on
.NET 10. [UltraPack](https://github.com/knutwurst/ps5-ultrapack) reports homebrew PKG
installation on firmware 11.60 with kstuff-lite. This establishes a packaging route
worth building, not verification on 12.40. Native-title creation, package assembly,
bundled model integrity and runtime compatibility are separate acceptance gates.
No zip or arbitrary file has been renamed to .pkg.

## Opti3 build evidence — 2026-10-19

The Docker image, SDK hello-world, Muse notification payload, AudioIn probe and C
approval-gate object now compile. Linux gate tests pass under ASan and UBSan.
The current SDK wrapper uses `-target x86_64-sie-ps5`, rather than the older
`x86_64-pc-freebsd` triple named in the brief. The outputs are x86-64 ELF64 DYN
payloads, built with the upstream wrapper and linker recipe. We did not override
its target to match stale prose. The SDK itself emits warnings for deliberately
unimplemented libc functions; our payloads compile under -Wall -Wextra -Werror.
The Dockerfile needed an absolute LLVM_CONFIG path and libclang-rt-18-dev for
the sanitizer tests. These are host-build fixes, not evidence of console behavior.
Current SDK/Relapse READMEs were fetched again on the build host into logs.

## Native integration update

The initial native OpenGL title and CPU Qwen/Whisper/eSpeak archives linked together.
The upgrade adds source-built RADV, real Vulkan ggml shaders, Qwen3-TTS and a fresh
installer helper. See BUILD-REPORT.md for the final native link/package result.
NATIVE-APP.md, INFERENCE.md and PACKAGING.md describe the current implementation
and evidence; initial payload checks do not substitute for native-title testing.
Native `.pkg` tooling
required updated PS5 SELF inspection for both public native container magics and
PS5 dynamic/procparam segments; a payload renamed eboot is rejected.

The Relapse payload tree was rechecked for this upgrade at full revision
`dd8e4e0914c5e9d066ee1f9b056be76cb992c8e0`; its two payload filenames above are
unchanged (logs/relapse-upgrade-audit.json). The draft in reddit-draft.txt asks
for exact separately tested 12.40 payload revisions; it has not been posted.
