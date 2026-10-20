# Connect Muse to the provider API

Muse needs outbound HTTPS and a private credential store. No shared drive is
required for this transport: Opti3 retains requests, plans, source and results.
The same messages can also use the folder protocol in [MUSE-EXCHANGE.md](MUSE-EXCHANGE.md).

## Named Cloudflare route

The owner's configured hostname is `musestation-api.interestingsoup.com`, with
an HTTP origin at `192.168.5.54:8861` on Opti3. This is the provider API, not the
landing page or the console API.

For a tunnel running in a separate Docker bridge network, its `127.0.0.1` is
not the host's loopback interface. Configure the relay's private deployment file
with `MUSE_PROVIDER_HOST_BIND=<your-server-LAN-IP>` and `MUSE_PROVIDER_PORT=8861`,
then apply that change. If the temporary `provider-tunnel` is running, stop and
remove just that service first: it shares the old relay's network namespace.
No runtime data is mounted in that temporary container.

```sh
docker compose --env-file .env.relay --profile provider-test stop provider-tunnel
docker compose --env-file .env.relay --profile provider-test rm -f provider-tunnel
```

Validate and recreate only the relay:

```sh
docker compose --env-file .env.relay --profile relay config --quiet
docker compose --env-file .env.relay --profile relay up -d --no-deps relay
```

The checked-in default stays `127.0.0.1`; the operator must explicitly select a
private interface. On this Opti3 deployment, the selected bind is `192.168.5.54`.
Do not use `0.0.0.0` or create a router port-forward. The provider key is required
on the private LAN as well as through the tunnel. HTTP on the origin/LAN segment
is not encrypted; Muse uses the public HTTPS hostname.

In Cloudflare, publish an application on the existing tunnel with service type
**HTTP** and origin `<your-server-LAN-IP>:8861`. A host-network tunnel may instead
use `127.0.0.1:8861` when the relay retains its loopback bind.
[Cloudflare's routing documentation](https://developers.cloudflare.com/tunnel/concepts/routing/)
describes the hostname-to-service mapping and associated CNAME.
Never route this hostname to port 8860, which serves console approval.

Keep the same provider token when changing the hostname. Update Muse's stored
URL, then verify the outbox request below before processing work.
The owner's temporary tunnel has been removed; use the named hostname above.

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
MUSESTATION_PROVIDER_URL=https://<your-configured-provider-hostname>
MUSESTATION_PROVIDER_TOKEN=<value from provider-token>
MUSESTATION_USER_AGENT=MuseStationConnector/1.0
```

Every request uses:

```http
Authorization: Bearer <MUSESTATION_PROVIDER_TOKEN>
User-Agent: MuseStationConnector/1.0
Accept: application/json
```

Set these headers explicitly in the HTTP client; defining an environment variable
alone does not add the header. During the named-route check, Cloudflare returned
403 with error 1010 for Python urllib's default client signature. The explicit
application User-Agent above passed, with no Cloudflare security changes.
[Cloudflare documents error 1010](https://developers.cloudflare.com/support/troubleshooting/http-status-codes/cloudflare-1xxx-errors/error-1010/)
as a rejection based on a browser/client signature. If it recurs, check the
domain's Cloudflare security events; a Cloudflare 403 is distinct from the
relay's 401 for missing or invalid credentials.

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

## Named endpoint verification

Checked from the development Mac over
`https://musestation-api.interestingsoup.com` on 2026-10-19 (America/Los_Angeles),
using `User-Agent: MuseStationConnector/1.0`:

| Probe | Result |
|---|---|
| Outbox with the existing provider credential | 200; empty event list, cursor 0 |
| Outbox with no credential or an incorrect credential | 401 |
| Console approval path on this provider origin | 404 |
| JSON plan submission for a nonexistent request ID | 404; no plan/job created |

The only effective Compose configuration change was the provider's host binding
from `127.0.0.1` to `192.168.5.54`. The relay was recreated, the builder was left
running, and the temporary tunnel was removed. A request from the existing
personal tunnel's network namespace also reached the origin and returned the
expected 401 without a key. The private connection file now contains the named
URL, unchanged provider credential and explicit User-Agent.

These checks verify API reachability and authentication from the Mac. They do
not establish a real Muse request/plan round trip or PS5 hardware operation.

## Initial temporary endpoint verification

Checked from the development Mac over the public HTTPS test route on 2026-10-19:

| Probe | Result |
|---|---|
| Outbox with the provider credential | 200; empty event list, cursor 0 |
| Outbox with no credential or an incorrect credential | 401 |
| Console approval path on this provider origin | 404 |
| JSON plan submission for a nonexistent request ID | 404; no plan/job created |

Compose validation confirmed the existing relay and builder configurations were
unchanged. Only the optional tunnel container started; the relay remained healthy.
The credential and private connection file are not checked into Git.
These checks verify the HTTPS/API connection from the Mac, not a request made by
Muse or a PS5 hardware test.
