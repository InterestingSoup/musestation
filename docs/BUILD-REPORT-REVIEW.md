# MuseStation 01.004.000 — Muse exchange review

Build output and log paths below refer to the developer’s retained verification
files. They are not downloads in this Git checkout; public binary release assets
have not been uploaded.

This candidate addresses Muse's five-item review. All execution evidence is from
Docker on Opti3 or host fixtures. No PS5 installation, GPU, microphone, inference
or launch verification is claimed. The real Muse connector remains unconnected.

## Changes

1. The console generates a random `p<32 hex>` request ID and saves the exact JSON
   body before sending `/voice`. A missing response recovers by ID; 404 permits
   an exact replay. The relay rejects changed content under the same ID.
2. `data/title-ledger.json` reserves title identities outside SQLite, with a
   process lock, checksum and fsynced atomic replacement. Reservations precede
   job commits; rollback burns an ID. Legacy jobs reconcile on startup and
   corrupt/conflicting history fails closed. Losing both stores still requires
   operator reconciliation with installed console titles.
3. The model-bundled MuseStation release uses the separate native installation
   route. The generated-app recipe retains its 128 MiB limit. Custom assets stay
   unsupported pending the documented recipe/manifest review process.
4. An interrupted-session marker and JSONL journal are durable. Startup appends
   the recovery event before uploading the old journal. Offline failures retain
   both; new Hybrid work waits for recovery, while settings and Local mode remain
   available. Installer journals use the same authenticated recovery path.
5. Every console endpoint except `GET /` and `GET /health` requires a separate random console key.
   Provider credentials cannot approve plans. Setup masks the key and rejects
   public HTTP relay origins. Existing Hybrid configs require the new key;
   Local configs do not. Plain HTTP still requires a trusted private network.

A short code returned by the unauthenticated API would not establish possession
of the controller. The provisioned key authenticates the installation; physical
approval remains a client gate, not remote hardware attestation. Pending-plan
approval was already rejected; authentication closes access to ready approvals.

The authoritative updated protocol is [MUSE-EXCHANGE.md](MUSE-EXCHANGE.md).
The [public README](../README.md), [deployment guide](RELAY-DEPLOY.md) and
[hardware checklist](SMOKE-TEST.md) include the new setup/recovery behavior.

## Native compile

The full Vulkan/OpenGL/voice native title and installer helper compiled in Docker
on Opti3. Build log (`logs/native-review-v5.log`); no compiler warnings or errors
were reported. Intermediates are preserved in `dist/native-v5`:

- `eboot.bin`: 87,885,444 bytes, SHA-256
  `8cec07326d87c6eee6bd399fd23dbcb9f2a306ffb4d0a6e76f64b05acb9e182c`.
- `eboot.elf`: 88,905,076 bytes, SHA-256
  `5761c2b8479f5898e84e64395aab449d4e717c6546314c316552eb15de481bff`.

The intermediate native ELF is not a deployable loader payload. Install the final
package through the separately confirmed native installer environment.

## Package

MuseStation 01.004.000 package (`dist/pkg-v5/IV0000-PPSA99576_00-MUSEGADGET000001.pkg`)
is **2,101,965,680 bytes**, SHA-256
`b337b8aff14ab0d51d66295c6132a464816f5f282c61f4acb63bdfa06312d901`.
All **443 input files** passed full extraction/hash round-trip and all **8 structural
checks** passed. The four locked model files total 1,957,178,283 bytes.
The packaged helper is 227,736 bytes, SHA-256
`b97646fa59a9fc4fe430066ca725038cc6505109c7de903696b840b75657bd69`.

Evidence: package log (`logs/package-review-v5.log`),
independent verifier (`logs/musestation-review-release-check.json`),
full package report (`dist/pkg-v5/IV0000-PPSA99576_00-MUSEGADGET000001.pkg.report.json`).
The matching source archive (`dist/pkg-v5/musestation-corresponding-source.tar.gz`),
its checksum and release manifest (`dist/pkg-v5/release.json`) accompany the package.
Preserved 01.003.000 artifacts remain in `dist/pkg-v4`; this candidate does not
overwrite them. Package structure verification is not hardware installation proof.

## Verification

- Relay suite (`logs/review-relay-regression.log`): **70 tests passed** in a
  256 MiB Docker container, including SQLite loss, concurrent allocation,
  migration, exact request replay and credential separation.
- Full-stack report (`logs/review-relay-e2e/report.json`): **passed in 144.6 s**.
  A proxy deliberately drops the first `/voice` response after commit. The actual
  C client recovers, explicitly simulates X, downloads and verifies the package,
  and uploads its log. The test uses a human-authored source fixture, not Muse.
  Its compiled package is 38,054,688 bytes, SHA-256
  `1820822d4bbe26dc155374320ebaa426580b48437e57dd65b6efc549b60f8864`.
- Crash journal checks (`logs/log-recovery-check.log`): sanitized child-process
  crash, retained offline records, recovery ordering, Local privacy, descriptor
  journals, FIFO rejection and injected fsync failures.
- Installer checks (`logs/install-auth-check.log`): actual helper fixtures use
  authenticated journal delivery; credentials are excluded from log contents.
- Final client/worker suite (`logs/review-client-regression.log`): sanitizers,
  SDK objects and helper linking passed, including truncated responses, exact
  retry bytes, no credential forwarding to inference, recovery before work,
  actionable 401/403 errors, protected key files and Local zero-HTTP behavior.

Journal delivery is at least once; a lost acknowledgement can produce duplicate
records. Partial final JSONL lines are repaired. File data is fsynced and directory
entries are fsynced where supported; native filesystems returning EINVAL/ENOTSUP
cannot provide the stronger directory-durability guarantee without hardware tests.
The recovered event reports an unclean session, not a crash cause or backtrace.

## Deployment

The tested relay image is
`sha256:7c9280fd0ec02566cee18b885de886a47ee2698b5cb8f6558a57253ffc2cf33d`.
It is deployed at `http://192.168.5.54:8860`; provider publication remains
`127.0.0.1:8861`. A private stopped-service database backup preceded migration.
The worker heartbeat and user-approved Homepage labels are present. Production
contains zero requests, jobs and log uploads; fixtures ran in separate storage.
Deployment evidence (`logs/review-relay-deployment.json`) confirms missing keys
and the provider key receive 401 on the console API; health remains accessible.

Console and provider keys stay in separate private files and are not part of any
release artifact. Neither the unchanged native kit nor this relay update verifies
a compatible firmware-12.40 homebrew install environment. The dated repository
audit and unresolved payload coverage remain in [RESEARCH.md](RESEARCH.md).
