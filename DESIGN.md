# MuseStation — architecture authority

Source: the user's supplied Appendix, received 2026-10-19. This file records its
requirements for implementation; upstream verification lives in docs/RESEARCH.md.
Status: planning, v1 scope locked 2026-10-18.
Owner: user. Architect/server pipeline: Muse. Coder: Astra (Codex).

## Title inventory and deferred ZIP amendment — 2026-10-19 (current)

These requirements are implemented in the 01.005.000 candidate. Compilation and
package evidence is in docs/BUILD-REPORT-INVENTORY.md; hardware remains unverified.

- Before a new Hybrid approval, the console reads its ShadowMountPlus API version
  and requires API v1 with `list_games` and `installed_pkg_games`, then validates
  the complete returned game-list response. `shadowmount_url` defaults to
  `http://127.0.0.1:10101` and can be set in the native wizard or with
  `MUSE_SHADOWMOUNT_URL` at installation. Only loopback or the exact configured
  console IP is allowed. Never forward a relay/provider credential to this API.
- Approval carries `inventory:{source:"shadowmountplus",api_version:1,title_ids:[...]}`.
  It contains at most 4,096 unique IDs with `PPSA`, `CUSA`, `LAPY` or `FAKE` followed
  by five digits. Invalid/truncated responses stop approval. A new allocation
  without inventory returns 428. Exact replay of an accepted approval can recover
  its existing job without inventory because it creates no new reservation.
- Allocate against the snapshot, the durable title ledger and permanent exclusions
  for ProsperoStore (`PPSA99000`) and MuseStation (`PPSA99576`). Store the snapshot
  privately with the job; do not export it to Muse through the provider or folder
  protocol. The ledger protects allocated/in-flight jobs but is not the only
  source of console-title collision information.
- ShadowMount's list is not a complete or fresh census attestation: it has no boot
  ID, generation or completeness marker, its discovered-game cache is bounded,
  and some unavailable registrations are omitted. Retain the final OS check that
  refuses to replace an existing title. A console credential authenticates a
  client report; it does not attest hardware state or physical button presses.
- ZIP delivery is deferred at the user's request. Its future purposes are a
  qualified loose-title install route and updates to an owned installation.
  Updates require verified staging, ownership/version checks, a closed title,
  durable journal/backup and tested rollback. Private configuration stays outside
  the replaceable bundle. Blind directory replacement is not an update design.
- The installer helper loads AppInstUtil at runtime after authenticated journal
  setup; it resolves every required function before initialization and installation.
  Missing capabilities fail visibly and preserve logs. Loading changes do not
  grant permissions or establish firmware-12.40 runtime compatibility.
- Upstream folder-permission guidance is limited to app bundles; secrets remain
  private. Correct the console clock for HTTPS and keep certificate validation.
  A 12.40 offset in source is not 12.40 hardware validation; the current PKG path
  still requires the owner's test on the actual Slim.

