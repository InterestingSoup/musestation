# Unattended Muse processing

**Status, 2026-10-20:** plan exchange and a Muse-submitted native build have passed.
The owner reports that Muse's native runtime scheduler now runs the existing
`custom.musestation` connector every 60 seconds. A separately submitted plan-only
canary received a verified plan without another operator message to Muse, ready
by the 2-minute-40-second check. Muse subsequently reported the matching scheduled
run, saved checkpoint and successful retry/lock checks. Independent relay checks
confirmed unchanged plan content, zero jobs and an empty outbox after event 2.
A subsequent [isolated unattended build](MUSE-UNATTENDED-BUILD-TEST.md#verified-build-result)
received a plan and source without follow-up prompts, then passed Docker
compilation, package readback and independent artifact hashing after operator
approval. Muse subsequently reported terminal result handling, test cursor 3 and
worker self-disable; the isolated services are stopped and the normal relay is
unchanged. Per-run UUIDs were not supplied. Actual runtime restart, timeout
termination and simultaneous lock acquisition remain unverified.

The Opti3 relay and compiler worker already run continuously. A relay poller can
detect work, but it also needs a supported way to invoke Muse with the connector
tools available. A script waiting on Opti3 cannot create that capability by itself.

## Owner's scheduler configuration

The owner relayed the following Muse-side setup report on October 19. These
runtime details have not been independently inspected from Opti3:

| Setting | Reported configuration |
|---|---|
| Automation | `musestation-provider-worker`, owned by the PS5 gadget goal. |
| Invocation | Muse's native runtime scheduler; continues after the chat closes and across runtime restarts. |
| Cadence | Every 60 seconds, the runtime's minimum interval. Each tick is a full agent run, including an empty outbox check. |
| Run limit | 30 minutes. |
| Stale-lock threshold | Changed from 25 to 35 minutes after the canary; see the recovery report below. |
| Durable state | `~/workspace/goals/ps5-homebrew-muse-gadget/hidden_files/musestation-worker/`: cursor/lock, event journal, exact prepared POST bodies, receipts, results, diagnostics and run logs. |
| Initial checkpoint | Normal event sequence 1; isolated test checkpoint 3 was not imported. |
| Empty checks | Three successful scheduled checks reported at 23:23, 23:24 and 23:25 PDT, with no submissions or jobs. |
| Stop | Ask Muse to disable this automation, or disable/remove it in that runtime's scheduler. |

The reported lighter hook detectors cannot access connector credentials, so they
cannot perform an authenticated empty-outbox pre-check. At continuous 60-second
cadence this configuration schedules 1,440 runs per day; an empty check still
invokes an agent. This is the owner's current setup, not a scheduler bundled with
the MuseStation Docker image or a guarantee for every Muse account.

The reported 35-minute threshold fixes the numerical mismatch with the 30-minute
run limit. Treating such a lock as orphaned additionally depends on enforced
termination, including any in-flight work, before takeover. A configured timeout
alone does not demonstrate that termination occurred.

The reported re-read of the old lock detects an intervening change, but a
separate read followed by a write is not an atomic claim: two contenders can
both read the same old lock before either writes. A scheduler-enforced
single-consumer guarantee, a process-held lock or an atomic ownership transition
is still required. If a renewable lease is used, a worker that loses ownership
must stop submitting work. These are implementation properties to confirm, not
failures observed during the plan-only canary.

## Observed plan-only canary

The operator submitted a new host-simulated request at
`2026-10-20T06:32:54Z` (23:32:54 PDT on October 19), after Muse reported setup
complete. The request explicitly limited this check to a plan and stated that
no PS5 was attached. Its protocol field `source:"on_console"` is required by
the API; this fixture value does not establish hardware execution.

| Check | Independently observed result |
|---|---|
| Submission | Private `POST /voice` returned HTTP 200; request `pd92ee4afa6f24fc09a6719b1e132a6b9`. The exact body and client ID were saved before POST. |
| Provider visibility | Authenticated public outbox returned the same request at normal sequence **2**, checked from Opti3 and the development Mac. |
| Initial state | Private plan reads returned HTTP 202 `planning`, including the check at `06:34:55Z`. |
| Response | Private plan read returned HTTP 200 at `06:35:33.971Z`, 159.709 seconds after submission. Stored state was `ready`. No follow-up message was sent to Muse. |
| Plan contents | Fullscreen color swatch; blue initially; Cross cycles blue/green/orange, Square resets blue, Circle exits. It explicitly remained plan-only. |
| Revision | Stored text matched the API response. Independent canonical SHA-256 matched revision `748189b53e8aa3eb8d6a0f2611b919987433a8b06923e4174dcf7fe88d799c40`. |
| Approval boundary | Zero build jobs and zero `approved_job` events. The title ledger and original counter request were unchanged. |

This establishes an automatic request-to-plan response through the live connector.
The polling interval bounds when it was observed; 159.709 seconds is not an exact
processing-duration measurement. The relay does not expose Muse's scheduler run
identifier or its private cursor; the subsequent Muse-side report below provides
that correlation. No source, compilation, installation or hardware execution
occurred.

Detailed input, baseline, outbox, submission and completion evidence is retained
privately under `runtime/handoff/unattended-canary-*.json` on Opti3 and the
development machine. Muse reports its accepted receipt and persisted checkpoint
**2**; its private files have not been independently read from Opti3. Do not rewind
it to the setup checkpoint.

## Muse-reported run and recovery checks

The owner relayed these results after the canary. They are Muse-side evidence
reported by the owner, distinct from the direct relay observations above.

| Check | Reported result |
|---|---|
| Canary execution | Run `0f8a789d-002a-4c9c-a09c-253324e8c042`, scheduled October 19 at 23:33:21 PDT (`2026-10-20T06:33:21Z`), status `succeeded`. |
| Cursor and journal | `state.json` cursor **2**; `journal/2.json` contains `confirmed_receipt` with HTTP 200 acceptance. The SHA-256 of `journal/2.body.json` matches its recorded digest. |
| Identical-body retry | Re-POST of the saved plan body returned HTTP 200 `accepted:true`; cursor stayed 2 and the outbox remained empty after 2. |
| Fresh-lock exclusion | A planted age-zero lock made the 23:46 PDT scheduled tick record `lock_held` and skip without changing the lock or cursor. |
| Simulated stale-lock recovery | A planted 60-minute-old lock was taken over and released by the 23:47 PDT tick; cursor remained 2. |
| Threshold change | Runbook now requires a 35-minute stale threshold above the 30-minute timeout, with a re-read of the same old lock before takeover. |

An independent private API/database check at `2026-10-20T06:49:36Z` found the same
canary text and verified revision, two requests, two request events, zero jobs,
zero approval events, and an unchanged title ledger and original counter request.
An independent public `GET /provider/outbox?after=2` at
`2026-10-20T06:50:42Z` returned HTTP 200 with no events and `next_after:2`.

The reported tests cover accepted-body replay, an already-held fresh lock, and
simulated stale-lock recovery. They do not exercise two simultaneous claimants,
an actual process/runtime restart, interruption during a POST, timeout termination
or a transient network failure. The relay observations corroborate unchanged
application state; they cannot inspect the remote worker's locking implementation.

The subsequent [isolated unattended build test](MUSE-UNATTENDED-BUILD-TEST.md#verified-build-result)
passed its relay/compiler/package checks with a separate connector state directory
and provider endpoint. Muse reported its terminal checkpoint and self-disable;
the operator then stopped the test services and retained the evidence. Do not
switch the live normal worker's hostname to another database.

## Establish the execution mechanism first

Ask Muse to inspect the capabilities actually available in the owner's workspace.
If supported, configure one persistent automation using the existing connector
and credentials. Start with a 60-second check interval if supported; otherwise
report the actual minimum and expected request latency. Prefer a lightweight empty
outbox check before invoking a model when the platform supports it.

Muse must return:

- The scheduler/worker/trigger used and its real automation or service identifier.
- Whether it runs after the chat closes and after its execution environment restarts.
- Its configured interval, time limits, overlap behavior and next scheduled run.
- Where the normal cursor, event journal and prepared POST bodies are persisted.
- How to inspect status and stop or disable it.

A saved helper, a proposed schedule or a loop running only during the current
chat does not establish unattended operation. If Muse cannot configure a supported
mechanism, it should report that limitation and any documented invocation interface
available to this account. Do not invent a cloud endpoint or replace Muse with
another provider to make the test appear complete.

The official [Linux SDK README](https://github.com/facebookincubator/muse-gadget-sdk/blob/main/linux/README.md)
includes a scheduled-task example and a paired-device message path through
`musegadget send-user-msg`. That device path requires enrollment; it is separate
from this project's authenticated HTTPS connector. The README does not establish
which scheduler features are available to this specific custom connector.
Keep the selected HTTPS architecture while checking Muse's own supported runtime.

## Connection and current checkpoint

Use the existing protected connector configuration:

```text
Origin: https://musestation-api.interestingsoup.com
Connector: custom.musestation
Authorization: existing provider credential from the secret store
User-Agent: MuseStationConnector/1.0
Accept: application/json
Content-Type: application/json for POSTs
```

The normal relay was restored after the isolated build test. At the original
handoff it had one already-handled request event, sequence **1**, and no approved
jobs; the owner initialized the worker there. The later canary is normal event
**2**, with its plan now accepted. Resume from the worker's durable checkpoint and
journal. For a new deployment, start at its own first unhandled event; do not copy
this owner's checkpoint. Do not use the isolated test cursor **3**, and do not
rewind a valid normal checkpoint that subsequently advances.
Keep test and normal state separate even when a hostname is temporarily reused.

The original counter plan is already accepted and remains unapproved. Do not
rewrite it or manufacture an approval while configuring the automation.

## Event processing requirements

Read `GET /provider/outbox?after=<normal-cursor>`. Exact message schemas are in
[the exchange contract](MUSE-EXCHANGE.md#authenticated-provider-transport).

| Event | Connector action |
|---|---|
| `request` | Generate a 2–4 sentence plan, then submit it to `/provider/plans`. If planning cannot succeed, submit the documented `plan_error`. |
| `approved_job` | Generate C++ source for that exact approved text, job ID and revision; submit to `/provider/sources`. Follow the [native source contract](https://raw.githubusercontent.com/InterestingSoup/musestation/main/relay/build/README.md). |
| `result` | Persist the raw result JSON. Retrieve and verify available diagnostics; report package fields directly from the matching event. A failed build needs a fresh request and explicit approval before revised source can run. |
| `logs` | Retrieve and preserve the referenced JSONL with its request/job context where available. |

Muse never calls the console approval route. The provider credential does not
permit it. Source generation waits for `approved_job`; a plan response alone is
not approval. Keep `max_attempts:1` and the existing compiler recipe.

Use one active consumer with a platform-supported lock or non-overlap guarantee.
Persist each event and the exact prepared response body before its first POST.
After a timeout or uncertain response, retry that same body; do not regenerate a
different plan or source for the same immutable ID.

Commit the cursor only after the event has a confirmed accepted receipt or has
been durably handled according to its type. Advance through contiguous completed
events, not straight to a batch's `next_after` when an earlier event failed.
Restart must resume the prepared operation instead of losing it or creating a
second worker.

Apply bounded retry delays with jitter to transient connection failures, 429s
and 5xx responses; respect `Retry-After` when present. Pause and surface
authentication or permanent protocol failures instead of silently advancing.
An empty outbox is normal. Avoid repeated notifications for empty checks or an
unchanged failure.

## Verify after setup

First obtain the real automation identifier and an observed empty scheduled run.
End the setup interaction. The operator then submits a fresh, clearly labeled
host-only **plan-only** canary request; do not queue it during setup and count an
immediate chat response as an unattended run.

Success requires a later scheduled execution, with its run identifier/timestamps,
to fetch that request and return a plan without another message to Muse. The
operator independently verifies the plan through the console-facing API.
No approval, source, compilation or installation is part of this first canary.

Then test scheduler restart and replay using the persisted journal and identical
payload. Verify the checkpoint resumes, accepted contents remain unchanged, and
there are no duplicate jobs. Check a transient network failure and recovery.
Any additional source/compile test uses a separately authorized isolated store
and a new job; the completed test allocation is not reused.

Record actual checks, timestamps and unresolved limits before describing the
connector as unattended. PS5 operation remains a separate hardware milestone.
