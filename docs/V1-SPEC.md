# Muse gadget v1 — implemented integration boundary

The supplied architecture in [DESIGN.md](../DESIGN.md) remains the product
requirement. This file describes the candidate that was actually built. It does
not mark the full architecture complete or imply PS5 hardware verification.

## Native application

The gadget is a native PS5 title, with a C state/transport/configuration/logging
core and C++ graphics/inference adapters. The public native runtime and OpenGL
SDK provide GPU-rendered 1080p screens, controller input and AudioOut. The separate
hello-world ELF remains a loader smoke test; it is not the native executable.

The package includes Qwen3.5 0.8B Q4_K_M, Whisper tiny.en, Qwen3-TTS 0.6B
backbone/projector and generated eSpeak NG fallback data. llama.cpp and Whisper
share ggml with a real Vulkan backend linked to pinned PS5 RADV. Automatic GPU,
explicit CPU recovery and required-Vulkan policies report actual runtime status.
Speech unloads the draft/transcription models, synthesizes a complete WAV, then
releases its models; later requests reload lazily. Optional PS5LM/speech HTTP
origins may stay blank. No separately running PS5LM foreground app is required.

Native code, artwork, model hashes, licenses and complete corresponding source
are preserved separately from mutable console configuration. The real package
builder validates native SELF metadata and extracts the complete archive to
compare every file hash. Consult [PACKAGING.md](PACKAGING.md) for actual results.

## Interaction and approval

DualSense R2 captures 48 kHz PCM16 mono through the AudioIn API identified in
PSBox. Mute/no-device conditions fail visibly. Capture, inference and networking
run on a worker; graphics remain on the main thread. Recording stops reply audio.
Phone WAV upload is a paired fallback using the same processing pipeline. Browser
hold-to-talk requires trusted HTTPS; ordinary LAN HTTP supports WAV upload only.

Local transcription and a short draft go to Muse through the proposed JSON
variant of POST /voice. Muse's returned plan ID is fetched and only that final
plan is displayed. Square preserves the parent plan ID/revision when adding detail.
Refinement cannot fall back to raw WAV if that would lose its parent relationship.

X requires a fresh press after the plan appears and all displayed pages have been
reviewed. Voice approval remains disabled until the owner verifies it on hardware.
When enabled, approval requires the current generation, a classifier approve
result, and exactly one of three explicit spoken commands: approve, approve this
plan, or build it. Bare yes, negation, questions and conditions cannot approve.
The independent deterministic guard was added after the real model falsely
approved unrelated questions during Docker tests.

Circle invalidates pending local work; generation checks reject stale callbacks.
The API has no server cancellation endpoint. Already accepted server work may
continue, and a completed loader dispatch cannot be undone by a later cancel.
No automatic retry follows an ambiguous approval or launch result.

## Server and artifact boundary

[API-CONTRACT.md](API-CONTRACT.md) specifies the implemented schemas, limits and
proposed extensions. The endpoint paths match the brief. Muse must implement or
agree the body schemas, immutable revisions, idempotent approvals, artifact size
and SHA-256, transcript/draft submission and optional reply_wav path. Tests use
loopback fixtures; Muse's actual server has not been exercised.

The downloader accepts native FIH packages or ELF64 little-endian x86-64
payloads. Packages stream to disk with size/SHA256 and embedded-content identity
checks, then enter an authenticated local helper spawned through elfldr. The
helper checks title absence, submits AppInstUtil installation, polls native
playable/completed status and requests launch only after rechecking full completion.
Success requires observing the matching running title. Existing IDs and Muse's
own identity are refused; no uninstall or update is attempted. Package bytes
are never sent directly to the ELF loader. ELF fallback remains explicitly
reported as dispatched, without execution proof.

Local neural speech falls back to eSpeak and then a validated same-server WAV.
No arbitrary response URL or redirect is followed. The
transport currently supports LAN HTTP numeric origins; it explicitly rejects DNS
names and HTTPS rather than silently weakening an HTTPS configuration.

## Logs, setup and migration

The logger fsyncs JSONL records and automatically attempts upload for handled
failures, session ends, periodic idle retries and startup recovery. Unacknowledged
records survive restart; an active marker reports an unclean previous termination.
A killed process or kernel panic cannot upload until execution resumes. Immediate
dead-process reporting would require a separate supervisor, which is not bundled.
FTP and kernel logs remain recovery tools. Users do not need to copy/paste logs.

The first-launch controller wizard collects console and server endpoints and the
AudioIn module path. scripts/configure.py also collects all mutable paths and
owner-reported payload versions. Payload reports stay unverified. Application
mount defaults can be overridden through runtime configuration/environment; LAN
addresses are never compiled into the app. Settings persist outside package assets.

The root Compose file provides a build profile and an intentionally unconfigured
Muse image slot. Muse owns the server image and its environment contract. No
production server is deployed or claimed operational by this project. The gadget
can be repointed at Opti3/Coolify without recompilation. Provider: Muse is the only
provider UI.

See [NATIVE-APP.md](NATIVE-APP.md) for build commands and runtime limits,
[INFERENCE.md](INFERENCE.md) for measured host tests, and
[SMOKE-TEST.md](SMOKE-TEST.md) for the owner's unperformed hardware checklist.
