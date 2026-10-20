# Building from the corresponding-source archive

The companion `musestation-corresponding-source.tar.gz` contains the app, native kit,
public SDK, inference engines (including eSpeak NG), packager, build scripts,
dependency locks, and license texts. `SOURCE-MANIFEST.json` records every included
source file and hash. The archive omits device configuration, credentials, logs,
build products, model weights, Git metadata, upstream sample packages, and the
unused opaque upstream `right.sprx` resource.

Extract it, inspect the root `README.md`, then use Docker for builds. Pinned
third-party sources are already supplied. Skip the Git checkout bootstrap for
the SDK and package library (`scripts/fetch-sdk.sh` and
`scripts/package-image.sh` expect Git metadata in a live developer checkout).
Build their Docker images directly from the supplied snapshots instead:

```sh
docker build -t ps5-muse-toolchain .
docker build -t muse-package:station packaging
```

The native/inference staging and compile commands are in the root documentation
and scripts. Engine source caches include their `.muse-revision` markers and can
be used directly. Download model weights with `scripts/inference-fetch-models.py`,
which verifies the exact sizes and SHA-256 hashes from `inference/lock.json`.
Download the neural model/projector using scripts/neural-fetch-models.py and
inference/neural-lock.json. The GPU sources are supplied under inference/vendor/gpu
with pins in inference/gpu/lock.json; see docs/GPU.md and docs/NATIVE-APP.md for the
source-built RADV/ggml/native link sequence. Model weights are also already embedded in the accompanying installable package.

After native compilation and title staging, build the package with
`scripts/package.sh`. It performs a complete extraction/hash round-trip. The
public package library's opaque optional `right.sprx` is excluded from the build
by `packaging/Directory.Build.targets`; that adjustment is part of the supplied
source. Installation and execution on the owner's console remain unverified by
these off-console checks.

## Graphics corresponding sources

`packaging/source-deps/ps5-opengl-sdk-1.0.2/sources/` contains the actual source archives shipped with the same hash-verified graphics SDK release used by this title. This includes the ps5-opengl project with PS5 patches/build files, Mesa 26.2.0, OpenGNM/PSBC, SPIR-V/Vulkan headers and example dependencies. The adjacent dependencies manifest pins origins/revisions, and `graphics-sources.json` records every extracted source archive's SHA-256. See that release's `docs/building.md` and the source project's build instructions when rebuilding the static graphics SDK instead of using the exact pinned binary release. The current delivered title was linked to the release SDK; its graphics SDK was not rebuilt locally for this deliverable.

The packaging Dockerfile applies the eight patches listed in `packaging/UPSTREAM.json`: raw storage (including both metadata generators), padded multiple-block inode tables, typed NAPS lookup indexes, padding-aware index counts, outer single/double-indirect pointers, removal of a redundant inner-image clone, bounded source writers/shared finalization buffers, and direct block-backed mount reconstruction. The original pinned checkout remains intact. See `docs/PACKAGING.md` for the discovered upstream limits and exact regression checks.

## GPU driver and neural speech sources

The upgraded archive contains the PS5_Vulkan driver, its pinned PS5_Mesa and
PS5_PayloadSDK platform sources, llama.cpp/mtmd, the Qwen3-TTS adapter, Whisper,
eSpeak NG, upstream license trees and local patches. The packaged title carries
model cards, licenses and exact model provenance. Weights are omitted from the
source archive and retrieved with the locked, size/hash-verifying downloaders.
The OpenGL release's corresponding sources above remain included because the
UI and RADV driver are linked together with isolated Mesa symbols.

The runtime source collector, scripts/inference-collect-sources.py, retains the
actual RADV zlib source and Meson patch plus the LLVM sources and notices used by
the SDK C++ recipe and the build host's compiler-rt package. The manifest under
packaging/source-deps/runtime records source hashes, linked archive hashes and
compiler versions. Its LLVM 18.1.8 SDK recipe and Ubuntu LLVM 18.1.3 compiler-rt
inputs are distinct; the supplied SDK C++ release archives were not rebuilt for
this delivery. The final GPU native build refreshes this collector automatically.

GPU source snapshots omit Git metadata. Run scripts/inference-fetch-gpu.py after
extracting to restore the exact pinned checkouts before using the upstream build
recipes. The fetcher initializes metadata around the supplied files and verifies
the recorded revisions. Docker apt/tool dependencies and pinned upstream SDK
releases may still be downloaded. This is not an offline, hermetic-build claim.
