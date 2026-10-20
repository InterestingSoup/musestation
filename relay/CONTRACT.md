# Relay exchange version 1

This relay accepts plans and C++ source from an external Muse connector. It does
not log into Muse, call a Muse cloud endpoint, transcribe uploaded audio, or claim
that a Muse account is connected. The folder and provider API are two transports
for the same validated messages. The console API alone accepts approval.

## Folder transport

The operator sets an absolute `MUSE_EXCHANGE_DIR`. The relay creates:

| Directory | Writer | Content |
| --- | --- | --- |
| `outbound/requests` | Relay | `<request_id>.json` requests |
| `inbound/plans` | Connector | `<request_id>.json` plan responses |
| `outbound/approved_jobs` | Relay | `<job_id>.json` exact approved plan, identity |
| `inbound/sources` | Connector | `<job_id>.json` source bundles |
| `outbound/results` | Relay | `<job_id>.json` result and optional `<job_id>.diagnostics.json` |
| `outbound/logs` | Relay | `<log_id>.json` metadata and exact `<log_id>.jsonl` |
| `receipts/plans`, `receipts/sources` | Relay | Immutable acceptance/rejection receipts |

The connector must write a temporary filename **without `.json`**, fsync and
close it, then rename to the final filename in the same directory. Files and
IDs are immutable; an acknowledged input is never run again. A malformed input
gets a rejection receipt and remains pending, never executable. Correcting a
rejected folder file requires an operator to remove that file and its rejection
receipt before publishing a replacement; an already accepted plan/source still
cannot change because SQLite enforces immutability. Provider submissions return
errors directly and can be corrected before their first acceptance.

Each inbox is limited to 4,096 committed filenames. Archive acknowledged files
before reaching the bound; leave database history intact so IDs are never reused.
Only ordinary files are accepted. Symlinks, hard-linked aliases, FIFOs, path
traversal, duplicate JSON keys, invalid UTF-8, unknown schema fields, and NUL text
are rejected. No input can supply scripts, environment variables, shell commands,
image names, build flags, native identity, or arbitrary assets.

## Messages

Request emitted after `POST /voice`:

```json
{"schema":1,"type":"request","request_id":"p<32 lowercase hex>","transcript":"Make a counter","draft":""}
```

A revision request also contains `parent_plan_id`, `parent_revision`, and the
previous authoritative `parent_text`. The new request needs a new approval.

Plan supplied by the connector:

```json
{"schema":1,"type":"plan","request_id":"p<32 lowercase hex>","text":"Build a counter. Cross adds one."}
```

Text is 1–4,096 UTF-8 bytes. The relay derives the revision as SHA-256 over its
canonical request ID, transcript, parent ID/revision and plan text. The connector
cannot supply a revision. A terminal planning failure instead uses
`{"schema":1,"type":"plan_error","request_id":"...","detail":"..."}`;
detail is at most 1,023 UTF-8 bytes.

After console approval, the relay emits:

```json
{"schema":1,"type":"approved_job","job_id":"j<32 lowercase hex>","request_id":"p<32 lowercase hex>","approved_revision":"<64 lowercase hex>","transcript":"Make a counter","text":"Build a counter. Cross adds one.","artifact_kind":"pkg","identity":{"title_id":"PPSA99001","content_id":"IV0000-PPSA99001_00-MUSE123456789ABC","name":"Muse App 001","version":"01.000.000"},"max_attempts":1}
```

IDs in this example are explanatory. The relay allocates unused `PPSA99xxx`
identities, excludes ProsperoStore's `PPSA99000`, MuseStation's `PPSA99576`,
all titles in the console's fresh ShadowMountPlus report and previous allocations,
and records the new allocation permanently. Each new approval must include
`inventory:{source:"shadowmountplus",api_version:1,title_ids:[...]}` from the
console's API read immediately before approval. Missing inventory returns 428;
malformed inventory returns 400. Lists are bounded to 4,096 unique title IDs.
The snapshot stays in the private job record, outside the provider exchange.
It is not proof that unreported titles are unused; the helper's final OS
existing-title check remains required. See the authoritative
[console contract](../docs/MUSE-EXCHANGE.md) for the bounds and replay rules.

Only then may the connector submit:

```json
{"schema":1,"type":"source","job_id":"j<32 lowercase hex>","approved_revision":"<64 lowercase hex>","language":"cpp","files":{"main.cpp":"/* native source using the fixed kit API */"}}
```

