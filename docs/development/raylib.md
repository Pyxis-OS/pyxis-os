# Drawing with raylib

The [raylib port](../../ports/raylib/README.md) builds raylib 6.2 with its bundled
rlsw software renderer and Pyxis SDL2. It exports static libraries and headers;
it needs no OpenGL driver. Shapes, textures, default/TrueType text and basic
filled 3D are selected. Audio is a separate, unassigned task. The
[milestone](../wip/raylib.md) and [qualification record](experiments/raylib/README.md)
distinguish source, QEMU and native evidence.

## Canvas and input

`InitWindow(width,height)` chooses the logical canvas. SDL supplies the actual
space content size, which excludes the bar. The port fits the canvas into it
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
SDL's current text input is printable US ASCII, not an IME.

## Example and development packages

The [package recipes](../../packages/README.md#raylib-development-and-shapes)
are opt-in. `raylib-shapes` exports one no-audio bundle, with generated checker
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
by name on the separate `sdl2-dev` package from the SDL2 port. Both use the
existing toolchain package kind for compiled archives. Neither bundles the
other's development files.

The planned package integration supplies `sdk://include`,
`sdk://lib/libraylib.a` and `sdk://lib/libSDL2.a`; only `include/` and `lib/`
enter the view. The editable example stays at
`pkg://raylib-dev/share/raylib/examples/shapes.c`. Individual package contents
live under `pkg://NAME/...`; no package-named roots are introduced. The
[development-package migration](../wip/raylib.md#development-package-migration-planned)
will switch both recipes to `dev` and qualify hosted Clang. **Task 2's merge
waits for that SDK view to land.** Host prefix/CMake build commands remain in
the port README. Assets require a declared root such as `app://`;
executable-location inference is unavailable. Missing native modification
metadata is an error. Runtime audio is not silently substituted.
