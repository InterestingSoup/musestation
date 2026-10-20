# Muse source submission and compilation test

This follows the [passed plan exchange](MUSE-CONNECT.md#request-to-plan-verification).
It tests whether the actual Muse connector can submit C++ source for an approved
job and receive the result of the real Docker compiler and packager.

**Passed, 2026-10-19:** Muse submitted source through the authenticated provider
API, and the isolated worker completed native compilation and package verification.
The operator independently checked the preserved compiler input, result event,
package report and artifact download. The test services have been stopped and the
public hostname restored to the normal relay. No PS5 installation or launch occurred.

## Verified result

Verification completed at `2026-10-20T06:04:02Z` (October 19 in Los Angeles).

| Check | Verified value |
|---|---|
| Source receipt | Owner reported HTTP 200, `accepted:true`; the stored source and compiler input independently match. |
| Result event | Sequence 3, `state:done`, `kind:pkg`; independently retrieved through the public provider API. |
| Package bytes | **38,054,688** |
| Package SHA-256 | `0cdea7e9337d05e8d1ef7889d85f2850d514d7a72f018c6098573b36f1669d74` |
| Canonical source-map SHA-256 | `a0d00ef474f3f017cccaf96b6bc17e9829dd8440892e5bb9d7ba6f35dfefba53` |
| Raw `main.cpp` SHA-256 | `200b1012472901f1404a760e996eb0be6ab6a951ad239ea386754132dc7220a2` |
| Assigned test title | `PPSA99003`, version `01.000.000` |
| Compile / package time | 125.859 s / 7.798 s |
| Package readback | 82 input files checked; structural and NAPS round-trip validation passed. |
| Native modules | `eboot.bin` is native SELF `0xFE10`; `sce_module/libc.prx` is native SELF `0xFE18`. |
| Independent artifact GET | HTTP 200; length, full SHA-256 and embedded content identity match the stored result. |
| Diagnostics GET | HTTP 200; 11,618 bytes, digest matched the result event; both stage logs were untruncated. |
| Normal relay | Request/job/event records and title-ledger digest match the pre-test baseline; zero jobs. |

The source hash covers the **file map**, including filenames and JSON-escaped
contents: UTF-8 JSON with sorted keys, `ensure_ascii=False` and separators
`(',', ':')`. It does not hash just the raw `main.cpp` bytes or the entire source
submission envelope. The two source digests above are therefore expected to differ.

The first summary relayed from Muse reported a 238,187-byte package and a different
artifact hash. Those package values do **not** match the actual result. The public
event, database, build receipt, package report and downloaded bytes all agree on
the values in this table. The source digest in that summary was correct. The
reason for its package-metadata discrepancy has not been established.

Detailed evidence remains in the private test runtime: `reports/completion.json`,
the build receipt and stage logs, package report, accepted source and downloaded
package. The immutable kit digest was
`687a5605557b58ed1a954f59f228bdb198aa267cdd767d2a8bc999a2aeab86d1`.
No changed-source retry or second build was needed.

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

During the test, the existing public provider hostname served this isolated relay
using the existing provider credential. The normal console API remained on its
configured private LAN address; the test console API bound only to host loopback.
The normal provider listener was temporarily parked on a different loopback port.
The original listener has now been restored, without changing DNS or Cloudflare.

**Keep test and normal outbox cursors separate.** Start this test with `after=0`,
then follow only the request/job IDs in the operator's handoff. Sequence numbers
are scoped to a database; they are not globally unique. The normal cursor must
not be overwritten with a test cursor.
For the completed run, the normal cursor is **1** and the test result cursor is
**3**. Resume the normal connector at 1; using the test cursor could skip future
normal events.

## Instructions for another authorized test

These steps require a newly staged isolated test and a fresh operator handoff.
The completed test job is immutable and is no longer exposed by the public route.

1. Use the saved MuseStation connector and the operator-supplied new test job ID.
   Read `GET /provider/outbox?after=0` and select its exact `approved_job` event.
   The plan is already accepted and approved in the fixture. Do not submit a new
   plan or attempt to approve anything.
2. Read the [native source contract](https://github.com/moiz-qureshi/musestation/blob/main/relay/build/README.md#muse-source-contract)
   ([raw text fallback](https://raw.githubusercontent.com/moiz-qureshi/musestation/main/relay/build/README.md))
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
6. Report the receipt, final result, package size/hash and diagnostics. Include the
   raw matching result JSON so the operator can check the summary against it.
   Stop after this single compile/package attempt. Do not install, launch or contact a PS5.

The fixed recipe accepts C++20 without exceptions or RTTI. For this small app,
`main.cpp` alone is sufficient. The native kit already includes fonts; no binary
assets, dependencies, build scripts, identity overrides or network features are
needed.

The owner reported that Muse could not load the contract README link during the
test. The file is present in GitHub, and its raw URL returned HTTP 200 with the
checked-in content during operator verification. The explicit raw-text link above
provides an alternative for clients that cannot resolve the rendered document link.

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
