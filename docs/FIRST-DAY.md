# PS5 arrival-day guide for MuseStation

Prepared 2026-10-19 for the owner's PS5 Slim, expected firmware **12.40**.
This guide accompanies the **01.005.000 experimental candidate**. Builds and
package checks passed on the build host; no PS5 installation, voice, GPU or
generated-app launch test has happened.

The network arrangement is **PS5 ↔ private LAN ↔ Opti3 ↔ Muse**. The PS5 gets
local connectivity but no Internet access. Opti3 keeps its Internet connection.
This is an application relay, not a general Internet proxy for the console.

## 0. Prepare before connecting the console

- Tell the seller not to update the console. Keep it on 12.40, even if an exploit
  advertises a wider firmware range.
- Have HDMI, a controller USB cable, and preferably Ethernet ready. Use the
  appropriate stand/feet and leave ventilation unobstructed.
- Have router administration access and the Opti3/Mac available. The exact
  firewall menus depend on the router; the policy below is router-independent.
- Keep the console unplugged from Ethernet and do not give it Wi-Fi credentials
  until its Internet block is installed.
- Keep discs and USB firmware-update media out during initial setup. Do not
  factory-reset or reinstall system software as an onboarding shortcut.
- Download homebrew files on the Mac/Opti3, with their source URLs, versions and
  checksums. The console does not need Internet access to download MuseStation's
  bundled models.

