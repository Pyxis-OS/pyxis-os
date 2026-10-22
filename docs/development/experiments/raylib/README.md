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
Vela, native exclusive CPU cost, wider raylib APIs and audio remain unqualified.
Task 2 remains unchecked pending vela or an explicitly accepted limit.
