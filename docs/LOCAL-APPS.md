# MuseStation local Lua apps

Local projects are text Lua programs executed inside MuseStation. They are not
native ELF files or installable PS5 packages. The host owns rendering, input,
approval and files. A generated revision must pass validation before being saved
as a runnable revision; validation does not prove game quality or every input path.
The user separately chooses to run it.

## Program contract

Provide raw Lua 5.4 source, at most 65,536 bytes, with global `update(dt, input)` and
`draw()` functions. Top-level code initializes ordinary Lua tables and variables.
`update` runs before `draw` each frame. `dt` is seconds clamped to 0..0.1. The logical
canvas is 1280×720; the host scales the recorded commands to its actual GPU surface.
No network, filesystem, process, package installation or native code API is exposed.

```lua
local x, y = 640, 360
function update(dt, input)
  if input.left then x = math.max(20, x - 300 * dt) end
  if input.right then x = math.min(WIDTH - 20, x + 300 * dt) end
end
function draw()
  gfx.clear(12, 16, 28)
  gfx.circle(x, y, 20, 90, 220, 160)
  gfx.text(40, 40, "Move with the D-pad", 28, 240, 240, 255)
end
```

Input contains boolean `up`, `down`, `left`, `right`, `cross`, `circle`, `square`,
`triangle`, `l1`, `r1`, `l2`, `r2`, `options`. `input.pressed` contains those names
for new presses. `lx`, `ly`, `rx`, `ry` are normalized axes in -1..1. The host may
reserve Options to leave the app. Do not assume simulated validation inputs cover
all hardware behavior.

Only `draw()` can call the drawing API:

| Call | Result |
|---|---|
| `gfx.clear(r,g,b[,a])` | Canvas clear |
| `gfx.rect(x,y,w,h,r,g,b[,a])` | Filled rectangle |
| `gfx.circle(x,y,radius,r,g,b[,a])` | Filled circle |
| `gfx.line(x1,y1,x2,y2,width,r,g,b[,a])` | Line segment |
| `gfx.text(x,y,text,size,r,g,b[,a])` | UTF-8 text |

Colors are 0..255; alpha defaults to 255. Positions are finite and bounded to
±8192, dimensions/radius to 0..4096, line width to 0.1..64 and font size to 8..96.
There are at most 512 commands per frame, and each text command has at most 192
UTF-8 bytes with no NUL. For the C draw-list ABI, line endpoint x2/y2 are stored in
`w`/`h`; the list is borrowed until the next frame or destroy.

## State and available language facilities

`state.get()` reads an opaque persisted UTF-8 string; `state.set(string)` replaces
it, up to 16,384 bytes. The host restores it after loading the program, so read it
on the first `update`, not at top level. The host chooses safe storage and atomic
persistence; Lua does not receive a path. This is not VM serialization. A runtime
error terminates that instance; hosts should save state only after successful
frames. Standard globals/tables retain transient state while the app runs.

Available helpers: `assert`, `error`, `ipairs`, `next`, `pairs`, `select`, `tonumber`,
`tostring`, `type`; math operations including deterministic `math.random` and
`math.randomseed(integer)`; `string.byte/char/len/lower/reverse/sub/upper`;
`table.insert/remove` for arrays up to 2048 elements. String helper inputs are
limited to 8192 bytes and 256 arguments. The host sets the initial random seed;
repeated validation with the same seed is deterministic. The integer random range
must stay within ±1,000,000,000. `WIDTH`/`HEIGHT` provide canvas dimensions.

No `os`, `io`, `package`, `require`, `debug`, coroutine, metatable, bytecode loader,
`load`, `loadfile`, `dofile`, `collectgarbage`, `pcall`, `xpcall` or native extension
entry point is reachable. Pattern matching, string format/repetition/packing and
unbounded table library functions are deliberately absent: those C routines do
not consume Lua instruction hooks. The string method metatable points to the
same restricted string API. There is no audio or rumble API in this version.

## Limits and validation

