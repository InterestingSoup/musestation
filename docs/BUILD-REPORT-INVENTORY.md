# MuseStation 01.005.000 — title inventory and runtime installer loading

Build output and log paths below refer to the developer’s retained verification
files. They are not downloads in this Git checkout; public binary release assets
have not been uploaded.

This candidate implements the user's ordered follow-up: title allocation first,
AppInstUtil runtime loading second. ZIP installation and folder updates remain
deferred pending confirmation of the required firmware-12.40 environment.
All compilation and execution evidence is from Docker on Opti3 and host fixtures.
No PS5 installation, voice, inference or launch verification is claimed.

## Behavior

- Immediately before approval, the console reads ShadowMountPlus API v1 and
  requires `list_games` and `installed_pkg_games`, then validates its returned
  title list. The configurable `shadowmount_url` defaults to loopback port 10101.
  No relay credential is forwarded to those queries. Local mode remains offline.
- New relay allocations exclude `PPSA99000`, `PPSA99576`, every reported title
  and previous allocations. Missing inventory returns 428; malformed input fails
  without consuming a reservation. The normalized report stays in the private
  job record, outside the provider exchange. Accepted approval retries recover
  the same job without allocating again, including legacy retries without inventory.
- The ledger continues to protect allocated and in-flight jobs. Inventory also
  avoids reported collisions after loss of both SQLite and the ledger. The API
  can omit unavailable registrations and silently has a bounded discovery cache;
  absence never proves an ID is unused. The final OS existing-title check remains.
- The helper loads AppInstUtil by path and resolves all five required functions
  through the public SDK resolver before initialization. Authenticated journal
  setup and upload precede loading. Failures retain specific diagnostics; no
  installation is submitted after a module, symbol or initialization failure.
- Future ZIP updates require verified staging, ownership/version checks, a closed
  title, backup/journal recovery and rollback. App-bundle `0777` guidance does not
  apply to private credentials/configuration. Correct console time is required
  for HTTPS validation; certificate checks must remain enabled.

The authoritative [exchange contract](MUSE-EXCHANGE.md),
[hardware checklist](SMOKE-TEST.md), [native configuration](NATIVE-APP.md) and
[deferred ZIP/update design](PACKAGING.md#deferred-zip-delivery-and-updates) are updated.
Older clients cannot create new relay jobs without the inventory-aware approval.
The real Muse connector remains unconnected.

## Native build and package

MuseStation 01.005.000 package (`dist/pkg-v6/IV0000-PPSA99576_00-MUSEGADGET000001.pkg`):
**2,101,965,680 bytes**, SHA-256
`93e77e1f80fe508d45009fa22c54f85bd4f10bf15cd707d7295e75670dee6be6`.
All **443 files** passed complete extraction/hash round-trip and all **8 structural
checks** passed. The four unchanged locked models total 1,957,178,283 bytes.
The package and matching source archive (`dist/pkg-v6/musestation-corresponding-source.tar.gz`)
have adjacent checksums; release.json (`dist/pkg-v6/release.json`) records their
sizes, hashes and verification evidence. Previous candidates remain preserved.

The complete Vulkan/OpenGL/voice app rebuilt in Docker on Opti3 with
`MUSE_GPU=1`, `MUSE_NATIVE_VERSION=01.005.000`, and one compile job.
The immutable compiler image was
`sha256:fb763b1836b5c05b89e54d1f8d6d3621c30682931744aa5c2c56fb2d8c7815f2`;
packaging used
`sha256:20d470e8ebdc7f49dc67c342ecc1922ca789aaf4ffaa0988b62ea3f04a59f858`.

- Native SELF: 87,891,044 bytes, SHA-256
  `bd14c1a941475474e273f982a46b140bc87c89ecc3194c3a636145325a98f714`.
- Intermediate native ELF: 88,905,100 bytes, SHA-256
  `721d27b2d4bf1e8258d87ee835974e347418e918d9150f92b7eca2a63218d470`.
- Installer helper: 227,968 bytes, SHA-256
  `c763c7db1d881a0d923365ec5d4beadb0b505a74c5f556671dc2a9961da6c648`.
  ELF inspection confirms no AppInstUtil startup dependency or undefined import.

Evidence: native compile (`logs/native-inventory-v6.log`),
package build (`logs/package-inventory-v6.log`),
independent package verification (`logs/musestation-inventory-release-check.json`),
full package report (`dist/pkg-v6/IV0000-PPSA99576_00-MUSEGADGET000001.pkg.report.json`).
The native intermediate ELF is not an elfldr payload. No ZIP is published by
this release; the native kit's intermediate archive is not a model-bundled release.

## Tests and deployment evidence

- Relay tests (`logs/title-inventory-relay-tests.log`): **81 passed**, including
  fixed exclusions, reported collisions, exhaustion, concurrent allocation,
  missing/invalid snapshots, private audit, legacy migration and database loss.
- Client tests (`logs/title-inventory-client-tests.log`): **28 inventory cases**,
  including 4,096 titles, responses beyond the old parser token limit, duplicate
  keys/IDs, truncation, unavailable/malformed responses, no credential forwarding
  and Local zero HTTP. Sanitizers, SDK object compilation, Hybrid workers and
  setup/configuration checks also passed.
- Runtime-loader tests (`logs/runtime-load-helper-tests.log`): **11 scenarios**
  cover module failure, each missing symbol, initialization failure, 401/403,
  offline journal retention and success. Existing installer regressions passed
  in the companion log (`logs/runtime-load-helper-regression.log`).
- Full-stack test (`logs/title-inventory-e2e-final/report.json`): actual C client,
  a simulated console inventory occupying the first IDs, dropped initial response,
  explicit simulated X, idempotent approval, a real native compile, package
  verification and automatic log export. The assigned title is `PPSA99003`.
  It uses a human-authored provider fixture and attempts no PS5 installation.
- Deployment record (`logs/title-inventory-deployment.json`): image, stopped-service
  backup, health/worker state, private interface bindings, credential separation
  and the user-approved Homepage labels. The relay remains at
  `http://192.168.5.54:8860`; provider access remains Opti3 loopback port 8861.

The upstream audit (`logs/title-inventory-upstream-audit.json`) records fresh SDK,
Relapse and ShadowMountPlus checks. Source support and successful compilation do
not establish a working firmware-12.40 payload combination. See [research](RESEARCH.md).
