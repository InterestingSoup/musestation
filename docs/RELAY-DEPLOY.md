# Opti3 relay deployment

Compose lives at the repository root. The API service receives no Docker socket;
the separate compiler worker receives it and launches only the fixed native recipe.
The worker is a trusted service with host Docker privileges. Do not expose its
private data directory or modify its code through the provider exchange.

The application, this stack and the website now share
[`moiz-qureshi/musestation`](https://github.com/moiz-qureshi/musestation).
Clone that repository on your Docker host; the existing Opti3 deployment keeps
its directory name `ps5-muse`. A Git push does not restart or update running
containers. See [repository layout and update procedure](REPOSITORY.md).

Current Opti3 deployment: relay healthy, worker heartbeat present, and Homepage
entry discovered. Evidence is in [BUILD-REPORT-INVENTORY.md](BUILD-REPORT-INVENTORY.md).
The live data directory is `runtime/data`, exchange is `runtime/exchange`, and
trusted kit is `runtime/native-kit-v1` under the project root. A host-simulated
request reached Muse and its returned plan was independently verified through
the console API. The plan-only check created no build jobs or title allocations.
Muse source/build integration and PS5 operation remain unverified.
See [connection and test status](MUSE-CONNECT.md#request-to-plan-verification).
The inventory requirement documented here is implemented in 01.005.000. Use the
release report and deployment record for exact build and service evidence.

## Build and configure

Run on Opti3 from `/home/opti3/services/ps5-muse`. First build the pinned toolchain,
GPU/native and packager images using the existing build instructions. Prepare a
fresh immutable kit (the command refuses to replace one):

```sh
python3 relay/build/prepare-kit.py --project "$PWD" --output "$PWD/runtime/native-kit-v1"
```

Create `runtime/data`, `runtime/exchange`, and `runtime/secrets`, owned by the
runtime UID/GID with mode 0700. As that user, run
`python3 scripts/relay-init-secrets.py runtime/secrets`. It creates distinct random
keys in `provider-token` and `console-token`, mode 0600, without printing them or
replacing existing keys. The console key is exactly 64 lowercase hex characters.
Never put keys into shell history or source control. Copy `.env.example` to
`.env.relay` and set `MUSE_PROVIDER_TOKEN_FILE` and `MUSE_CONSOLE_TOKEN_FILE`, the absolute runtime
and token paths, runtime IDs, socket group, immutable compiler/packager image IDs,
LAN bind address and Homepage settings. The container paths must match the host
paths because the worker starts sibling build containers on the host daemon.

```sh
docker compose --env-file .env.relay --profile relay config --quiet
docker compose --env-file .env.relay --profile relay build relay
docker compose --env-file .env.relay --profile relay up -d relay builder
docker compose --env-file .env.relay --profile relay ps
```

Both host bindings default to loopback. On the owner's Opti3 deployment the
console listener uses `192.168.5.54:8860`, and the provider now uses
`192.168.5.54:8861` so the existing bridge-network Cloudflare tunnel can reach it.
The named HTTPS route forwards only to the provider listener; see
[connection setup](MUSE-CONNECT.md). It does not publish the console API.
The optional `provider-test` profile remains available for a temporary HTTPS
test instead. No router port-forward or Muse account enrollment is needed.
The authenticated SSH route can also operate the exchange.
A real Muse connector needs access to the exchange or the protected provider API;
merely starting these containers does not connect Muse.

The relay refuses to start without a valid console key. Every console endpoint
except `GET /` and `GET /health` requires it; the provider key cannot approve or download console
jobs. Provision the console key through the protected install-time config or
setup wizard. This is a relay connection credential, not Muse device enrollment.
The wizard rejects public relay origins. Keep Compose's published bind on loopback
or your chosen private interface, never `0.0.0.0`. Container-internal wildcard
binding is necessary for port publication and does not change the host binding.
Plain HTTP still exposes credentials to an on-path network observer; a public
deployment needs an encrypted transport and compatible console client first.

Configure the console's `shadowmount_url` as well as the relay URL. The default
`http://127.0.0.1:10101` refers to ShadowMountPlus on the PS5, not the relay
container. File-based setup accepts `MUSE_SHADOWMOUNT_URL`; the native wizard's
field is **ShadowMount API on this console**. Only loopback or the exact configured
console IP is accepted. Keep ShadowMount's listener local where supported; do not
publish it through the relay or send either relay key to it. Its public
[API documentation](https://github.com/drakmor/ShadowMountPlus/blob/d7e35e6ce90abc6f9d0880ff40c2a5cc1fbfa075/docs/api.md#L6-L35)
describes an unauthenticated console-local API and a separate private ShellCore
socket that third-party clients must not use.

Before each approval, MuseStation requires API v1 capabilities `list_games` and
`installed_pkg_games`, then validates a bounded game list. A new relay allocation
without this report returns 428; update/configure the client rather than bypassing
the requirement with an invented empty list. An exact retry of an accepted
approval can still recover its existing job without a new allocation. See the
[inventory contract and limitations](MUSE-EXCHANGE.md#title-inventory-before-approval).
Inventory snapshots stay in private job storage and backups, not the shared
exchange or provider outbox. A healthy relay does not verify the console's SMP
listener, complete title inventory or firmware support.

Homepage labels use the user-approved group **Personal**, name **MuseStation**,
description **PS5 build server and job relay**, icon **mdi-sony-playstation**,
link `http://192.168.5.54:8860`, monitor `http://192.168.5.54:8860/health`.
Homepage configuration files are not edited by this project.

## Verification and service operation

Read `/health` for API/exchange and worker status. The real connector and hardware
verification fields remain false. The current `/` page is operational JSON rather
than a user dashboard. The console UI remains the place to submit/review requests.

```sh
curl --fail http://192.168.5.54:8860/health
docker compose --env-file .env.relay --profile relay logs --tail=60 relay builder
```

Use `scripts/relay-e2e.py --help` for the isolated full-stack compile/download test.
It creates a new fixture runtime and uses a human-authored provider response,
explicit simulated controller approval, actual C client and real native compiler.
It does not install anything, contact Muse or connect to a PS5. Its API binds
only to loopback ports 18860/18861 plus an ephemeral response-loss proxy, and its
temporary containers are removed at exit. The proxy drops the first successful
voice response to exercise recovery by the persisted client request ID.

To stop the relay, use Compose `stop relay builder`. Stopping the worker during
compilation will fail that attempt on recovery. Keep the data/exchange/kit/token
volumes; do not remove them as part of ordinary restart. Back up the database and
private directories while both services are stopped to obtain a coherent snapshot.
Do not manually delete SQLite jobs just to reset a title ID or quota.
The independent `data/title-ledger.json` must be included in backups. It survives
SQLite replacement and never frees a reserved title ID. Restore the most recent
ledger even if recovering an older database; an old ledger can omit installations
made after that backup. New allocations exclude the console-reported snapshot,
the permanent ledger and the fixed ProsperoStore/MuseStation IDs (`PPSA99000` and
`PPSA99576`). The ledger is still needed for in-flight jobs and titles missing from
the snapshot. Loss of both histories requires explicit reconciliation before
generating more titles; the upstream API has no completeness or freshness
attestation. Installed-title refusal remains the final collision check.

## Coolify migration

The root Compose definition and environment settings remain the deployment inputs.
On the target host prepare the same verified images and kit, restore the stopped
service's private data/exchange, and set absolute paths, UID/GID, Docker socket
group and private bindings for that host. The worker needs a Docker socket and
sibling-container mount paths, so the target must provide those capabilities.
Coolify can manage the Compose services but does not supply the PS5 SDK or move
these volumes automatically. Update the console's configured relay URL if the
endpoint changes; no console recompilation is required.
