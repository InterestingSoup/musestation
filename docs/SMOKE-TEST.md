# MuseStation hardware smoke test

For first boot, update settings and network isolation, start with
[the arrival-day guide](FIRST-DAY.md).

This is the owner's first-console checklist, not a report of completed PS5 tests.
The target is the owned Slim on firmware 12.40. Do not update firmware. Reapply the
jailbreak after each reboot. Confirm the separate native homebrew install/launch
payloads against their actual versions; Relapse support alone is insufficient.
The current source audit and a question for other owners are in RESEARCH.md and
reddit-draft.txt. The draft has not been posted.
Inventory checks below require the 01.005.000 candidate and matching relay.

## Package and loader preflight

1. Match the package and source archive against their adjacent SHA-256 files.
   Use the release path in BUILD-REPORT-INVENTORY.md. Keep the previous package separately;
   the title/content ID stays the same and this is an update, not a second app.
2. Reapply the exploit and establish the owner's compatible payload environment.
   Record exact payload versions in configuration. The package installs no
   jailbreak components and never updates firmware. Stop if native title install
   and launch support on 12.40 has not been established.
3. For the independent loader test, send `dist/muse-hello.elf` with the SDK's
   prospero-deploy to the console's supplied IP and loader port. Expect the notification **Muse: hello from your PS5**. Set PS5_HOST
   to the actual IP and run `bash scripts/deploy-hello.sh`; returning stdout is
   saved to `logs/hello-console.log` on the deployment host. Silence alone is not
   proof of a crash because stdout return depends on the loader. Do not send native eboot.elf
   to the ELF loader: it is an intermediate title executable.
4. Install the single MuseStation.pkg through the confirmed native installer.
   Confirm the title launches, displays MuseStation, accepts the DualSense and
   stays responsive while its bundled engines initialize. Package extraction and
   SELF checks on the build host do not establish console acceptance.
