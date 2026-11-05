# PS5 rules for generated native apps

Read this before writing source for an `approved_job`. Every rule below was
verified on the owner's PS5 (firmware 12.40) or is required by the fixed
native kit. The relay checks the rules marked **[gate]** before compiling; a
violation fails the build with a fix hint, and you may resubmit corrected
source for the same job (see [Automatic fixes](#automatic-fixes)).

## Name the app in your plan

When you POST a plan to `/provider/plans`, add a short app name:
`{"schema": 1, "type": "plan", "request_id": "…", "text": "…", "name": "Button Buddies"}`.
At most 32 characters; it becomes the title on the PS5 home screen and in
MuseStation's Creations tab. Without it the relay derives one from the plan
text ("A bright learning game…" → "Bright Learning Game").

## What works

- One C++20 program: `main.cpp` plus optional `include/*.h|.hpp`, built
  against the homebrew UI toolkit (`gfx/`, `ui/`, `core/`, `audio/`,
  `platform/ps5/`). [example/main.cpp](example/main.cpp) is a complete,
  working app; Muse's own counter app (job `j7bd07ff…`, PPSA99001) ran
  cleanly on the console with a loading screen and DualSense input.
- Rendering with `hui::gfx::Renderer` / `DrawList`: rounded rects, lines,
  triangles, stars, gradients, glass panels, SDF text with the kit fonts in
  `/app0/assets/fonts/`, and textured images (see [Images](#images)).
- DualSense input through `platform/ps5/pad.hpp` and `core/input.hpp`
  (Cross = confirm, Circle = back), rumble, and `audio/` mixer sounds.
- Saving small files with `hui::save::read_file` / `write_file`
  (`core/save_file.hpp`) under `/download0/`.

## DualSense buttons (exact names)

`hui::Action` (from `core/input.hpp`) has **only** these members. Do not guess
others (`r1`, `r2`, `options`, `cross`, `square` do not exist):

| Button | `hui::Action` | Button | `hui::Action` |
|---|---|---|---|
| Cross | `confirm` | L1 | `page_prev` |
| Circle | `back` | R1 | `page_next` |
| Triangle | `north` | L2 | `jump_prev` |
| Square | `west` | R2 | `jump_next` |
| D-pad | `up` `down` `left` `right` | Options | `menu` |
| Touchpad click | `touch` | L3 / R3 | `l3` / `r3` |

Use `input.is_pressed(hui::Action::page_next)` for a press this frame and
`input.is_held(...)` while held. Analogue sticks: `input.stick_x` / `stick_y`.

## Rules

1. **[gate] End through `hui::sys::quit()`.** Never `exit()`, `abort()` or a
   return from `main`. Make Circle (or Options) quit.
2. **[gate] No networking.** Generated apps get no network access, and the
   PS5 title sandbox blocks BSD `socket()` anyway.
3. **[gate] No stdio/iostream file reads.** `fread`/`fgets`/`std::ifstream`
   reading to end-of-file reports an error on the title filesystem. Use
   `hui::save::read_file` (exact `read()` loop).
4. **[gate] No exceptions or RTTI.** The kit compiles with
   `-fno-exceptions -fno-rtti`: no `throw`, `try`/`catch`, `typeid`,
   `dynamic_cast`. Return error values instead.
5. **[gate] No processes or dynamic loading**: no `system`, `popen`, `fork`,
   `exec*`, `dlopen`.
6. **[gate] Only kit and standard headers.** No SDL, GLES/Vulkan headers,
   or anything that is not in the kit.
7. Keep every frame under 16 ms: no blocking waits or long loops on the
   render thread. Animate with the frame delta time.
8. The screen is 1920×1080. Leave a 64 px safe margin for TV overscan.
9. A loading or splash screen should last at most ~2 seconds and must not
   block input.

## Images

You can generate pictures (logo, background, sprites) and send them in the
same source submission, as base64 PNG data in an optional `images` object.
Nothing is fetched from the internet.

```json
{
  "schema": 1, "type": "source", "job_id": "…", "approved_revision": "…",
  "language": "cpp",
  "files": {"main.cpp": "…"},
  "images": {"logo.png": "<base64 PNG>", "background.png": "<base64 PNG>"}
}
```

Limits: up to 8 images; names like `logo.png` (lowercase letters, digits,
underscore); 8-bit non-interlaced PNG (RGB, RGBA, grey or palette); at most
1024×1024 pixels each and 3 MiB of decoded pixels in total (e.g. one
1024×512 logo plus a 512×512 background). Scale them when drawing.

The relay converts them into `include/muse_images.hpp` (do not send that
file yourself). Use it like this:

```cpp
#include "include/muse_images.hpp"
// once, after the renderer is created:
const std::uint32_t logo = muse_images::upload(renderer, muse_images::logo);
// every frame:
d.image(logo, {760, 300, 400, 200}, hui::gfx::kFullUv, hui::gfx::Color::rgb(0xffffff));
```

With images or sounds, the source hash covers the canonical JSON of the parts
present (`{"files", "images", "sounds"}`); with only files it remains the hash
of the file map alone.

## Sounds and music

Send sound effects and music in an optional `sounds` object next to `files`
(and `images`), as base64 data. Nothing is fetched from the internet.

```json
"sounds": {"capture.wav": "<base64 WAV>", "theme.ogg": "<base64 OGG Vorbis>"}
```

- **Effects: WAV**, uncompressed PCM 8/16/24-bit, mono or stereo, 8-96 kHz,
  at most 60 s each. The relay converts them to the 48 kHz 16-bit stereo the
  toolkit plays.
- **Music: OGG Vorbis, 48 kHz** (not Opus, not 44.1 kHz), mono or stereo. It
  streams from memory and loops.
- Up to 12 sounds; names like `capture.wav` / `theme.ogg` (lowercase letters,
  digits, underscore); at most 8 MiB each and 24 MiB after conversion.

The relay ships them in the app as `/app0/assets/sounds/<name>` and generates
`include/muse_sounds.hpp` (do not send it) with one path constant per sound:

```cpp
#include "include/muse_sounds.hpp"
#include "audio/mixer.hpp"
#include "platform/ps5/audio_out.hpp"

hui::audio::Mixer mixer;
muse_sounds::Music music;
music.start(mixer, muse_sounds::theme);      // BEFORE audio_out.start(mixer)
hui::ps5::AudioOut audio_out;
audio_out.start(mixer);
muse_sounds::Effect capture;
muse_sounds::load(capture, muse_sounds::capture);
// in the frame loop:
music.pump();
if (correct) muse_sounds::play(mixer, capture);   // gain, pitch optional
// on exit: audio_out.stop(); before hui::sys::quit()
```

Without sound files you can still synthesise tones with
`mixer.play_tone(hui::audio::Tone{...}, params)` (sine, triangle, soft square
sweeps). With sounds, the source hash covers `{"files", "images", "sounds"}`
(only the parts present).

## Automatic fixes

This replaces the older one-attempt rule: `approved_job` events now carry
`"max_attempts": 3`.

If the rules gate or the compiler fails, the `result` event has
`"fix_allowed": true`, `attempts`, `attempts_left` and a `diagnostics`
reference. Read the diagnostics, fix the code and POST corrected source to
`/provider/sources` for the **same** `job_id` and `approved_revision`. You
get 3 attempts in total for one approval; after the third failure the result
has `fix_allowed: false` and the owner must submit a new request. Do not
change what the app does while fixing: the plan was approved as written.

## Reference homebrew

For features these rules do not cover yet, look at how working PS5 homebrew
does it (read, do not copy code without its licence):

- **Toolkit headers (exact API, read these instead of guessing):**
  <https://github.com/InterestingSoup/musestation/tree/main/vendor/native-ui/src>
  (`core/input.hpp`, `gfx/draw_list.hpp`, `gfx/renderer.hpp`, `audio/mixer.hpp`,
  `platform/ps5/pad.hpp`; raw files under
  `https://raw.githubusercontent.com/InterestingSoup/musestation/main/vendor/native-ui/src/...`)
- ProsperoStore (a native title app with sandbox-safe networking, sceHttp):
  <https://github.com/darkxex/ProsperoStore>
- PS5 payload SDK (system library stubs, crt): <https://github.com/ps5-payload-dev/sdk>
- MuseStation itself: `native/src/` in this repository.
