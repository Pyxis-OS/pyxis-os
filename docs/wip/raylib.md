# raylib library for Pyxis

Status: task 1 source closure delivered for review (chronium, 2026-10-22);
task 2 passed QEMU and chronium's ThinkPad visual/input check; vela is pending.
Audio remains task 3. The present consumer is chronium's proof-of-concept C programs;
this is a library plus one shapes example, not a game-engine or GPU milestone.
Private projects and `home://test` were not inspected.

## Inspected source and pin

Pin **raylib 6.2**, the [latest stable release](https://github.com/raysan5/raylib/releases/tag/6.2)
(2026-10-15), commit `02566453880e06bec97f5b8160fa6fe1e98397ca`.
The public reference archive was fetched through the existing cache, not upstream:

- Provenance: `https://github.com/raysan5/raylib/archive/refs/tags/6.2.tar.gz`.
- Build input: `https://repo.internal/repository/raw-github/raysan5/raylib/archive/refs/tags/6.2.tar.gz`.
- SHA-256: `df7a13129b1bec9e73a6962efff170beba310ee6eab8f94cd000b7316b4db5ee`.

The cache returned the archive; no new mirror or compiler container is required.
A ports recipe must retain this exact archive checksum/commit, ordered patches,
provenance and reached-file notices. Raylib is [zlib-licensed](https://github.com/raysan5/raylib/blob/6.2/LICENSE),
allowing static linking; altered sources must be marked and the notice retained.
Its bundled `rlsw` is MIT, not zlib. Miniaudio offers Unlicense or MIT-0; select
MIT-0 if reached. Audit the bundled image/font/audio decoder notices separately;
the whole source tree must not be labelled only zlib. Nothing is vendored by
this proposal.

## Rendering and limits

The [6.0 release](https://github.com/raysan5/raylib/releases/tag/6.0) introduced
`rlsw` and the memory platform. The inspected 6.2 tree bundles **rlsw 1.6**:
[`src/external/rlsw.h`](https://github.com/raysan5/raylib/blob/6.2/src/external/rlsw.h).
`GRAPHICS_API_OPENGL_SOFTWARE` makes `rlgl` use this CPU implementation through
its OpenGL-1.1-style path; it requires no OpenGL context/driver. SDL's software
renderer is not a replacement for `rlgl`, and `ImageDraw*` alone is not the normal
`BeginDrawing`/`Draw*` interface chronium wants.

| Reached path | Expected coverage from source inspection |
| --- | --- |
| 2D shapes | Points, lines, triangles/quads, clipping, scissor and blending; raylib tessellates higher-level shapes |
| Textures and text | Uncompressed textures, point/bilinear filtering and wrapping; default font and glyph-atlas text use textured primitives |
| Basic 3D | Matrix stacks, perspective correction, depth test and face culling; simple colored/textured geometry, not shader-based materials |
| Beyond that | No GLSL/compute/GPU feature promise, compressed-texture or cubemap profile. Software FBO entry points exist, but helpers such as depth-texture loading remain GPU-guarded: qualify render-texture combinations before advertising them |

This is a relatively recent backend, not a complete OpenGL implementation. Only the selected Pyxis profile is
qualified below. Source still contains depth-function/mask TODOs; SIMD
acceleration is opt-in, not the default profile. Build
and visual checks must establish the selected subset; do not claim all raylib
examples work. Keep portable SDK CPU flags; no host-native AVX requirement.

### Cost and scaling

The experiment records nested-QEMU costs and two ThinkPad application runs at
59.9 capped FPS; native CPU cost and vela remain unqualified. These are arithmetic costs at
the panel resolutions chronium supplied, before subtracting the navigation bar:

| Raster size | Pixels | One 32-bit color buffer | One full color write at 60 frames/s |
| --- | ---: | ---: | ---: |
| ThinkPad 1920×1080 | 2,073,600 | 7.91 MiB | 0.498 GB/s |
| vela 2880×1800 | 5,184,000 | 19.78 MiB | 1.244 GB/s |
| Half-size ThinkPad 960×540 | 518,400 | 1.98 MiB | 0.124 GB/s |
| Half-size vela 1440×900 | 1,296,000 | 4.94 MiB | 0.311 GB/s |

One full copy addresses twice the write-only bytes (read plus write). Default
rlsw also allocates a 32-bit depth buffer. Textured blending, overdraw, clear,
pixel conversion, scaling and SDL/presenter copies add work; vela has 2.5 times
the full-resolution pixels. This is traffic arithmetic, not measured DRAM
bandwidth or a CPU/FPS prediction. Expect fill-heavy/3D scenes to cost materially
more than a few shapes; no 60 FPS guarantee at native resolution.

Use an application-selected logical canvas using `InitWindow(w,h)`, with
the example at **960×540**, aspect-fit CPU scaling and letterboxing into the
actual content area. Point sampling is sufficient initially; native-size 1:1
and half-size canvases remain ordinary application choices. Mouse coordinates
must use the inverse viewport transform, including bar/letterbox exclusion.
Lower raster resolution reduces geometry/shading/depth work; full-size output
scaling and presentation copies remain. The scaling path must not depend on
unqualified render-texture/depth APIs. A 60 FPS example cap is initial tuning;
30 FPS halves per-second work, and measurement may justify changing the cap.

## Platform choice

Inspected Pyxis main `c7104817`, ports `96a0cdf0`, userland `65cd3572`;
[SDL2](../development/sdl2.md) is 2.32.10. The
[pointer/font examples](../development/edit-build-run.md#try-the-pointer-example)
already show native display slots, resize, pointer identities and explicit asset
roots. Raylib can use its own default font/texture atlas; it need not depend on
`libfont`. External fonts/assets need declared read authority and their licences.

| Platform | Benefit | Work and trade-off |
| --- | --- | --- |
| **SDL2 + rlsw (recommended)** | Reuses current keyboard/pointer/focus/resize/clipboard/timer integration | Adapt the existing software presentation path; one SDL surface copy before native slot submission |
| Native `rcore_pyxis`, guided by `rcore_template`/`rcore_memory` | Can convert rlsw output directly into an owned display slot | Reimplements input/focus/reset, slot/resize lifetime, timing and cursor policy; no direct scanout ownership or GPU acceleration |

The [upstream SDL platform](https://github.com/raysan5/raylib/blob/6.2/src/platforms/rcore_desktop_sdl.c)
already skips OpenGL setup in software mode and copies `rlsw` output to an SDL
window surface. Required Pyxis adaptations are concrete:

- One window fills the space's actual content area. Query actual SDL dimensions;
  preserve logical canvas dimensions separately. Resize must obtain the new
  surface/layout before copying, and use the existing transactional native
  replacement. Report logical drawing sizes and actual output geometry truthfully;
  no OS window placement, multiple-window or invented DPI support.
- Upstream `SwapScreenBuffer` assumes tightly packed pixels. Check surface/error
  results and handle actual pitch, channel masks, orientation and scaling.
  rlsw defaults to BGRA output and flips rows on readback; verify a color/corner
  pattern instead of guessing from the requested RGBA enum.
- Reuse SDL keyboard/text events and both wheel axes, buttons and relative motion
  from USB/PS2/touchpad. Current text input is printable US ASCII, not an IME.
  Honor native focus/reset and geometry boundaries. Raylib cursor lock/warp must
  not claim success after SDL refusal, nor relock after native revocation.
  Gamepads remain unavailable; do not expose synthetic devices.
- SDL performance counters already use the native monotonic clock. Patch shared
  `rcore.c:WaitTime` to use real sleeping on Pyxis and disable busy-wait pacing;
  its current OS guards otherwise omit the sleep. Native SUBMIT is asynchronous
  and may drop a pending frame: no vsync or scanout-completion claim.

A native backend is an alternative if measured copy costs justify it, not a
second implementation to build now. No kernel display/input changes are planned.

## Audio

SDL platform selection **does not route raudio through SDL**. In 6.2,
[`raudio.c`](https://github.com/raysan5/raylib/blob/6.2/src/raudio.c) uses bundled
miniaudio; its backend list has neither SDL nor Pyxis. Initial shapes delivery
omits the raudio module, rather than report a silent device as working.

Task 3 uses a **custom miniaudio backend over SDL audio**: preserve raudio's mixer/decoders, convert through SDL to
48 kHz S16LE stereo, and reuse native session/write/close handling. Select only
that backend and disable the null fallback. Audit miniaudio threading for real
normal-priority joinable Pyxis threads; its POSIX scheduling assumptions are not
all available. SDL output already needs declared `audio`/`threads`/`clock`
authority, and `pipe/create` supplies threaded event wakeups. Join callbacks
before releasing the session. Existing SDL latency limits still apply.

A direct native miniaudio backend could avoid SDL conversion/copies but would
repeat ownership, writable waits, bounded writes and cancellation. Neither
backend exists today. Audio capture, device selection and fake successful audio
initialization are outside the proposed profile.

## Packaging and linking

Use the [ports conventions](../development/ports.md): metadata, cached archive,
ordered patches, a static `libraylib.a`, `raylib.h`/`raymath.h`/`rlgl.h`, notices
and source/profile provenance under `build/ports-dev/raylib`. SDL2 is an explicit
build/link dependency. Consumers link application → raylib → SDL2 → SDK runtime,
using exported `pyxis.mk`/toolchain settings, not host GL/X11 libraries. A small
relocatable CMake imported target can declare the same SDL dependency.

For in-Pyxis builds, `raylib-dev` and `sdl2-dev` join
[the `sdk://` view](sdk-view.md) (decision 32, chronium, 2026-10-22), so
`cc shapes.c -lraylib -lSDL2` needs no flags. No shared objects, `dlopen` or
compiler-container rebuild is needed.

Ship one public **shapes example** as an opt-in `.pxb`, with source for editing
like `pointer-min`/`font-min`: rectangles/circles/lines, generated checker texture,
default-font text, pointer/keyboard response and Escape exit. It declares
memory, clock READ/SLEEP, display DRAW and keyboard input; pointer is optional.
No network, launcher, credential or private-project grants. Assets use `app://`
or explicit roots, never program-location inference. Audio/thread grants are
added only to programs that actually use the later audio slice.

## Delivery tasks

1. [x] **Static library and source closure.** Add the pinned recipe/configuration,
   SDL dependency, licences and SDK compile/link audit. Fix reached standard libc
   gaps in libc on native objects; stop for any new behavioral contract, never
   fake success. After this task chronium can: link a minimal no-audio C consumer.
2. [ ] **SDL/rlsw integration and shapes.** Implement layout/resize/input/pacing
   corrections and logical-canvas presentation; package the example and development
   files. Capture existing presenter/idle/copy baseline before code. Validate in
   QEMU with debugger inspection, then ThinkPad and vela at native and reduced
   raster sizes: shape/texture/text correctness, basic depth-tested 3D smoke,
   input/focus/resize/release and missing-authority errors. Record repeated frame,
   copy and CPU-cost observations with exact revisions and ranges; separate
   nested from native. After this task chronium can: build and run graphical PoCs.
3. [ ] **Audio.** Implement the selected miniaudio/SDL bridge; qualify
   format conversion, denied/absent device, stop/reopen and thread/session cleanup
   with quiet known audio. After this task chronium can: use the qualified raudio
   APIs with explicitly declared audio/thread grants. No network or device probe.

## Accepted decisions (chronium, 2026-10-22)

1. **Renderer/platform:** raylib **6.2 + rlsw + SDL2**, restricted to the inspected
   2D/basic-3D profile, with unsupported features stated explicitly. A native
   platform is deferred unless copy-cost evidence justifies it.
2. **Canvas/scaling:** application-selected logical canvas, example **960×540**,
   with aspect-fit point scaling. Keyboard/pointer coordinates use that canvas;
   actual output size is tracked separately. A native-size canvas remains an
   application choice; scaling does not remove full-output presentation costs.
3. **First completion:** silent library and shapes first; **audio is task 3**,
   using custom miniaudio over SDL. No null-device success fallback.

### Task 1 source-closure qualification

SDK build and the pinned recipe passed with the ordinary LLVM23.1.3/`3d69e83`
container. The selected five translation units also passed a forced whole-archive
link with SDL2 and the real SDK runtime, covering unresolved references beyond a
minimal consumer. Reached libc gaps were `asinf`, `fminf`, `fmaxf`, `hypotf` and
`strtok`; their implementations are shared libc, not port-local substitutes.
The port preserves scheme roots, requires valid modification metadata and uses
real mkdir/stat. Licences for the reached bundled source closure ship beside the
archive. No compiler-container rebuild is required. This is compile/link evidence;
SDL presentation, input, timing and native performance remain task 2.

### Task 2 integration checkpoint

The [drawing reference](../development/raylib.md) describes the implemented
logical canvas, SDL presentation/input, explicit refusals and opt-in packages.
The [experiment](../development/experiments/raylib/README.md) records the baseline
and QEMU/ThinkPad checks. Task 2 stays unchecked pending vela qualification or an
explicitly accepted limit. No task 3 implementation is authorized.
