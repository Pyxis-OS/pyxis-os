# Native ImGui slice (2026-10-24)

Initial qualification base `e2fa2694`, userland `f3fa4b5a`, ports base `a3bfc58a`, ImGui
`4806a1924ff6181180bf5e4b8b79ab4394118875` (1.91.9b-docking), SDL 2.32.10,
Mesa 26.2.5. Existing `pyxis-llvm23.1.3-3d69e83` builder; host Meson/Ninja/Mako
prerequisites installed in the disposable container, no compiler rebuild.

Baseline captured before port edits: ordinary `make -j16 image DISTRIBUTION=full`
passed at `a9b9b0d9` with userland `7eebc915`. Compiling the unmodified core and
built-in backends against that SDK failed at `sys/wait.h` (default shell helper)
and `dlfcn.h` (default GL loader). Thus there is no runnable ImGui timing baseline.
The subsequent main changes concern Git; final qualification uses the base/pin
above. No kernel, libc or existing SDL behavior is changed by this slice.

Native-profile compilation and ordinary full image pass. Core and separate
backend prefixes expose pkg-config metadata and relocatable CMake targets.
An independent CMake application linked using `ImGui::SDL2OpenGL3` with separate
prefixes in `CMAKE_FIND_ROOT_PATH`; it did not require manual include/link flags
beyond its own GL prototypes definition. MIT/font/stb and CC-BY range notices,
profile provenance and source accompany the archives.

Interactive nested QEMU: Q35/KVM, host CPU, four CPUs, 4 GiB, modern VirtIO
GPU/net/RNG, 1280×800 output. The private qualification archive added only the
asset-free demo; the shipping image/package policy does not embed it. GL 3.3
compatibility, static Mesa softpipe, MSAA off, swap interval zero. Real text
entry, pointer button activation and keyboard activation worked; count advanced
from zero to two. Context-local cut/paste preserved the edited text. The initial fixed canvas resized without a crash, but its fractional
aspect-fit enlargement made ProggyClean strokes uneven. The corrected demo
follows output geometry: native 1024×768 → 1440×900 → 1280×800, with canvas
1024×736 → 1440×868 → 1280×768 after the 32-pixel navigation bar. GDB confirms
matching canvas, IO DisplaySize, viewport and native buffer, and
DisplayFramebufferScale=(1,1). The replacement gallery is 1:1; a 4× nearest
zoom confirms uniform glyph strokes. Default ProggyClean remains 13 px,
oversample H/V=1, PixelSnapH=true, FontGlobalScale=1; GL atlas filtering is
the upstream linear default. No font change or fonts-package dependency.
The upstream demo window also rendered. Escape returned to the prompt.

GDB hardware breakpoint at `ImGui_ImplSDL2_NewFrame` (no injected calls) showed
matching IO/canvas sizes at the tested resolutions, rendered vertices, one-window
platform flags and keyboard/docking enabled. A Remote launch lacking DISPLAY
failed visibly with `Missing required resource display`, without starting a
second renderer. This is not native-machine or SoH qualification, a comprehensive
ImGui API check, a frame-rate comparison, or a system-clipboard claim.

Raw build/interaction/GDB records and private QEMU configuration:
`/shared/present/soh/imgui-task1/`. The gallery keeps an actual framebuffer.
Final package check at `088edde3`: packed all eight named core/backend/runtime
and demo development/program packages, checked the repository, installed from
an unsigned loopback repository in a matching fresh guest, and launched the
installed bare `imgui-demo` command successfully. Native `pkg-config --cflags
--libs imgui-sdl2-opengl3` produced the two backend archives, core, SDL2-osmesa,
OSMesa and C++/C runtime in dependency order, without ordinary SDL2. The final
staged demo and the interacted-with binary have identical SHA-256. Rebased onto
`b14eafb0` / userland `c0c7c159` / ports `029a712` to retain OpenArena integration;
ImGui profile and program files are byte-identical after this rebase.

No ROM or derived game assets were accessed by this task.

## Frame-time split (2026-10-24)

Measurement-only userland overlay of demo `e191882`, same SDK/runtime and
Mesa/SDL closure; no kernel change or per-frame logging. Source, hashes, raw
CSV, configurations and summaries: `/shared/present/soh/imgui-task1/timing/`.
It adds SDL performance-counter reads and explicit `glFinish`, retains the
8 ms `SDL_Delay`, stores samples in memory and prints after capture. Timer
cost is not subtracted; these are elapsed times, not scheduled CPU accounting
or shown/scanout frame counts.

