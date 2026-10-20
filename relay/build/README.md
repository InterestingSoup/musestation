# Approved native C++ builds

The relay builds an approved source manifest into a real native PS5 title and
packages it with the public, patched LibProsperoPkg pipeline. The compiler does
not run the resulting program. The package must pass native-module checks,
structural validation, NAPS index checks, complete extraction and a SHA-256
comparison covering every input file before the worker reports success.

This is a compiler service, not the Muse account/device SDK. Muse supplies a plan
and source through the relay's authenticated provider API or local exchange.
The relay store binds source to the user's approved plan revision and assigns
the title identity. The builder cannot approve a plan or select its own title.

## Muse source contract

```json
{
  "schema": 1,
  "type": "source",
  "job_id": "<relay-issued job id>",
  "approved_revision": "<approved plan SHA-256>",
  "language": "cpp",
  "files": {
    "main.cpp": "// Complete C++20 native application source\n...",
    "include/example.hpp": "// Optional project header\n..."
  }
}
```

`main.cpp` is mandatory. Optional headers live under `include/` and use `.h` or
`.hpp`. File paths contain ASCII letters, digits, underscore, hyphen and ordinary
directory separators; dot directories, traversal, absolute paths and empty
segments are rejected. At most 32 files, 256 KiB per file, and 1 MiB total UTF-8
are accepted. NUL and malformed UTF-8 are rejected. No makefiles, commands,
environment overrides, dependency downloads, binary assets or metadata come
from the source manifest. Include a project header with `#include "include/example.hpp"`.

The SDK uses C++20 without exceptions or RTTI. The public homebrew UI toolkit's
headers are available under `gfx/`, `ui/`, `core/`, `audio/` and `platform/ps5/`.
Use its OpenGL renderer, display, DualSense input and audio wrappers. The fixed
kit supplies fonts under `/app0/assets/fonts/` and generic launcher artwork.
The application must end through `hui::sys::quit()`, not `exit()` or a return
from `main`. [example/main.cpp](example/main.cpp) is a complete, human-authored
graphics build fixture: a moving colored rectangle, controller input, a short
rumble and clean system exit. It is not a Muse-generated sample or a verified
hardware demonstration.

The source hash is SHA-256 of the file map encoded as JSON with sorted keys,
UTF-8, `ensure_ascii=False` and separators `(',', ':')`. It is distinct from the
plan revision, binary hash and immutable kit manifest hash.

## Prepare the trusted kit

After the public native dependencies have been fetched and verified by the
project's existing native setup, run as the trusted administrator:

```sh
python3 relay/build/prepare-kit.py \
  --project /home/opti3/services/ps5-muse \
  --output /home/opti3/services/ps5-muse/runtime/native-kit-v1
```

This copies selected pristine `vendor/native-ui` sources, SDK v0.42, cached zlib
1.3.2 and OpenGL SDK 1.0.2, the checksum-verified clean-room runtime, fonts and
license notices. It excludes MuseStation code, inference weights, the repository
root, runtime data and credentials. It replaces only the dependency bootstrap
with an offline presence/version check. The output contains a complete file
hash manifest and read-only files/directories. Existing kit directories are never
overwritten. To update dependencies or the fixed recipe, prepare a new kit and
update the operator configuration explicitly. Corresponding source remains in
the repository's source bundle and its exact runtime-source manifests; the kit
is a build cache, not a replacement for that source distribution.

The worker must see the kit and data directories at their actual absolute host
paths. This is required because the worker talks to the host Docker daemon;
bind-mount paths are resolved on the host. Mount the kit read-only in the worker.
The API container receives neither the Docker socket nor the kit.

Configure `MUSE_RELAY_NATIVE_KIT`, `MUSE_BUILD_IMAGE` and `MUSE_PACKAGE_IMAGE` in
the worker. Use immutable image IDs. The builder resolves an installed image
before starting and executes that exact ID with `--pull=never`; it never pulls
dependencies or images while handling a request.

## Isolation, recovery and limits

Each stage runs as the worker's nonroot UID with no network, a read-only root
filesystem, all Linux capabilities dropped, `no-new-privileges`, 128 PIDs,
1 CPU, a 3 GiB memory limit with no additional swap, a 128 MiB per-file limit,
and bounded temporary storage. Only the immutable kit, this job's source and
identity, and this job's output are mounted. The compiler has no Docker socket.
Compilation uses one job and a fixed recipe; no generated native program runs
on the host. The compiled app itself is native code with the console's normal
homebrew privileges, not a sandboxed Lua app.

Compilation is limited to 15 minutes and packaging to 5 minutes. Both have an
internal timeout as well as worker supervision, so a stopped worker cannot
leave an unlimited compiler behind. A reserved container name scoped by SHA-256
of the canonical private data directory prevents overlapping stages within a
store. Fixture and production stores have different names and full namespace
ownership labels. The required `Builder(..., namespace=store.path)` argument
comes from trusted worker configuration. Under the exclusive worker lock,
`recover_containers()` checks both ownership labels and removes the inspected container ID before the
store marks interrupted jobs failed. It never stops arbitrary services or
automatically rebuilds interrupted jobs. New requests need a new approved job.

Logs are bounded to 2 MiB per stage. Output is bounded to 384 MiB/4096 files
while building; final packages are limited to 128 MiB for this first relay
contract. Output symlinks, hardlinks and special files are rejected. Dependency
copies and object files live in the disposable container's temporary filesystem.
The builder removes its native title and unfinished packaging scratch folders,
retaining bounded source, logs, provenance and final artifact for store handling.
The worker can export bounded diagnostics to Muse so a failed compile can be
revised through a new approved request.

## Verification evidence

The [actual Muse source test](../../docs/MUSE-SOURCE-TEST.md#verified-result) passed
on 2026-10-19: the connector submitted a counter app, and the isolated worker
compiled and packaged it in 125.859 s and 7.798 s respectively. The 38,054,688-byte
artifact passed 82-file readback and independent download/hash verification.
Its approval and inventory were simulated; no PS5 execution occurred.
The earlier human-authored fixture below remains separate evidence.

`relay/tests/test_builder.py` covers source/path/identity limits, container
isolation options, kit tampering, output traversal/quotas, deadline cleanup,
bounded diagnostics and ownership-checked orphan recovery. These tests run in a
network-disabled Docker container.

The first actual fixture passed native compilation and full package readback:

| Item | Result |
| --- | --- |
| Title | `PPSA99998`, `01.000.000` |
| Content ID | `IV0000-PPSA99998_00-MUSERELAYSMOKE01` |
| Input files compared after extraction | 82 |
| Package bytes | 38,054,698 |
| Package SHA-256 | `695045115843cc0ec037db86a8732d22ae028386f5c120d0ed8ec8de8eab9f05` |
| Compile / package wall time | 133.912 s / 7.189 s |
| Kit manifest SHA-256 | `687a5605557b58ed1a954f59f228bdb198aa267cdd767d2a8bc999a2aeab86d1` |

Reproduce in a new, empty job directory:

```sh
python3 relay/build/smoke.py \
  --kit /home/opti3/services/ps5-muse/runtime/native-kit-v1 \
  --job /home/opti3/services/ps5-muse/runtime/relay-smoke-new \
  --compiler-image "$MUSE_BUILD_IMAGE" \
  --package-image "$MUSE_PACKAGE_IMAGE"
```

The build receipt records the resolved image IDs, source/kit/artifact hashes,
wall times and verification report path. Installation, launch, controller feel,
graphics, sound and firmware compatibility have **not** been tested on a PS5.
The artifact always reports `hardware_verified: false`.
