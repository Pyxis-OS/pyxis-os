# raylib graphics and audio

The [raylib port](../../ports/raylib/README.md) builds raylib 6.2 with its bundled
rlsw software renderer and Pyxis SDL2. It exports static libraries and headers;
it needs no OpenGL driver. Shapes, textures, default/TrueType text and basic
filled 3D are selected. Playback uses the custom miniaudio/SDL backend described below; audio applications
need their own declared grants. The
[qualification record](experiments/raylib/README.md) distinguishes source, QEMU
and native evidence.

## Source and selected profile

The cache-only pin/checksum are in [metadata.lua](../../ports/raylib/metadata.lua).
The static profile uses bundled rlsw 1.6 with no OpenGL context, host-native AVX
requirement, shared objects or dynamic loader. PNG/TrueType/default-font text,
2D primitives, textures and basic depth-tested 3D are selected; model-file
loaders, GPU/shader APIs and wider render-texture/depth combinations are outside
the qualified profile. The [port notices](../../ports/raylib/PORT-NOTICE) and
[third-party licences](../../ports/raylib/THIRD-PARTY-LICENSES) cover raylib's zlib,
rlsw's MIT and the selected bundled components. The packages also carry the
MPL terms and covered Pyxis configuration/adapter sources.

## Canvas and input

`InitWindow(width,height)` chooses the logical canvas. One window occupies the
space content area. SDL supplies its actual size, which excludes the bar. The port fits the canvas into it
with point sampling and black letterbox, preserving aspect ratio. The example
uses 960x540; programs can choose native or other raster sizes. Larger canvases
increase software color/depth/shading work; scaling and presentation still touch
the full output. There is no native-resolution FPS guarantee.

Screen/render queries describe the logical canvas; SDL display queries describe
the output. Output resizing keeps the canvas and updates presentation/input.
`SetWindowSize` refuses post-init canvas resizing because rlsw's resize interface
does not safely report partial allocation failure. Window-manager controls and
GPU/shader APIs are outside the profile. Wider render-texture combinations need
qualification.

Pointer coordinates use the inverse viewport transform. The bar/letterbox are
outside the canvas. Missing optional pointer authority permits keyboard/drawing
use; cursor lock/warp still need real native authority and refuse honestly.
Focus changes clear held input and revoke lock; returning does not relock.
Frame pacing sleeps, with no busy-wait or vsync/scanout-completion promise.
SDL's current text input is printable US ASCII, not an IME. Gamepads and
per-window DPI are outside this profile.

## Example and development packages

The [release package recipes](../../packages/README.md#raylib-development-and-shapes)
produce separate packages; installing them is opt-in. `raylib-shapes` exports one silent bundle, with generated checker
texture, default-font text and color corners. It requests memory, clock READ/PRECISE/SLEEP,
display DRAW, keyboard input and optional pointer; no network or launcher.

```text
pkg install raylib-shapes
raylib-shapes
raylib-shapes 1280 768
```

The optional dimensions choose the canvas, not an OS window. Space pauses the
texture, F1 switches 2D/basic 3D, the wheel changes the circle, L toggles cursor
lock, W requests centre positioning and Escape releases the display/input and
exits. Exit prints capped application frames/time/FPS; it is not exclusive CPU
or renderer time. The near orange cube is drawn before the distant blue cube,
so overlap exercises depth testing.

`raylib-dev` contains raylib headers, its static archive and notices, and depends
by name on the separate `sdl2-dev` package. Both are `dev` packages, bound to the
selected build's ABI, with no commands. SDL2 depends on `pyxis-dev`. Neither embeds the other's library.

Install `raylib-dev` into a development space with the base SDK and hosted
Clang installed. Their headers, static archives and `.pc` files join the live
`sdk://` view. No search flags are needed:

```text
pkg install raylib-dev
cp pkg://raylib-dev/share/raylib/examples/shapes.c home://shapes.c
clang home://shapes.c -lraylib -lSDL2 -o home://shapes.pxe
```

Only `include/`, archives directly in `lib/`, and `lib/pkgconfig/` enter the
view. CMake files, notices, provenance and the editable example remain under
`pkg://NAME/...`; there are no package-named roots or `sdk://share`.
`pkg-config --cflags --libs raylib` includes the separate SDL2 module.
The installed example manifest describes its normal drawing/input grants.
Host prefix/CMake commands remain in the port README. Assets require a declared
root such as `app://`; executable-location inference is unavailable. Missing
native modification metadata is an error. Audio is not silently substituted.

## Audio playback

`InitAudioDevice()` selects only the custom miniaudio backend over Pyxis SDL2;
there is no null fallback. Raudio mixes 48 kHz float stereo, and SDL converts
1,024-frame callbacks to the native 48 kHz S16LE stereo session. Generated
`Wave`/`Sound`, `AudioStream` and WAV decoding are selected. Other codecs,
capture and output-device selection are outside this profile.

A consumer declares `audio/create`, `threads/manage`, `clock/read,precise,sleep`
and `pipe/create`; the silent shapes bundle gains none of the audio/thread
authority. SDL owns the real normal-priority callback thread. Miniaudio keeps
real mixer synchronization and creates no redundant audio worker. A denied
open or stopped SDL device leaves `IsAudioDeviceReady()` false. Callers still
unload their sounds/streams and call `CloseAudioDevice()` after device loss.
Close joins callbacks before releasing the native session or destroying the
mixer; a subsequent open acquires a fresh session. SDL queue/pause latency
limits remain in [the SDL reference](sdl2.md#audio).

The [audio experiment](experiments/raylib-audio/README.md) records QEMU
output/capture and denial/cleanup evidence;
chronium qualified stream/WAV playback and stop/reopen on vela on 2026-10-23.

## Qualification and remaining limits

QEMU covers the selected drawing/input/resize/release paths, package installation
and hosted Clang through `sdk://`; the [drawing record](experiments/raylib/README.md)
keeps nested costs and exact revisions. Chronium's ThinkPad drawing/input check
ran at a capped 59.9 FPS; vela's 1920x1080 PXE output passed at 60/59.4 FPS for
its tested canvases. Rare extreme-motion hiccups are an
[accepted limit](https://git.internal/PyxisOS/pyxis-os/issues/1347), not a new
performance guarantee. Vela 2880x1800 and exclusive native CPU cost are unrun.
Native audio on other hardware and device-loss behavior remain unqualified; the
[qualification/backend follow-up](https://git.internal/PyxisOS/pyxis-os/issues/1346)
records when to revisit it. Additional raylib APIs require their own concrete
consumer qualification. Native backends are not scheduled merely to replace SDL.
