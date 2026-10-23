# raylib drawing qualification

## Before-runtime baseline

Captured before SDL/rlsw runtime edits, 2026-10-22: parent `0ae18f44` with the
separate task-1 library/build integration, userland `b547d32d`, ports `f545377e`.
Task-1 code is not running in these controls. Ordinary builder
`pyxis-llvm23.1.3-3d69e83`; full SDK/image build, `DISPLAY_SIZE=1280x800 DISPLAY_FLIP_METRICS=1`. QEMU Q35, nested KVM, `-cpu max`, four CPUs, 4 GiB,
fresh OVMF, Bochs 1280x800 (size option), VirtIO-SCSI CD/RNG, no NIC/disk/audio,
headless display. Native costs are not inferred.

Existing read-only GDB counters (`display.c` metric_frames, compose_total,
copy_total) bracket three windows. `/proc/QEMU_PID/stat` deltas count whole-QEMU
host user+system CPU, not isolated guest/presenter time. GDB stays detached during
workloads. Idle is the Development prompt; it submitted no frames in the sampled
windows. The pointer control runs ordinary `pointer-min`, with 60 alternating
HMP `mouse_move 3 1` / `mouse_move -3 -1` inputs per window. Each input generated
two presentation frames (120 per window). No profiling patch or new benchmark
was added. These controls establish the pre-change configuration and spread,
not a raylib-vs-native drawing comparison.

| Control, three windows | Host CPU (% of one CPU) | Mean composition (ms/frame) | Mean copy (ms/frame) |
| --- | ---: | ---: | ---: |
| Idle prompt | 1.887–1.986 | No frames | No frames |
| Moving pointer-min | 4.443–5.574 | 0.335–0.380 | 0.296–0.357 |

Raw captures, counter brackets, hashes and baseline image remain local at
`/shared/present/raylib/task2/baseline/`. Timing is nested and includes normal
host scheduling variation. The matched after samples below repeat these controls.

## SDL integration and QEMU checks

Task 2 uses the same ordinary builder and SDK, rebased userland `4cedc2c`
(package-tool main changes only) and ports task-2 integration (head recorded in its PR). Main's intervening changes
are docs/package code; the kernel interface and rendering paths are unchanged.
Only the needed SDK/raylib targets and kernel identity/link/image assembly were
rebuilt; existing ordinary archive programs were reused. No compatibility shim,
ABI renumbering or compiler rebuild.

The selected recipe and edited shapes consumer compile/link. A relocated,
checked `raylib-dev` archive also configures and whole-archive links through its
installed CMake target. Both packages packed and passed host `pxpkg check`; in
QEMU they installed, verified and published the bundle command. Existing in-guest
TCC compiled the editable example against those unpacked development-package
headers/archives, and the result drew the scene. This is QEMU execution, not a
native ThinkPad/vela result. The package-format pass used parent `d203ee36` and
the shapes source before the additional cursor controls/exit diagnostics; the
renderer code is the same.

Functional QEMU adds a read-only HOST export, user networking and VirtIO GPU.
Visual checks passed color corners/orientation, shapes, rotating checker,
default-font text, pointer click/wheel, letterbox exclusion and near-first filled
3D occlusion. F1 sent while another tab had focus did not change the scene on
return. Native lock was true after L, false after switching away/back, and never
relocked automatically. W reached the expected real native position (500,366 in
the 1000x700 output), matching the viewport transform. A bundle without pointer
authority still drew and refused lock/warp. Missing display authority refused
Remote launch before entry. Escape returned to the prompt; read-only GDB found
zero display/pointer owners and all three display slot pointers released.

VNC's standard ExtendedDesktopSize request changed the real VirtIO GPU output
from 1280x800 to 1000x700, then 800x600. Read-only GDB confirmed
`DISPLAY_VIRTIO_GPU`, 1000x668 content/4000-byte pitch and released state afterward;
visible canvas stayed 960x540 with refitted letterbox/input. This uses installed
QEMU's normal display path, not a guest state mutation. Logical `SetWindowSize`
remains explicitly unsupported. Wider render-texture/GPU combinations are not
qualified.

