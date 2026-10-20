# MuseStation local jobs and Muse client contract

Current source defaults to **Hybrid / Muse**; experimental Local mode keeps its jobs, inference and logs offline. The Opti3 relay implements transcript submission, asynchronous plans, revision-bound approval, compilation, packages and log storage. [MUSE-EXCHANGE.md](MUSE-EXCHANGE.md) is authoritative for the current relay schemas, client-generated request IDs, separate console/provider credentials and limits. Earlier optional audio/model endpoints below describe proposed adapters, not connected Muse services. Host fixtures exercise the implemented client/relay; Muse's production connector and PS5 behavior remain unverified.

## Local jobs and mode changes

`build_mode` is `local` or `hybrid`; `hybrid_provider` is `muse` or `codex`. New defaults are Hybrid / Muse, phone listener disabled. Explicit saved Local configurations keep Local mode and their private journals. A schema-1 config without `build_mode` retains its previous Hybrid / Muse behavior and legacy phone default; upgrading never silently changes an existing installation's network behavior. `MUSE_BUILD_MODE` and `MUSE_HYBRID_PROVIDER` control CLI setup. Codex is only a reserved provider selection: pairing, device authorization, and build transport are **not implemented**. Selecting it cannot send builds and returns an explicit unavailable error.

Local mode rejects every shared HTTP call, streaming package download, ELF dispatch, installer start, and phone-listener start before opening a network connection. It never falls back to external PS5LM, speech, or Muse audio. Blank console IP, server URL, and AudioIn module are valid for Local typed requests; a microphone request with no AudioIn path reports the needed setting. Nonempty origins still require the validation below. Model and writable storage paths remain required.

`Task::configure` runs only on the idle serialized worker and refuses an active installer or queued local job; pause a job first. Selecting Local suppresses old journal uploads before closing that journal. The result explicitly reports whether the new config was applied, independently of a subsequent journal-open failure. Changing mode unloads the engine and returns `needs_init`, so Hybrid releases the coder and Local can load it. Changing model/cache/backend/speech settings also returns `needs_init`; changing only endpoints preserves the engine. The UI owns saving accepted settings and phone-listener lifecycle. Local private logs use `<log_dir>/local/pending.jsonl`; a bounded previous file is retained when the current file exceeds 7 MiB. Those files never become upload spools after a later mode switch. Hybrid logs retain the delivery behavior described below.

Local projects live under `<artifact_dir>/projects/<random-128-bit-id>/`, with at most 64 projects. Each request is bounded to 4,096 bytes, each plan to 4,096 bytes, and each Lua source to 65,536 bytes. A new typed or spoken request is drafted locally and checkpointed before its plan can be displayed. Add-detail creates a fresh saved plan including the parent plan and requested change, checks the parent revision, and requires new approval. Approval binds the exact SHA-256 revision of request, plan, attempt budget, and active-time budget. Model classification alone is insufficient; the same physical/verified-voice gate applies.

The state sequence is `awaiting approval → queued → generating → queued → validating → ready`. Validation failure queues another generation with the actual diagnostic and previous candidate. Each attempt logs its seed derived from the saved job ID and attempt number; identical GPU output across platforms is not promised. An oversized model context fails visibly rather than silently truncating source or repeating the same impossible repair. The local model must explicitly finish generation before a candidate can enter validation; partial, stopped, or truncated output is never marked ready. The isolated Lua validator applies the runtime's memory/instruction limits and 60 simulated input frames. A display that remains provably empty or a uniform background across every probe frame is rejected for repair. Definite hidden text draws in an otherwise nonuniform app are reported in the saved job detail. These are bounded checks, not a complete contrast or legibility test; passing them does not prove the requested game is correct. `ready` still requires a separate explicit Play action; no local job auto-launches. Source SHA and the approved revision are checked again on Play. This format runs inside MuseStation; it is not offline C compilation or native `.pkg` generation.

