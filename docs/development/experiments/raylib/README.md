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
host scheduling variation. Runtime qualification and matched after samples are
not yet recorded.

## Task 2 correctness checkpoint

Work paused by chronium, 2026-10-22, before task 2 delivery. The patched recipe
and shapes consumer compile/link. On the same kernel/SDK in a separate functional
QEMU boot (VirtIO network and read-only HOST export added), manual checks showed
correct color corners/orientation, shapes, texture, built-in font, aspect-fit
letterbox and pointer coordinates. Two overlapping filled cubes drawn near-first
showed the farther cube correctly occluded. F1 sent while another tab had focus
did not change the scene on return. Pointer click changed the indicator; the
letterbox excluded it. Escape returned to the shell; read-only GDB confirmed
zero display/pointer owners and all three display slot pointers released.
Remote launch without display authority was refused before entry.

This is not final qualification. Actual output resize, remaining input/refusal
cases, matched after-cost samples, package replay/install, exact-head CI and
native ThinkPad/vela checks remain. No native packet was staged or run. Raw
captures and GDB output stay under `/shared/present/raylib/task2/qemu/`.