The source map requires `main.cpp` and permits `include/...h` and
`include/...hpp`, at most 32 files, 256 KiB per file and 1 MiB combined. The JSON
envelope itself is capped at 2 MiB. Source uses the fixed native kit/build recipe;
see the builder's example and kit manifest. No source is compiled before exact
revision-bound approval. A second different source submission for the same job
is rejected. Version 1 permits **one attempt** per approved job. After an error,
Muse can use the exported diagnostics to propose a new request/plan, which needs
fresh console approval. There is no automatic repair loop or build retry.

Result events contain `state:"done"` or `state:"error"`, job/revision and optional
diagnostics reference `{job_id,size,sha256}`. Successful results contain `kind`,
`size`, `sha256`, `title_id`, `content_id`, `source_sha256`, and
`hardware_verified:false`. Diagnostic files contain up to three 12 KiB log tails,
their original lengths and truncation flags (256 KiB maximum JSON envelope).
They may contain generated source snippets and belong in the protected exchange.

## Provider API

Separate listener, default `127.0.0.1:8861`. It starts only with a valid private
token file. Every request requires `Authorization: Bearer <token>`; this is the
relay integration token, **not a Muse account credential**. The console listener
does not expose these routes.

- `GET /provider/outbox?after=0`: `{schema:1,events:[{sequence,type,payload}],next_after}`.
  Pages contain at most 32 events and approximately 256 KiB. Persist the cursor
  after consuming events; replay is safe. Events remain in SQLite.
- `POST /provider/plans`: same plan/plan_error messages as the folder.
- `POST /provider/sources`: same source message as the folder.
- `GET /provider/logs/<log_id>`: exact uploaded JSONL.
- `GET /provider/diagnostics/<job_id>`: bounded compiler/package diagnostic JSON.

There is deliberately no provider approval endpoint. This listener is plain
HTTP; use loopback forwarding or an authenticated encrypted transport when moving
it beyond the local host. A provider knows only exchange state, not account
pairing status.

## Console API

Every console route except `GET /` and `GET /health` requires a separate
`Authorization: Bearer <console-key>`. The key is provisioned by the operator,
not supplied by Muse, returned in plans or shared through the exchange. The
provider credential cannot authenticate console calls. Physical controller
approval is enforced by the client, not remotely attested by the bearer token.

`POST /voice` accepts JSON `{client_request_id,transcript,draft,source:"on_console"}` and optional
paired `parent_plan_id`/`parent_revision`; returns `{transcript,plan_id}` immediately.
The optional `client_request_id` is `p` plus 32 lowercase hex digits and becomes
the plan ID. Repeating the exact payload with that ID returns the original result;
changing its content returns 409. MuseStation saves the ID and exact body before
POST, recovers with GET after a dropped response, and repeats that saved POST only
when the relay reports 404. Omission retains server-generated IDs for older callers.
Audio uploads return 415 because server transcription is not configured.

`GET /plan/<id>` returns 202 `{id,state:"planning",detail}` while pending, 200
`{id,revision,text}` when ready, 422 on terminal planning failure, or 404 unknown.
The client must not approve a 202 response.

`POST /plan/<id>/approve` requires
`{revision,idempotency_key,artifact_kind:"pkg",inventory:{source:"shadowmountplus",api_version:1,title_ids:[...]}}`
for a new job, with a maximum 64 KiB request body. Stale/pending approvals return
409. The same revision creates at most one job, even under concurrent calls or
different keys; reuse of a key for a different approval returns 409. Recovering an
already accepted approval may omit inventory because it performs no new allocation.

`GET /build/<job>/status` returns `building` while awaiting source, queued, or
compiling; otherwise `error` with detail or `done` with the exact package manifest.
`GET /build/<job>/artifact` streams exact bytes with one Content-Length, no
compression or transfer encoding. The relay verifies package bounds, FIH/CNT
identity, size and SHA-256 before publishing; the client independently verifies
again. These checks do not prove installation or PS5 runtime behavior.

`POST /logs` accepts `application/x-ndjson`, 1 byte–8 MiB, records with
`ts,level,event,detail`. Exact duplicate uploads return the original log ID.
`GET /` and `/health` report exchange state, provider enablement, worker heartbeat,
and explicitly false Muse connection/hardware verification flags. A recent worker
heartbeat means the process is alive, not that a package has run on a console.

