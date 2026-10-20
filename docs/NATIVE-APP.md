# Native MuseStation title

The app is a native PS5 title built from public source, with a C transport/state/logging
core and C++ rendering/inference adapters. It uses the pinned ps5-homebrew-ui and
ps5-opengl stack: OpenGL 4.6 shaders, signed-distance-field fonts, an animated aurora
backdrop, controller focus, microphone meter, light-bar feedback and short rumble.
The pinned display SDK profile is 1920×1080 at 60 Hz. This is a requested display
mode, not a measured console frame rate. The PNG previews use the same draw code
on Linux Mesa software rendering; their frame rate/status values are fixtures.

## Current Hybrid-first source

The 01.005.000 candidate is compile- and package-verified in Docker; see
[BUILD-REPORT-INVENTORY.md](BUILD-REPORT-INVENTORY.md). The inventory and runtime
loading changes have no on-console verification.

New source selects Hybrid with Muse first and keeps Local lab experimental.
Hybrid loads Whisper and speech without the coding LLM; transcripts go to Muse
with an empty draft and retain parent plan/revision for refinements. Only exact
explicit voice commands can reach the independent verified-voice gate. A saved
Local config is not migrated to Hybrid. New phone listeners stay disabled unless
explicitly configured.

The Muse connection screen configures the Docker job relay. X or Square opens
LAN setup; this does not connect an account. No BLE enrollment or Muse SDK token
is required for this exchange route. “Opti3” in the interface is the development
server's name; use your own relay's address. Other providers remain unavailable.
See [Hybrid-first behavior](HYBRID-FIRST.md) and the [Muse exchange](MUSE-EXCHANGE.md).

The following local feature and package inventory also applies to the preserved
01.002.000 release, which still defaults to All Local and loads its coder in Hybrid.

## Experimental local operation

New installations open a creation-mode picker. All Local works with typed requests
and bundled models without a LAN address or account. Its optional microphone field
accepts the actual AudioIn module path; leave it blank for typed creation. Local
jobs are approved with their compute budget, checkpointed, repaired by the bundled
model, validated and saved in the project library. Play is an additional explicit
controller action. These apps run inside MuseStation through the restricted
[Lua runtime](LOCAL-APPS.md); a native compiler and package builder do not run on
the console. Hybrid with Muse retains the native package workflow below.

Left/right selects the mode; up/down changes the new-job budget when Local lab is selected. Triangle opens
the local library. Square pauses active work or refines a saved project through a new plan and
approval; the existing project stays intact. Interrupted jobs can be explicitly
resumed. L1+R1 leaves a running local app after saving; the same chord from the main
interface closes MuseStation. State saves are atomic and integrity-checked; at
most the last 15 seconds of unsaved state may be lost on a process failure.

All Local disables client HTTP, phone serving, external inference and speech,
installer recovery and uploads. Its bounded private log directory is never
uploaded when switching to Hybrid. Existing configurations without build_mode
retain Hybrid behavior. Mode changes apply through the idle worker; they refuse
an active/queued job or installation. The UI never presents an unconfigured
account connection as working. See [account connection research](ACCOUNT-CONNECTIONS.md).

Keep MuseStation open with the console awake for unattended work. Render and input
run separately from inference, but that does not establish execution during rest
mode or after launching another title. File checkpoints resume source/phase;
model decoding restarts without a saved KV cache. No multi-day hardware soak test
has occurred.

## Included behavior

- Native fullscreen home, plan, setup, progress and error screens.
- DualSense R2 push-to-talk through sceAudioIn, 48 kHz PCM16 mono. Mute/no-device
  fails visibly. Capture, inference and networking run off the rendering thread.
- Bundled Qwen2.5-Coder 1.5B Q4_K_M, Whisper tiny.en, Qwen3-TTS 0.6B with its
  projector, eSpeak NG voice data and component licenses. The native executable
  links real Vulkan/RADV inference and OpenGL graphics. Automatic GPU selection,
  explicit CPU recovery and required-Vulkan policies are configurable. PS5LM HTTP
  remains an optional fallback; the standalone PS5LM application is not required.