The exact contract and pinned upstream evidence are in
[docs/MUSE-EXCHANGE.md](docs/MUSE-EXCHANGE.md#title-inventory-before-approval) and
[docs/PACKAGING.md](docs/PACKAGING.md#deferred-zip-delivery-and-updates).

## Opti3 exchange amendment — 2026-10-19 (current)

The user selected an Opti3 Docker relay that receives Muse plans/source, saves
jobs, compiles native packages and serves them to MuseStation. This supersedes
mandatory Muse SDK/Bluetooth enrollment and Muse-hosted compilation below.

- Muse owns plans and generated source. Opti3 owns durable requests, exact-revision
  approvals, compilation, packaging, artifact storage and diagnostic exchange.
- Start with a local exchange folder. An authenticated provider API supports a
  connector pushing responses and polling requests; no undocumented Muse endpoint
  or account-pairing flow is invented. A real Muse connector is still required.
- MuseStation uses local speech or typed input, waits for an asynchronous plan,
  and approves on-console before a build. Provider access cannot approve plans.
- Each approved native job receives one fixed-recipe compile attempt. A failed
  or interrupted build needs a new request and approval. Code cannot select shell
  commands, images, dependencies, title identity or host paths.
- The PS5 downloads, checks, installs and launches the package using the existing
  installer helper. Opti3 does not initiate an unsolicited installation. Firmware
  compatibility and all console behavior still require the owner's hardware test.
- New requests can survive console restart while awaiting a plan. The server
  keeps approved jobs; automatic console recovery of an already-approved build
  is separate work and is not advertised. Keep the title open through installation.
- The current transport is trusted-LAN HTTP. Provider credentials stay on Opti3.
  No public endpoint or Muse account connection is established by entering an IP.
- Console endpoints require a separate install-time key. Request IDs and exact
  bodies are saved before submission; a lost response is recoverable by ID.
  Title reservations persist outside SQLite. Handled failures upload immediately;
  an unclean exit replays the durable crash journal on next launch.
- MuseStation's model-bundled release is installed separately from the 128 MiB
  generated-app recipe. Custom assets require a reviewed recipe extension.

See docs/MUSE-EXCHANGE.md for the implemented handoff and docs/RELAY-DEPLOY.md
for deployment and health checks.

## Hybrid-first amendment — 2026-10-19 (earlier connection proposal)

The user's latest decision makes **Hybrid with Muse** the normal product path.
This supersedes the Local-first default and local-draft requirement below.
Local lab remains experimental; existing offline configurations, projects and
private journals are preserved without automatically enabling network access.

- The PS5 captures and transcribes speech and speaks replies locally. Hybrid
  does not load a coding LLM or draft a plan. Muse owns planning, development and
  compilation; the console displays Muse's plan for explicit approval before a
  build. Local voice is useful independently of local code generation.
- The first provider is Muse. Other providers can be added behind an adapter,
  with their supported authentication and development protocol. Selecting a
  provider is not permission to build, install or run arbitrary instructions.
- Muse device connection must follow the actual public gadget SDK. The supplied
  official SDK uses Bluetooth Add Device pairing and an SDK token, followed by
  authenticated cloud transport; it does not document a QR/device-code grant.
  See docs/HYBRID-FIRST.md and docs/ACCOUNT-CONNECTIONS.md. Do not invent a code,
  login endpoint or connected state. A configured LAN build endpoint is a
  separate development path and does not prove account pairing.
- No coding model is needed by Hybrid initialization. The previous packaged
  release still bundles its coding model; changing initialization does not make
  that existing package smaller. A future Hybrid package can omit the coder
  while keeping Whisper, neural speech and their libraries.
- Server jobs can in principle outlive a console session. Reconnection and job
  recovery must be implemented and tested before advertising that behavior.
  PS5 rest-mode or second-game background execution remains unverified.
- Preserve physical approval and the independent verified-voice gate. Hybrid
  accepts only exact explicit approval/detail/cancel phrases, without a local
  classifier model. Questions, conditions and extra clauses are not approval.

## Local-first amendment — 2026-10-19 (superseded default)

The user's subsequent instructions rename the product **MuseStation** and add
All Local alongside Hybrid with Muse. This earlier amendment took precedence over the
Muse-only provider UI and mandatory server authority requirements below. It does
not change the owner's hardware-test role or the firmware constraint.

- All Local is the new-install default. Bundled Whisper, Qwen and speech run in
  the title. Typed requests work without a microphone module, LAN IP, server or
  account. HTTP clients, external inference/voice fallbacks, phone listener,
  package installer and log uploads are disabled by policy in this mode. Private
  local journals are kept separate and never uploaded later by changing modes.
- An offline generated app is a Lua project running inside MuseStation's native
  renderer. The console has no verified native compiler/packager port in this
  implementation. Standalone generated home-screen packages therefore remain a
  Hybrid feature. The MuseStation application itself remains one model-bundled
  native package. This is the first offline implementation assumption presented
  to the user; a separate package per offline app was not silently promised.
- Each local job stores its request, plan, revision, explicit approval, candidate
  source, validator diagnostics and compute limits. The approval hash includes
  the plan and limits. Generation and repair use the actual bundled model;
  failures never substitute a canned game. Finished code must pass a bounded Lua
  runtime test before becoming READY. The owner separately presses Play.
- Local job budgets are 4 attempts/2 active hours, 64/24 hours or 512/7 days; the
  first exhausted limit stops work. These are ceilings, not promised useful work
  duration or model quality. File checkpoints preserve partial source and phase;
  interrupted jobs require explicit resume. A model's KV cache is not serialized.
  The title must stay open with the console awake. A process exit, rest mode,
  system suspension or another game is not supported background execution.
- Hybrid keeps local voice and draft planning, then uses Muse's stable API for
  final plan authority, native compilation, artifact serving and log collection.
  Pending server work can continue independently of a console UI cancellation.
- Optional provider connection is an architectural boundary. A personal Codex
  companion has a documented device-code flow, but the current client has no
  secure account-pairing transport. It must not display a fabricated code or
  connected state. Muse owns its authentication service. Account connection
  implementation and distribution eligibility are recorded separately in
  docs/ACCOUNT-CONNECTIONS.md; login is never build/execute approval.

## Build-host update — 2026-10-19

The user subsequently authorized SSH deployment and Docker compilation on Opti3.
The working directory is /home/opti3/services/ps5-muse, under its existing services
directory. This replaces the original MacBook-only location constraint below;
Docker compilation and owner-only hardware verification remain mandatory.

## Target and verification

- User-owned PS5 Slim, firmware 12.40. Never update past 13.60.
- The jailbreak must be reapplied after every reboot. Homebrew is unavailable
  until then. The console is homebrew-only; account/console bans are a risk.
- All compilation runs in Docker on the authorized Opti3 build host. The public
  ps5-payload-dev/sdk supplies prospero-clang targeting x86_64-pc-freebsd.
- No claim of on-console verification by Codex or Muse. The user is the hardware
  test loop when the console arrives, expected early November 2026.
- Check current official repositories at build/install time. Relapse exploit
  support does not establish etaHEN, kstuff-lite or other payload compatibility.
- Confirm the actual installed payloads and endpoints at installation; nothing
  about the console's IP or mutable filesystem paths is compiled into the gadget.

## Three components

### Gadget

A C application core against the public SDK, with adapters for the C++ inference
engines. DualSense microphone capture is primary; phone hold-to-talk over LAN is
fallback. Capture targets 48 kHz, 16-bit mono using the API demonstrated by PSBox.

On-console whisper.cpp performs transcription. PS5LM/llama.cpp supplies a local
decision model for approve/add_detail/cancel and a small Qwen for a 2–4 sentence
draft plan. Muse approves or edits the draft before the gadget displays it.

The decision model becomes the primary approval path once verified. Physical
X = approve, Square = add detail, Circle = cancel remain fallback controls.
This appendix takes precedence over the earlier body text requiring X for every
approval. Before hardware verification, use buttons and treat voice as advisory.
Inference timing claims in the brief are targets/upstream reports, not test results
for the user's console.

Speak-back is local: ggml-native Qwen3-TTS is the recommended neural path;
espeak-ng is the lightweight fallback to port. Server-generated WAV is last resort.
Neither fallback is considered working until it compiles and is hardware-tested.

The final approved plan is fullscreen and readable from the couch. Display
Provider: Muse; no provider picker for v1. The installation wizard or configuration
variables collect the console IP, PS5LM origin, Muse origin, payload status and
required paths. Configuration must remain editable after installation.

### Muse server

Muse owns plan authority, compilation, API keys, artifacts and log storage. Local
transcription/drafts are submitted for final planning; server transcription is
fallback. The provider interface is transcribe → plan → build-spec. Muse is the
only provider implemented for v1; OpenAI and Anthropic come later.

Stable LAN HTTP paths:

- POST /voice — audio upload, transcript and plan ID response.
- GET /plan/<id> — final plan JSON.
- POST /plan/<id>/approve — explicit approval, build job ID response.
- GET /build/<job>/status — build progress, done/error.
- GET /build/<job>/artifact — finished build.
- POST /logs — JSONL upload.

Response schemas and a local-transcript/draft input variant are integration details
not specified by the appendix. Document adapter assumptions without claiming Muse
already implements them. Build queue concurrency is one; states include
transcribing → preview → building → done/error. Keep artifacts with their plan,
transcript and logs.

Develop the server in Docker on the MacBook. Root Docker Compose and environment
configuration must support later migration to Opti3/Coolify. Muse supplies the
server implementation; do not add provider UI or unrelated server voice/build code.

### Built artifacts and installation

Part A: compile the SDK and a hello-world ELF in Docker. Deploy through the actual
ELF loader, normally port 9021, using prospero-deploy. Supply build evidence and an
owner-run smoke checklist including display expectations and log location.

The final gadget deliverable is a single genuine .pkg including a small LLM by
default. Codex chooses integration of PS5LM components or a bundled prerequisite.
No bring-your-own-model step. The final package must contain its runtime and assets;
an external model dependency or renamed zip does not satisfy this requirement.
ELF payloads are stepping stones. Package installation/launch is in scope for the
final built-app loop; distinguish ELF dispatch from PKG installation in the adapter.

## User loop

1. User holds the talk control and speaks a request.
2. Local Whisper transcribes; the local decision model classifies intent.
3. Small Qwen drafts; Muse approves/edits; the final plan is displayed.
4. Explicit user approval through the verified voice gate or button fallback.
5. Muse compiles with the Docker SDK toolchain and serves the build.
6. The gadget downloads and installs/launches the appropriate artifact.
7. Local TTS speaks back; logs upload automatically. Muse consults logs first.

## Remote logs

Structured JSONL contains ts, level, event and detail for boot, voice submission,
plan fetch, approval, download, launch and crash. Upload after every session and
immediately on failure while the system/network can operate. Persist pending logs
and recover after reboot. The user never copy-pastes logs. Muse diagnoses from the
latest session/build logs before asking the user to perform troubleshooting.

FTP access to the configured log directory (the brief suggests
/user/homebrew/logs/) and klogsrv are recovery paths. A supervisor is needed to
report app death when the app cannot upload itself; kernel panic/power failure
requires server heartbeat timeout and upload after recovery.

## Phases and exclusions

Owner jailbreak/setup → Docker hello-world → Muse server and local Whisper →
gadget voice/gate/UI/config/logging and bundled-model package → first Tetris build
with DualSense controls and TTS → polish → future providers → Coolify migration.

The live bridge to the user's actual Codex session is out of v1 scope. Provider
connectors and provider-switch UI are later phases. Firmware upgrades are excluded.
The old phone-first/server-TTS and package-out-of-scope decisions are superseded.

## Open verification work

etaHEN 12.40 coverage remains a release-validation question. The proven upstream
microphone, Whisper and Qwen3-TTS work justifies adaptation, but does not establish
this implementation on Slim/12.40. An ONNX Runtime port is a higher-risk optional
path; it is not required for the selected ggml-native TTS design.
