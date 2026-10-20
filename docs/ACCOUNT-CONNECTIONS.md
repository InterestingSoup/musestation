# MuseStation account connections

Current decision: use the [Opti3 exchange](MUSE-EXCHANGE.md), without SDK/BLE enrollment. The account flows below are retained research for future direct integrations.

## Muse first — current decision, 2026-10-19

The user supplied [Muse Gadgets](https://gadgets.muse.ai/) and its
[official SDK](https://github.com/facebookincubator/muse-gadget-sdk). We inspected
commit `e5c4662242c23a5a786cf49f75626ae31283bbc1`, licensed Apache-2.0.
The [Linux README](https://github.com/facebookincubator/muse-gadget-sdk/blob/e5c4662242c23a5a786cf49f75626ae31283bbc1/linux/README.md)
describes **Bluetooth Add Device pairing**, not a QR/device-code grant:

1. Obtain an SDK token through the official gadget account site.
2. Explicitly open pairing on a supported BLE Linux device.
3. In the Muse phone app, enable Devices / Developer mode and select Add Device.
4. The SDK establishes its encrypted session and persists the device credentials.

The source implements BLE GATT with BlueZ, community provisioning, cloud token
refresh and WSS/Noise sessions. The SDK's standard executor can run shell commands
and read/write files as its service account. MuseStation's companion prototype
must advertise only its explicit commands and reject everything else. Do not run
the stock installer on Opti3 or expose a general command executor as part of this
integration. No account sign-in, token read or device enrollment was performed.

Opti3 currently exposes no Bluetooth adapter (`/sys/class/bluetooth` absent during
the read-only check). Real provisioning therefore needs a supported adapter/driver
or another supported BLE Linux host. We found no proven PS5 BLE advertising/GATT
peripheral implementation in the inspected public SDKs. Generic FreeBSD Bluetooth
headers and controller-address APIs do not establish that capability.

The native client still has only manual LAN HTTP transport; it cannot hold or
transmit account credentials securely. The restricted companion prototype and its
limits are in [companion/README.md](../companion/README.md). Its health capability
is not the plan/build/installation bridge. Pairing a companion does not by itself
connect the PS5 or implement the [proposed build API](API-CONTRACT.md).

A product connection flow should report separately: companion paired to Muse,
PS5 securely linked to its companion, build service ready. Show a connected state
only from verified live state, never from an entered address or a locally created
QR. Account connection is not permission to approve a plan or execute an app.

## Other providers — deferred research

The remaining notes record the prior OpenAI research from 2026-10-19; they are not
implemented provider connections or a commitment to use the same login protocol
as Muse. Recheck the applicable official documentation when implementing them.

## Personal Codex companion: documented device-code route

A user-controlled local/open-source companion can run actual Codex app-server over
stdio. `account/login/start` with `type: "chatgptDeviceCode"` returns `loginId`,
`verificationUrl`, and `userCode`. The console can show a QR of that returned URL
and the code. Connection is established only after matching
`account/login/completed` success and `account/updated`; cancellation uses
`account/login/cancel`. Device-code login must be enabled in ChatGPT security
settings or permitted by the workspace administrator. Keep tokens in the
companion's protected credential storage; never put tokens in QR codes or logs.
[App-server authentication](https://learn.chatgpt.com/docs/app-server),
[Codex authentication](https://learn.chatgpt.com/docs/auth).

This managed app-server authentication is **not permitted for commercial or hosted
services**. A public Muse multi-user broker must not repurpose this route. The docs
recommend Sign in with ChatGPT (SIWC) for control and visibility, with the applicable
partner onboarding. Owner-controlled self-hosting is distinct from a public hosted
service. [App-server documentation](https://learn.chatgpt.com/docs/app-server).

## Branded open-source app: SIWC public-client route

Current SIWC documentation supports a real customer ChatGPT-plan connection without
an API key or embedded client secret. The documented open-source flow is dynamic
registration followed by OAuth Authorization Code + PKCE, with fresh state/nonce,
a stable host identifier, issued client ID, validated identity and granted plan
permission. It requires `http://127.0.0.1:<port>/auth/callback`; only the port varies.
No generic SIWC device-authorization grant was documented in the reviewed pages.
Do not invent a device endpoint or substitute a PS5 LAN callback.
[SIWC sign-in](https://developers.openai.com/siwc/token-sharing-open-source/sign-in).

A phone scanning an authorization URL sees **its own** loopback address, not the
PS5 or Opti3. An owner-controlled VM can use the documented local sign-in and secure
credential-transfer workflow, preserving the VM's host identifier. Paid/remotely
hosted offerings require the stated interest/onboarding process; obtain the right
integration approval before turning this into a public broker.
[SIWC overview](https://developers.openai.com/siwc/token-sharing-open-source),
[Self-hosted VMs](https://developers.openai.com/siwc/token-sharing-open-source/self-hosted-vms).

SIWC can feed Codex app-server through its official custom Responses provider:
`ACCESS_TOKEN` is a child-process environment credential, provider URL is the public
`https://api.openai.com/v1`, and app-server runs over stdio with
`requires_openai_auth=false`. The app owns token refresh and restarts/resumes the
child with refreshed credentials. This differs from managed Codex device login.
[SIWC app-server adapter](https://developers.openai.com/siwc/token-sharing-open-source/codex-app-server).

Plan permission does not grant ChatGPT history. Supported models must be discovered
with the account token, and a streamed turn is successful only at its completed
terminal event. The current SIWC preview does not supply audio transcription or
video/audio inputs, so local Whisper/TTS remain the voice path.
[Models and inference](https://developers.openai.com/siwc/token-sharing-open-source/models-and-inference),
[Preview limitations](https://developers.openai.com/siwc/token-sharing-open-source/preview-limitations).

## Transport work required before implementation

The current gadget client (`src/client.c`, `src/config.c`) only accepts numeric LAN
HTTP origins. It has no TLS, certificate validation, DNS or authenticated provider
header path. Adding QR artwork does not fix that gap. Implement and test an
authenticated encrypted console-to-companion channel first (TLS with appropriate
certificate validation/pinning, or an explicitly secured tunnel). The companion
should expose a narrow, scoped pairing/session/action API; keep OpenAI tokens there.
Do not expose app-server's raw control interface on an unauthenticated LAN listener.

For a future OpenAI adapter, use a secure paired companion and the supported
authentication route for its distribution model. Muse Add Device integration is
the current priority. Retain explicit user approval
for actions; account login is not approval to execute generated work. No account
login, token inspection or external account mutation was performed for this research.