## Repeated after controls and application cost

After controls repeat the exact Bochs/CPU/memory/devices and HMP pointer workload
above on the reassembled image. Three windows per case; no raylib is running in
either control. Different normal host scheduling explains the spread; unchanged
control code does not establish a raylib-induced speedup.

| Control | Before host CPU (%) | After host CPU (%) | After composition (ms/frame) | After copy (ms/frame) |
| --- | ---: | ---: | ---: | ---: |
| Idle prompt | 1.887–1.986 | 1.986–2.086 | No frames | No frames |
| Moving pointer-min | 4.443–5.574 | 3.950–4.443 | 0.318–0.334 | 0.274–0.304 |

Application samples use the functional VirtIO GPU configuration above, full
1280x800 output and the simple 2D scene, three ten-second counter brackets per
canvas. They include QEMU device emulation and ordinary host scheduling; there
is no isolated guest CPU claim. Their different display/device setup prevents a
direct subtraction of the Bochs baseline. The example is capped at 60 FPS.

| Logical canvas | Whole-QEMU host CPU (% of one CPU) | Composition (ms/frame) | Copy (ms/frame) | Presented frames per window |
| --- | ---: | ---: | ---: | ---: |
| 960x540 | 24.13–24.63 | 0.286–0.310 | 0.569–0.573 | 589–595 |
| 1280x768 | 26.31–29.18 | 0.302–0.325 | 0.574–0.659 | 590–597 |

These are a few shapes, not a fill-heavy scene or native-resolution guarantee.
The simple scene's larger canvas trades extra clear/raster work against less
scaling. No general CPU/FPS prediction follows. Raw samples, counters, hashes,
screenshots and local images remain at `/shared/present/raylib/task2/`.

The application-only preparation script fetched the bundle and printed
`RAYLIB_PREPARED` in a fresh QEMU boot using `shell tmp://raylib-prepare.pxs`.
The attended application-only packet is under `task2/native/`.

## ThinkPad native check (chronium, 2026-10-22)

Chronium ran the packet application on main `98c2773e`, using the published
ports `78bde0e` example and the SDK userland `4cedc2c` closure. He reported the
visual/input checks passed ("looked great, ran great"). Application exit lines:

| Logical canvas | Actual content output | Frames | Elapsed (s) | Average capped FPS |
| --- | --- | ---: | ---: | ---: |
| 960x540 | 1920x1048 | 2,829 | 47.219 | 59.9 |
| 1920x1080 | 1920x1048 | 1,034 | 17.251 | 59.9 |

The second run explicitly used `1920 1080`, not the printed 1048-pixel content
height: it exercised aspect-fit scaling of that canvas, not a 1:1 output raster.
These are two attended application runs, not repeated throughput/CPU samples or
a scanout-completion measurement. No general workload/FPS guarantee follows.
## Vela native check (chronium, 2026-10-22)

Vela PXE-booted main `98c2773e` at **1920x1080**, not its native 2880x1800.
Chronium reported 59.9 FPS with the default 960x540 canvas and 58.7 FPS with a
1920x1048 canvas; visuals/input were fine. Fast pointer motion briefly lowered
the displayed rate to about 55 FPS before recovery; clicks or no motion were
fine. Exact frame/elapsed counts were not supplied for these runs.

Task 2 remains unchecked while that motion cost is investigated. Vela's native
2880x1800 output, native exclusive CPU cost, wider raylib APIs and audio remain
unqualified.

## Fast-motion investigation (2026-10-22)

No code change or native rerun. Reused kernel `d203ee36b0b2`, ports `78bde0e`
and the packet application; reassembled only the local boot image. Q35 nested
KVM, four max CPUs, 4 GiB, Bochs 1920x1080, software cursor, user network,
`display.flip.metrics=1 display.cursor.probe=1`; 960x540 canvas. HMP alternated
relative motion at 125 or 1,000 submissions/s. Read-only GDB bracketed existing
counters and detached during six-second windows; `/proc` supplied whole-QEMU CPU.
Three windows per case. A same-binary control omitted the optional pointer grant
while the native cursor still moved. Its quiet comparison uses two windows: a
third had an extra GDB inspection and is excluded.