Every state transition is fsynced into alternating checksum-protected atomic checkpoint slots. Source revisions are separate immutable hash-named files, committed by temporary-file fsync and rename. Retention protects the candidate and validated source named by each checksum-valid checkpoint slot (at most four canonical files per project). Unreferenced hash-named revisions and abandoned store temporary files are pruned before loading, starting another phase, or writing more source. A source write can temporarily add one revision and its temporary file; failed cleanup prevents further writes. Other files, including saved app state, are not pruned. This bounds store-owned source data to at most 384 KiB per project during a write, at most 24 MiB across 64 projects, excluding checkpoints/app state/models. Older unreferenced repair attempts are not an unlimited source archive. During generation, partial source is checkpointed at intervals of 512 generated tokens; pause/cancel also preserves available partial source. Recovery resumes semantically from source and diagnostics, not a serialized model KV cache. An interrupted queued/generating/validating state becomes `interrupted` on explicit load and requires explicit Resume. There is no automatic job restart after power loss or a crash. File data is fsynced; platforms that reject directory fsync cannot provide a stronger directory-durability guarantee without hardware evidence.

Budgets are 1–512 generation attempts and 1–604,800 active seconds. The worker durably reserves up to 1,800 seconds before generation or 10 seconds before validation, then refunds unused reservation after a normal phase completion. A crash keeps the reservation charged, so repeated restarts cannot reset the budget. Inference and validation observe an independent atomic pause/cancel flag; an in-flight GPU operation must return before the pause takes effect. A budget exhausted or cancelled job cannot silently receive more work. Pause is resumable; Cancel is terminal. Keeping MuseStation open allows bounded work over many phases. Running while another title is active, in rest mode, or after process termination is **not implemented or hardware-verified**.

The worker and Lua runtime have host integration fixtures; those fixtures use a controlled model result to test orchestration. Separate inference tests exercise the actual bundled model. Neither establishes PS5 runtime behavior.

## Muse account integration is a separate adapter

The official Muse Gadget SDK at commit `e5c4662242c23a5a786cf49f75626ae31283bbc1`
uses BLE provisioning plus authenticated cloud gadget sessions. It does **not**
implement the REST paths or response schemas in this document. A device paired
with Muse is not automatically a build service. An adapter must connect Muse
commands and messages to plan authority, explicit approval, compilation and the
artifact store. See [Hybrid-first status](HYBRID-FIRST.md). The existing LAN
configuration is for manual integration tests; it does not establish an account
connection and must not carry SDK/account credentials.

## Transport and configuration

The Muse server uses an HTTP origin configured at install time. `ps5lm_url` and `speech_url` remain required string fields in configuration but may be empty: blank disables those optional external fallback services, because bundled inference needs neither. A nonempty fallback origin must pass the same validation as the server URL. Calling a disabled fallback returns a clear local “not configured” error without network or file access. The current transport supports numeric IPv4/IPv6 addresses and `localhost`, including configurable ports. HTTPS and DNS hostnames return explicit errors; there is no silent downgrade. This keeps DNS resolution from exceeding the request deadline. Example syntax uses placeholders, not configured addresses: `http://<server-ip>:<port>`, `http://[<ipv6>]:<port>`.

Ordinary buffered calls have a single monotonic deadline spanning connect, headers, request upload, and response body (`timeout_ms`, default 10,000; range 1,000–60,000). HTTP response headers are limited to 8 KiB. JSON is limited to 32 KiB, 512 tokens, and 32 nesting levels. JSON rejects duplicate keys, invalid Unicode, embedded NUL strings, and malformed/overflowing numbers. HTTP accepts Content-Length, chunked, or connection-delimited responses; conflicting lengths and compressed bodies are rejected. Redirects are never followed.

Raw LAN HTTP has no authenticated integrity or confidentiality. SHA-256 below detects corruption and mismatched artifacts, but is not a signature and does not protect against a party that can replace both the manifest and artifact. This implementation should not be exposed directly on the public Internet. No bearer/API keys are required or written to logs by the client.

`muse_config_load/save` uses JSON schema version 1. Fields include `build_mode`, `hybrid_provider`, `console_ip`, `server_url`, `ps5lm_url`, `speech_url`, `log_dir`, `artifact_dir`, `model_dir`, `audio_in_module`, `firmware_reported`, optional `install_helper_path`, `inference_backend`, `speech_mode`, `model`, `voice_gate_verified`, `timeout_ms`, `loader_port`, `phone_port`. Firmware must be reported as `12.40`; configuration is not proof of compatibility or payload presence. Payload objects retain owner-reported versions under `payloads.{elfldr,etahen,kstuff_lite,shadowmountplus,ftpsrv}.user_reported`; every stored `verified` flag remains false. Voice gate verification defaults false and is never enabled by a model response. Paths must be absolute and reject parent traversal.