If the Slim has an unpaired detachable disc drive, leave pairing for a separate
decision. Sony requires Internet access to pair that drive. Do not lift the
console's Internet block just to satisfy this prompt; whether pairing can be
completed while preserving 12.40 is not established here.
[Sony's disc-drive instructions](https://www.playstation.com/en-us/support/hardware/ps5-disc-drive-set-up/)

## 1. First boot: stay offline and confirm the actual firmware

Connect power, HDMI and the controller only. Complete the offline portions of
setup and use a local user. Skip network setup and PSN sign-in. Do not sign in
with a main account, a spare account or the PlayStation App.

Open **Settings → System → System Software → Console Information**. Record the
complete software version, model number and network-interface MAC addresses.
The first gate is that the actual version matches **12.40**. If it differs,
stop the payload/install procedure and reassess; do not update to make an
instruction sheet match.
[Sony's version-check instructions](https://www.playstation.com/en-gb/support/hardware/playstation-system-software-application-version/)

An update prompt is not a prerequisite for this project. Decline it. If an
installation has already begun, do not cut power in an attempt to reverse it.

## 2. Disable automatic updates and rest-mode networking

In Settings, open the System section, then the system-software update settings.
Switch **Download Update Files Automatically** and **Install Update Files
Automatically** off.

In the saved-data/game settings, open automatic updates and turn both automatic
game download and rest-mode installation off. These settings are defense in
depth; the router block is what prevents new Internet downloads.
[Sony's automatic-update controls](https://www.playstation.com/en-us/support/hardware/automatic-updates-playstation/)

Under **Settings → System → Power Saving → Features Available in Rest Mode**,
disable **Stay Connected to the Internet** and network wake. During initial
testing, also set the idle rest-mode timer to **Don't Put in Rest Mode**.
[Sony's power settings](https://www.playstation.com/en-us/support/hardware/power-saving/)

Inspect the downloads queue. Cancel/delete a pending system update if the
normal UI offers that action. A network block does not remove an update already
on disk or prevent an update supplied by removable media. Do not accept a game,
USB or recovery prompt that asks to update the system.

## 3. Block the PS5's Internet access while preserving LAN access

Prefer a reserved address plus a device Internet-block rule, or a dedicated
console VLAN/SSID with no Internet route permitted. Apply the policy before
connecting the PS5. A dedicated network should still permit the required paths
to Opti3 and the deployment machine.

| Traffic | Required policy |
|---|---|
| PS5 to Opti3's console API | Allow TCP 8860 on the private LAN |
| PS5 to local network services | Allow DHCP and, if needed, the local resolver |
| Mac/Opti3 to the PS5 loader | Allow TCP 9021 on the private LAN once the loader is running |
| Operator to console file transfer | Allow only the chosen payload's actual ports while needed |
| PS5 to Internet destinations | Deny all protocols, IPv4 **and** IPv6 |
| Internet to PS5 | No forwarding, DMZ assignment or UPnP-created mappings |
| Opti3 to Muse/Internet | Keep the server's normal permitted connectivity |
| ShadowMount API | Keep console-local, normally `127.0.0.1:10101` |

For a rules-based firewall, place the console restriction ahead of a general
LAN-to-Internet allow rule, retaining the required local exceptions. Use the
router's equivalent of filtering traffic originating from the console network;
an inbound Internet rule alone does not block connections the console starts.
Account for existing connection states if the console was already connected.
[Netgate's rule-processing explanation](https://docs.netgate.com/pfsense/en/latest/firewall/rule-methodology.html)

Important details:

- A Wi-Fi MAC and a wired MAC may need separate rules. Keep only the chosen
  connection configured, or cover both. An IPv4 address reservation alone does
  not cover IPv6.
- If using a dedicated console network, either deny its IPv6 Internet traffic
  too or disable IPv6 on that network. Do not change the whole household's
  configuration unnecessarily.
- Avoid a guest network that isolates the PS5 from Opti3. A router's "pause"
  button is useful only if it preserves LAN access and covers both IP families.
- DNS blocklists can supplement this setup, but are not the primary control:
  filtering selected names is different from denying all routed Internet traffic.
  No unverified public "jailbreak DNS" address is required by this guide.
- Do not add a broad forward proxy, VPN exit or other Internet bypass through
  Opti3. Its MuseStation relay only needs the project API.

Keep the PS5 physically offline if the router cannot enforce this policy yet.

## 4. Connect the protected LAN and check both halves

After the policy is active, connect Ethernet or the protected Wi-Fi network.
Keep the normal local gateway and address configuration; the firewall enforces
the restriction. The PS5's networking toggle may need to remain enabled for LAN
use even though its label mentions Internet.

Expected results:

- The console receives the reserved local IP.
- Internet/PSN connectivity tests fail. Do not sign in or check for updates to
  "fix" that expected failure.
- Router counters/logs show the console's Internet attempts being denied,
  including IPv6 if present.
- On the Mac/Opti3 LAN, `http://192.168.5.54:8860/health` responds. This proves
  server reachability from that machine, not yet from the PS5.
- After MuseStation launches, an authenticated request reaching the relay
  proves the PS5-to-Opti3 path as well.

Set the console's date/time and time zone manually while offline. A wrong clock
can break HTTPS certificate validation; do not disable certificate checking or
open Internet access merely to synchronize time.
[ProsperoStore's clock guidance](https://github.com/darkxex/ProsperoStore/blob/e8b9b09fcd54cce27ea1bcda99a57af9df0fb4b4/README.md)

## 5. Qualify the payload stack before attempting installation

The following are distinct prerequisites. An exploit's firmware range is not
evidence that every homebrew installer works on that firmware.

| Component | Current primary-source evidence | Arrival-day requirement |
|---|---|---|
| Relapse | README advertises 7.00–13.60 and loader port 9021. | Confirm the exact 12.40 entry procedure and source revision. |
| Loader | Relapse currently includes `elfldr-ps5-1360.elf` and `kexp_2026_05_25.bin`. | Confirm the actual loaded build, then pass the independent hello test. A filename or open port alone is insufficient. |
| kstuff-lite | Official **v1.11 Beta** release lists 1.00–13.60 support. | Use a documented compatible revision and its loading instructions; this is upstream support coverage, not our console test. |
| ShadowMountPlus | **1.7beta4** adds the relevant sandbox/inventory functionality; README requires kstuff-lite 1.07+. | Confirm operation on this console and API v1 capabilities `list_games` and `installed_pkg_games`. |
| Native package installer | No initial-install route has been qualified by this project on 12.40. | Establish the exact installer, payload dependencies and procedure before installing MuseStation. |
| etaHEN, if required by the chosen installer | Official latest release resolves to 2.5B; its release notes do not establish 12.40 support. | Do not assume the stock release is compatible or that etaHEN is mandatory for every possible route. |

Sources: [Relapse README](https://github.com/ntfargo/Relapse-Exploit),
[Relapse payload directory](https://github.com/ntfargo/Relapse-Exploit/tree/main/payloads),
[kstuff-lite v1.11](https://github.com/EchoStretch/kstuff-lite/releases/tag/v1.11),
[ShadowMountPlus tagged README](https://github.com/drakmor/ShadowMountPlus/blob/1.7beta4/README.md),
[ShadowMountPlus 1.7beta4](https://github.com/drakmor/ShadowMountPlus/releases/tag/1.7beta4),
[etaHEN 2.5B](https://github.com/etaHEN/etaHEN/releases/tag/2.5B).

Relapse currently does **not** bundle etaHEN, kstuff or ShadowMountPlus, and its
README no longer supplies the older DNS/hosting instructions. This guide cannot
honestly give a confirmed browser-entry URL or exact 12.40 installer sequence
from that README. Recheck the official sources when the console arrives; until
the missing procedure is established, the console can remain safely configured
at step 4. Do not substitute a random hosted exploit page.

Once that procedure is established, run the verified entry from the protected
network and load the required components in their documented order. Retain the
exact release filenames, hashes and results. If the exploit hangs or panics,
follow its recovery instructions; do not choose a firmware-update recovery path.
Homebrew needs reapplication after a full reboot.

The loose-folder/ZIP route is a future option for MuseStation, not a shipped
fallback in this release. ProsperoStore's folder instructions do not turn the
MuseStation `.pkg` into an installable folder. See [research](RESEARCH.md) and
[packaging](PACKAGING.md).

A concrete **initial-installer candidate**, rather than a confirmed 12.40 route,
is [standalone PKG Manager v1.5.1](https://github.com/itsPLK/ps5-pkg-manager/releases/tag/v1.5.1).
Its [tagged instructions](https://github.com/itsPLK/ps5-pkg-manager/blob/v1.5.1/README.md)
document loading its ELF, opening `http://<PS5-IP>:8844` from the Mac, and selecting
the local package using Direct Install. This needs a temporary LAN allowance from
the Mac to port 8844, not console Internet access. It does not list etaHEN as a
prerequisite. Its current AppInstUtil imports are a potential startup issue on
newer firmware, so stop if the ELF or web interface fails; do not treat a release
download as evidence of compatibility.

## 6. Run the independent loader smoke test

With the verified loader active, use the existing toolchain on **Opti3**. No Mac
Docker setup is needed. In an SSH session on Opti3:

```sh
cd /home/opti3/services/ps5-muse
read -r -p "PS5 LAN IP: " PS5_HOST
export PS5_HOST
bash scripts/deploy-hello.sh
```

The expected on-screen notification is **Muse: hello from your PS5**. The script
saves returning output to `logs/hello-console.log` on Opti3. Some loaders do not
return stdout, so an empty log is not by itself a failed console test.

Stop and diagnose if the notification does not appear. Sending bytes or opening
port 9021 proves neither payload execution nor native package support.
Only send `dist/muse-hello.elf` in this test: never send a `.pkg` or native
`eboot.elf` through the payload loader.

## 7. Verify and install the MuseStation candidate

The current candidate is:

```text
dist/pkg-v6/IV0000-PPSA99576_00-MUSEGADGET000001.pkg
Version: 01.005.000
Size: 2,101,965,680 bytes
SHA-256: 93e77e1f80fe508d45009fa22c54f85bd4f10bf15cd707d7295e75670dee6be6
```

On the Mac:

```sh
cd /Users/mqureshi/work/ps5-muse/dist/pkg-v6
shasum -a 256 -c IV0000-PPSA99576_00-MUSEGADGET000001.pkg.sha256
```

On Linux use `sha256sum -c` with that checksum file. A newer candidate must use
its own matching release manifest and checksum, not the values above.

Allow space for the 2.10 GB package, installed assets and the title's requested
4 GiB writable data allocation. Install using the native installer qualified in
step 5. Open MuseStation and check its display and controller input before
testing inference.

The package bundles its small coding model, Whisper, neural speech model and
eSpeak fallback. It does not install a jailbreak or require downloading those
models on first launch.

Do not apply `chmod -R 777` to the console's data directories. Where a separately
qualified loose-folder homebrew app requires 777, scope that to its bundle only.
MuseStation's private config, credentials and journals belong outside the bundle;
the default app-visible config path is `/download0/muse/config.json`. FTP may
expose different mounts, so discover the actual path before copying.

## 8. Configure Hybrid, starting with typing

Choose **Hybrid with Muse**, then configure the relay:

| Setup field | This owner's value |
|---|---|
| Console IP | The PS5's actual reserved LAN IP |
| Relay origin | `http://192.168.5.54:8860` |
| Console key | The private 64-character console key provisioned on Opti3 |
| ShadowMount API on this console | Normally `http://127.0.0.1:10101` |
| PS5LM fallback | Leave blank initially |
| Speech fallback | Leave blank initially |
| AudioIn module path | Leave blank for the first typed test |

Use the relay's **console-token**, never its provider-token or a Muse account
credential. Do not paste keys into chat, screenshots, logs or shell command
history. The operator can provision a private config using
`scripts/configure.py` and `MUSE_RELAY_TOKEN_FILE`; see
[native configuration](NATIVE-APP.md#configuration). Do not regenerate existing
relay credentials merely to configure the console.

The relay uses a private numeric HTTP origin. The Muse website is not the relay
URL. Loopback in the ShadowMount field refers to the PS5 itself, not Opti3.
Use the controller keyboard with D-pad/X and R1 to continue.

**A real Muse connection is still required.** The Opti3 relay is deployed, but
Muse account pairing and the real provider connector are not complete. The
exchange transport exists; it does not generate plans by itself. Connect Muse
using [MUSE-EXCHANGE.md](MUSE-EXCHANGE.md) before expecting a Hybrid build.
There is no working QR/device-code login screen in this candidate.

## 9. Complete one small typed build

1. Type a small request, such as a counter with Cross to increment, and submit
   with R1. Expect a request in the relay and "Waiting for Muse."
2. After Muse supplies a plan, read every page. Release X, then deliberately
   press X to approve. Square requests refinement; Circle leaves local review.
3. Approval must first obtain the real console inventory from ShadowMountPlus.
   If it fails, fix the payload/API path. Do not invent an empty inventory or
   bypass the check. ProsperoStore `PPSA99000`, MuseStation `PPSA99576` and
   reported existing titles must not be allocated to a generated app.
4. Keep MuseStation open through source arrival, compilation, download and
   complete installation. Launch should follow full completion, not merely
   "playable" status.
5. Verify the generated app's actual behavior and controls, then reopen
   MuseStation so its installer records can reach Opti3.

If an install outcome is uncertain, inspect its existing job/logs before creating
another. Automatic console recovery of an already-approved job is not implemented.
Do not close the app or interrupt power during installation.

## 10. Add voice, GPU tests and the optional offline experiment

After the typed path works, configure the AudioIn module path from the verified
native environment. Hold R2, speak briefly and release. Check the microphone
meter, transcript, mute behavior and local speech playback. Leave voice approval
disabled initially; use the controller approval gate.

Start inference in `auto`; inspect the logs to identify the actual backend.
Then test `cpu` and `vulkan_required` separately. No GPU throughput or voice
latency is promised before these console measurements.

Local lab is optional. It can be tested with LAN disconnected using bundled
models and typed input. It creates bounded Lua apps **inside MuseStation**, not
standalone offline `.pkg` files. Start with the smallest compute budget.
Keep the console awake and MuseStation open: background generation while another
game runs, after closing MuseStation, or in rest mode is not supported.

The phone/server audio fallback is not ready for this relay workflow. Typing is
the fallback while DualSense/local transcription is being qualified.
Detailed cases are in [SMOKE-TEST.md](SMOKE-TEST.md).

## 11. Normal startup, failures and backups

After each full reboot:

1. Confirm the router Internet block still applies to the console's current
   interface/address.
2. Reapply the verified exploit and required payloads.
3. Confirm the loader and ShadowMount API are available.
4. Open MuseStation; allow pending Hybrid journals to upload.
5. Check for interrupted work before starting a new request.

Hybrid logs upload automatically after sessions and handled failures. A killed
process cannot POST while dead; its persistent crash marker and journals are
recovered on the next launch. Restore relay connectivity and reopen the app
before asking for diagnosis. The operator should inspect the latest relay and
installer logs first. FTP or kernel logging is a separately configured fallback,
not a reason to make the user retype logs.

Local lab logs stay private and do not automatically upload when switching modes.
Retrieving them for outside diagnosis needs an explicit owner-authorized export
or file-retrieval step.

Back up the private console data/config and Opti3's relay data, exchange and
title ledger using the [relay guide](RELAY-DEPLOY.md). Keep credentials out of
public bug reports. Do not delete databases or uninstall existing titles to
resolve an allocation failure without diagnosis.

If Internet/PSN unexpectedly becomes reachable, disconnect the PS5's network and
repair the block before continuing. If a title asks for newer system software,
decline and stop that install. Leave the system on **12.40**.
