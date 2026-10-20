# Unattended Muse processing

**Status, 2026-10-19:** plan exchange and a Muse-submitted native build have passed.
Unattended operation is not enabled or verified by those tests. The remaining
dependency is a supported Muse-side scheduler, persistent worker or invocation
trigger for the existing `custom.musestation` connector.

The Opti3 relay and compiler worker already run continuously. A relay poller can
detect work, but it also needs a supported way to invoke Muse with the connector
tools available. A script waiting on Opti3 cannot create that capability by itself.

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

The normal relay was restored after the isolated build test. At handoff it has
one already-handled request event, sequence **1**, and no approved jobs. Initialize
a new normal connector checkpoint at **1**. Do not use the isolated test cursor
**3**, and do not rewind a valid normal checkpoint that subsequently advances.
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
