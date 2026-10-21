# MuseStation 01.006.000 — speech included, coding model optional

Verified on 2026-10-20 using Docker on Opti3. This is a hardware-test candidate;
no PS5 execution, internet download, microphone, speech or GPU performance is
claimed. Binary release assets have not been uploaded publicly.

## Result

The package includes Whisper tiny.en, Qwen3-TTS 0.6B plus its projector, and
eSpeak NG data. The coding model is no longer bundled. Hybrid needs no coder;
Local setup offers a recommended download or a compatible public Hugging Face
GGUF file URL. See [setup, compatibility and hardware checks](LOCAL-MODELS.md).

Package: `dist/pkg-v7-final/IV0000-PPSA99576_00-MUSEGADGET000001.pkg`

- Size: **983,262,550 bytes**, compared with 2,101,965,680 bytes for 01.005.000.
- SHA-256: `c523cc233ad7d0467af33da23dc791cb052b7bcdbfc300037dcff6623f4eee26`.
- Complete extraction/hash verification: **442 files**, **8 structural checks**.
- Three locked speech weight files: **839,857,515 bytes**. The removed coder
  accounts for 1,117,320,768 bytes of weights.
- Native SELF: 87,918,654 bytes, SHA-256
  `c45210e3f84e3767fcdff0ae8ec3093e259a75cf27758927d8f017d9dd09267b`.
- Installer helper: 227,968 bytes, SHA-256
  `340269cfce04574ebc4cbef4e837d8e6559a0e363b892d19e296f2a7b25b91e7`.

The verifier independently checks identity, package hash, every staged file and
locked model, plus the packager's explicit zero-version metadata transformation.

## Verification

- Native Vulkan/OpenGL/voice application compiled with `MUSE_GPU=1`,
  `MUSE_NATIVE_VERSION=01.006.000` and one compile job. The real platform HTTPS
  symbols linked successfully; this does not establish runtime TLS support.
- Address/undefined-behavior sanitizer fixtures passed URL and redirect policy,
  bounded headers/GGUF parsing, streamed SHA-256, size limits, truncation,
  cancellation and preservation of the previous selected model.
- Native HTTPS API fixtures passed certificate-option failure handling,
  explicit redirects, metadata-only reads, cancellation and resource cleanup.
- The cached recommended GGUF passed the metadata parser. An operator-host
  HTTPS metadata check returned the pinned 1,117,320,768-byte size and SHA-256;
  its CDN host was within the downloader's allowlist. No PS5 download was tested.
- Staging tests removed a stale bundled coder while preserving the developer
  cache. Configuration tests retained an empty optional coder by default.
- Worker fixtures passed absent/broken-coder voice-only recovery, explicit
  model download/reconfiguration, rejection during queued local work and
  ordinary Local work without downloader/network access. Inference is mocked
  in these worker fixtures; this is not an actual console speech test.
- Real Lua player checks passed source integrity, controller input, save/reopen,
  deterministic seed and preservation of previous saves on runtime failures.
- Three host-rendered model setup/download/URL screens were visually inspected.
- The normal relay's retained database baseline remained unchanged.

## Reproducibility and retained evidence

Compiler image: `sha256:edcc1bc833d8df395696b9ab19b5a025a42c921ece7c641b66b7e8111af1707d`.
Packager image: `sha256:6e776a921544a4188f60633736361b0efbfdc8f3ff2df76d2f3072ec4523d567`.
Both were rebuilt from the project's pinned build inputs after old local images
were pruned. Model hashes and upstream revisions remain locked in the repo.

Retained developer evidence (not files shipped through Git):

- `logs/optional-model-final-native.log`
- `logs/optional-model-final-checks.log`
- `logs/optional-model-client-final.log`
- `logs/optional-model-final-package.log`
- `logs/optional-model-release-verification.json`
- The adjacent package report and corresponding-source archive under
  `dist/pkg-v7-final/`.

Custom downloads are limited to public, single-file Qwen2/Qwen3 ChatML GGUF v3
files up to 1.5 GiB. Eligibility is not a runtime compatibility guarantee. No
private/gated model authentication, interrupted-transfer resume or automatic
model eviction is implemented. External coding-model/config storage survives
bundle replacement; the owner must verify this and the remaining hardware
checklist on the actual console.
