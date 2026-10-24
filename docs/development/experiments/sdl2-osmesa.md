# SDL2 OpenGL through OSMesa

2026-10-24, task 3 of [Quake 3/Mesa](../../wip/quake3-llvmpipe.md).
Base main `f265289a`, userland `1453c097`, baseline ports `bb3da8db`, changed
ports `a3bfc58`; builder `pyxis-llvm23.1.3-3d69e83`. No kernel ABI, compiler pin,
JIT or new runtime authority. `kernel.config` SHA-256
`106bf0ae11de93dc8269178a724abdce370cb1a92164690059774b0c69a3cc8e`.

Fresh mirrored Mesa/SDL recipes and a full development image build pass.
Commands: SDK export, `make -j8 image DISTRIBUTION=full REMOTE_BEACON=t14`, then
the [gears Makefile](../../../ports/sdl2/manual/gears/Makefile) with that SDK and
the staged SDL/Mesa prefixes. Host Mesa dependencies are installed separately;
the compiler container itself is unchanged. A C-only CMake project using
`SDL2::SDL2-osmesa-static` links its complete real Mesa/C++ closure.

## Runtime checks

QEMU 10.2.2, Q35/nested KVM, four host CPUs, 4 GiB, OVMF, modern VirtIO
GPU/network/RNG, no audio. The full image is built from this stack. Manual
programs use declared bundles over ordinary native files; no device passthrough.

- Direct OSMesa checks: actual double-buffer visual; independent red back and
  green front; two differently coloured swaps; front GL readback and CPU image
  agree after each swap; detached swap refuses. This exercises framebuffer
  invalidation, rather than merely copying a second CPU array.
- Gears uses SDL's default double-buffer request and reports softpipe, OpenGL
  3.3 compatibility. Keyboard/drag input, focus loss/return and explicit
  960×640 → 640×480 canvas resize work. Native 1280×800 → 1024×768 → 1440×900
  output changes preserve the canvas and aspect-fit presentation.
- A separate manual program checks actual version/colour/acceleration attributes,
  unsupported MSAA/stereo/ES/context flags/version and swap-interval refusal,
  render-thread detach/rebind, resize refusal while another thread owns the
  context, fullscreen canvas size, single-buffer operation, and window destruction
  before context deletion.

The clean gears screenshot, with its renderer label drawn in-frame, is retained
at `/shared/present/screenshots-keep/quake3-task3-gears-20261024.png`.

## Cost and limits

The pre-code link audit estimated unconditional SDL+Mesa closure at 17,949,138
P1F bytes versus 982,998 for a minimal ordinary SDL window. The implemented
ordinary archive has no Mesa/C++ imports; its same C consumer is 987,126 bytes
(+4,128 bytes, 0.42%). The gears executable is 18,006,930 bytes. GL's added
closure is explicit at link/package selection.

Existing `raylib-shapes 640 480`, with identical SDK/raylib and only the SDL
archive changed, ran in three interleaved pairs at 1280×800 output. Its existing
60-FPS limiter remained in effect:

| Nested QEMU | Median FPS | Range |
| --- | ---: | ---: |
| Baseline SDL | 60.0 | 60.0–60.0 |
| Changed ordinary SDL | 60.0 | 59.2–60.0 |

This qualifies operation at the cap, not equal CPU cost or native performance.
One pair disturbed by concurrent Remote log collection was replaced; raw runs,
commands and receipts remain under `/shared/present/quake3/sdl2-osmesa/`.
No native GL, formal conformance, allocation-failure injection, game or JIT claim.
