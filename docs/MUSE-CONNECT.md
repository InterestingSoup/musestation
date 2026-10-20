# Connect Muse to the provider API

Muse needs outbound HTTPS and a private credential store. No shared drive is
required for this transport: Opti3 retains requests, plans, source and results.
The same messages can also use the folder protocol in [MUSE-EXCHANGE.md](MUSE-EXCHANGE.md).

## Temporary HTTPS test

On the Docker host, with the relay already running:

```sh
docker compose --env-file .env.relay --profile relay --profile provider-test config --quiet
docker compose --env-file .env.relay --profile relay --profile provider-test up -d --no-deps provider-tunnel
docker compose --env-file .env.relay --profile provider-test logs --tail=80 provider-tunnel
```

The optional tunnel shares the relay's network namespace and forwards only to
its provider listener on port 8861. It has no token-file, runtime-data or Docker
socket mounts. Provider authentication remains enforced by the relay. Console
approval and artifact routes are not served on that listener.

Copy the generated `https://…trycloudflare.com` origin from the tunnel log.
Share the existing `runtime/secrets/provider-token` through Muse's secret store,
never through the public repository. Do not share `console-token`.
The tunnel reuses the existing MuseStation dashboard label settings.

This is a [Cloudflare Quick Tunnel](https://developers.cloudflare.com/tunnel/get-started/quick-tunnels/):
the URL is temporary, can change when the tunnel restarts, and has no uptime
guarantee. It is suitable for this connection test, not a permanent service
address. It is not automatically restarted after a crash or host reboot.
Stopping the tunnel removes remote reachability without stopping the relay:

```sh
docker compose --env-file .env.relay --profile provider-test stop provider-tunnel
```

For ongoing use, replace the test tunnel with a deliberately configured named
tunnel/custom hostname and review the public API's operating limits. Do not
forward the console listener on port 8860.

## Credentials and first request

Configure these values privately in Muse:

```text
MUSESTATION_PROVIDER_URL=https://<actual-tunnel-hostname>
MUSESTATION_PROVIDER_TOKEN=<value from provider-token>
```

Every request uses:

```http
Authorization: Bearer <MUSESTATION_PROVIDER_TOKEN>
```

First make `GET /provider/outbox?after=0`. A successful empty relay returns:

```json
{"schema":1,"events":[],"next_after":0}
```

Missing or wrong credentials return 401. An empty event list is a successful
connection with no queued requests, not a build failure. Do not test connectivity
by inventing a request ID or approving a plan.

The connector then:

1. Polls the outbox and saves its cursor after successfully processing events.
2. Replies to `request` events using `POST /provider/plans`.
3. Waits for `approved_job`, then sends source to `POST /provider/sources` with
   the exact job ID and approved revision.
4. Reads result events, diagnostics and logs. Source corrections need a new plan
   and approval; a completed attempt is not silently retried.

Use `Content-Type: application/json` for POSTs. Exact schemas, source bounds and
idempotency rules are in [the exchange contract](MUSE-EXCHANGE.md#authenticated-provider-transport).
The provider credential cannot approve jobs, fetch installable artifacts through
console routes, or directly launch anything on the PS5.

The health response currently keeps `muse_connector_verified` hardcoded false.
It is not a live authentication indicator. A successful provider response proves
network/authentication reachability; a real request/plan round trip must still
be observed before claiming the Muse integration works.

## Initial endpoint verification

Checked from the development Mac over the public HTTPS test route on 2026-10-19:

| Probe | Result |
|---|---|
| Outbox with the provider credential | 200; empty event list, cursor 0 |
| Outbox with no credential or an incorrect credential | 401 |
| Console approval path on this provider origin | 404 |
| JSON plan submission for a nonexistent request ID | 404; no plan/job created |

Compose validation confirmed the existing relay and builder configurations were
unchanged. Only the optional tunnel container started; the relay remained healthy.
The live URL and credential are handed off privately and are not checked into Git.
These checks verify the HTTPS/API connection from the Mac, not a request made by
Muse or a PS5 hardware test.