5. Set the console's date and time correctly before using HTTPS catalogs or
   downloads. Do not disable certificate checks to compensate for a wrong clock.
   The current private relay uses HTTP, so a successful relay health request does
   not test HTTPS. ProsperoStore documents this [clock requirement](https://github.com/darkxex/ProsperoStore/blob/e8b9b09fcd54cce27ea1bcda99a57af9df0fb4b4/README.md#L115-L121).
6. For a separately supported loose-folder app only, apply any required `0777`
   permissions to that app bundle, never recursively to its parent data directory.
   Private configuration, relay keys and journals stay outside the bundle with
   private permissions (configuration/key files `0600`, private directories `0700`).
   The upstream [folder-install guidance](https://github.com/darkxex/ProsperoStore/blob/e8b9b09fcd54cce27ea1bcda99a57af9df0fb4b4/README.md#L103-L124)
   is not a request to chmod an installed MuseStation PKG or a new ZIP deliverable.

ShadowMountPlus 1.7beta4 includes a [12.40 offset entry](https://github.com/drakmor/ShadowMountPlus/blob/d7e35e6ce90abc6f9d0880ff40c2a5cc1fbfa075/src/sm_shellcore_offsets.inc#L435-L440).
That establishes source coverage only. ProsperoStore's pinned README reports
install/update/uninstall testing on [6.02 and 12.70](https://github.com/darkxex/ProsperoStore/blob/e8b9b09fcd54cce27ea1bcda99a57af9df0fb4b4/README.md#L37-L45),
not the owner's Slim on 12.40. Neither result qualifies MuseStation's package path.

## Hybrid with Muse and generated native apps

1. Start in Hybrid with Muse. Enter the actual console IP and relay URL in setup;
   the deployed Opti3 endpoint is `http://192.168.5.54:8860`. Leave AudioIn blank
   for the typed test. Provision the separate console key from the operator's
   private config; never use the provider key. Payload status and paths are
   install-time inputs. A public relay origin must be rejected by setup.
   Set **ShadowMount API on this console** (`shadowmount_url`), normally
   `http://127.0.0.1:10101`; another host must be rejected. The console's exact
   configured LAN IP is allowed when its native environment requires that route.
2. Verify the relay health endpoint. Have Muse's connector use MUSE-EXCHANGE.md.
   This integration still needs a real Muse file/API connection; entering the
   relay address does not pair an account or cause a plan to be invented.
3. Type a small request and submit. Expect “Waiting for Muse,” a durable request
   in the exchange, and no approval control while planning. Close and reopen
   before a response: the unapproved request should recover. While the relay is
   unreachable, retries should remain responsive and never approve a plan.
   With a controlled dropped initial POST response, the saved request ID must
   recover the same request, without needing to re-speak or creating a second one.
4. Have Muse return a short plan. Read every page; release X before pressing it
   deliberately. Square adds detail as a new request; Circle cancels local review.
   Only the exact displayed revision can create a job. The provider cannot approve.
   Calls with a missing, wrong or provider key must return 401, including calls
   against a ready plan; health remains readable without a key.
   Immediately before approval, expect the SMP version/capability check followed
   by the game-list query, with no relay bearer sent to either request. Unavailable
   SMP, missing `installed_pkg_games`, a bad count, duplicate/invalid IDs or a
   response above 4,096 titles/4 MiB must leave the plan unapproved. An authenticated
   new approval with no inventory must receive 428 from the relay.
5. Have Muse supply source for the approved job. Confirm waiting-for-source →
   compiling → download → exact size/SHA/identity checks → installer progress.
   A build failure exports diagnostics; a correction needs a new reviewed request.
   The allocated title must avoid every returned installed/discovered ID, previous
   ledger reservations, ProsperoStore (`PPSA99000`) and MuseStation (`PPSA99576`).
   Repeat a successful approval with its same revision/idempotency key in a
   controlled API test: it must return the same job without another reservation,
   including an older retry without inventory. This does not imply automatic
   console recovery of an approved job. An existing title, changed package or
   protected-title replacement must still be rejected at installation.
6. Keep MuseStation open until the entire package has installed. Playable is not
   complete. Automatic launch waits for full completion; only a matching observed
   running title confirms launch. Closing early can unmount the source during copy.
   Console recovery of already-approved jobs is not implemented; the relay retains
   the job and logs. Do not blindly submit a second install after uncertainty.
7. Reopen MuseStation after handoff and confirm installer logs reach Opti3.
   If the helper cannot access the package or maintain its journal, installation
   must fail visibly before the OS install call.
   READY confirms only the authenticated helper connection. AppInstUtil is loaded
   after journal setup; missing modules/symbols or Initialize errors must appear
   in the retained/uploaded installer log without submitting an installation.
8. During a disposable typed test, force-close MuseStation, then reopen it.
   Expect an unclean-session record and retained journal upload before normal work.
   Repeat with the relay offline, then restore connectivity: logs must remain
   available and retry automatically. Do not force power loss during installation.

The phone/server-transcription fallback is not connected in this relay: raw WAV
uploads return a clear 415. Use local Whisper or typing for this test. Other
provider/account screens must not claim a completed connection or show fabricated
login codes. No account credentials belong on the console or in test logs.
The inventory is only what SMP returns: it supplies no boot/generation or
completeness/freshness attestation and may omit unavailable registrations. Do not
treat an absent ID as permission to replace an existing title. ZIP delivery and
updates are deferred; no ZIP installation/update smoke pass is claimed here.

## Voice and GPU checks

1. Set the actual AudioIn module path for this native environment. Hold R2, speak
   a short request and release. Check the meter, transcript and plan; test mute,
   absent microphone and controller reconnection. The targeted capture format is
   48 kHz PCM16 mono; hardware permission/routing remain unverified.
2. Try auto backend first and inspect the recorded backend allocations. A Vulkan
   device name alone does not prove every operation used GPU buffers. Exercise
   cpu mode and vulkan_required separately: required mode must fail visibly when
   unavailable. GPU speed, memory coexistence and driver stability need PS5 tests.
3. Verify local speech and speaker routing. The bundled neural model writes a
   complete WAV before playback; eSpeak is the local fallback. Repeat plan →
   speech → transcription → generation to test model unloading/reloading.
4. Voice approval stays advisory by default. Only enable voice_gate_verified
   after testing positive phrases, negation, questions, background speech and
   stale plans. Buttons remain available. A model decision alone cannot approve
   an unrelated utterance.

## Optional Local lab

1. Choose Local lab explicitly; new installs default to Hybrid. Leave AudioIn blank for the typed smoke test.
   No IP, server, account or model download should be required. Model files are
   bundled. Existing saved configurations retain their selected mode.
2. Disconnect LAN connectivity for this test. Press X and type a small request,
   for example a counter with Cross to increment and Circle to reset. R1 requests
   a local plan. Check that its text and attempt/active-time budget appear before
   approval. No generation starts merely by showing a plan or opening the library.
3. Release X, read every plan page, then press X to approve. Keep the title open
   and the console awake. Rendering/controller input should remain responsive.
   Generation may fail; the current real diagnostic must be saved and used for
   a bounded repair attempt. A token/time limit never makes partial source READY.
4. Press Square during generation. Expect cooperative pause after the active
   decode returns. Triangle opens the library. Reopen the project and explicitly
   resume it. Verify the request, approved revision, consumed attempts and time
   survived. No serialized model KV cache or token-perfect continuation is claimed.
5. When a revision passes validation, expect READY, with no automatic execution.
   Press X to Play. Test its actual controls, drawing and the requested behavior;
   validation only probes a short set of inputs and does not prove app quality.
   L1+R1 saves and returns to the project without closing MuseStation.
   Square on a saved project opens a refinement request. The resulting new plan
   needs fresh approval and must not replace the original saved app.
6. For an app that uses state.get/state.set, change state, leave it, reopen it and
   confirm persistence. Saves occur at most every 15 seconds and on normal exit;
   an unexpected stop can lose the latest unsaved interval. A corrupt save must
   produce an error, not be silently accepted.
7. Close MuseStation normally and reopen. Resume is always explicit. Test process
   interruption only after a successful normal save/reopen; do not deliberately
   cut power while the system writes. In-progress work should show INTERRUPTED
   with its reserved budget charged, never silently restart or install anything.
8. Test the small budget first. Exceeding either attempt or active-time limit
   must stop the job. The 7-day setting is a ceiling with a simultaneous 512-attempt
   cap, not a promise of seven days of useful generation. Work stops as soon as
   the app validates, either budget is exhausted, or the owner pauses it.

The offline runtime supports bounded 2D Lua apps inside MuseStation. It does not
compile native standalone packages on-console. It exposes no filesystem/network
or native install API to generated code. Keep the console awake for unattended
work: rest mode, a closed title or another PS5 game is not supported background
execution. A multi-day console soak remains to be performed.

## Logs and first-failure triage

Hybrid journals upload after a session/handled failure and retry automatically.
Muse should retrieve the latest server log first, including installer logs after
reopen. If the process cannot run, it cannot upload: startup recovery handles the
retained marker; kernel failures may need klogsrv or authorized FTP retrieval.

All Local journals stay in their separate private directory and never get uploaded
merely by switching modes. Local failures therefore require an owner-authorized
retrieval path for outside diagnosis; a privacy-preserving export UI is not in
this build. Logs are never required to be manually retyped. Consult configuration
for the actual log directory; do not assume the brief's illustrative path exists.

Report the precise stage and observed behavior. The build team can inspect its
compile/test evidence and supplied logs; only the owner can verify PS5 behavior.