## Processes, limits and recovery

`python3 -m relay.server` serves HTTP and polls the folder. `python3 -m relay.worker`
owns the Docker socket and the fixed compiler recipe. The API has no Docker socket.
Both use the same private data directory, SQLite WAL and FULL synchronization.
Every external event is committed with its state transition; exports can replay
after a crash. Atomic immutable output files are fsynced before cursor advancement.
The Linux container uses `renameat2(RENAME_NOREPLACE)` for publication; it never
creates a temporary hard-link alias or falls back to an overwriting rename. A
filesystem without atomic no-replace rename support fails visibly.

An exclusive worker lock and SQLite claim allow one active compilation per private
data directory. Docker container names and ownership labels include a stable hash
of that directory, so fixture and production workers cannot stop one another's
builds. On worker restart only its exact ownership-labelled orphan container is stopped first;
interrupted jobs become explicit errors and are never silently rebuilt. Queued
approved jobs can proceed normally. Compilation/container deadlines and quotas are
enforced by the builder. A success removes the redundant worker output package
after the durable served copy commits; cleanup failure never changes a completed
job into an error. Interrupted jobs export existing bounded compiler logs before
the recovery error when those logs are readable. Source, bounded logs and verification
reports remain for diagnosis.

Default retention bounds are 256 requests, 256 jobs, 4,096 log uploads, 256 MiB
total logs and 8 GiB served artifacts. A quota failure is visible (507), never a
silent eviction. The builder also bounds per-attempt input/output/logs. No HTTP
endpoint deletes history or frees identity reservations; operator archival is a
future maintenance task. Keep the private data directory inaccessible to provider
writers. It contains source and state; only the separate exchange is shared.

Title reservations additionally live in `data/title-ledger.json`, protected by
an interprocess lock and an fsynced atomic replacement. Reservations commit before
SQLite jobs and never expire. Startup reconciles legacy job identities into the
ledger; malformed history fails closed. Deleting SQLite alone cannot reuse an ID.
Backup/restore must preserve the newest ledger. Losing both histories or moving
to a new unrelated relay still needs console/operator reconciliation.

The 128 MiB generated-app recipe does not distribute MuseStation itself. The
model-bundled title is a separate release installed through the native installer.
Custom assets are rejected until an operator-reviewed, pinned recipe and
manifest-bound approval extension exists. See [the exchange guide](../docs/MUSE-EXCHANGE.md)
for the proposed review process; asset upload is not enabled in version 1.

Server environment:

```text
MUSE_RELAY_DATA_DIR=/absolute/private/data
MUSE_EXCHANGE_DIR=/absolute/shared/exchange
MUSE_RELAY_BIND=127.0.0.1
MUSE_RELAY_PORT=8860
MUSE_EXCHANGE_POLL_SECONDS=2           # 0.25..60
MUSE_PROVIDER_BIND=127.0.0.1
MUSE_PROVIDER_PORT=8861
MUSE_PROVIDER_TOKEN_FILE=/absolute/private/token  # omit to disable provider listener
MUSE_CONSOLE_TOKEN_FILE=/absolute/private/console-token  # required, separate key
```

Token files must be mode 0600/0400. Provider keys contain 32–256 URL-safe characters;
console keys contain exactly 64 lowercase hex characters and must differ. Their
values and request bodies are never written to access logs. Worker environment:

```text
MUSE_RELAY_DATA_DIR=/same/absolute/private/data
MUSE_RELAY_NATIVE_KIT=/absolute/verified/native-kit
MUSE_BUILD_IMAGE=sha256:<trusted local image ID>
MUSE_PACKAGE_IMAGE=sha256:<trusted local image ID>
```

The API's default bind is loopback. Deployments must explicitly select their
private interface. The console rejects public relay origins; LAN HTTP still
does not protect a bearer credential against network observation or interception.

## Verification

`python3 -m unittest relay.tests.test_relay -v` exercises SQLite concurrency,
pending/stale approval gates, immutable source, crash recovery, folder replay,
special-file rejection, provider authentication, bounded diagnostic export,
artifact identity/hash rejection, exact HTTP download and JSONL delivery. Run it
inside the designated Docker environment. Package fixtures in these tests are
structural test bytes, not installable applications; separate full-stack tests
compile and package the human-authored native example. Neither is a live Muse
connector or hardware test.
