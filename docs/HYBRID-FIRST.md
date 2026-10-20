# MuseStation: Hybrid with Muse first

The current connection decision is **Muse → Opti3 job relay → PS5**, using the
[Muse exchange](MUSE-EXCHANGE.md). Muse writes plans and C++ source. Opti3 compiles
and packages approved jobs. MuseStation handles local voice, review, download,
installation and launch. This supersedes the earlier Bluetooth companion proposal.

## Product behavior

New configurations default to Hybrid/Muse with the phone listener disabled.
Hybrid does not load the coding model or draft plans. It submits a local transcript
or typed request, retains a pending plan across restarts, and displays Muse's
response when available. Adding detail includes the parent plan ID/revision.
Nothing builds before explicit approval of the returned plan. Voice approval
remains behind owner verification and conservative exact-phrase checks; physical
controls remain available. Typing works with no microphone module configured.

The relay saves requests, approvals, source, compiled packages and diagnostic
exports. Its folder and authenticated provider API support a Muse connector.
A host-simulated request reached the actual Muse workspace, and its returned plan
was independently verified through the console API with exact text and revision;
see [plan-exchange evidence](MUSE-CONNECT.md#request-to-plan-verification).
That plan-only check created no build jobs or title allocations. A separate,
authorized [source/compile test](MUSE-SOURCE-TEST.md#verified-result) then accepted
Muse's C++ source, compiled a native package and verified its download. Approval
and inventory were host simulations; console operation remains unverified.
An IP address is a relay configuration, not an account login. Until the connector
submits a plan, MuseStation displays “Waiting for Muse.”

The proposed direct SDK route is optional research, not a deployment prerequisite.
The [restricted companion prototype](../companion/README.md) is preserved; it is not
running and no BLE hardware or Muse device enrollment is required for the selected
exchange route. [Account connection research](ACCOUNT-CONNECTIONS.md) records the
SDK findings but does not govern the current relay protocol.

Experimental Local lab remains available. Existing offline configurations, saved
projects and private journals stay local. Switching modes reinitializes inference
so the coder loads only for Local. Closing MuseStation does not make local model
generation continue in the background. PS5 rest mode, running beside a second game,
and native installation behavior still need hardware validation.

## Verification boundaries

Client and actual worker regression tests cover pending/ready/error responses,
restart recovery, cancellation, typed requests, transient outages, exact revision
approval and saved configuration. The Local regression retains zero HTTP requests.
Host previews exercise waiting, relay setup and provider screens; they are not
console screenshots or performance measurements.

Existing real-engine checks exercised Whisper and both neural/eSpeak output without
a coding model configured (`logs/hybrid-voice-only-regression.log`). The native
GPU archive links the actual engine. This does not establish mic routing, GPU
stability or audio latency on the user's Slim.

The relay's full-stack test uses a clearly labeled, human-authored graphics app
and simulated controller approval. It verifies the real compiler, package
roundtrip, C-client download/hash/identity checks and automatic log export. It
does not claim Muse generated the code or that the package ran on hardware.

See [relay deployment](RELAY-DEPLOY.md), [exchange contract](MUSE-EXCHANGE.md) and
the versioned release report for exact artifacts and completed test evidence.
