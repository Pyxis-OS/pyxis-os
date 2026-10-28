# llvmpipe qualification

## Attended vela check, 2026-10-27

Chronium ran the private live-RAM packet from parent
`d99630dc3dfaf899b22d05ef8802865f8ced9a40`, with compiler/container LLVM
`2b3a3a41df746cc0ee3d4bfccc5dba495d232f0e`. Development had JIT authority;
`GALLIUM_DRIVER=llvmpipe` was explicit and `LP_NUM_THREADS` unset, preserving
Mesa's default worker selection. Kernel SHA-256:
`12b1752d896abece8a7f4b68938d627d87435e37c10175092ddd4a4d6754fcbb`.
The packet and checksums remain in
`/shared/present/llvmpipe/vela-attended-20261027/`.

Chronium reported OpenArena above 60 fps and SoH at 45–60 fps at 1080p
(he selected a 40 fps cap), responsive DualSense input and working saves.
He described SoH as fully playable. These are attended observations, not timedemo
samples or an NVMe persistence check: this packet used RAM-backed home storage.
Tearing was visible on vela. No display-driver change is included here.

OpenArena used the public 0.8.8 assets in an explicit qualification bundle;
SoH used the previously staged private assets and a same-compiler relink.
Neither is a package-install check. The later merged script-authority change
(#1542, userland #416) preserves the shell's held launch sources, including JIT,
for `.pxs` scripts. The llvmpipe stack includes that change.

The later native comparison below uses the same trimmed OpenArena demo and
upstream default workers on Pyxis and Fedora. The refreshed container
uses LLVM `8bdf1360b8c47640c92eeeb515ee4a08814987e6` for both compiler and
libraries, retaining the JIT adapter and merged append fixes; last night's
native observations do not qualify that new container.

## Trimmed demo playback, 2026-10-28

A prefix of 1,800 whole messages from OpenArena 0.8.8's shipped
`demo088-test1.dm_71`, followed by its ordinary `(-1,-1)` end marker, played
and quit normally once in four-vCPU nested KVM (`host,+invtsc`, 4 GiB,
1280×800 VirtIO display). The source SHA-256 is
`a939cef30d41e78d552785cfd6f0f9f6a42defe05fdfbf6dd84bd943b49c718b`;
the trimmed file is
`c02c5a071f520939a33dc00ffd05c8e4f7b9c190b85e9289203cab685c1d8d9f`.
It retains an unchanged 599,193-byte prefix plus the 8-byte trailer.

The check used the coherent d996/2b image above, default Mesa workers,
`r_mode 0` (320×240), `timedemo 1`, `nextdemo quit`, and `demo bench`.
The renderer reported `llvmpipe (LLVM 23.1.3, 256 bits)`; playback completed
1,803 timed frames in 18.3 s. This single nested playback check establishes
demo/quit correctness, not the new 8b-container build or native throughput.
The native pair below uses identical demo bytes and default workers; actual
wall time is machine-dependent. Evidence:
`/shared/present/llvmpipe/timedemo/`.

## Published-container qualification, 2026-10-28

The rebuilt stack uses compiler container
`git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-8bdf136`, image
`baeaa0e204f54a6a90ade6cd57e88476a170641b33f47cd57df90162ed114cee`.
Compiler, SDK compiler provenance and Mesa's copied target-library metadata
all identify LLVM `8bdf1360b8c47640c92eeeb515ee4a08814987e6`. Target libraries
have threads enabled and RTTI/EH disabled. Their bootstrap SDK is published
Pyxis `a18140067e5fb97a2bd15625d8c5c061f9c1f23e`, including native append.
Ordinary image builds consumed the container prefix without building LLVM.

Parent `1adcae9272c68d546143c485000f608e692bc7e6` included main's script
launch-source fix, userland `2a63b32de4109f6ba0952b240544e718130e569c`
and ports `bf08f5da3c955098d8eddf6b76dff7f20275c31e`. Fresh SDK/Mesa, SDL, ImGui and ioquake3 builds passed; both core and full live archives assembled.
The final ports follow-up only replaces ImGui's stale hardcoded softpipe label
with renderer-neutral wording; its stderr already reports the actual renderer.

Four-vCPU Q35 nested KVM, `host,+invtsc`, 4 GiB, VirtIO 1280×800 display,
RAM roots: nextafterf boundary checks, llvmpipe default/zero/four-worker triangle,
softpipe without a declared JIT grant, explicit missing-JIT refusal, and `.pxs`
JIT inheritance all passed. These successful triangle files share SHA-256
`21bb67e3254d075dc512ef1308e4ea2e1c6bfe7b9e8363ccbdc6cb62226d47b2`.
With AVX and AVX2 hidden, llvmpipe reported 128-bit vectors and the same checks
passed; ordinary host exposure reported 256-bit vectors. ImGui rendered and
reported `llvmpipe (LLVM 23.1.3, 256 bits)`.

The same 1,800-message demo ran once per worker setting through a `.pxs`
launcher and quit normally, with no LLVM or renderer source changes:

| Workers | Frames | Seconds | FPS | Minimum / mean / maximum / standard deviation, ms |
| --- | ---: | ---: | ---: | --- |
| Mesa default | 1,803 | 17.0 | 106.3 | 4.0 / 9.4 / 555.0 / 8.5 |
| `LP_NUM_THREADS=0` | 1,803 | 25.7 | 70.1 | 6.0 / 14.3 / 566.0 / 8.5 |
| `LP_NUM_THREADS=4` | 1,803 | 16.5 | 109.1 | 4.0 / 9.2 / 540.0 / 8.4 |

Each sample used opengl1, 320×240 and a fresh game process, with no disk shader
cache. These single shared-host nested samples are not a controlled scaling
study or a native comparison. An uppercase-key encoding error in the host QMP
helper interrupted setup after the default sample; zero/four resumed in a fresh
guest with corrected key encoding. No successful sample was repeated. The
native Pyxis/Fedora pair below uses default workers on both systems.
Small evidence is under `/shared/present/llvmpipe/qualification-20261028/`;
build trees and test images remain VM-local.

## Native vela complete-stack comparison, 2026-10-28

Chronium and the orchestrator ran one sample on each OS, on AC. Same vela,
unchanged OpenArena 0.8.8 assets, upstream ioquake3 commit
`87a99d18206e878a20289f05ac3c16005dcae5c3`, the trimmed demo above,
320×240, opengl1, interpreted QVMs and Mesa default workers. Both completed
1,803 frames and exited normally. Pyxis used its normal authored Development
services/`.pxs` chain; the Remote collector only read the results. Fedora ran in
chronium's Plasma Wayland session with software llvmpipe forced, persistent shader
cache disabled, `LP_NUM_THREADS` unset and a fresh game home.

| Metric | Pyxis | Fedora |
| --- | ---: | ---: |
| Engine elapsed, s | 22.2 | 16.3 |
| FPS | 81.2 | 110.4 |
| Frame minimum / mean / maximum / standard deviation, ms | 6.0 / 12.3 / 493.0 / 8.3 | 3.0 / 9.1 / 380.0 / 7.9 |
| Logged frame median / p95 / p99, ms | 11 / 17 / 30 | 8 / 15 / 18 |
| Logged frames over 16 ms | 124 | 27 |
| Mesa | 26.2.5 | 26.2.5-1.fc44 |
| LLVM reported by renderer | 23.1.3 | 22.1.8 |
| llvmpipe vector width | 256 bits | 256 bits |
| Display path | OSMesa, CPU scale to full Pyxis window | Native SDL/GL, 320×240 Wayland window |

Pyxis reached 73.6% of Fedora's throughput, with 36.2% more elapsed time.
These are single native samples, not ranges or a kernel-only A/B. Besides the
LLVM version and display path, the host compiler differs: Pyxis Clang 23.1.3,
Fedora test executable GCC 14.2.0. Both used SDL audio; the backends were Pyxis
and PipeWire. The actual physical Pyxis presentation extent was not captured
by this result, so 320×240 is the shared rendering canvas, not a claim of equal
presentation work.

Pyxis boot evidence reports HWP applied on all 16 CPUs without failures and
XSAVE `XCR0=0x7`, 832 bytes on 16 CPUs. Its configured EPP is 128. Fedora
allowed 16 CPUs, with `intel_pstate`, the `powersave` governor and
`balance_performance` on every CPU. AC was confirmed by chronium and Fedora's
captured power-supply state. Neither sample measured APERF/MPERF or the native
CPU residency of each worker; these settings do not prove equal effective clocks
or P/E-core placement.

The raw frame logs contain 1,802 durations each and cap individual entries at
255 ms; each has one capped entry. The independent engine maxima above retain
the uncapped 493/380 ms values. The medians and tail counts show that the gap
persists beyond that one large startup frame. No successful native sample was
repeated.

### Attribution limits and next measurements

The first concrete candidate to measure is presentation, because its extra work
is visible in the implementation: SDL's `PYXIS_GL_SwapWindow` calls
`OSMesaSwapBuffers` then `PYXIS_PresentPixels`; the latter clears the full output
slot and CPU-scales/channel-converts the small canvas before `display_submit`.
See `ports/sdl2/pyxis/SDL_pyxisopengl.c` and `SDL_pyxisvideo.c` in the pinned
ports source. Fedora presented a small Wayland window instead. Time those phases
separately before attributing any number of milliseconds or changing the path.
The BSP presenter then composes/copies on its own cadence; this sample has no
phase metrics separating its cost from llvmpipe.

Hybrid-core scheduling is another plausible contributor: `place_locked` in
`kernel/task.c` compares runnable load and idle SMT siblings, without a P/E-core
performance class. There is no worker-residency or per-core clock trace here to
establish the size of that effect. HWP is active, so the old disabled-HWP failure
is not demonstrated by this result; effective frequency still needs measurement.

JIT W^X protection/invalidation cost and LLVM 23-versus-22 code generation also
remain unmeasured. There are no JIT transition counts/times to assign the gap
to them, and the ordinary-frame difference rules out explaining everything as
one initial compile stall. JIT permission transitions are separate from display
page flips: vela currently uses the unsynchronized firmware-framebuffer copy
path, not the qualified Renoir hardware-flip backend. The observed tearing is
consistent with that path; adding llvmpipe double buffering does not synchronize
the final scanout copy.

Evidence: `/shared/present/llvmpipe/vela-timedemo-20261028/results-vela-pyxis/`
and `results-vela-fedora/results-20261028T103204/` beside it, with the exact
commands, renderer logs, frame logs and CPU-policy capture. The immutable
packet's provenance records kernel/base-program build
`1adcae9272c68d546143c485000f608e692bc7e6`, userland `2a63b32`, ports
`bf08f5d` plus the separately rebuilt cosmetic ImGui `4985ae0` follow-up; later
integration changes were documentation. Fedora's public-only packet includes
the unmodified source archive and records its compiler/configuration.
