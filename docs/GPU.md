# GPU inference integration

The upgraded title compiles llama.cpp's real Vulkan backend and uses it for Qwen
and Whisper. It links the public PS5 RADV driver into the same native executable
as the OpenGL interface. Console execution remains a user smoke test; compilation
does not establish firmware 12.40 compatibility or speed.

## Source and driver integration

On 2026-10-19, the official PS5LM and PSllama repository HEADs were rechecked with
`git ls-remote`: `6323ecbf443e257932989a55cf535567697e2624` and
`d079717184d8153793789f13ed157c1c70390ba0`. Their native build recipes provide the
integration pattern. The driver sources are pinned separately in
`inference/gpu/lock.json`:

| Component | Revision |
|---|---|
| PS5_Vulkan | `f410e67a0241b3e8edcdbcd2a9b285dfc4a412ed` |
| PS5_Mesa | `1ba9874f478657ccf6da72ce55f0260164cc0bd0` |
| PS5_PayloadSDK | `b83202be73e930050e00de0f1b4d9ec46c0391bf` |

The latter two are the pins used by that PS5_Vulkan revision. SDK v0.42's public
release is installed with the fork's source-built platform library. The CPU
baseline's local libc adapters stay available for a CPU-only build; the GPU
title uses the fork's actual locale, thread, filesystem and direct-memory
implementations through its `radv-link.sh` recipe.

RADV additionally includes PS5LM's three pinned `patches/mesa` patches, with
their exact SHA-256 hashes in `inference/gpu/mesa-patches.json`. Patch 0007 keeps
destroyed GPU buffers alive until already-submitted work completes; this is
necessary for safe model unloads. Patches 0008/0009 add optional submission and
wait tuning, whose switches remain disabled/default. The build creates the
reproducible derived Mesa revision `b86c6e3979cfb580b32ffb27a90c6a39cb83fb2c`
from the base and patches using a temporary Git index; it leaves the source
checkout unchanged. Full patch sources, author headers and notices are included.

The OpenGL archive is combined into one relocatable object and only its GL/EGL
API remains global. This prevents its Mesa internals from resolving RADV's
similarly named functions. Both drivers initialize the console's AGC through a
shared once-only wrapper. Import stubs describe real system modules to the
converter; those stub bodies are never packaged or run as replacement drivers.
LLD 18 leaves Mesa's absent optional weak dispatch entries as dynamic imports.
Following PSllama's pinned build, the link runs twice: inspect undefined weak
symbols, reject names outside the known Mesa/C++ thread-local prefixes, and
bind only that verified set to NULL. Strong unresolved symbols are never
substituted. This preserves the dispatch tables' intended optional-entry
semantics while satisfying the title converter.

The platform's `getaddrinfo` intentionally fails. Muse supplies a numeric IPv4/
IPv6 adapter for its numeric-only LAN HTTP contract and excludes those two
resolver aliases from the RADV recipe. No DNS success is fabricated.

## Runtime selection and memory

`muse_inference_config.backend_policy` selects automatic GPU with CPU fallback,
explicit CPU recovery, or required Vulkan. Explicit CPU mode sets ggml's
documented `GGML_DISABLE_VULKAN` switch before registry initialization. Change
the policy before the first engine creation; restarting the title is the clear
recovery path after a driver fault.

Automatic mode enumerates a real Vulkan device, requests all Qwen layers and KV
operations on it, and initializes Whisper with GPU use. Whisper's status comes
from its initialized backend objects, because upstream can silently select CPU.
Returned initialization/evaluation failures retry on CPU. A driver assertion,
process crash or GPU hang cannot be caught as a normal initialization error;
the existing startup/failure journal and next-run recovery remain necessary.

`muse_inference_status()` distinguishes model/context loading, successful Qwen
evaluation, actual Whisper backend, unloaded models and fallback reasons. TTS
reports the live audio-generation projector's CPU/GPU/other buffer allocations
and backend names. That proves placement of model and reserved compute buffers,
not that every graph operation executed on the GPU. Successful audio must still
be heard on hardware.

Neural speech releases Qwen and Whisper before loading its own model/projector.
It releases those speech models after writing the WAV. A later draft or
transcription reloads its model lazily. Failed neural synthesis retries CPU when
allowed, then reports its eSpeak fallback. Required Vulkan rejects neural
failure or absent GPU projector allocations before committing the output WAV;
it does not silently substitute eSpeak. Explicitly choosing eSpeak speech mode
is independent of the Qwen/Whisper backend policy. No substitute text or audio
is returned as successful neural inference.