`inference_backend` accepts `auto` (default), `cpu`, or `vulkan_required`. `speech_mode` accepts `neural` (default) or `espeak`. These optional fields preserve old configurations and permit hardware recovery without a rebuild. The command-line setup script accepts `MUSE_INFERENCE_BACKEND`, `MUSE_SPEECH_MODE`, and `MUSE_INSTALL_HELPER_PATH` environment overrides without adding wizard questions. A blank helper path is filled from the native title's bundled asset location. Its `.payload` extension prevents the package builder from interpreting the helper as another native title.

Optional `installer_artifact_dir` and `installer_log_dir` are helper-visible path overrides, blank by default. The setup script accepts `MUSE_INSTALLER_ARTIFACT_DIR` and `MUSE_INSTALLER_LOG_DIR`. The former identifies the helper's view of the configured artifact directory; the latter identifies the helper's view of `<log_dir>/installer`. They must refer to the same underlying files as the title's paths, not independent copies. Normal installation first tries runtime mount discovery, so these are recovery settings for a loader that cannot provide a usable mapping.

Configuration saves to a mode-0600 temporary file, fsyncs, and atomically renames it. A stale `.tmp` causes an explicit save error rather than silently replacing it. The caller chooses the writable config location and supplies packaged defaults separately.

On the native title's first launch, Local mode permits typed requests with no console IP, server URL, or AudioIn module. Enter AudioIn only to enable the microphone. Hybrid / Muse setup requires its connection details; neither optional fallback origin is prefilled. Storage defaults to `/download0/muse` (`MUSE_DATA_DIR` override), configuration to its `config.json` (`MUSE_CONFIG_PATH` override), and packaged assets to `/app0/assets` (`MUSE_ASSET_DIR` override). Model and helper paths derive from the assets directory, and writable log/artifact paths derive from storage. Backend defaults are `auto` and `neural`; voice approval remains disabled until owner verification. The CLI setup additionally collects the owner-reported payload versions; the native wizard leaves those as `unknown`.

## Stable Muse endpoints

### Submit captured audio

`POST /voice`, `Content-Type: audio/wav`: body is a RIFF/WAVE file, capped at 16 MiB. This is the fallback when local speech is unavailable.

```json
{"transcript":"make me Tetris","plan_id":"p1"}
```

The client immediately fetches `/plan/p1`; it does not display a local draft as an authorized server plan.

### Submit local transcription and optional draft — proposed extension

`POST /voice`, `Content-Type: application/json`. Current Hybrid sends an empty draft and gives Muse full plan authority; the coding LLM is not loaded:

```json
{"transcript":"make me Tetris","draft":"","source":"on_console"}
```

For a detail/revision request, the proposed JSON extension includes both `parent_plan_id` and `parent_revision`:

```json
{"transcript":"Add a two-player mode","draft":"","source":"on_console","parent_plan_id":"p1","parent_revision":"r1"}
```

`muse_client_transcript_context` validates and sends both identifiers together. The original `muse_client_transcript` function omits both fields, preserving the initial-request behavior. Muse must resolve the prior plan and apply the requested changes instead of treating the utterance as an unrelated new project. An audio-only fallback does not carry this JSON context; callers should report that limitation rather than silently lose the revision relationship.

The response is the same transcript/plan-id object as audio upload. Transcript and draft strings are capped at 4,096 bytes; plan IDs are capped at 128 characters. Muse must create or review the plan and return a server-owned plan. An empty draft is valid. For an initial request, the native worker falls back to the captured WAV when this JSON variant returns HTTP 404 or 415. Revision requests fail visibly rather than dropping their parent context. Local transcripts must never be treated as permission to build.

### Fetch the authorized plan

`GET /plan/p1`:

```json
{"id":"p1","revision":"r1","text":"Build a falling-block game. Use the D-pad and face buttons."}
```

Plan text is 1–4,096 bytes; the text is intended to contain two to four sentences. IDs and revisions use `[A-Za-z0-9_-]`, 1–128 characters. The returned ID must equal the requested ID. The server remains the authority on plan wording.

A proposed optional `reply_wav` field may identify a server-rendered speech file, for example `"reply_wav":"/replies/p1.wav"`. It must be a same-server absolute resource path beginning with a single `/`, at most 511 bytes, containing only ASCII letters, digits, `/`, `_`, `-`, and `.`; `..` is rejected. Authorities, schemes, percent escapes, backslashes, queries, fragments, and control characters are rejected. The client never accepts an arbitrary remote URL. Absence leaves `muse_plan.reply_wav` empty. Muse must supply this field and the resource for a native WAV fallback to become available; no new fixed endpoint is invented.

