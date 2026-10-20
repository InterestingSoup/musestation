# Muse source submission and compilation test

This follows the [passed plan exchange](MUSE-CONNECT.md#request-to-plan-verification).
It tests whether the actual Muse connector can submit C++ source for an approved
job and receive the result of the real Docker compiler and packager.

**Status, 2026-10-19:** a separate test relay and compiler worker are running.
The public provider outbox returns its approved counter-app job. Source submission
and compilation are pending; this document does not record a passed build.
The worker's heartbeat is current and the immutable kit passed its integrity check.

## Test boundary

There is no PS5 attached. The operator copied the previously accepted Muse counter
plan verbatim into a new request in an isolated test database, then created a
simulated approval under the owner's explicit authorization to test compilation.
The inventory is a labeled host fixture occupying `PPSA99000`, `PPSA99001`,
`PPSA99002` and `PPSA99576`; the test allocator selected `PPSA99003`.
This is not an inventory read from a console.

The normal relay retains its original unapproved plan and has zero build jobs.
The test has its own database, title ledger, exchange folder, console credential
and worker namespace. Its package must not be installed: a test allocation does
not establish that the title is available on a future console.

The existing public provider hostname temporarily serves this isolated test relay.
Muse uses its existing provider credential. The normal console API remains on its
configured private LAN address; the test console API binds only to host loopback.
The normal provider listener is temporarily parked on a different loopback port.
No DNS or Cloudflare configuration change is required.

**Keep test and normal outbox cursors separate.** Start this test with `after=0`,
then follow only the request/job IDs in the operator's handoff. Sequence numbers
are scoped to a database; they are not globally unique. The normal cursor must
not be overwritten with a test cursor.

## Instructions for Muse

1. Use the saved MuseStation connector and the operator-supplied test job ID.
   Read `GET /provider/outbox?after=0` and select its exact `approved_job` event.
   The plan is already accepted and approved in the fixture. Do not submit a new
   plan or attempt to approve anything.
2. Read the [native source contract](../relay/build/README.md#muse-source-contract)
   and [graphics example](../relay/build/example/main.cpp). Produce the actual
   source for the event's approved counter plan: one large fullscreen number,
   initially zero; Cross increments once per press, Square resets, Circle exits.
   No additional screens, menus or features.
3. Submit `POST /provider/sources` with the exact job ID and approved revision
   from the event:

   ```json
   {
     "schema": 1,
     "type": "source",
     "job_id": "<operator-supplied job id>",
     "approved_revision": "<revision from approved_job>",
     "language": "cpp",
     "files": {"main.cpp": "<complete source, serialized as a JSON string>"}
   }
   ```

   Use the existing bearer credential, `User-Agent: MuseStationConnector/1.0`,
   `Accept: application/json` and `Content-Type: application/json`. Construct JSON
   with a serializer; do not hand-escape the C++ source.
4. Save the accepted receipt and source hash. A lost response may be retried with
   exactly the same body. Changed source is not an idempotent retry.
5. Poll the outbox using a test-only cursor until the matching `result` event
   arrives. Read `/provider/diagnostics/<job_id>` for compiler/package diagnostics.
   A failed attempt needs a new request and approval before revised source can
   be built. Do not retry a changed program against this job.
6. Report the receipt, final result, package size/hash and diagnostics. Stop after
   this single compile/package attempt. Do not install, launch or contact a PS5.

The fixed recipe accepts C++20 without exceptions or RTTI. For this small app,
`main.cpp` alone is sufficient. The native kit already includes fonts; no binary
assets, dependencies, build scripts, identity overrides or network features are
needed.

Use the actual kit interfaces rather than guessing names:

- [`InputTracker` and controller actions](../vendor/native-ui/src/core/input.hpp):
  `Action::confirm` is Cross by default, `Action::west` is Square and
  `Action::back` is Circle. Use press edges rather than held state.
- [`DrawList::text`](../vendor/native-ui/src/gfx/draw_list.hpp) accepts a baseline
  and optional `gfx::Align::center`; [`Font`](../vendor/native-ui/src/gfx/font.hpp)
  exposes measurement, ascent and descent. Keep the number readable as it grows,
  and avoid integer overflow.
- Follow the example's display, renderer, bundled-font loading and splash-screen
  handling. Release rendering/controller resources and end with `hui::sys::quit()`.

## What establishes success

A source receipt alone does not establish successful compilation. The operator
checks the matching result event, preserved source digest, compiler/package logs,
package verification report and independent artifact-download hash. The recipe
performs native-module checks, complete package extraction and input-file hash
comparisons before reporting `done`.

The original relay's request/job/event records and title-ledger digest are checked
against the baseline. After results and diagnostics are collected, stop the test
services and restore the normal provider listener. Keep the fixture records for
review; do not merge test allocations into production.

The current deployment keeps its private test configuration and reports under
`runtime/muse-source-test/`. Its private operator helper is
`runtime/handoff/source-test-control.py`; `status` reads the test result, and
`restore` stops the test stack and restores the normal provider route while
retaining evidence. Restoration refuses to interrupt a queued or running compile.
Neither credentials nor deployment-specific job IDs are committed to this guide.

Even a successful package result establishes only host compilation and package
verification. Installation, rendering, controller behavior and firmware
compatibility remain unverified until the owner tests on hardware.
