# Opti3 build host

On 2026-10-19 the user authorized moving Docker compilation/deployment from the
MacBook to Opti3. This supersedes the original Mac-only build-location requirement;
all C compilation still runs inside Docker.

The saved SSH entry for `192.168.5.54` logs in as `opti3`. The host reports hostname
`opti3`, Linux amd64 and Docker Engine 28.5.1. The existing services directory is
`/home/opti3/services`, so this project is staged at:

```
/home/opti3/services/ps5-muse
```

Literal `/services` does not exist and the login account cannot create it without
a sudo password. No system permissions, existing services, or Docker daemon settings
were changed. The local Colima build waiter was stopped to prevent duplicate builds.

Rebuild from the Mac and retrieve logs/artifacts:

```sh
python3 scripts/remote-build.py --host 192.168.5.54 --root /home/opti3/services/ps5-muse
```

Or run `bash scripts/build.sh` from that directory over SSH. The remote Docker image
is `ps5-muse-toolchain`. Build containers are temporary; they expose no network ports.
The remote-build helper excludes local secrets, runtime configuration, stale build
outputs and unused research checkouts. It does not delete remote files.

The selected architecture is now a local job relay and compiler worker, described
in [RELAY-DEPLOY.md](RELAY-DEPLOY.md). It receives Muse plans/source through a
shared-folder contract or authenticated provider API. It does not log into Muse
or require Opti3 to pair as a Muse gadget. A real connector must still deliver
those messages. The user approved the required Homepage labels for MuseStation;
no Homepage configuration file or unrelated service is changed.

After the console is available, run `scripts/deploy-hello.sh` on Opti3 with PS5_HOST
set to its LAN address, or retrieve the ELFs and use another verified loader client.
No console addresses are compiled into the artifacts.

During this build the LAN SSH endpoint stopped accepting connections. The existing
key agent and configured Tailscale SSH route restored access; no SSH policy or
firewall settings were changed. The temporary shared control socket is a session
convenience, not a requirement of the build scripts. Native details and rebuild
commands are in NATIVE-APP.md.