The reply resource must return an uncompressed RIFF/WAVE file no larger than 8 MiB, containing mono PCM16 audio at 8–96 kHz and 0.1–60 seconds in duration. The worker validates this before native playback.

### Approve exactly the displayed revision

After the gate enters `MUSE_APPROVING`, send `POST /plan/p1/approve`:

```json
{"revision":"r1","idempotency_key":"p1:r1","artifact_kind":"pkg"}
```

Response:

```json
{"job_id":"j1"}
```

The proposed `artifact_kind: "pkg"` field requests a native generated title; Muse must honor it to use the automatic installer path. A legacy ELF response remains supported. Muse must reject stale revisions and make identical idempotency keys return the original job without starting another build. The client does not automatically retry an uncertain approval request. A local cancel invalidates pending UI results; the fixed server API has no cancellation endpoint, so it cannot guarantee cancellation of a job already accepted by Muse.

Physical X requires release followed by a new press after displaying the plan. Voice approval requires the owner's hardware-verification flag, matching plan generation, an `approve` classifier result, and the actual transcript matching one of three explicit commands: `approve`, `approve this plan`, or `build it`. ASCII case and whitespace normalize; one final period or exclamation mark is allowed, a question mark is not. Bare `yes`, questions, negation, quoted instructions, and extra clauses cannot approve. Native callers must use `muse_voice_utterance`, not the lower-level trusted-intent `muse_voice` helper. Model output alone cannot enable verification or authorize a build.

This deterministic check is necessary: host inference tests with the actual bundled Qwen model classified both “Add a two player mode” and “What colors will it use?” as `APPROVE`. Prompting alone was insufficient. The engine and core both veto approval unless the spoken command matches the explicit allowlist; the model still performs its classification first.

### Poll build status and verify the artifact

`GET /build/j1/status` returns one of `transcribing`, `building`, `done`, `error` as `state`. An optional `detail` string is limited to 1,023 bytes. A completed build must include:

```json
{"state":"done","kind":"elf","size":123456,"sha256":"<64 lowercase hex characters>"}
```

`GET /build/j1/artifact` returns the exact ELF bytes. The client rejects files over 128 MiB, length/hash mismatches, non-ELF files, non-little-endian or non-x86-64 executables, malformed program-header extents, oversized load segments, and an entry point outside executable load memory. Accepted bytes are fsynced to a temporary file and atomically renamed within the configured artifact directory.

Generated native applications may instead use this proposed manifest extension:

```json
{"state":"done","kind":"pkg","size":23456789,"sha256":"<64 lowercase hex characters>","title_id":"PPSA99991","content_id":"IV0000-PPSA99991_00-MUSEAPP000000001"}
```

Package identity is deliberately restricted to new homebrew titles: `PPSA99` followed by three digits, publisher `IV0000`, content label `_00`, and a 16-character uppercase ASCII/digit content suffix. Muse's own `PPSA99576` is rejected; this is not a self-updater. Muse must allocate a new unused title ID for each generated application. The client verifies the debug FIH header, PFS/CNT extents, embedded CNT content ID, exact length, and full SHA-256, both after download and again in the installer helper. It does not claim full package signature or filesystem validation; the system installer remains responsible for accepting the package format.

Packages are capped at 1 GiB and stream to a mode-0600 temporary file using a 64 KiB buffer, with no package-sized allocation. The package response must be `200` with one exact `Content-Length`, no transfer encoding, and no content encoding other than identity. Package downloads use the configured stall deadline per read and a ten-minute total deadline. A truncated/malformed response, hash mismatch, or storage error deletes only its temporary download. A valid download is fsynced and atomically renamed. ELF behavior and its 128 MiB cap are unchanged.

Muse can use `muse-package:upgrade build-generated /input /output` for native generated titles that do not bundle the gadget's models; the default gadget build still requires the bundled models. This is a packaging integration route, not a server implementation.

### Install and launch a generated package

The native worker additionally binds each artifact action to the job ID and UI generation returned by its successful approval request. Installation accepts only the same cached path and manifest that this worker downloaded and verified. An old generation, a changed manifest, or a raw arbitrary path is refused before helper submission.

