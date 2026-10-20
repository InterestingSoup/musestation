# Unattended Muse processing

**Status, 2026-10-19:** plan exchange and a Muse-submitted native build have passed.
The owner reports that Muse's native runtime scheduler now runs the existing
`custom.musestation` connector every 60 seconds. A separately submitted plan-only
canary received a verified plan without another operator message to Muse, ready
by the 2-minute-40-second check. The scheduler run identifier, restart recovery
and worker overlap protection remain unverified.

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
| Durable state | `~/workspace/goals/ps5-homebrew-muse-gadget/hidden_files/musestation-worker/`: cursor/lock, event journal, exact prepared POST bodies, receipts, results, diagnostics and run logs. |
| Initial checkpoint | Normal event sequence 1; isolated test checkpoint 3 was not imported. |
| Empty checks | Three successful scheduled checks reported at 23:23, 23:24 and 23:25 PDT, with no submissions or jobs. |
| Stop | Ask Muse to disable this automation, or disable/remove it in that runtime's scheduler. |

The reported lighter hook detectors cannot access connector credentials, so they
cannot perform an authenticated empty-outbox pre-check. At continuous 60-second
cadence this configuration schedules 1,440 runs per day; an empty check still
invokes an agent. This is the owner's current setup, not a scheduler bundled with
the MuseStation Docker image or a guarantee for every Muse account.

**Lock review remains open:** the report says a lock becomes stale after 25
minutes, but a run may last 30 minutes. If expiry is based only on lock age,
another tick could take over while the original run is alive. Confirm a scheduler
non-overlap guarantee or use a process-held lock. A renewable lease needs owner
identification, renewal and ownership checks before submitting work; a worker
that loses ownership must stop. Do not reclaim a lock from elapsed age alone
while its owner may still run. A non-renewing lease must cover the enforced run
limit plus termination grace, with the former worker confirmed stopped before
takeover. The plan-only canary does not validate these recovery guarantees.

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
identifier or its private cursor, so matching this response to a specific
scheduled run still needs Muse-side run evidence. Scheduler survival, stale-lock
takeover, replay after interruption and network-failure recovery were not tested.
No source, compilation, installation or hardware execution occurred.

Detailed input, baseline, outbox, submission and completion evidence is retained
privately under `runtime/handoff/unattended-canary-*.json` on Opti3 and the
development machine. The provider worker should retain its own accepted receipt
and persist checkpoint **2** after handling this event; its private checkpoint
has not been independently read. Do not rewind it to the setup checkpoint.

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
| `approved_job` | Generate C++ source for that exact approved text, job ID and revision; submit to `/provider/sources`. Follow the [native source contract](https://raw.githubusercontent.com/moiz-qureshi/musestation/main/relay/build/README.md). |
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