The default Lua allocation quota is 8 MiB (host-configurable 256 KiB..16 MiB), plus
roughly 140 KiB of fixed host draw/state storage. Startup and each combined
update/draw frame have a 200,000-instruction budget, checked every 1000 instructions;
limits can be configured from 1000..1,000,000. A nonblocking cancellation callback
is checked before startup/frame and in instruction hooks. These are allocation and
instruction limits, not a measured PS5 frame-time guarantee. Source parsing is
bounded by source size; the remaining exposed C helpers have bounded inputs.

The C API is `native/src/local_app.h`. Worker validation creates a private VM,
executes 1..120 ticks with neutral and scripted button/axis probes, records peak
Lua allocations/draw count, and destroys it. The renderer owns a separate VM.
Neither shares Lua state across threads. Errors discard the partial draw list and
make that instance terminal. All Lua callbacks and protected call boundaries are
implemented in C, avoiding longjmp through C++ object destructors.

`muse_local_app_prompt_contract()` supplies the exact supported API to codegen.
The validator never repairs prose, strips code fences or silently substitutes a
template. Failed diagnostics can be sent back to the model for an explicit repair
attempt. Limit-exhausted/truncated generation must not be treated as complete.

Validation rejects an app only when every probed frame is provably empty or a
uniform background. A later opaque full-canvas fill can cover earlier content;
matching-color or fully transparent text does not make a uniform frame useful.
Content that appears later in the probe window is allowed, and uncertain geometry,
thin lines and translucent drawing count as potentially visible. The validation
report counts text commands proved hidden by a known uniform background, zero
alpha, or a later opaque full-canvas fill.

This is deliberately not a general readability or behavior test. An inset panel
can make the canvas nonuniform while its same-colored text remains unreadable.
The actual generated counter exposed that case: app-specific control/visibility
checks and host screenshots must establish that its count can be seen and changes
correctly. Passing generic runtime validation does not prove an app is usable.

## Source and build

Lua 5.4.9 is pinned by archive size and official SHA-256 in `native/lua/lock.json`.
`scripts/fetch-lua.py` verifies the archive before extracting `vendor/lua`, retains
its full source and copies its MIT notice to `native/lua/LICENSE.txt`. The upstream
version/checksum and license were checked against the
[official download area](https://www.lua.org/ftp/) and
[Lua license](https://www.lua.org/license.html).

The precise C source subset is `native/lua/sources.txt`; it excludes CLI binaries,
the package loader and IO/OS/debug/coroutine libraries. In a build container,
`scripts/local-app-build.sh` produces `build/local-app/host/libmuse-local-app.a`;
`TARGET=ps5` produces PS5 objects with the public SDK. Use C11, `LUA_USE_C89` for
portable Lua library assumptions, both include directories, and `-lm` when linking.
The native title may instead compile the same source subset directly.

`scripts/local-app-check.sh` builds and runs the real runtime with ASan/UBSan in
Docker, including draw/input/state, reproducible seed, binary/source rejection,
forbidden APIs, infinite loops, allocation exhaustion, text/geometry/draw quotas,
cancellation and terminal error behavior. Compile/test outcome is recorded in
`logs/local-app-check.log`; hardware rendering and controller behavior still require
the user's PS5.

Verification on 2026-10-19: Docker ASan/UBSan runtime suite passed; strict
`-Wall -Wextra -Werror` host and SDK runtime objects passed; host and PS5 SDK
static archives built successfully. Combined evidence: `logs/local-app-final-check.log`.
Visibility follow-up checks also passed under ASan/UBSan and strict host/PS5 SDK compilation; see `logs/local-app-visibility-check.log`. The updated host archive includes the uniform-display gate and hidden-text report. On-console execution remains a hardware test.

## Inspecting actual generated apps

Inside the native Docker image, run:

```sh
bash scripts/local-app-preview.sh actual-generated.lua dist/app-preview
```

This uses the same Lua runtime and `LocalPlayer` renderer with packaged fonts,
fresh state, seed 17, and host llvmpipe software OpenGL. It captures the initial
frame, a Cross press and a Circle press as PNGs, preserving the exact input as
`source.lua`. `preview.json` records source/image hashes, the renderer, validation
statistics and `hardware_verified: false`; `contact-sheet.png` labels every image
as a host software preview. It does not stage or modify the installable title.
