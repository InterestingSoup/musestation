# MuseStation companion connection prototype

This is a runnable, status-only Linux companion using the official Muse gadget
SDK. It pairs through the Muse app's real **Add Device** BLE flow and uses the
SDK's account credential refresh, encrypted transport and reconnect logic. It
does not connect to a PS5 yet, submit chat messages, build, install, execute
commands, or implement our earlier `/voice`, `/plan`, `/build` HTTP contract.

The companion advertises exactly one command, `station.health`, and rejects all
others at execution time, even if the server sends an unadvertised command.
Its response explicitly reports `ps5_connected: false`, `build_supported: false`
and `install_supported: false`. The official SDK's unrestricted shell/file
executor, installer, local chat socket and service entrypoint are never used.

## Source and protocol

The unmodified `vendor/muse-gadget-sdk/linux` source and root Apache-2.0 license
are pinned to commit
[`e5c4662242c23a5a786cf49f75626ae31283bbc1`](https://github.com/facebookincubator/muse-gadget-sdk/tree/e5c4662242c23a5a786cf49f75626ae31283bbc1).
`UPSTREAM.json` hashes every supplied upstream file. The wrapper overrides only
`Service._session` construction because that method otherwise hardcodes the
stock shell/file command specifications. It supplies `DeviceDescription` with
our allowlist and `LinkSession.run_command` with our restricted callback.

The [official Linux instructions](https://github.com/facebookincubator/muse-gadget-sdk/blob/e5c4662242c23a5a786cf49f75626ae31283bbc1/linux/README.md)
require a Linux BLE peripheral adapter, BlueZ/D-Bus, a valid SDK token from
[Muse SDK tokens](https://gadgets.muse.ai/settings/sdk-tokens), and the Muse phone
app with Developer mode enabled. There is no QR/device-code enrollment flow in
this source. The companion appears as `MuseGadgetXXXXXX` and registers as a Linux
`homehub`, not as an ESP32 `link`; this avoids advertising the ESP32 OTA path.

Pairing v5 uses P-256 ECDH, HKDF-SHA256 and AES-256-GCM with app confirmation.
The phone supplies device access/refresh credentials through encrypted BLE
provisioning. The SDK checks `/fetch_vms` over HTTPS, upgrades
`wss://<noise_host>/v1/noise?vm_id=...` with a per-VM bearer, then uses
`Noise_XX_25519_AESGCM_SHA256`. Its long-lived `/link-control` stream carries
length-prefixed JSON `link.register`, `link.invoke`, and `link.result` messages.
These are official SDK protocol details, not endpoints added by this project.

Device access tokens are rotated at three hours; SDK comments describe their
lifetime as about four hours. A refresh 401 or `link.unpaired` clears pairing.
Changing/revoking an SDK token requires a new pairing. Credentials stay on this
Linux companion and are never returned in health/status responses. Logging
emits fixed event classes only; raw SDK/server messages and tracebacks are
suppressed, including token hints.

## Offline verification

```sh
docker build -t musestation-companion:test companion
docker run --rm --memory=1g --cpus=1 --network none \
  --entrypoint sh musestation-companion:test /app/check.sh
```

The image uses a pinned Ubuntu base and the upstream hash-pinned WebSocket
dependencies; distribution packages are resolved at build time, so this is not
an offline/hermetic dependency-build claim. Tests require no SDK token, account,
Bluetooth or host D-Bus mount. They use the SDK's actual Noise implementation
and its in-memory VM fixture, verify the registered allowlist and rejected
commands, and run the complete upstream Linux test suite.

Verified on Opti3 in disposable Docker on 2026-10-19: **25 companion tests and
218 upstream tests passed**, using `--network none --memory=1g --cpus=1` and the
image's non-root user. The smoke-status response reported no saved pairing,
no live connection check, no PS5 connection and no Bluetooth adapters. Logs are
`logs/companion-image.log` and `logs/companion-check.log` at the project root.
This verifies the protocol fixtures and wrapper, not a real Muse account session.

## Running on a BLE-capable Linux host

No installer or persistent service has been deployed. Opti3's read-only check
on 2026-10-19 found no `/sys/class/bluetooth` and no exposed HCI adapter. Actual
pairing is blocked there until a BLE-capable host/adapter and BlueZ are available.
The PS5 SDK has no verified BLE GATT peripheral API in the inspected sources;
this companion does not claim native PS5 Bluetooth pairing.

For an explicit manual trial, use the supported Linux system Python with
`python3-dbus`, `python3-gi`, `python3-cryptography`, BlueZ and a virtualenv made
with `--system-site-packages`. Install the upstream `requirements.lock` with
`pip --require-hashes`. The account must have BlueZ's required D-Bus permissions.
The official Android pairing requirement is `[GATT] ExchangeMTU = 256` in
BlueZ's configuration; this prototype does not modify it automatically.

Create a private state directory owned by the runtime account, mode `0700`.
Place the user's SDK token in its `sdk_token` file, mode `0600`, through a local
secret input method, or provide `MUSEGADGET_SDK_TOKEN` through the execution
environment. Do not put credentials in command arguments, logs or this repo.
Keep the same identity/state and SDK token available for later refreshes.

From `companion/`, with that virtualenv active:

```sh
export PYTHONPATH="$PWD/src:$PWD/vendor/muse-gadget-sdk/linux/src"
python -m musestation_companion --state-dir /private/path/musestation status
python -m musestation_companion --state-dir /private/path/musestation pair
# In Muse: Settings > Devices > Developer mode > Add Device; choose printed name.
python -m musestation_companion --state-dir /private/path/musestation run
```

`pair` is an explicit enrollment operation and opens the official ten-minute BLE
window. `--force` is accepted only for `pair`, to replace an existing pairing.
`run` makes outbound connections only; Ctrl-C stops it. No listening HTTP or chat
socket is created. A lock prevents concurrent pairing/run processes using the
same state. State files must be private owned regular files; symlinks, shared
permissions, multiple hardlinks and oversized files are rejected, including
the SDK's predictable temporary identity/pairing files. This does not protect
against a malicious process already running as the same account. `status`
does not create a device identity and distinguishes saved pairing from a live
connection. Actual account pairing and PS5 connectivity remain unverified.

A future PS5 bridge requires a separate authenticated station protocol and
explicit approval-bound plan/build/install commands. It must preserve the
console's approval gate; adding the stock `system.run` command is not that bridge.
