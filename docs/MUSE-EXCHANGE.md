# Muse → Opti3 → MuseStation

Current contract, 2026-10-19. Muse develops the app; Opti3 saves and compiles the
approved job. MuseStation downloads the package and performs installation locally.
The inventory gate below is implemented in 01.005.000; see
[the release report](BUILD-REPORT-INVENTORY.md) for compile and package evidence.
Opti3 is not enrolled as a Muse gadget. The SDK/Bluetooth companion under
`companion/` is retained research and is not required or deployed for this route.

The implemented transport can receive files from a future shared-storage connector
or accept authenticated provider pushes. It does not call an undocumented Muse
cloud endpoint. A host-simulated request completed the
[Muse plan exchange](MUSE-CONNECT.md#request-to-plan-verification), with the exact
returned text and revision verified through the console API. A separate
[source/compile test](MUSE-SOURCE-TEST.md#verified-result) accepted actual Muse
source and produced a package whose readback and download were verified.
Approval/inventory were host simulations; PS5 operation remains unverified.
Requests wait until the connector supplies their plans.
Provider access can return a plan/source and read results; it cannot approve a plan.

```mermaid
sequenceDiagram
    participant PS5 as MuseStation
    participant Relay as Opti3 relay
    participant Muse
    participant Worker as Docker compiler
    PS5->>Relay: Locally transcribed or typed request
    Relay->>Muse: Saved request (folder / provider outbox)
    Muse->>Relay: Plan response
    PS5->>Relay: Poll plan; display for review
    Note over PS5: Read ShadowMountPlus title inventory
    PS5->>Relay: Explicit approval of exact revision + inventory
    Relay->>Muse: Approved job + assigned title identity
    Muse->>Relay: C++ source for that job and revision
    Relay->>Worker: One isolated compile/package attempt
    Worker->>Relay: Verified package + diagnostics
    PS5->>Relay: Download and independently verify package
    Note over PS5: Existing helper installs and launches on console
    PS5->>Relay: Structured session/install logs
    Relay->>Muse: Results and diagnostic/log exports
```

## Console API

The console URL is configured at installation, for example
`http://192.168.5.54:8860`. No IP or credentials are compiled into the title.
Every console request except `GET /health` and `GET /` requires
`Authorization: Bearer <console-key>`. The operator provisions a separate random
64-hex console key in a private file and the console's install-time configuration.
It is not the provider key or a Muse account credential. The default published
bind is loopback; select the intended private interface for console access.
Setup rejects public relay origins. HTTP does not conceal the key from a network
observer; this is a trusted private-network deployment, not Internet authentication.

| Request | Implemented response |
|---|---|
| `POST /voice`, JSON `{client_request_id,transcript,draft,source:"on_console"}` | `200 {transcript,plan_id}` after durable storage. `client_request_id` is `p` plus 32 lowercase hex digits and becomes `plan_id`. Identical retries return the same ID; changed content with that ID returns 409. Legacy callers may omit it. Hybrid sends an empty draft. Optional `parent_plan_id` and `parent_revision` must be provided together for refinement. |
| `GET /plan/<id>` | `202 {id,state:"planning",detail}`, then `200 {id,revision,text}`; explicit plan failure is `422 {error}`. A pending response cannot be approved. |
| `POST /plan/<id>/approve` | JSON `{revision,idempotency_key,artifact_kind:"pkg",inventory:{source:"shadowmountplus",api_version:1,title_ids:[...]}}` → `{job_id}`. New allocations require inventory; missing inventory returns 428 and invalid inventory returns 400. An already accepted exact approval returns its existing job without allocating another title, including an older retry without inventory. Stale, failed or pending plans are rejected. |
| `GET /build/<job>/status` | `{state:"building",detail}` while waiting/queued/compiling, `{state:"error",detail}` on failure, or the completed manifest below. |
| `GET /build/<job>/artifact` | Exact bytes with `Content-Length`. The client independently checks size, SHA-256 and package identity. |
| `POST /logs` | `application/x-ndjson`, maximum 8 MiB, records containing `ts`, `level`, `event`, `detail`; durable storage and content-based duplicate suppression. |
| `GET /health` or `/` | Operational JSON including exchange status and explicitly false Muse/hardware verification flags. |

The relay accepts local transcripts, not raw WAV. Raw audio fails clearly with
415 because a server transcription provider is not connected. DualSense/local
Whisper or typing is the working integration path; the phone/server-audio fallback
needs a real transcription adapter. Existing console speech stays local.

Completed manifest:

```json
{"state":"done","kind":"pkg","size":12345678,"sha256":"64-lowercase-hex","title_id":"PPSA99001","content_id":"IV0000-PPSA99001_00-MUSE0123456789AB"}
```

MuseStation retains one pending, unapproved request tied to its server URL. It
polls every two seconds, retries transient outages every five seconds, and stops
visibly after thirty minutes while retaining the record for reopening. Closing
and reopening resumes plan retrieval; cancellation removes only the local pending
record. It does not cancel a server request or accepted install. Before the first
POST, the console generates a random request ID and durably saves it with the
exact JSON body. After a lost response, `GET /plan/<client_request_id>` recovers
an accepted request. If that ID is unknown, it retries the saved body with the
same ID; it never generates a replacement ID for that retry. Recovery does not
approve anything.
Already-approved job recovery on the console is not implemented in this version.

Approval still requires the displayed revision and the controller gate. A key
authenticates the configured console installation; it does not attest that a
physical controller button was pressed. A short code returned by the same API
would not provide that attestation either. The provider credential cannot call
console endpoints, and neither credential belongs in plans, source or shared files.

### Title inventory before approval

The client reads ShadowMountPlus immediately before sending an approval. Configure
`shadowmount_url` in setup; its default is `http://127.0.0.1:10101`. Only loopback
or the console's exact configured LAN IP is accepted. These requests carry no
relay or provider bearer credential. There is no reason to publish the console's
ShadowMount API through Opti3.

The client first sends `POST /api/v1/version` with `{}` and requires HTTP 200,
`status:0`, `api_version:1`, and both `list_games` and `installed_pkg_games`
capabilities. It then sends `POST /api/v1/games` with `{"include_size":false}`.
The whole response must be valid and bounded, with `status:0`, `size_included:false`
and `count` equal to the array length. Truncation, duplicate IDs, invalid entries,
more than 4,096 titles or more than 4 MiB stops approval; no partial list is sent.
Every returned ID is retained regardless of its installed/source-available flags.
The normalized approval inventory contains only unique IDs matching
`(?:PPSA|CUSA|LAPY|FAKE)[0-9]{5}`. An empty validated list is permitted.

This is a collision observation, not proof of a complete or fresh title census.
At the pinned ShadowMountPlus 1.7beta4 source, the response has no boot ID,
generation, timestamp or completeness flag; `count` describes the returned list.
Its discovered-game cache has 512 slots, and registered entries without a resolved
package file may be omitted. Reading immediately before approval cannot prevent
another installation afterward. The final OS installed-title refusal remains
necessary. See the pinned [capability response](https://github.com/drakmor/ShadowMountPlus/blob/d7e35e6ce90abc6f9d0880ff40c2a5cc1fbfa075/src/sm_api_service.c#L595-L641),
[inventory implementation](https://github.com/drakmor/ShadowMountPlus/blob/d7e35e6ce90abc6f9d0880ff40c2a5cc1fbfa075/src/sm_api_service.c#L1232-L1342)
and [cache insertion limit](https://github.com/drakmor/ShadowMountPlus/blob/d7e35e6ce90abc6f9d0880ff40c2a5cc1fbfa075/src/sm_game_cache.c#L155-L184).

The relay stores the accepted snapshot privately with its job. It is not included
in provider events or folder exports. Provider input cannot supply this approval
report, although possession of the console key is not hardware attestation.

### Crash and failure logs

Hybrid uses a durable JSONL spool and an active-session marker on the console.
Handled failures attempt an immediate upload. After an unclean exit, startup
records the interrupted session and attempts to upload the retained journal
before normal session work. A failed upload retains it for retry. A process that
has died, a powered-off console or an unavailable network cannot send a POST;
next-launch recovery is the explicit path for those cases. A recovered marker
reports an unclean exit, not an inferred crash cause. Local lab logs stay local.

## Folder connector

The deployment sets `MUSE_EXCHANGE_DIR` to a local directory on Opti3. A later
connector may synchronize its inbound/outbound files. Never share the private
`data/`, worker socket, token file, or full project directory with Muse.

```
exchange/
  outbound/requests/<request_id>.json
  outbound/approved_jobs/<job_id>.json
  outbound/results/<job_id>.json
  outbound/results/<job_id>.diagnostics.json
  outbound/logs/<log_id>.json       # metadata
  outbound/logs/<log_id>.jsonl      # actual console records
  inbound/plans/<request_id>.json
  inbound/sources/<job_id>.json
  receipts/plans/<request_id>.json
  receipts/sources/<job_id>.json
```

Write each input to a temporary name that does not end in `.json`, flush/fsync,
then rename it atomically to the exact final filename. IDs and accepted contents
are immutable. Read the receipt: rejected submissions are never silently treated
as accepted. Accepted responses cannot be changed; a changed response needs a new
request. To repair a rejected folder publication, the operator must remove that
invalid file and its rejection receipt before publishing the corrected file. Archive
acknowledged files before the per-inbox limit of 4,096 committed files is reached.
Keep publication atomic when implementing a remote shared-drive synchronizer.

A request contains `schema:1`, `type:"request"`, `request_id`, `transcript`, `draft`,
and optional parent plan identifiers/text. Respond with:

```json
{"schema":1,"type":"plan","request_id":"p<32 hex>","text":"Two to four sentences describing the app. State its controls and expected behavior."}
```

Or report an explicit planning failure:

```json
{"schema":1,"type":"plan_error","request_id":"p<32 hex>","detail":"A concise, actionable reason."}
```

The relay derives the revision digest itself and binds it to the request and plan.
Wait for `outbound/approved_jobs/<job_id>.json`. It contains the approved text,
transcript, `approved_revision`, assigned `identity`, `artifact_kind:"pkg"`, and
`max_attempts:1`. Return source with that exact job/revision:

```json
{"schema":1,"type":"source","job_id":"j<32 hex>","approved_revision":"<64 hex from approved job>","language":"cpp","files":{"main.cpp":"// Full C++ application source here\n"}}
```

The human-authored [native graphics example](../relay/build/example/main.cpp)
shows the pinned renderer, fonts, controller, rumble and lifecycle APIs. It is a
compile fixture and starting point, not an app produced by Muse.

## Authenticated provider transport

The same exchange is exposed on a separate listener, port 8861. Its host binding
defaults to loopback. The owner's Opti3 deployment explicitly selects its private
LAN address so the named Cloudflare tunnel can reach it. Remote Muse requests use
that [HTTPS provider hostname](MUSE-CONNECT.md#named-cloudflare-route), with the
optional test tunnel retained as an alternative. The bearer token is read from a mode-0600
file, is never passed to the PS5, and must not appear in prompts or source.

| Endpoint | Purpose |
|---|---|
| `GET /provider/outbox?after=0` | `{schema,events:[{sequence,type,payload}],next_after}`; store the cursor only after handling the events. |
| `POST /provider/plans` | Same immutable plan or plan_error object as the folder protocol. |
| `POST /provider/sources` | Same approved-source object as the folder protocol. |
| `GET /provider/logs/<log_id>` | Read the actual JSONL uploaded by the console. |
| `GET /provider/diagnostics/<job_id>` | Read bounded compiler/packager diagnostics after a build attempt. |

Every provider request requires `Authorization: Bearer <token>`. Missing or wrong
authentication returns 401. Console and provider routes are separated. Provider
outbox events are durable and ordered; identical plan/source retries are accepted.

## Compiler and operational bounds

Only `main.cpp` and headers below `include/` are accepted: up to 32 files,
256 KiB per file and 1 MiB total. The JSON envelope is limited to 2 MiB. No custom
shell scripts, build recipes, binaries, extra dependencies or identity overrides
are accepted. Apps use the pinned native OpenGL/UI kit and its bundled font/art.
Custom media/assets and additional toolchains need a separately reviewed recipe.

**MuseStation itself is distributed outside this generated-app recipe.** Install
the compile-verified release `.pkg` with the console's native homebrew installer. The
01.003.000 baseline is 2,101,900,140 bytes (about 2.10 GB); 661 MiB described an
older package. Its bundled models and the MuseStation title identity are never
submitted through `/provider/sources` or `/build/<job>/artifact`. The 128 MiB
recipe limit is intentional and is not raised to accommodate this bootstrap app.

For an app that needs custom art or sound, the current recipe returns a rejection.
The extension process is: propose the media types, paths, provenance/licenses,
exact sizes and digests; have the operator review a versioned asset recipe with
bounded decoding/staging and no scripts; pin and test that recipe on the relay;
then obtain a fresh plan approval identifying the asset manifest and recipe.
The manifest must be bound into that new approval revision before asset-bearing
jobs are enabled. This extension is not implemented in recipe v1. Muse cannot
enable it by adding assets, URLs or build instructions to a source submission.

One worker serializes builds. Each compiler/packager runs non-root with no network,
no Docker socket, read-only inputs and bounded writable output. Images are local,
resolved to immutable IDs; the worker never pulls a provider-selected image.
Compilation has a 15-minute limit and packaging a 5-minute limit. Output packages
are capped at 128 MiB by this recipe even though the console client accepts up to
1 GiB. The package is read back completely and its assigned identity/hash checked.
Generated native code is compiled, never executed on Opti3.

An interrupted in-progress attempt becomes an explicit error after worker restart;
it is not silently rebuilt. Plans, approvals, queued jobs, results and logs live
in SQLite with durable file publication. Default limits are 256 requests, 256 jobs,
256 MiB of console logs and 8 GiB of final artifacts. Working directories also
retain bounded compiler outputs; an operator must archive old jobs rather than
expect automatic deletion. Title IDs are allocated from `PPSA99000`–`PPSA99999`,
excluding `PPSA99000` (ProsperoStore), `PPSA99576` (MuseStation), every ID in the
approval snapshot and every permanent ledger reservation. The two application
IDs are reserved even when absent from the snapshot. The console separately
refuses an already-installed title.
A separate `data/title-ledger.json` permanently reserves allocated IDs under a
process lock. Each reservation is fsynced before its job can commit to SQLite;
a failed transaction can waste an ID but cannot return it to the pool. Startup
reconciles existing SQLite jobs into the ledger, and a corrupt ledger fails
closed. Losing SQLite alone therefore does not reuse a reserved title. Back up
the ledger with the database. The ledger protects allocated and in-flight jobs;
it is not the sole record of titles already on the console. Losing both, or
switching to an unrelated relay, still loses reservation history that a ShadowMount
snapshot cannot reconstruct completely. Reconcile that history before continuing.
Existing-title refusal is an explicit installation failure, recorded in logs,
never a silent successful install.

Compiler failures export bounded diagnostic text for Muse; the next correction
requires a new plan/request and approval. A successful compile does not establish
that generated code implements the requested behavior or runs on firmware 12.40.
The production installation/launch path still needs the owner's hardware test.
ZIP delivery and transactional updates are deferred at the user's request. They
are not accepted artifact kinds in this API; the planned update requirements are
recorded in [PACKAGING.md](PACKAGING.md#deferred-zip-delivery-and-updates).