- Local transcription; in current Hybrid source, Muse creates the plan without
  a local coding model. Only the final Muse response becomes the plan. Square preserves parent plan/revision for a refinement request.
- X approval after release and review of every plan page. Circle invalidates stale
  local results. There is no server cancellation endpoint, so already accepted
  server jobs can continue. A transfer already dispatched cannot be cancelled.
- Before an approval POST, query ShadowMountPlus on this console for API v1 with
  `list_games` and `installed_pkg_games`, then read its game list without sizes.
  A malformed, duplicate, truncated or oversized list stops approval. The relay
  allocates against the reported IDs, permanent job reservations and the fixed
  MuseStation/ProsperoStore IDs. The source API does not attest a complete or fresh
  census, so the final installed-title refusal remains required. See the
  [inventory contract](MUSE-EXCHANGE.md#title-inventory-before-approval).
- Voice classification with an independent explicit-utterance guard. The bundled
  model actually misclassified unrelated questions in testing. Approval now also
  requires exactly “approve”, “approve this plan”, or “build it” after conservative
  normalization. Negation, questions, conditions and bare “yes” are rejected.
  Voice approval remains disabled until the owner verifies it on the console.
- SHA-256, size and identity checks before launching generated builds. Native
  packages are streamed to disk and verified, then a fresh authenticated helper
  submits installation, polls readiness and requests launch. Only a matching
  running-title observation reports execution. Generated apps use new IV0000 /
  PPSA99xxx identities; existing titles, MuseStation (`PPSA99576`) and
  ProsperoStore (`PPSA99000`) are rejected.
  The ELF fallback validates ELF64/x86-64 and sends bytes to the configured loader;
  loader TCP completion alone is not proof of execution.
- Local Qwen3-TTS WAV playback, with CPU retry where allowed and eSpeak fallback.
  Qwen/Whisper are released before neural synthesis and lazily reloaded afterward.
  Speech models are released after each request. If local synthesis fails and Muse
  supplies a validated plan.reply_wav path, the worker fetches that same-server
  PCM WAV. Response-controlled URL authorities and redirects are rejected.
- Paired phone page on an explicitly configured port (disabled for new configs). WAV upload works over LAN HTTP;
  browser microphone capture requires a trusted HTTPS reverse proxy. Pairing is
  required and phone input cannot directly approve or launch. Do not publish this
  listener on the public internet.
- JSONL journals are fsynced; handled failures/session ends upload immediately,
  with periodic retries and startup recovery. Partial trailing records are repaired.
  Clean close flushes and joins the worker. A killed process/kernel crash cannot
  itself perform HTTP: its marker is reported on the next launch. Immediate crash
  detection while the process is dead would require a separate supervisor.

## Configuration

In Hybrid setup, the controller keyboard collects console IP, relay origin, the separate
64-hex relay console key, PS5LM origin,
local speech fallback origin, the actual AudioIn module path and **ShadowMount API
on this console** (`shadowmount_url`). The latter defaults to
`http://127.0.0.1:10101`; `MUSE_SHADOWMOUNT_URL` overrides it in file-based setup.
Only loopback or the exact configured console IP is accepted. The client sends
no relay/provider credentials to this API. Both inference/speech fallback
origins can be left blank: the bundled engines require neither service. AudioIn
may also be blank for typing; voice capture needs the actual module path from
the installed native environment. The program does not invent that path.
A pre-created configuration from scripts/configure.py also collects all payload
status reports and mutable paths. Payload reports default to unknown and never
become “verified” merely because a port is open.
The relay key is stored in private configuration and sent only to the configured
relay API, including installer log recovery. It is not forwarded to model/speech
fallbacks or ShadowMountPlus. The relay origin must be a private or loopback numeric HTTP origin;
public origins are rejected. This key is distinct from provider/account credentials.
For file-based setup, set `MUSE_RELAY_TOKEN_FILE` to a private local copy of the
relay's `console-token` file when running `scripts/configure.py`. The output config
is private and must stay outside packages, source archives and shared exchange
folders. Existing Hybrid configurations need this new field before reconnecting;
existing Local configurations still work without a relay key.
Existing Hybrid configurations inherit the SMP default when the field is absent;
they need a compatible reachable SMP API before a new approval. The relay returns
428 for a new allocation from an older client that omits inventory. An exact
previously accepted approval retry can recover its old job without reallocating.

Native defaults use the OS application mounts: assets under /app0/assets and
configuration under /download0/muse/config.json. These are mount defaults, not LAN
addresses or per-console installation paths. MUSE_ASSET_DIR, MUSE_DATA_DIR and
MUSE_CONFIG_PATH override them. Configured log/artifact/model directories and
module paths remain editable. Configuration is kept outside the installed assets.
The optional inference_backend setting accepts auto, cpu or vulkan_required;
speech_mode accepts neural or espeak. The title requests 4 GiB of writable download-data space (downloadDataSize=4096
MiB), allowing room for the client's 1 GiB package ceiling, logs and caches. The
current relay recipe limits generated packages to 128 MiB. Actual
free storage still depends on the console; downloaded artifacts are retained.
Mode changes take effect through the idle worker; externally edited settings take effect on reopen. L1+R1 closes cleanly, including
from setup. No firmware update or jailbreak payload installation is performed.

The local speech fallback expects whisper.cpp-compatible /inference multipart
WAV, which is not identical to PSllama's base64-JSON service. The in-process Whisper
engine does not need that fallback service. See API-CONTRACT.md for exact proposed
Muse JSON schemas, parent context and optional reply WAV support. Server integration
has not been tested against Muse's actual implementation.

## Runtime adaptations and limits

The GPU build uses the pinned platform's dynamic direct-memory allocator instead
of the earlier fixed 2 GiB UI heap. Its virtual reservation is not a physical
allocation. Logs report current/peak mapped bytes and libc fallbacks. Explicit
CPU mode disables Vulkan initialization and GPU offload. Returned GPU failures
can fall back; a driver abort or GPU hang requires restart and log recovery.
See GPU.md for source pins, driver coexistence and allocation details.

The Linux neural-speech harness used about 2.4–2.5 GiB peak RSS. Those host CPU
measurements establish synthesis and output correctness, not console memory or
latency. Neural speech writes a complete WAV before playback; it does not promise
streaming first audio. English transcription and baked font coverage remain the
baseline. See INFERENCE.md and inference/NEURAL-SPEECH.md for recorded tests.

The installer helper is a separate payload, bootstrapped through the local ELF
loader with a one-use secret and loopback callback. Package bytes are never sent
as ELF. It does not install jailbreak components, inject into system processes,
uninstall titles or apply guessed firmware patches. Its native installation and
launch calls must have permission in the owner's actual 12.40 environment.
The helper loads `/system/common/lib/libSceAppInstUtil.sprx` at runtime and resolves
Initialize, Terminate, AppExists, InstallByPackage and GetInstallStatus through
the SDK's `kernel_dynlib_dlsym` for its own process. All five must resolve before
initialization. Authenticated START, verified package access and a successful
durable journal upload precede module loading. READY only establishes the IPC
connection; it is not proof that installation capabilities are available.
Load, missing-symbol and initialization failures produce specific retained logs;
no install is submitted on those paths. The helper has no AppInstUtil startup
dependency. This follows the payload-specific
[ProsperoStore implementation](https://github.com/darkxex/ProsperoStore/blob/e8b9b09fcd54cce27ea1bcda99a57af9df0fb4b4/helper/main.cpp#L152-L174),
which reports a direct-import startup failure on 12.70. It does not prove the
same failure or this workaround on 12.40.
Accepted submission, ready-to-play status and observed running title are distinct;
completion is rechecked before launch. Playable status is informational; the
automatic handoff waits for full completion to retain the source sandbox while
the OS copies package data. The parent obtains mount metadata through a checked runtime libkernel _fstatfs
lookup; it does not import unsupported statfs or issue a raw filesystem syscall.
The parent supplies that metadata so the helper can find and verify the same
package without guessing a sandbox instance. It passes already-open writable
journal, active-marker and lock descriptors through authenticated Unix IPC. The
helper proves journal access before submitting installation; unsupported access
fails explicitly, with optional path overrides for unusual loader environments.
The helper journal survives Muse's launch handoff and is recovered automatically
on reopen or periodic retry.

## Rebuild on Opti3

From /home/opti3/services/ps5-muse, use the following sequence for a Git developer
checkout. For an extracted corresponding-source archive, replace the
`bash scripts/build.sh` line with:

```sh
mkdir -p logs dist
docker build -t ps5-muse-toolchain .
```

Also replace the `scripts/package-image.sh` invocation below with:

```sh
docker build -t muse-package:station packaging
```

Continue with all the other commands below unchanged. The archive contains the
pinned SDK and packager source snapshots but omits their Git metadata; their
checkout bootstrap scripts therefore do not apply. The direct toolchain image
build still compiles the SDK and its hello-world sample. The native pipeline does
not depend on the separate payload test artifacts produced by scripts/build.sh.
See [the archive build instructions](../packaging/SOURCE-BUILD.md) for the source
manifest and downloaded dependencies.

```sh
bash scripts/build.sh
bash scripts/fetch-native-ui.sh
python3 scripts/inference-fetch-sources.py
python3 scripts/inference-fetch-models.py
python3 scripts/fetch-lua.py
python3 scripts/neural-fetch-models.py
python3 scripts/inference-fetch-gpu.py
docker build -t ps5-muse-native -f native/Dockerfile .
docker build -t ps5-muse-gpu -f inference/gpu/Dockerfile .
docker run --rm --memory=3g --memory-swap=3g --cpus=2 \
  -v "$PWD:/work" ps5-muse-gpu bash -lc '
  bash scripts/inference-build-data.sh
  bash scripts/inference-build-gpu-driver.sh
  MUSE_GPU=1 MUSE_BUILD_JOBS=2 bash scripts/inference-build.sh
  MUSE_NATIVE_VERSION=01.005.000 MUSE_GPU=1 MUSE_BUILD_JOBS=2 bash scripts/native-build.sh
'
MUSE_PACKAGE_IMAGE=muse-package:station bash scripts/package-image.sh
MUSE_PACKAGE_IMAGE=muse-package:station MUSE_PACKAGE_MEMORY=5g \
  bash scripts/package.sh build/native/title/PPSA99576 dist/rebuilt-01.005.000
python3 scripts/verify-musestation-release.py dist/rebuilt-01.005.000 --version 01.005.000
bash scripts/package-source-deps.sh
bash scripts/package-source.sh dist/rebuilt-01.005.000/musestation-corresponding-source.tar.gz
```

Run tests/client_run.sh, scripts/local-app-check.sh, scripts/inference-check.sh and scripts/native-preview.sh
inside their documented Docker images. Additional neural tests and source pins
are in inference/NEURAL-SPEECH.md. There is no console deployment in these build
commands. The native and package scripts never turn a payload ELF into a native
application by renaming it. See PACKAGING.md for generated-title build commands.
ZIP delivery is deferred, including any future update path. Current source does
not replace an installed title's directory to update it; see the
[planned transaction requirements](PACKAGING.md#deferred-zip-delivery-and-updates).

Storage metadata units were checked against the primary
[PS5LM-Plus change](https://github.com/abhi245y/PS5LM-Plus/commit/02bf3b9fc83a1080614064cdddd94a19dab99286)
and its [value parser](https://github.com/abhi245y/PS5LM-Plus/blob/6729b4acbaf65e23b2e8bc8708b2acba524a7aa8/ps5/app/main.cpp#L991);
the public packager's XML comment calling this field bytes is inconsistent with
that implementation. This is a requested title quota, not measured free space.