QEMU 10.2.2, Q35/nested KVM, four host CPUs, 4 GiB, modern `virtio-gpu-pci`
(no virgl), native SDL2/OSMesa softpipe. Content/drawable 1280×768, viewport
and framebuffer scale 1:1, GL3.3 compatibility, MSAA off, interval zero.
Three 600-frame runs per UI backend, each after 120 warm-up frames. No debugger
stops during capture. Shared host, no CPU pinning; runs vary substantially.
One missed manual launch while idle-blanked produced no file and was discarded,
not counted as a sample.

| Elapsed ms (mean / median / p95, pooled 1,800 frames) | GTK, GL off | Headless `-display none` |
| --- | --- | --- |
| Full application frame | 37.417 / 34.835 / 57.095 | 37.658 / 35.664 / 52.167 |
| ImGui + GL rendering through `glFinish` | 26.905 / 24.559 / 45.382 | 27.243 / 25.378 / 40.780 |
| OSMesa swap/front readback | 0.309 / 0.269 / 0.563 | 0.322 / 0.274 / 0.585 |
| SDL packing and present, including submit wait | 1.690 / 1.510 / 2.999 | 1.715 / 1.536 / 3.314 |
| `display_submit` alone, included in present | 0.078 / 0.061 / 0.142 | 0.079 / 0.068 / 0.151 |
| Sleep (requested 8 ms) | 8.066 / 8.051 / 8.126 | 8.069 / 8.053 / 8.146 |

Event/output setup adds mean 0.442 ms (GTK) / 0.306 ms (none); the remaining
swap/lock/counter overhead is a few microseconds. Per-run application FPS:
GTK **28.51 / 23.92 / 28.27**, headless **28.11 / 23.87 / 28.17**. Do not
interpret the small backend difference as a speedup or regression given this
variation. GTK's `refresh-rate=120` option was refused as unexpected by this
QEMU build; headless provides the UI-refresh control instead.

The sample is dominated by the ImGui/GL render envelope, with an additional
intentional 8 ms sleep. Submission waits are far below a refresh period and
sleep is not quantized to 16.667 ms. Removing the UI backend does not remove the
low frame rate, so these results do not support a 30 Hz GUI/fence cap on the
application. The render envelope still includes native runtime and scheduling
cost; it does not alone establish intrinsic softpipe cost.

The horse Linux control used the same ImGui pin/profile and UI, 1280×768
1:1, softpipe, interval zero and the same warm-up/sample/sleep counts. Horse:
i9-12900K, Arch Mesa **26.2.3-arch3.1**, GL3.3 compatibility, SDL offscreen
(no scanout/presentation control). Raw results and script:
`/shared/present/soh/imgui-task1/linux-baseline/results-20261024T151541Z/`.

| Horse run (600 frames each) | Render ms, mean / median / p95 | Application FPS |
| --- | --- | --- |
| 1 | 49.957 / 47.119 / 78.156 | 17.21 |
| 2 | 35.061 / 31.166 / 60.657 | 23.16 |
| 3 | 24.734 / 22.447 / 39.833 | 30.48 |

Luna and other agents were building concurrently on horse. These observations
are noisy and pessimistic; hardware, Mesa version and backend also differ from
the nested QEMU runs. They do not establish a controlled Pyxis/Linux speed
ratio. The initially selected NVIDIA renderer correctly refused; the accepted
runs forced Mesa using `__EGL_VENDOR_LIBRARY_FILENAMES=/usr/share/glvnd/egl_vendor.d/50_mesa.json`
and `__GLX_VENDOR_LIBRARY_NAME=mesa`, now included in the script and README.

Together, the controls point to softpipe's own rendering cost dominating this
demo, while Pyxis OSMesa swap (~0.3 ms) and present (~1.7 ms) overhead are small
beside the ~27 ms QEMU render envelope and intentional 8 ms sleep. There is no
evidence of a 30 Hz presentation cap. This does not isolate every runtime/VM
cost or measure native vela performance; its coordinated sample remains
pending. No performance optimization made.
