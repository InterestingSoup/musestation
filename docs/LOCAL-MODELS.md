# Optional local coding models

MuseStation 01.006.000 source keeps speech in the package and downloads a coding
model only when the user chooses Local lab. Hybrid with Muse needs no coding model.

| Included in the app | Used for |
|---|---|
| Whisper tiny.en | English speech-to-text |
| Qwen3-TTS 0.6B and its projector | Neural text-to-speech |
| eSpeak NG data | Speech fallback |

The three speech weight files total **839,857,515 bytes**. Removing the previously
bundled Qwen2.5-Coder saves **1,117,320,768 bytes** of weights. The inference engine
stays linked so a later download does not require reinstalling MuseStation.

## On-console setup

1. Open **Modes → Local lab**. Complete the optional AudioIn setup; a blank module
   path allows typed requests.
2. On the model screen, press **X** to download the recommended
   **Qwen2.5-Coder 1.5B Instruct Q4_K_M** (1.12 GB), or **Square** to enter a custom
   Hugging Face file URL with the controller keyboard. **R1** starts the custom
   download. No download starts just by opening the screen.
3. Download progress runs on the worker thread while the interface remains
   responsive. **Circle** cancels; the current network operation can take up to
   its configured timeout to return. Failed/interrupted transfers restart from
   the beginning when retried; HTTP Range resume is not implemented.
4. The downloader checks size, SHA-256 and bounded GGUF metadata, then the local
   engine attempts to load the selected file. **R1** returns to creation once it
   is ready. A failed optional coder load retries voice-only initialization and
   displays the error; it does not provide a successful placeholder plan.
5. After setup, Local creation and speech run offline. This explicit model download
   is an exception to Local's normal no-network behavior; it does not enable the
   relay, phone listener or log uploads.

The console needs internet access for setup, including `huggingface.co` and its
HTTPS delivery hosts under `*.hf.co` / `*.huggingface.co`. No PSN sign-in is used.
Correct console time and working platform certificates are required. TLS
verification stays enabled; a certificate error is not bypassed. Downloading
while the app is suspended, after launching another title or during rest mode
has not been established.

## Custom URLs and compatibility

Enter a **public GGUF file link**, for example:

```text
https://huggingface.co/OWNER/REPOSITORY/resolve/REVISION/MODEL.gguf
```

The file page form using `/blob/` is normalized to `/resolve/`. A repository URL
without a filename is insufficient. A commit revision is preferable to a mutable
branch when reproducibility matters. The exact recommended URL/hash/size are
pinned in `inference/codegen-lock.json` and checked against the downloader by tests.

This is **not support for every Hugging Face model**. The current adapter accepts:

- A single GGUF v3 text model with `qwen2` or `qwen3` architecture and a ChatML
  template, matching the current inference adapter.
- At most **1.5 GiB** per file; this is a conservative download bound, not a
  guarantee that every accepted model fits in runtime memory or produces useful apps.
- Public, ungated files whose Hugging Face response supplies SHA-256 and size
  metadata. No Hugging Face token or account integration is implemented.

Safetensors checkpoints, split GGUFs, vision models, other architectures and
private/gated repositories are rejected. Metadata checks establish eligibility;
actual model loading and generation can still fail. The only recommended model
remains the pinned Qwen2.5-Coder; other accepted models are experimental.

Hugging Face's [GGUF documentation](https://huggingface.co/docs/hub/en/gguf)
explains the file format. The console transport follows the sandboxed `sceHttp`
API pattern in [ProsperoStore's pinned implementation](https://github.com/blackbearreloaded/ProsperoStore/blob/82ea4a232ce3074893d9a61725d1b9f8706c6556/src/platform/ps5/store_http.cpp).
MuseStation does not elevate the main app or disable TLS verification to download.
Its own HTTPS path still requires testing on firmware 12.40.

## Storage and updates

Speech assets remain in `model_dir` inside the app bundle. Downloads go into
`<artifact_dir>/models/<sha256>.gguf`, outside the bundle. `coding_model_path` in
persistent configuration selects the optional coder separately from speech.
The file-based setup accepts `MUSE_CODING_MODEL_PATH` for an existing model path;
blank means voice-only until the model is downloaded. Hybrid ignores that path.

Partial files use `.part`; only exact size/hash matches with eligible GGUF
metadata are renamed into place. A failed download does not replace the selected
model. Previous model files are retained; there is no automatic eviction or model
library/delete UI yet. Leave room for the new model, retained models, generated
apps and logs. A full disk fails the download visibly. Config and model storage
must remain outside the app folder when updating the title.

## Developer checks and hardware checklist

- Run `tests/model_download_run.sh` in the native Docker image: URL/redirect
  restrictions, size/hash/truncation/cancellation, metadata parser, TLS-option
  failures and speech-only staging. A cached recommended model is inspected when
  available, but tests do not require downloading it.
- `tests/local_worker_run.sh` checks optional-coder absence/load failure and
  voice-only recovery with an inference fixture, alongside the real job store and
  Lua validation. Ordinary work must not call the downloader.
- `scripts/inference-fetch-models.py` fetches bundled Whisper by default. Use
  `--include-codegen` or `scripts/codegen-fetch-models.py` for developer model tests.
  `scripts/neural-fetch-models.py` supplies the two neural-speech files.
- Package staging and release verification exclude the coder. Historical package
  verification can explicitly use `--legacy-bundled-codegen` with its version.

On the owner's PS5: first verify local speech with no coder present; download the
recommended model; cancel and retry; restart MuseStation to check persistence;
create a small Local app; switch to Hybrid to confirm it ignores the coder; test
an invalid custom URL and a model that cannot load; confirm that app updates keep
the external model/config. Native compilation, host fixtures and preview renders
are not evidence of PS5 internet access, speech performance or inference success.