| Case | Delivered motion reports/s | Whole-QEMU CPU (% one CPU) | Presentations/s | Compose / copy (ms/frame) |
| --- | ---: | ---: | ---: | ---: |
| Pointer grant, stationary | 0 | 28.61–29.25 | 58.89–59.32 | 0.55–0.57 / 0.53–0.56 |
| Pointer grant, 125 submissions/s | 123.5 | 35.73–37.22 | 58.62–58.96 | 0.56–0.68 / 0.55–0.67 |
| Pointer grant, 1,000 submissions/s | 981.5–983.5 | 66.70–70.80 | 57.47–59.59 | 0.67–0.90 / 0.64–0.75 |
| No pointer grant, stationary | 0 | 27.78–28.60 | 59.01–59.33 | 0.55 / 0.52–0.53 |
| No pointer grant, 1,000 submissions/s | 982.2–983.6 | 57.81–60.62 | 59.13–59.74 | 0.67–0.68 / 0.64–0.67 |

This reproduces a rate-dependent cost and some presentation loss, not vela's
exact transient 55-FPS application result. QEMU PS/2/device emulation contributes;
these are sequential controls with host variation, not a native cost attribution.
Frame counts remain near the fixed presenter rate, not one frame per report.
The control still incurs native routing/cursor work but omits app event delivery
and SDL processing, supporting that path as an additional cost.

Source inspection finds one native `POINTER_READ` and one `POINTER_GEOMETRY`
for each ordinary event in SDL's `SDL_pyxisevents.c`. The geometry operation
parks the caller through `command_pointer` and the BSP request handoff. Raylib's
per-event relative-mode query only reads SDL state. The kernel event queue
coalesces compatible input only at its 64-entry capacity. Cursor movement can
force an otherwise skipped full presentation, but not another presentation
inside the same fixed tick.

The implemented SDL fix lazily acquires geometry once per pump/event identity,
invalidating on every state boundary and retaining all per-event identity/extent
checks. Failed queries are cached only for that pump/key too. All input events
remain in order; there is no new motion coalescing or kernel change. An event
already dequeued can use that pass's snapshot until its boundary is consumed;
the cache does not dereference pixel mappings or authorize spatial operations.
Raw investigation samples/configuration remain under `motion-qemu/` locally.

## Geometry reuse: matched QEMU A/B

Both binaries use SDK userland `2a0bb3d` and identical raylib code/configuration;
A uses ports `78bde0e`, B adds SDL change `3ea94fe`. Ordinary SDK, SDL and example
builds passed. Both run on the same rebuilt kernel `94a67fbd` (main `8bf17822`
plus the raylib documentation/integration stack), with the same configuration
as above. Only needed SDK/library/application/kernel targets were rebuilt;
archive programs were reused. Alternate A/B three times in one boot, a quiet
and 1,000-submission/s six-second window per cohort. Existing counters confirm
roughly 982 delivered reports/s. GDB is detached during these windows.

Values are median [minimum, maximum], three windows each:

| Case | CPU A (% one host CPU) | CPU B (%) | Presentations/s A | Presentations/s B |
| --- | ---: | ---: | ---: | ---: |
| Stationary | 29.59 [28.93,29.91] | 29.26 [29.11,30.51] | 58.85 [58.51,59.50] | 58.39 [58.20,59.70] |
| Fast motion | 64.73 [62.71,70.74] | 66.64 [64.22,69.64] | 58.89 [58.48,59.09] | 59.07 [58.78,59.28] |

The ranges overlap: no demonstrated whole-QEMU speedup; native confirmation follows below.
Fast-motion elapsed composition/copy ranges were A 0.69–0.82 / 0.66–0.73 ms,
B 0.88–0.92 / 0.71–0.78 ms; these include scheduling/preemption, and no
compositor code changed. Device emulation and other per-report work remain.

