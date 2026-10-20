# Isolated unattended build test

**Status:** procedure prepared; this additional test has not been staged or run.
The [automatic plan canary](MUSE-UNATTENDED.md#observed-plan-only-canary) passed,
and the earlier [Muse source/build test](MUSE-SOURCE-TEST.md#verified-result)
passed with an interactive handoff. This test joins those two paths under the
scheduled worker.

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
The prepared procedure does not imply that this endpoint or credential exists yet.

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