The GPU stage removes the UI kit's fixed 2 GiB heap and links the pinned
platform heap. That allocator reserves up to 16 GiB of virtual address space
(retrying smaller ranges down to 1 GiB), maps physical direct-memory segments
on demand, and releases them as dlmalloc returns segments. The virtual range
is not a physical allocation. CPU allocations and RADV share available direct
memory. The allocator supports up to eight thread arenas and 1,024 segments,
then falls back to libc when it cannot serve an allocation. Runtime status
reports its current/peak mapped bytes and libc fallback count. Available
physical capacity must be measured on the actual console.

## Reproduce in Docker on Opti3

```sh
python3 scripts/inference-fetch-gpu.py
docker build -t ps5-muse-gpu -f inference/gpu/Dockerfile .
docker run --rm --memory=2g --memory-swap=2g --cpus=1 \
  -v "$PWD:/work" ps5-muse-gpu bash scripts/inference-build-gpu-driver.sh
docker run --rm --memory=512m --memory-swap=512m --cpus=1 \
  -v "$PWD:/work" ps5-muse-gpu python3 scripts/inference-collect-sources.py
docker run --rm --memory=3g --memory-swap=3g --cpus=2 \
  -e MUSE_GPU=1 -e MUSE_BUILD_JOBS=2 -v "$PWD:/work" \
  ps5-muse-gpu bash scripts/inference-build.sh
docker run --rm --memory=3g --memory-swap=3g --cpus=2 \
  -e MUSE_GPU=1 -e MUSE_BUILD_JOBS=2 -e MUSE_SKIP_FINAL_STAGE=1 \
  -v "$PWD:/work" ps5-muse-gpu bash scripts/native-build.sh
```

The driver build uses a one-job Ninja wrapper. ggml's shader generator can
launch multiple compiler children internally; its glslc wrapper serializes
those children without changing shader source or output. CPU and GPU archive
directories are separate (`inference/build/ps5` and `ps5-gpu`).

The package source archive must include `inference/vendor/gpu` and these build
scripts, together with OpenGL's existing corresponding sources. Build caches,
model weights, `.git` metadata and private install configuration are excluded.

## Compile verification

The pinned RADV archive, all ggml Vulkan shaders, Qwen, Whisper and Qwen3-TTS
archives compiled in Docker on Opti3. The complete native title linked,
converted to the console ELF format and passed SELF integrity inspection on
2026-10-19. The final integration build is recorded in
`logs/native-gpu-final-build.log`; the driver build is recorded in
`logs/inference-gpu-patched-driver.log`. This verifies the complete binary and its
imports, while console startup, device execution and performance remain
hardware checks. `dist/native-v2` keeps the upgraded ELF/SELF separate from the
previous CPU build.

The numeric resolver adapter also passed an ASan/UBSan Linux Docker test with
actual IPv4 loopback traffic, IPv6 parsing, and rejected malformed host/port
inputs (`logs/inference-gpu-network-check.log`). Neural CPU inference and
serial unload/reload evidence is in [the speech report](../inference/NEURAL-SPEECH.md).

## User verification

1. Open the upgraded title after the jailbreak/payload prerequisites are active.
2. Keep the UI rendering while model startup completes. Check the uploaded
   `inference_backend` event for the detected Vulkan device and both engines.
3. Record one short request. Verify its transcript and the draft. A successful
   Qwen evaluation and Whisper's Vulkan backend must appear in logs.
4. Listen to the neural reply. Verify the unloaded-model status during speech,
   then submit another request to test lazy reload.
5. Repeat with explicit CPU recovery mode and verify CPU labels. Do not compare
   measurements from Opti3 with claimed console performance.
6. On any returned GPU failure, verify the fallback reason was uploaded. A
   title crash requires restart and recovery-log upload before further testing.

Primary references: [PS5LM native build](https://github.com/cobanov/PS5LM/blob/6323ecbf443e257932989a55cf535567697e2624/scripts/build-app.sh),
[PSllama speech build](https://github.com/mansanitizer/psllama/blob/d079717184d8153793789f13ed157c1c70390ba0/app/build-gguf-vk-title.sh),
[PS5_Vulkan RADV recipe](https://github.com/mihawk-99/PS5_Vulkan/blob/f410e67a0241b3e8edcdbcd2a9b285dfc4a412ed/tools/radv-link.sh),
[platform heap](https://github.com/mihawk-99/PS5_PayloadSDK/blob/b83202be73e930050e00de0f1b4d9ec46c0391bf/platform/include/ps5platform/heap.h).
