# Isolated unattended build test

**Status, 2026-10-20:** isolated relay and compiler are running on Opti3; the fresh
test outbox is empty. Muse reports its separate test scheduler is enabled and its
test credential is in the vault, but its first scheduled check hit a transport
block. A successful empty check from that worker remains pending. The Quick
Tunnel passed operator checks but Muse reported HTTP 403 from its environment.
The Quick Tunnel has been removed. The owner created the separate named route,
and operator verification through it passed. Muse still needs to switch its test
connector to that URL and report a successful scheduled empty check. No request,
approval, source or build has been created in this test.
The [automatic plan canary](MUSE-UNATTENDED.md#observed-plan-only-canary) passed,
and the earlier [Muse source/build test](MUSE-SOURCE-TEST.md#verified-result)
passed with an interactive handoff. This test joins those two paths under the
scheduled worker.

## Active fixture and preflight

The owner authorized this isolated test on October 19; staging completed after
midnight locally on October 20. It uses Compose project
`musestation-unattended-test`, private runtime directory
`runtime/muse-unattended-test/` and loopback console port `18870`. The provider
initially used loopback port `18871` with a separate temporary HTTPS tunnel; it
now binds only the private LAN address `192.168.5.54:18871` for the proposed named
route below. The test uses distinct credentials; the normal named hostname and
worker were not changed.

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

The connection handoff is private and outside Git. Muse reports that credentials
can enter its vault only through its secure page; putting a token in a chat
message does not configure the connector. The owner now reports this vault step
is complete for the test connector. Muse uses its supported vault/surrogate
mechanism rather than writing the credential into worker files. Credentials must
not be included in the setup report.

The private operator helper is `runtime/handoff/unattended-build-control.py`.
Its `queue` action requires a saved Muse setup confirmation. Its `approve` action
requires the reviewed plan's exact revision; it does not approve on a timer.
`status` observes the test, and `stop` refuses to interrupt a queued/running build.
Runtime configuration, preflight evidence and later test results stay under the
isolated runtime directory. Staging and connectivity checks do not establish an
unattended source/build result.

## Named-route migration and Muse setup report

The owner relayed that Muse configured `musestation-unattended-test-worker` with
its own state directory, cursor 0, journal and atomic `mkdir` acquisition lock.
A later report confirms it is enabled and the test credential is in the vault,
used through surrogates. Its normal worker is reported unchanged.

The reported first scheduled run, `e1f02419-66a2-41b5-a087-6415de34dcbe` at
`2026-10-20T07:16:05Z` (00:16:05 PDT), acquired and released the lock and attempted
the outbox request at cursor 0. It received HTTP 403 and classified a transport
block without advancing the cursor. Scheduler status `succeeded` therefore does
not establish a successful API check. Muse reports that its 30-minute idle timer
now begins at the first reachable check. No successful scheduled empty check has
been provided yet.

Muse reports that the Quick Tunnel origin returned a Cloudflare HTTP 403 from its
environment with both the application and browser User-Agent. Operator-side
checks succeeded earlier; they do not establish reachability from Muse. The exact
cause of the environment-dependent block has not been independently established.
A separate named route now passes operator checks; resolution of Muse's egress
block remains unverified until its worker succeeds through that route.

The owner created this published application:

| Cloudflare setting | Value |
|---|---|
| Existing tunnel | The one already serving the normal MuseStation API. |
| New hostname | `musestation-test-api.interestingsoup.com` |
| Service type | `HTTP` |
| Service URL | `192.168.5.54:18871` |
| Path | Leave blank. |

It is an additional route; the normal route was retained. Cloudflare documents
the hostname-to-local-service mapping in its
[published application guide](https://developers.cloudflare.com/cloudflare-one/networks/connectors/cloudflare-tunnel/routing-to-tunnel/).
The test console approval listener remains `127.0.0.1:18870` and must not be
published.

At `2026-10-20T07:18:25Z`, a probe sharing the existing Cloudflare connector's
network namespace reached the new private backend and received the expected
HTTP 401 without credentials. An authenticated host check returned the empty
outbox. The normal database/ledger still matched the baseline. These verify the
origin connection, not DNS, public routing or Muse egress.

The named origin passed operator verification at `2026-10-20T07:24:00Z`:
authenticated outbox HTTP 200 with cursor 0 and no events; missing credentials
HTTP 401; console route HTTP 404 on the provider listener; cross-use of normal
and test credentials HTTP 401. The normal outbox remained empty after cursor 2,
its baseline snapshot and title ledger matched, and the test compiler heartbeat
was recent.

An independent HTTPS request from the development Mac passed at `07:25:24Z`.
The Mac's resolver still returned a negative result, so this probe used an address
from a fresh public DNS answer while preserving the hostname and TLS certificate
validation. This establishes the named public route from the operator's network,
not from Muse's runtime.

The existing test token is unchanged. After the new route is saved, update only
the existing test connector's origin (and its vault origin binding, if required)
so the already-enabled worker uses the named endpoint. Verify authenticated HTTP
200 through it and report a real scheduled empty-check run. Do not create a
duplicate worker or reset its cursor. The operator will then queue a fresh
request, review its returned plan, and approve exactly one compile/package
attempt under the owner's test authorization. Keep the queue empty until the
worker's transport check passes.

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
The active fixture's Quick Tunnel has been removed after the reported Muse
transport block; follow the named-route migration above. Future repetitions need
a fresh isolated configuration.

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