A separate, timing-distorting GDB breakpoint count confirms query reuse:
A made 602 geometry queries over 126 presentations; B made 159 over 158.
Delivered reports differed (1,681 / 2,446) because breakpoint stops affect
processing/coalescing; these counts are not a throughput or CPU comparison.
B is near one geometry query per presentation rather than repeated queries.

The actual B packet fetched/prepared/launched in QEMU. Separate VirtIO GPU
checks passed click/wheel, 2D/filled-depth 3D, lock then focus revocation without
relock, real output resize to 1000x700 with logical canvas unchanged, native
centre warp to (500,366), and Escape releasing both owners (read-only GDB).
The native confirmation packet is `/shared/present/raylib/task2/native-motion/`:
repeat vela's earlier 1920x1080 PXE check and fast-motion observation. The agent
performed no native staging/action; chronium's subsequent result follows below.
Raw samples, binary/image hashes, debugger counts and captures stay in
`/shared/present/raylib/task2/motion-ab/`.

## Vela confirmation accepted (chronium, 2026-10-22)

On the same PXE main `98c2773e` image at 1920x1080 output, using a mouse and the
`3ea94fe` SDL geometry-reuse candidate built with the matched SDK `2a0bb3d`,
chronium reported 60 FPS at 960x540 and 59.4 FPS at 1920x1048. Visible hiccups
under extreme mouse motion or scrolling were much rarer, about once every
20 seconds. He accepted this as satisfactory and requested no further work.
Keep the geometry-reuse change; task 2 is complete with this recorded limit.
These are attended observations, not timed repeated cohorts or CPU attribution.
Vela 2880x1800 output and native exclusive CPU cost remain unqualified.

The final integration also declares precise clock authority required by current
main. Historical measurements and native confirmation remain tied to the kernel,
SDK and grants named above; they are not measurements of later clock changes.


## SDK-view migration (2026-10-23)

Parent `8bc5af9e` includes main `69cceab3` and directory-identity fix #1147;
userland `6de2b65f`, ports `913e115c`. Ordinary `3d69e83` builder, fresh selected
SDK/kernel, SDL2/raylib/pkgconf and trusted startup/Remote/package helpers.
No compiler-container rebuild. The packages are separate ABI-bound `dev`
packages: SDL2 99 files / 7,888,384 bytes; raylib 13 / 4,096,000; the silent
shapes program 23 / 1,247,744. Host pack/check passes. Notices, covered backend
source and configuration remain package-local, outside the SDK view.

Q35 nested KVM, four max CPUs, 4 GiB, OVMF, VirtIO GPU 1280x800, VirtIO network,
PS/2 input and QEMU HDA/WAV output. A live boot installed the three packages
plus published `clang-hosted` `785717a1...`, `pyxis-dev` and `libcxx-dev`
`1+fecca0ee87b0`. The image alone declared the tested revision and `fecca0ee87b0`
as provided ABIs to consume those published tools, as in #1147's check; no
compatibility declaration is committed. The named SDL2 dependency was selected
automatically. Without a reboot, headers and archives appeared under `sdk://`,
and the example/manifest were read from `pkg://raylib-dev/share/raylib/examples`.

```text
pkg-config --cflags --libs raylib
clang tmp://shapes.c -lraylib -lSDL2 -o tmp://sdk-shapes.pxe
```

pkg-config emitted `-Isdk://include/SDL2 -D_REENTRANT -lraylib -lSDL2`.
The packaged example compiled/linked with no `-I`, `-L` or sysroot override;
a second compile added `<SDL2/SDL.h>` alongside its `<raylib.h>` and also passed.
The published command is `clang`; there is no `cc` alias in that package.
The generated binary, wrapped with the unchanged packaged example manifest,
drew shapes/textures/text, switched to 3D and accepted pointer input. Escape
returned to the shell; read-only GDB found display/pointer owners and all
three display slots null. Package verification passed. These are functional
QEMU checks, with no new native/performance claim; earlier TCC checks do not
qualify this view. Raw outputs remain in
`/shared/present/raylib/sdk-view-20261023/`.
