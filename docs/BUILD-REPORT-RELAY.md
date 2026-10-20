# MuseStation 01.003.000 — Hybrid relay candidate

Build output and log paths below refer to the developer’s retained verification
files. They are not downloads in this Git checkout; public binary release assets
have not been uploaded.

This is the preserved 01.003.000 report. The current recovery/authentication candidate is in [BUILD-REPORT-REVIEW.md](BUILD-REPORT-REVIEW.md).

Built 2026-10-19 on Opti3 in Docker. The current path is local speech/typing on
PS5, Muse-authored plans/source through an exchange, Opti3 compilation, then
verified console download/install/launch. No real Muse account connection or PS5
execution has been verified. Firmware 12.40 native payload compatibility remains
an external hardware prerequisite.

## Release artifact

Single package (`dist/pkg-v4/IV0000-PPSA99576_00-MUSEGADGET000001.pkg`):
**2,101,900,140 bytes**. SHA-256:
`21bae467efd6230884957ff09901c15ad73c474fc80040e19bda128345fa7511`.

The packager read back and hash-compared **443 input files**; all **8 structural
checks** passed. The independent verifier confirmed exact title/version, final
package hash, bundled weight digests, helper and licensing files. Only the
previously documented zero SDK/firmware-field promotion to the upstream 2.00
baseline occurs; that metadata does not prove 12.40 runtime compatibility.
Complete package report (`dist/pkg-v4/IV0000-PPSA99576_00-MUSEGADGET000001.pkg.report.json`),
independent check (`logs/musestation-relay-release-check.json`),
packaging log (`logs/package-relay-v4.log`).

The four bundled models still total **1,957,178,283 bytes**. Hybrid skips loading
the coding model; optional Local lab retains it. GPU/local voice engine behavior
is inherited from the earlier compiled engine and real host regression evidence.
The native title now includes asynchronous planning, typed Hybrid input, pending
request recovery, optional microphone setup and the revised relay connection UI.

- Native SELF: 87,877,860 bytes,
  SHA-256 `224fcfe169e487e34de29d0a74c45db58aeee4c2152aea2e981ad557df6f7e3a`.
- Installer helper: 210,736 bytes,
  SHA-256 `9840421dc2d8f690e6370ed45d8e71a3bc688efc6aedb27f33df170dcad49d60`.
- Native build log (`logs/native-relay-v4.log`); intermediates in `dist/native-v4`.
  The native ELF is not an elfldr payload. The independent hello payload remains
  `dist/muse-hello.elf`.
- Corresponding source (`dist/pkg-v4/musestation-corresponding-source.tar.gz`)
  includes the relay, client, fixed compiler recipe and pinned native/GPU/voice
  dependencies. Its adjacent checksum and release.json record the archive digest.
  Private runtime, logs, device configuration and credentials are excluded.

## Verification

- Client regression (`logs/shared-folder-client-regression.log`): real C client
  and actual Hybrid worker pending→ready/error, local checkpoint recovery,
  cancellation, transient 503 retry, typed requests and saved config; existing
  installer/IPC tests pass. Directory fsync tolerates only EINVAL/ENOTSUP, with
  file-sync and real I/O failures preserved. This cannot promise power-loss
  directory durability when the PS5 filesystem does not support it.
- Relay regression (`logs/relay-regression.log`): 50 combined relay/builder
  tests pass in a network-disabled Docker container with 256 MiB. Exact revision
  approval, concurrent idempotency, immutable source, provider authentication,
  bounded log/diagnostic export, private-store compiler ownership, SQLite retry,
  interrupted-build recovery and atomic publication are covered.
- Builder tests (`logs/relay-builder-check.log`): 13 focused tests pass, including
  namespace isolation, source bounds, trusted-kit integrity and container limits.
- UI preview log (`logs/shared-folder-native-preview.log`): actual view code
  rendered on the host, including waiting and optional-mic setup. No clipping
  or false account-connection claim was found. These are host previews, not PS5
  screenshots, performance readings or controller tests.
- Native graphics fixture (`dist/relay-smoke/`): human-authored sample compiled
  using the fixed kit, then packaged with full roundtrip checks across 82 files.
  Package size 38,054,698 bytes; SHA-256
  `695045115843cc0ec037db86a8732d22ae028386f5c120d0ed8ec8de8eab9f05`.
  This fixture is not Muse-generated and was not installed or run on a PS5.

## Full relay test and deployment

The full-stack report (`logs/relay-e2e/report.json`) passed in **162.14 seconds**.
It ran the actual C client against the real API/SQLite/exchange and separate
Docker worker. The fixture returned its plan through an atomic folder write,
waited for the client's explicit simulated X approval, pushed source through the
authenticated provider listener, compiled and fully checked the native package,
then exercised C-client download/hash/identity verification and JSONL upload/export.
The client log (`logs/relay-e2e/client.log`) records the observed transitions.
No mock package bytes were used in this test. The plan/source were a labeled
human-authored fixture, not a response from Muse; no install or PS5 contact occurred.

The full-stack package was **38,054,688 bytes**, SHA-256
`ea382d0ac37b981b5ad70777c01a05fdc9ba4fbc672fe199bfa7dfd4a5a5ce9b`, assigned
`PPSA99000`. Its independent fixture database is separate from production.

Production is deployed at **http://192.168.5.54:8860**. The API is healthy and the
worker heartbeat is current. The provider listener is bound only to host loopback
port 8861; authenticated access and rejection of unauthenticated access were checked.
Production contains zero fixture requests, jobs or logs. Homepage's API confirmed
the approved **Personal → MuseStation** entry with its link, icon, description and
health monitor. Deployment check (`logs/relay-deployment-check.json`),
Homepage check (`logs/relay-homepage-check.json`).

The tested and deployed relay image is
`sha256:5de86fc3d76fc85e29d5f5108b6bc08cf1e04ea0f5134d736b5095e63d2aa114`.
Compiler image: `sha256:fb763b1836b5c05b89e54d1f8d6d3621c30682931744aa5c2c56fb2d8c7815f2`.
Packager image: `sha256:20d470e8ebdc7f49dc67c342ecc1922ca789aaf4ffaa0988b62ea3f04a59f858`.
Native kit manifest SHA-256:
`687a5605557b58ed1a954f59f228bdb198aa267cdd767d2a8bc999a2aeab86d1`.
The local exchange is `/home/opti3/services/ps5-muse/runtime/exchange`.
The provider credential is stored privately on Opti3; no credential is embedded
in the package, source archive or this report. No actual Muse connector is active.

## Current limits and hardware handoff

Use [SMOKE-TEST.md](SMOKE-TEST.md), starting with the Hybrid typed flow. The console
must have a separately confirmed compatible native installer/loader environment.
The repository audit still finds no bundled etaHEN, kstuff or ShadowMountPlus in
Relapse and no independently confirmed etaHEN 12.40 matrix. SDK and Relapse HEADs
match the recorded pins; the fresh audit JSON (`logs/relay-upstream-audit.json`)
records exact revisions and the current payload listing. The [Reddit draft](reddit-draft.txt)
asks for first-hand versions and distinguishes ELF loading from native installation;
it has not been posted.

The relay accepts already-transcribed JSON, not raw audio. A real Muse connector
must read requests and write plans/source using [MUSE-EXCHANGE.md](MUSE-EXCHANGE.md).
No login, provider key or cloud endpoint is invented. Account connection,
on-console GPU/voice, installation, launch, background execution and a complete
real Muse→PS5 run remain unverified. Pending plan recovery is implemented;
already-approved console job recovery is not. Keep MuseStation open through install.