`muse_install_begin` starts one fresh bundled helper through the console's loopback ELF loader at the configured loader port. Its callback listener binds an ephemeral loopback port. A fresh 256-bit nonce is patched into a unique bootstrap record in the trusted helper; the helper must authenticate its callback before the parent sends any path or install instruction. It serves only that connection and one installation, with bounded IPC messages/deadlines. Nothing is exposed as a public LAN installer endpoint.

Title-local paths such as `/download0` are not assumed to exist in the helper's filesystem namespace. The title resolves the common libkernel `_fstatfs` export at runtime through `sceKernelDlsym`, opens the existing file/directory, and uses the returned filesystem identity and mount-relative path. A missing export, failed call, or unusable mount record leaves only the direct-path probe or configured alias available. There is no native `statfs` import or direct syscall fallback. The helper reads at most 512 `getfsstat` mount records, tries only matching identities, and verifies the resulting package's full hash/content identity. A fresh nonce file proves the journal directory refers to the title's actual storage. This does not guess a `/mnt/sandbox/<title>_<instance>` prefix or recursively search files.

The default journal route uses an authenticated Unix-domain socket under that proven directory. The title opens its spool, active marker, and lock file for writing and transfers exactly those three descriptors using `SCM_RIGHTS`. The helper rejects missing/truncated control data, incorrect nonces, duplicate files, non-regular files, and non-writable descriptors, then acquires its own process-owned file lock. Its descriptor logger appends, fsyncs, reads/uploads, and truncates through those open descriptors without reopening read-only sandbox paths. An active-marker byte distinguishes clean closure from crash recovery and remains compatible with legacy empty markers. Parent closure of its copies does not close the helper's descriptors.

Preparation completes before the separate install command. If descriptor transfer is unavailable, a nonce read/write probe must prove the same journal is writable by pathname before the fallback proceeds. If mount discovery, package identity, descriptor access, and the shared-path fallback cannot establish the required capabilities, installation fails explicitly before any OS install call. The error identifies artifact or journal configuration to fix. Native namespace mapping, Unix socket support, and descriptor behavior still require PS5 hardware validation; Linux tests do not prove these platform capabilities.

The helper calls the public community-documented `sceAppInstUtilInitialize`, `sceAppInstUtilInstallByPackage`, and `sceAppInstUtilGetInstallStatus` APIs. It repeats the package identity/hash verification and calls `sceAppInstUtilAppExists` before submission. Only a successful query that writes an explicit absent flag permits a new installation. Existing titles and an unchanged/invalid output sentinel are refused. This version intentionally does not overwrite/update or uninstall any existing app. An accepted install cannot be undone by closing Muse; closing stops monitoring and the OS task may continue.

`Result.install.submitted` means the OS call accepted an installation request; a returned content-ID mismatch retains this flag but fails the operation. `installed` means the matching system status currently reports `playable` or `completed`; it is informational and does not authorize launch. `completed` is true only for the native `completed` state. PlayGo `playable` can precede full installation, so both the client and helper require `completed` before auto-launch; the helper repeats that query immediately before submitting launch and refuses a regression. Numeric progress comes from the native download/copy fields, or is `-1` when unknown; only `completed` forces 100%. Keep Muse open while installation finishes: its sandbox contains the package source, and closing can interrupt the OS copy. These states do not establish game behavior. Native errors, status errors, communication loss, and the thirty-minute monitoring deadline remain failures with the prior accepted-state flags retained. No automatic retry resubmits an uncertain installation.

Launch requires a matching successful install session. The helper gets the foreground user and calls `sceSystemServiceLaunchApp` with a live, correctly sized context. It never kills another app or injects into a system process. An accepted launch sets `launch_requested` only. For up to thirty seconds, it queries `sceSystemServiceGetAppIdOfRunningBigApp` and `sceLncUtilGetAppTitleId`; only a matching title sets `running_observed` and records the returned app ID. Failure to observe the title is reported as unconfirmed, not successful. OS privilege restrictions or unavailable firmware exports cause explicit failure; there are no guessed firmware patches or fabricated fallbacks.

The helper keeps a separate durable JSONL spool under `<log_dir>/installer` and uploads installation/launch transitions itself. During launch observation it continues if the parent disconnects or suspends. Before launch, a disconnected parent makes the watchdog exit the helper; already-fsynced records and its active marker remain for recovery, and accepted OS tasks are not cancelled. A file lock prevents simultaneous helper writers or recovery readers. The native worker automatically retries a retired helper's spool at startup and its normal log-retry interval; a live helper's spool is skipped. Fatal process loss still uses the durable crash-marker recovery mechanism. Filesystem revocation, console power loss, or loss of the network can still prevent an immediate final upload. Installer errors include the native hexadecimal code and retained accepted-state flags.

