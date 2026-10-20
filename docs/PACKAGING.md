# Native PS5 package pipeline

The current 01.005.000 candidate is described in [BUILD-REPORT-INVENTORY.md](BUILD-REPORT-INVENTORY.md), under `dist/pkg-v6`. Set `MUSE_NATIVE_VERSION=01.005.000` and pass `--version 01.005.000` to the verifier when rebuilding. Commands below naming `pkg-v3` reproduce an earlier release.

The packaging tool emits a finalized PS5 debug image (`7f 46 49 48`, `.pkg`), using the public [LibProsperoPkg](https://github.com/drakmor/LibProsperoPkg) library at [`be79990357efa6c1a873ae7443dec6e640172914`](https://github.com/drakmor/LibProsperoPKG/tree/be79990357efa6c1a873ae7443dec6e640172914). The .NET SDK and runtime images are pinned by digest in `packaging/Dockerfile`; the source revision and image digests are recorded in `packaging/UPSTREAM.json`.

This is a development homebrew package. A successful build and structural report do not establish installation or launch support on the owner's PS5 Slim running 12.40. The console needs a separately verified compatible homebrew/debug installation and launch path. Relapse coverage alone does not establish that path. Never update the console past 13.60.

## Input title

The build consumes the complete staged native title folder:

```text
build/native/title/PPSA99576/
  eboot.bin                 native PS5 executable, raw ELF or development SELF
  sce_sys/
    param.json              PS5 metadata with matching titleId/contentId
    icon0.png               actual PNG, decoded and BC7-encoded during preflight
    pic1.png                actual PNG background
  sce_module/               native runtime PRX modules, if required
  assets/                   UI assets and non-secret defaults
    inference/              bundled language/voice models, eSpeak data and licenses
    install/                raw authenticated installer helper (.payload)
  licenses/                 source/dependency/model license notices
```

The development identity is `PPSA99576` / `IV0000-PPSA99576_00-MUSEGADGET000001`. This is a project-chosen development identifier, not a Sony-issued reservation. Check for a collision with another installed title before the hardware install.

The packager requires a native x86-64 FreeBSD executable with ABI version 2, `e_type=0xFE10`, `PT_DYNAMIC`, a flags-zero linking `PT_LOAD`, and `PT_SCE_PROCPARAM`; runtime native modules use `0xFE18`. Mapped loads must have correct 16 KiB alignment and sizes. These checks follow the pinned native converter and its inspection script. It explicitly rejects a normal elfldr payload renamed to `eboot.bin`. The native title build must perform genuine PS5 import, process-parameter and module conversion before packaging.

The native kit uses both SELF container magics `0x1D3D154F` and `0xEEF51454`; the latter is used by its clean-room `libc.prx`. The upstream package inspector recognizes only the former. Our additional native-module checker recognizes both and uses a private normalized header view for parsing the second form. It never rewrites the source or packed module; the full runtime file must still round-trip with an identical SHA-256.

The Muse build requires at least one GGUF v2/v3 model. The distinct build-generated
mode accepts a native game/application without model files. Models and assets are copied with the entire title folder. `ProsperoHomebrewPackager` is deliberately not used: at the pinned revision, that convenience API copies only the executable, `sce_sys`, and `sce_module`, silently omitting models and other assets. The pipeline instead uses `ProsperoPackageBuilder` against a separate complete staging copy.

The upstream license-free builder wraps raw `eboot.bin`, `.elf`, `.prx`, and `.sprx` files in development SELF containers. Native modules may already be SELF and are preserved. Helpers meant to be sent as raw payload bytes to elfldr must use a non-module extension such as `runtime/whisper.payload`; the package round-trip must preserve those bytes. Never put a payload-format `.elf` in this native-title input.

Symlinks and `.git` directories are rejected to keep unrelated host files out of the title. Runtime credentials and machine-specific configuration belong in the console's writable configuration path, not the package.

## Build on Opti3

Run in `/home/opti3/services/ps5-muse` after staging the native title. These commands use disposable Docker containers and do not deploy a persistent service:

```sh
MUSE_PACKAGE_IMAGE=muse-package:station bash scripts/package-image.sh
MUSE_PACKAGE_IMAGE=muse-package:station MUSE_PACKAGE_MEMORY=5g \
  bash scripts/package.sh build/native/title/PPSA99576 dist/pkg-v3
```

`package-image.sh` verifies the clean upstream checkout at the pinned revision before building the .NET CLI image. Packaging mounts the source read-only, disables container networking, uses the invoking user's UID/GID, and defaults to two CPUs and 4 GiB memory. `MUSE_PACKAGE_IMAGE` overrides the image name. Large bundled models may need a deliberately increased `MUSE_PACKAGE_MEMORY`; coordinate this with other builds on Opti3. The earlier CPU title passed with a 4 GiB cap. MuseStation uses a 5 GiB cap with patches 0007 and 0008 removing redundant image allocations; final full-bundle evidence is recorded in the build report. The wrapper sets the managed heap ceiling to 90% of that cap (`DOTNET_GCHeapHardLimitPercent=5A`, hexadecimal as required by [.NET environment configuration](https://learn.microsoft.com/en-us/dotnet/core/runtime-config/garbage-collector#heap-hard-limit-percent)); `MUSE_PACKAGE_HEAP_PERCENT_HEX` can override it.

For standalone checks with the compiled image:

```sh
docker run --rm --network none --memory=2g --cpus=2 \
  -v "$PWD/build/native/title/PPSA99576:/input:ro" \
  muse-package:station preflight /input

docker run --rm --network none --memory=2g --cpus=2 \
  -v "$PWD/dist/pkg-v3:/output:ro" \
  muse-package:station inspect /output/IV0000-PPSA99576_00-MUSEGADGET000001.pkg
```

## Validation and deliverables

The preserved 01.004.000 MuseStation candidate is under `dist/pkg-v5`. See
[BUILD-REPORT-REVIEW.md](BUILD-REPORT-REVIEW.md) for its exact size, SHA-256,
every-file extraction results and structural checks. The preserved 01.002.000
release remains under `dist/pkg-v3`, with its [historical report](BUILD-REPORT.md).
The package contains exactly the four complete weights in the active inference
locks, including Qwen2.5-Coder 1.5B. `scripts/verify-musestation-release.py`
independently checks the title/version, package hash, each model, the raw installer
helper and Lua license. Earlier GPU/voice and CPU packages are preserved under
`dist/pkg-v2` and `dist/pkg`, with historical reports kept separately.

The report preserves upstream informational warnings about already-SELF modules,
digest layout and the debug-console target. `WillRunOnDebugConsole` is a static
format classification; installation and execution have not been tested on a
console. See BUILD-REPORT.md for the upgraded native link and host runtime tests.

The .NET CLI was compiled in Docker on Opti3 on 2026-10-19. See `packaging/package-image-build.log` and `packaging/package-version.json` (.NET 10.0.13; opaque rights resource absent). `scripts/package-checks.sh` passed negative integration checks against the real SDK hello-world payload: a payload cannot pass as a native `eboot.bin`, and a title containing a symlink is rejected before traversal. Results are in `packaging/package-negative-checks.log`. The linked upstream code emits warnings; the Muse CLI compiles with warnings treated as errors.

A build is published into its requested output directory only after all of these checks pass:

1. Native executable headers, PS5 metadata, artwork decode, GGUF presence and upstream launch-readiness inspection.
2. Real FIH package assembly and the upstream structural acceptance validator, including matching content ID and the nested image/key metadata.
3. Full extraction of the built package. System-container entries are reconstructed separately because the upstream filesystem extractor does not return `param.json` or artwork entries.
4. SHA-256 comparison for every source file, including model files and assets. Raw native modules are compared with the expected development SELF transform. The only other permitted transform is the upstream promotion of all-zero `sdkVersion`/`requiredSystemSoftwareVersion` strings to its 2.00 baseline; both source and packaged hashes and the transform are recorded. This metadata baseline does not establish firmware compatibility. Missing files or any mismatch fail the build.
5. Serialized NAPS table round-trip and lookup-index validation, followed by another module/metadata readiness inspection of the extracted title.

The output consists of `.pkg`, `.pkg.sha256`, and `.pkg.report.json`. The report records the source revision, file hashes, structure checks, warnings, and `HardwareVerified: false`. The original title input is never modified. Failed temporary stages are removed; no newly generated package is moved to the release output on failure.

Upstream's validator checks known package structures; it does not reproduce console cryptographic validation. Its module-readiness classification is also a static check. Runtime behavior, ABI correctness, firmware compatibility, microphone access, inference speed, graphics output, and successful installation remain in the owner's hardware test loop. See [SMOKE-TEST.md](SMOKE-TEST.md).

The packaging CLI source declares GPL-3.0-or-later. The upstream root license is GPL-3.0-or-later, while some linked NAPS source files carry AGPL-3.0-or-later notices. Preserve the supplied license texts, per-file notices and corresponding source when sharing the combined tool. The console title's license notices must cover the distinct app, runtime, and bundled-model components.

Run `bash scripts/package-source-deps.sh` and then `bash scripts/package-source.sh` after completing the native build sources with an explicit output argument to create `dist/pkg-v3/musestation-corresponding-source.tar.gz` and its checksum. This contains the app and buildable corresponding engine sources, including eSpeak NG, with a per-file source manifest. It also includes the actual graphics project, Mesa, OpenGNM/PSBC and graphics header sources, patches and build instructions from the exact hash-verified OpenGL SDK release linked by the title. The binary SDK fetcher normally discards these source archives; `package-source-deps.sh` preserves and verifies them. It excludes weights, credentials, device configuration, logs, Git metadata and build products. The package remains one install; the source archive accompanies it for reproducibility and source-license obligations. Archive build instructions are in `packaging/SOURCE-BUILD.md`.

## Deliberate upstream adjustments

The upstream library embeds and automatically injects `PlayGo/Data/right.sprx`, a prebuilt module without corresponding source or provenance in its `NOTICE`. `packaging/Directory.Build.targets` excludes that optional resource from our library build; upstream `GetRightSprx()` permits its absence. We supply no fabricated replacement. The report records this omission, and the CLI independently rejects unknown executable formats after extraction instead of relying on upstream's permissive skip for unrecognized modules. Whether a particular console's installation path requires an entitlement/about module is an additional hardware-test condition.

The packaging image applies eight source patches to the pinned library, without changing the checked-out upstream tree. All patches are included in the source archive:

- `0001-store-inner-files-raw.patch` selects the library's existing raw-block format for inner files and metadata. The initial unpatched build exhausted its managed heap under a 4 GiB cap while compressing model weights; model-only compression avoidance still spent minutes on voice dictionaries. Compression is optional in this format, so the final pipeline keeps raw bytes, package encryption, integrity fields and full extraction checks. Both the NAPS layout generator and SI metadata generator use stored-block descriptors for that raw metadata; this also removes the second metadata compression pass.
- `0002-multiple-inode-blocks.patch` fixes the upstream assembler's one-block inode-table assumption. A 64 KiB block holds 390 168-byte inodes with 16 padding bytes. The patch scales the table and superblock descriptors, shifts metadata content, and makes the specialized reader honor those padded block boundaries. The generic upstream PFS writer/reader provides the block-packing reference. Exact 390/391/780/781 boundaries are checked.
- `0003-large-layout-indexes.patch` uses the public NAPS serializer's declared 24-bit base plus seven byte deltas; the original builder populated a different small-example byte arrangement and rejected large indexes. Raw files start at 64 KiB boundaries in both logical and stored layouts to keep the small per-group deltas in range. Serialized tables with bases over 255, and both complete and trailing groups, are checked independently of package extraction. Arbitrarily many zero-length files at one offset can still exceed the checked delta range and are rejected.

- `0004-naps-padding-count.patch` makes the NAPS parser prefer a header-declared file-index count when its section map and final alignment match the blob. Previously, 6 or 12 bytes of end padding could be mistaken for extra six-byte file-index rows. Serialized layout checks cover sixteen consecutive block counts and their different padding sizes.
- `0005-outer-double-indirect.patch` corrects the specialized outer filesystem writer to use one single-indirect pointer and one double-indirect pointer, matching the unchanged generic PFS builder/reader. Upstream incorrectly treated all five inode slots as independent single-indirect lists and rejected the full model bundle above 9,112 blocks (about 569 MiB). The patch hashes child pointer blocks before their parent and marks every pointer block for signed encryption. Actual 12/13, 1,832/1,833 and 9,113-block files are read back through the unchanged generic reader with full SHA-256 and indirect-block signature checks (`scripts/package-outer-checks.sh`).

- `0006-no-inner-image-clone.patch` decodes the existing read-only input span directly instead of making a redundant copy of the complete inner image. It leaves block decoding and file bytes unchanged. This saves roughly one package-sized allocation when verifying the larger neural speech bundle. Both existing 512/1024-file fixtures pass with this change.

- `0007-bounded-builder-memory.patch` streams unchanged source files directly into exact-size slices of the final inner image, preserving the four-byte executable magic used for inode flags. It rejects source-size changes and short/oversized writers. Generated files use exact allocations. Outer digest finalization reuses the existing encrypted image; FIH finalization reads into one complete mount buffer; extraction reuses the fully consumed probe buffer while retaining the original for key retries. No files, checksums or extraction checks are omitted. This addresses whole-image allocation failures observed with the larger four-model bundle under both 4 and 5 GiB caps. The exact-image equivalence/source-boundary tests and negative/format checks pass in `packaging/package-patch7-checks.log`. Both 512/1024-file full extraction/hash fixtures pass in `packaging/package-patch7-fixtures.log`. The model-free generated package also passes complete extraction and the gadget's actual C verifier in `packaging/package-generated-patch7-check.log`.
- `0008-bounded-mount-reconstruction.patch` reconstructs the inner mount directly from the existing decrypted outer file view. Raw input reads are at most 256 KiB and go straight into the destination mount; compressed input retains only one Cblock extent. This removes the additional complete stored-inner-image allocation from both listing and extraction. The existing span path and the new borrowed-reader path share identical decode logic. Long arithmetic and `Array.MaxLength` guards reject unsupported image sizes before allocation. The library still requires each image/mount to fit a single .NET byte array (2,147,483,591 bytes); this patch does not claim arbitrary-size streaming support. Format checks compare complete reconstructed bytes, bound reads, and reject short readers and oversized mounts. The Docker image `muse-package:station` passes the negative and format checks (`packaging/package-patch8-checks.log`), complete 518/1030-file package extraction/hash fixtures (`packaging/package-patch8-fixtures.log`), and the model-free generated package plus gadget verifier (`packaging/package-generated-patch8-check.log`). These fixtures use the frozen v2 native modules. Final larger-bundle acceptance still requires all input files to pass extraction and hashes.

The 512- and 1024-extra-file format fixtures use the actual native executable/runtime, inert text data and an explicitly empty GGUF container. They are test-only, deleted after validation, and never substituted for the full bundled model. Their real FIH packages must pass every-file extraction/hash comparison. The delivered title uses the complete hash-verified model weights and speech assets. Evidence: `packaging/package-negative-checks.log`, `packaging/package-many-files-check.log` and `packaging/package-outer-checks.log`.

These checks validate the public format implementation and its corrections. They do not establish that the owner's console accepts the package or its native runtime. No package is published when any structure, index, module or file-integrity check fails.

## Installing future generated packages from the gadget

There is a concrete public PS5 installation API. etaHEN's [package installation writeup](https://github.com/etaHEN/etaHEN/blob/main/PS5%20technical%20writeups/pkg-writeup.md), checked 2026-10-19, describes loading/initializing `libSceAppInstUtil.sprx`, submitting a filesystem path or HTTP URL through `sceAppInstUtilInstallByPackage`, and polling `sceAppInstUtilGetInstallStatus`. The writeup also identifies a privileged caller-context requirement and warns that missing initialization can crash the caller. A zero submission result is an asynchronous task being accepted, not evidence that installation has completed.

The pinned public SDK exports both symbols. Its [`samples/install_app/payload.c`](https://github.com/ps5-payload-dev/sdk/blob/f5d40f02770e9b0001dd9e18056eca70b3da8b40/samples/install_app/payload.c) instead demonstrates `sceAppInstUtilAppInstallTitleDir`: registration of an already staged loose title, a different operation. Current PS5 PKG Manager at [`914311f945d14a555e1c09635346bb069c3368e8`](https://github.com/itsPLK/ps5-pkg-manager/tree/914311f945d14a555e1c09635346bb069c3368e8) supplies pointer-bearing installer structures in [`include/install_appinst.h`](https://github.com/itsPLK/ps5-pkg-manager/blob/914311f945d14a555e1c09635346bb069c3368e8/include/install_appinst.h), isolating the installer call in a fresh helper process. Its [architecture document](https://github.com/itsPLK/ps5-pkg-manager/blob/914311f945d14a555e1c09635346bb069c3368e8/ARCHITECTURE.md) describes range streaming and status monitoring.

Launching an installed app is a separate operation. [Régie5](https://github.com/viviandsx/regie5) documents that ordinary native apps cannot use the system launch calls. [PS5Upload's FAQ](https://github.com/FermataRest/ps5upload/blob/main/FAQ.md) describes a ShellUI-context implementation with firmware 9.60 hardware results; that is not evidence for Muse on 12.40. Exported SDK symbol names alone do not establish that a normal Muse process has the required caller identity or capability.

The upgraded gadget implements the package path in C plus a separate public-SDK
installer payload. Muse's build result supplies artifact kind, title/content IDs,
byte length and SHA-256. The client streams and verifies the package before
bootstrapping an authenticated helper through the local ELF loader. The helper
initializes the installation API, submits the verified file, polls status and
rechecks completion immediately before launch. Playable status may precede full
installation; native progress is preserved, but automatic launch waits for the
completed state so the source sandbox stays mounted while copying. Launch success requires observing
the matching running title, not merely a successful launch call.

A mount-ID mapping and authenticated nonce probe establish package access without
guessing a sandbox path. The parent passes already-open writable journal, marker
and lock descriptors to the helper; logging is tested before any install call.
This handles the native sandbox's differing pathname permissions. Actual PS5
mount/IPC behavior still requires hardware testing, and explicit path overrides
are available when a loader environment cannot support the default route.

The helper operates only on new IV0000/PPSA99xxx generated identities and refuses
MuseStation (`PPSA99576`), ProsperoStore (`PPSA99000`) or any existing title. It does not uninstall/update applications or
patch firmware. Its own journal survives a parent-title handoff and is retried by
Muse. Its SDK compile, mock status/launch regressions and package-parser integration
pass off-console; native permissions and launch context require hardware testing.
The first hardware test still installs Muse through an independently established
12.40-compatible homebrew interface. `.pkg` bytes never go to port 9021 as an ELF.

The 01.005.000 approval path reads ShadowMountPlus's public title inventory before
requesting a new identity. The relay excludes all returned IDs, its permanent
ledger and both fixed application IDs. This is not an installed-title database
replacement: the upstream response has no completeness or freshness attestation.
Its discovered-game cache is bounded and some unavailable registered titles are
omitted. Keep the final OS collision check. The exact schema, capability checks
and privacy boundary are in [MUSE-EXCHANGE.md](MUSE-EXCHANGE.md#title-inventory-before-approval).

ShadowMountPlus's pinned 1.7beta4 source contains a [12.40 bridge-offset entry](https://github.com/drakmor/ShadowMountPlus/blob/d7e35e6ce90abc6f9d0880ff40c2a5cc1fbfa075/src/sm_shellcore_offsets.inc#L435-L440)
and [12.00+ registration routing](https://github.com/drakmor/ShadowMountPlus/blob/d7e35e6ce90abc6f9d0880ff40c2a5cc1fbfa075/src/sm_install.c#L33-L80).
These are source coverage, not a 12.40 hardware qualification. ProsperoStore
reports [6.02 and 12.70 installation/update/uninstall tests](https://github.com/darkxex/ProsperoStore/blob/e8b9b09fcd54cce27ea1bcda99a57af9df0fb4b4/README.md#L37-L45).
That evidence does not establish MuseStation PKG acceptance on the owner's Slim.

### Building Muse-generated applications

Muse should stage a genuine native title and invoke the same compiled builder:

```sh
docker run --rm --network none --memory=4g --cpus=2 \
  -e DOTNET_GCHeapHardLimitPercent=5A \
  -v "$GENERATED_TITLE_DIR:/input:ro" -v "$ARTIFACT_DIR:/output" \
  muse-package:station build-generated /input /output
```

This standalone mode requires an IV0000/PPSA99xxx identity, rejects MuseStation's
own ID, and
caps the final package at the client's 1 GiB generated-artifact limit. It keeps
native-module, metadata, structural and complete extraction/hash checks. It does
not impose Muse's bundled-model requirement on a generated game. A real native
fixture built with this mode passed both full extraction and the actual gadget
package verifier (packaging/package-generated-check.log). The temporary format
fixture was deleted and is not an installable game deliverable. The deployed
relay owns identity assignment and packaging; Muse submits only approved source
through [MUSE-EXCHANGE.md](MUSE-EXCHANGE.md). Its recipe further limits generated
packages to 128 MiB, and its builder and console verifier also reject ProsperoStore's
reserved `PPSA99000`. The standalone packager does not query console inventory.
Fixture-provider compile/download evidence does not establish
a live Muse connection or console installation.

## Deferred ZIP delivery and updates

The user deferred ZIP work. No MuseStation ZIP release, ZIP install endpoint or
automatic folder updater is delivered by these changes. `.pkg` remains the
artifact kind for the current relay. A future ZIP path has two purposes: a
separately qualified loose-title installation route and controlled updates to an
owned installation. It must preserve executable/metadata/assets as a real native
title; renaming a payload or package to `.zip` is not that implementation.

Before enabling updates, require an authenticated manifest and exact archive hash,
bounded extraction that rejects traversal, symlinks, duplicates and ambiguous
title roots, and title/version verification in a separate staging directory. Check
installed ownership and the expected old version, require the title to be closed,
and retain a backup with a durable transaction journal. Recheck ownership and
running state immediately before replacement. Recover interrupted swaps explicitly;
keep private configuration, keys and journals outside the replaceable bundle.
This must be a tested transaction with rollback, not blind directory replacement.
ProsperoStore supplies relevant pinned [archive validation](https://github.com/darkxex/ProsperoStore/blob/e8b9b09fcd54cce27ea1bcda99a57af9df0fb4b4/src/install/archive.cpp#L150-L265),
[staging and two-rename update](https://github.com/darkxex/ProsperoStore/blob/e8b9b09fcd54cce27ea1bcda99a57af9df0fb4b4/src/install/transaction.cpp#L595-L754)
and [journal recovery](https://github.com/darkxex/ProsperoStore/blob/e8b9b09fcd54cce27ea1bcda99a57af9df0fb4b4/src/install/transaction.cpp#L840-L951)
patterns; it does not make the whole update one atomic rename.

Any loose-folder `0777` requirement must be confined to the app bundle. It never
applies to private credentials/configuration (`0600`) or their directories (`0700`).
For HTTPS delivery, preserve certificate/hostname checks and correct the console
clock rather than disabling TLS verification. Upstream documents its
[folder permissions and clock checks](https://github.com/darkxex/ProsperoStore/blob/e8b9b09fcd54cce27ea1bcda99a57af9df0fb4b4/README.md#L103-L124).
Source offsets or another firmware's test report cannot waive a 12.40 hardware
test for either initial installation or recovery after a failed update.
