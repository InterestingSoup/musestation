# Isolated unattended build test

**Status, 2026-10-20:** isolated relay, compiler and temporary provider tunnel are
running on Opti3; operator preflight passed. The fresh test outbox is empty.
Muse's separate test scheduler still needs configuration and a confirmed scheduled
empty check before the operator queues the request. No request, approval, source
or build has been created in this test.
The [automatic plan canary](MUSE-UNATTENDED.md#observed-plan-only-canary) passed,
and the earlier [Muse source/build test](MUSE-SOURCE-TEST.md#verified-result)
passed with an interactive handoff. This test joins those two paths under the
scheduled worker.

## Active fixture and preflight

The owner authorized this isolated test on October 19; staging completed after
midnight locally on October 20. It uses Compose project
`musestation-unattended-test`, private runtime directory
`runtime/muse-unattended-test/`, loopback console port `18870` and loopback provider
port `18871`. A separate temporary HTTPS tunnel reaches only that test provider.
The test uses distinct credentials; the normal named hostname and worker were
not changed.

Preflight completed at `2026-10-20T07:01:50Z` on Opti3. An independent public
outbox read from the development Mac also passed at `07:03:29Z`.

| Check | Observed result |
|---|---|
| Test provider with its own key | HTTP 200, empty outbox, `next_after:0`. |
| Test provider without a key | HTTP 401. |
| Console plan route on the provider listener | HTTP 404 with an authenticated provider key. |
| Test provider key on test console API | HTTP 401. |
| Normal key on test provider / test key on normal provider | Both HTTP 401. |
| Compiler worker | Recent heartbeat through the isolated console health endpoint. |
| Test store | Zero requests, zero jobs, zero events; fresh title ledger. |
| Normal relay | Empty outbox after cursor 2; request/job/event snapshot and title ledger match the pre-test baseline. |
| Native kit | Checksum matches the earlier successful build: `687a5605557b58ed1a954f59f228bdb198aa267cdd767d2a8bc999a2aeab86d1`. |

The actual temporary URL and test credential are supplied in the owner's private
`Muse-isolated-test-setup-PRIVATE.txt` handoff, outside Git. It asks Muse to
configure a separate test connector and scheduler, retain its own cursor/journal/
lock, and report a scheduled empty check. Credentials must not be included in that
report. The operator will then queue a fresh request, review its returned plan,
and approve exactly one compile/package attempt under the owner's test
authorization.

The private operator helper is `runtime/handoff/unattended-build-control.py`.
Its `queue` action requires a saved Muse setup confirmation. Its `approve` action
requires the reviewed plan's exact revision; it does not approve on a timer.
`status` observes the test, and `stop` refuses to interrupt a queued/running build.
Runtime configuration, preflight evidence and later test results stay under the
isolated runtime directory. Staging and connectivity checks do not establish an
unattended source/build result.

## Preserve the running connector

The normal worker now retains event cursor 2 and journal entries scoped to the
normal relay database. The earlier source test temporarily switched the public
hostname to a test database while Muse used a separate test cursor. Reusing that
cutover with the unattended normal worker running could skip test requests or
mix its checkpoints and journal entries with another database.

Use a separate test provider origin, credential, relay database, title ledger,
exchange directory, compiler namespace and console credential. Keep the normal
origin and worker running against their existing state. The Muse test connector
must use its own cursor, journal and lock, initialized at 0 for its fresh store.
Do not copy, reset or temporarily repurpose the normal worker's state.

A temporary HTTPS tunnel can provide the test origin using the existing Compose
provider-tunnel profile. Its target must be the isolated provider listener, never
the console approval API. Configure the test runtime and ports separately before
starting services. Share its generated credential only through Muse's private
credential store, then verify the test connector sees the fresh empty outbox.
For the active fixture above, the operator has already created this endpoint and
credential. Future repetitions need a fresh isolated configuration.

## Run one attempt

1. Record the normal relay's request/job/event snapshot and title-ledger digest.
   Start a fresh isolated relay and builder using the verified native kit and
   existing fixed compiler recipe. Record the image and kit digests.
2. Configure Muse's test scheduler with its own state and the test provider
   credential. Confirm exclusive worker ownership and timeout/lock behavior.
   End the setup conversation before queuing the actual test request.
3. Submit a new, labeled host fixture through the test console API. Save its
   client-chosen request ID and exact body before POST. Request a fullscreen
   color-swatch app: initially blue, Cross cycles blue/green/orange, Square resets
   blue, Circle exits. Explicitly allow one operator-approved host compile/package
   attempt, with no installation or console contact.
4. Let a scheduled run return the plan. Retrieve it through the test console API,
   verify the revision and review its actual contents before approving. Do not
   approve the existing normal plan-only canary or copy its approval state.
5. The operator submits the test approval for that exact revision with a labeled
   simulated inventory and a saved idempotency key. Reserve `PPSA99000` and
   `PPSA99576`; use the existing inventory-validation/allocation path. Record that
   this is a host fixture, not a controller press or real console inventory.
6. Without messaging Muse again, let the test scheduler process `approved_job`
   and submit source. Record its accepted receipt and canonical source digest.
   Muse never receives or uses the console approval credential.
7. Allow the single compiler/package attempt to finish. Verify the matching
   result event, preserved compiler input, diagnostics, package report and full
   artifact-download SHA-256. Have Muse retain the raw result and advance its
   test checkpoint only after handling each event.
8. Compare the normal relay with its baseline. Preserve all test evidence, stop
   the test scheduler and services after work finishes, and remove the temporary
   tunnel. Leave the normal endpoint and checkpoint unchanged.

If planning, source generation or compilation fails, preserve the diagnostics and
stop this attempt. A changed program needs a fresh request and explicit approval;
do not silently regenerate source or reuse an immutable failed job.

## Evidence and boundary

Success requires the scheduled plan and source run identifiers, accepted
receipts, source digest, final `done` result, package verification report and an
independent artifact hash. A successful POST alone does not establish a build.
Record the normal relay comparison and test teardown as well.

The resulting package is a host-test artifact using a simulated title inventory.
Do not install it on a PS5. Console installation, rendering, input, voice and GPU
execution remain separate hardware tests.