Primary source/ABI references checked at implementation time:

- [ps5-pkg-manager 914311f](https://github.com/itsPLK/ps5-pkg-manager/tree/914311f945d14a555e1c09635346bb069c3368e8): `include/install_appinst.h`, `include/install_service.h`, `src/install_helper.c`, `src/install_process.c`, `src/installer.c`, and `src/pkg_parser.c`. These establish the helper context, package/status structures, direct filesystem URI, FIH/CNT identity location, and OS `playable`/`completed` status handling.
- [ps5-payload-manager 3f881e8](https://github.com/itsPLK/ps5-payload-manager/blob/3f881e8e569eba3054d1784a1962439b2eee92c2/src/ps5_launcher.c): running-title query ABI.
- [ps5-payload-hbldr](https://github.com/john-tornblom/ps5-payload-hbldr/blob/master/hbldr.c): SystemService launch-context ABI. This implementation sets its structure size and retains all arguments during observation.
- [flatz AppInstUtil reference](https://github.com/flatz/ps4_remote_pkg_installer/blob/master/installer.c): output-flag `AppExists` ABI; the PS5 SDK exports that symbol. Its PS5 behavior remains hardware-unverified, so the helper requires an explicit valid output rather than assuming absence.
- The pinned SDK's `include/freebsd/sys/mount.h` defines the standard `fstatfs` ABI; `sce_stubs/libkernel.c` lists `_fstatfs` and `sceKernelDlsym`. The native title checks that export dynamically, since common libkernel does not export `statfs`. The SDK's `libc/syscalls.c` supplies `getfsstat` for the payload helper, whose CRT establishes raw-syscall permissions. `sys/socket.h` and `sys/un.h` define Unix sockets and `SCM_RIGHTS`; the complete helper links `sendmsg`/`recvmsg`. The pinned native UI's `docs/CONSOLE_VALIDATION.md` records the distinct title/payload path views and why writable descriptors are preferable to assuming external sandbox-path writes. [PS5LM-Plus 6729b4a](https://github.com/abhi245y/PS5LM-Plus/blob/6729b4acbaf65e23b2e8bc8708b2acba524a7aa8/CHANGELOG.md) independently documents unavailable native `statfs` and fabricated `statvfs` capacity values; neither is used for a capacity or namespace assertion here.

All installer and launch claims above describe implemented checks. No console has been used to verify firmware 12.40 installation, native API privileges, title suspension behavior, or launch compatibility.

`muse_client_dispatch` revalidates the ELF structure and sends it to configured `console_ip:loader_port` (default 9021). Successful return means the TCP send completed. The loader protocol provides no execution acknowledgement; it is not evidence that the payload ran or that the console is compatible.

### Upload durable logs

`POST /logs`, `Content-Type: application/x-ndjson`: raw JSONL. Any HTTP 2xx confirms receipt. Example:

```json
{"ts":1792411200,"level":"info","event":"plan_fetch","detail":"plan=p1 revision=r1"}
```

In Hybrid / Muse mode, the native app logs boot, voice submission, plan retrieval, approval, download, launch, failure, and session completion. The `ts` field is a numeric Unix timestamp in seconds from the console clock. The logger escapes strings, appends and fsyncs every event. It automatically tries upload on errors, crash markers, `session_end`, every sixteen new events, explicit close, and startup recovery. A failed upload keeps the spool. A confirmed 2xx permits truncation; upload delivery is **at least once**, so Muse should tolerate duplicates if acknowledgement is lost.

An `.active` marker detects an unclean prior process termination. On restart, the logger repairs a trailing partial JSON line, retries durable records, and records a `crash` recovery event without guessing the cause. Fatal signals, process kills, power loss, and network loss cannot guarantee an immediate HTTP POST; handled failures attempt immediate upload, while persisted records retry at next opportunity. The logger stops accepting new records once its roughly 7 MiB spool cannot flush, reporting an error instead of silently dropping old evidence. A log API error must be surfaced by the UI. The current journal has no offline rotation and cannot capture kernel crashes itself; FTP and klogsrv remain fallbacks.

No provider secrets or pairing codes should be supplied to `detail`. Identifiers such as the current plan, revision, and job should be included by the native caller so Muse can associate records with builds.

## PS5LM and local speech

Bundled inference is the native worker's primary path and uses its linked C API without HTTP. Current Hybrid uses only local voice and deterministic command matching; Local lab uses the bundled coder and prohibits HTTP. The legacy external-model client helpers remain available for older integrations, but the current native worker does not call them in Hybrid. Those helpers use configured `ps5lm_url` plus `/v1/chat/completions` for drafts and intent classification. Requests use `temperature: 0`, `stream: false`, `chat_template_kwargs.enable_thinking: false`, and optional configured model identifier. They parse `choices[0].message.content`. The optional model identifier is omitted when empty, permitting a server default model.

The classifier accepts only exact `approve`, `add_detail`, or `cancel` labels after surrounding whitespace is removed. Explanations, embedded labels, and thinking output fail closed to controller fallback. It is a prompted local classifier with a deterministic spoken-command approval veto, not a claim that a specialized d1 model or sub-second timing has been verified. Current Hybrid submits an empty draft for Muse to plan.

The optional external transcription fallback uses configured `speech_url` plus `/inference`, multipart field `file` with WAV bytes and `response_format=json`, compatible with the whisper.cpp server convention. Expected response: `{"text":"make me Tetris"}`. The caller must actually run a compatible on-console speech service; endpoint configuration does not provision or prove one exists. An unavailable endpoint reports an error for caller-managed server fallback. These two optional service routes are not Muse server endpoints.

Server-rendered WAV fallback is not specified in the original stable API brief. The optional `reply_wav` plan field above is the proposed extension; Muse must implement it. Native playback should use it only after local TTS fails and must validate the fetched WAV before playing it.

## Phone microphone fallback

`muse_phone_start` runs a bounded HTTP listener on configured `phone_port` (new default 0 disables it; 8088 is the legacy config migration default). It serves a responsive hold-to-talk page and receives WAV uploads at `/audio`, capped at 8 MiB. Each process generates a random 128-bit pairing token from `/dev/urandom`; the UI shows it, and the phone supplies it as `X-Muse-Pair`. Pairing codes are not embedded in the public page or logs. There is no approval/build route on this listener.

The callback executes on the phone thread and must only enqueue an audio path; returning zero accepts it. Accepted temporary files belong to the application and should be unlinked after processing. A busy callback produces 409 and deletes its file. This path feeds the same local speech/server fallback flow as DualSense capture.

Mobile browsers normally block `getUserMedia` on ordinary LAN HTTP. The page explicitly disables recording when the browser lacks a secure context and offers WAV upload instead. Hold-to-talk needs a trusted HTTPS reverse proxy or another browser-approved secure context. The console listener itself does not implement TLS. The JavaScript records mono audio and encodes a 16 kHz PCM WAV, capped at 45 seconds; no browser microphone behavior has been hardware-verified.

## Build and test evidence

`tests/client_run.sh` runs inside the existing `ps5-muse-toolchain` Docker image. It compiles Linux integration binaries with Clang AddressSanitizer and UndefinedBehaviorSanitizer, executes loopback fixtures, and compiles the core/phone/installer translation units and a complete installer-helper ELF with `prospero-clang -Wall -Wextra -Werror`.

Tests cover strict JSON and SHA-256 published vectors; configuration round-trip and HTTPS/DNS rejection; plan ID/revision validation; one approval submission; malformed/oversized/hash-mismatched artifacts; local model exact-label rejection; multipart speech; HTTP chunking, length conflicts, truncation, redirects, and total timeout; JSONL escaping, offline retention, partial-record and crash recovery; real gate transitions around one approval and a byte-exact transfer to a loopback ELF loader; and phone pairing/file delivery. The suite also host-compiles the actual native worker with bundled inference disabled and verifies its server WAV fallback, saved-audio validation, log flush, and clean thread shutdown. The package fixture additionally drives the actual helper process and authenticated IPC with test-only native API adapters. It covers package framing and hash/identity rejection, existing-title refusal, an accepted install without completion, native install/status/launch failures, content-identity mismatch, launch-before-install refusal, accepted launch with an unrelated running title, and observed matching-title success. Native-worker tests reject unapproved artifact actions. The helper's native API adapters are explicitly mocks, not console evidence. These tests establish host behavior and target compilation only. They do not establish PS5 runtime correctness, graphics performance, microphone compatibility, or end-to-end Muse server compatibility.
