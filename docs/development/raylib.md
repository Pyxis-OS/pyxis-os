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
texture, default-font text and color corners. It requests memory, clock READ/SLEEP,
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

`raylib-dev` holds `raylib/` and `sdl2/` development prefixes and notices. It uses
the existing toolchain package kind for compiled archives. A trusted space can
name an explicit read-only root, e.g. `raylib = { package = "raylib-dev" }`.
A bundle compiler instead declares that package directory grant. There is no
ambient developer-root or SDK expansion. Programs link their objects, raylib,
SDL2 and the ordinary SDK runtime; the port README gives the host/CMake commands.

Headers and archives are under `raylib://raylib/include`,
`raylib://raylib/lib/libraylib.a`, and `raylib://sdl2/{include,lib}` when that
root is declared. The editable example is
`raylib://raylib/share/raylib/examples/shapes.c`. Assets require a declared root
such as `app://`; executable-location inference is unavailable. Missing native
modification metadata is an error. Runtime audio is not silently substituted.
