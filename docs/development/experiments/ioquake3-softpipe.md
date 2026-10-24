# ioquake3/OpenArena on softpipe

2026-10-24, task 4 of [Quake 3/Mesa](../../wip/quake3-llvmpipe.md).
Base main `0f4153cb`, ports base `a3bfc58`, userland base `1453c097`;
changed userland `434b6cb`, ports `589bbce`. Builder
`pyxis-llvm23.1.3-3d69e83`; no compiler rebuild or kernel/ABI change.

Fresh SDK, full image and ordinary ioquake3 recipe build/stage pass. The recipe
uses pinned source `87a99d18206e878a20289f05ac3c16005dcae5c3`, static opengl1,
Mesa 26.2.5 softpipe with the restored double-buffer OSMesa frontend, and
interpreted release QVMs. No JIT or external networking.

## Configuration and checks

QEMU 10.2.2, Q35/nested KVM, four host vCPUs, 4 GiB, OVMF, modern VirtIO
GPU at 1280×800, network/RNG and read-only HOST fixtures. Supported QEMU
ICH6 HDA initially captures S16LE stereo 48 kHz to a WAV. An initial ICH9-model attempt
was refused by the existing native PCIe-coherence gate; no driver bypass was
added. Compiler work is stopped during timedemo measurements. The three initial
OpenArena and matched reference samples run in one full-size mux pane;
a direct Development-space control follows separately.

The runtime fixture explicitly packages the unchanged release PK3s under its
own `app://data`, selected with `+set fs_basepath app://data`. It is not an
installed-package test. The production bundle instead declares `openarena`
and defaults to `pkg://openarena`.

- OpenArena menu and `oa_dm1` / `oa_rpg3dm2` maps render, all three release QVMs
  load, and quit returns to the shell. The actual renderer reports `softpipe`.
- SDL initializes S16LE, 48 kHz, stereo, 1,024-frame audio; captured PCM is
  nonzero. The longer session later hit the kernel's `output position observation
  unstable` guard, disabling audio until reboot. The cause is unattributed;
  the [existing guard contract](../../devices/hda.md#failure-and-retained-ownership) is
  unchanged. All three timedemos subsequently report unavailable audio and are
  **without sound**. A fresh direct-space run also hit the existing DMA-commit
  / unstable-position guards during immediate map startup and captured only
  silence. Initial nonzero output is evidence for the SDL path, but reliable
  sustained game audio and native listening remain unqualified.
- Relative motion changes the recorded view yaw from −20 to −30 degrees;
  keyboard movement, fire, focus/capture release and fresh-click reacquisition
  work. A live `major` bot joins and runs through the server game QVM. The canvas also changes from 320×240 to 640×480 through `r_mode` /
  `vid_restart`.
- The same manifest launched from Remote without display authority is refused
  before startup: `Missing required resource display` / `launch_failed`.
- The libc calendar probe checks normal and one-digit years, invalid fields,
  overflow, epoch conversion and NULL refusal. `ctime` retains real TZif-backed
  `localtime`; there is no POSIX-TZ-string fallback.

Raw captures, exact launch arguments, frame-duration files and the in-game PNG
are retained under `/shared/present/quake3/ioquake3/` and
`/shared/present/screenshots-keep/quake3-task4-openarena-rpg-640.png`.

## Measurements

Existing reference workloads: Doom `-timedemo demo1` at 320×200 and Chocolate
Quake `-nosound +vid_forcemode 4 +timedemo demo1` at 320×240, from unchanged
main port recipes. They are reference renderers, not an A/B comparison of the
same game. OpenArena uses the shipped `demo088-test1.dm_71`, opengl1,
320×240, `timedemo 1`, `cl_timedemoLog`, and `nextdemo quit`. The engine's
millisecond-resolution frame log records all 3,397 timed frame durations.

| OpenArena sample | Frames | Seconds | FPS | Median / p95 / p99 / max frame ms |
| --- | ---: | ---: | ---: | --- |
| First qualified build | 3,398 | 281.6 | 12.1 | 79 / 110 / 120 / 141 |
| Ordinary recipe 1 | 3,398 | 279.9 | 12.1 | 79 / 108 / 117 / 137 |
| Ordinary recipe 2 | 3,398 | 282.9 | 12.0 | 80 / 110 / 120 / 141 |
| Direct Development control | 3,398 | 290.2 | 11.7 | 81 / 116 / 144 / 350 |

Median throughput is 12.1 FPS, range 12.0–12.1, with the ordinary recipe
reproducing the initial build. The single direct-space control explicitly sets
`s_initsound 0`; it is not a matched presentation A/B and does not establish a
mux/direct difference. Upstream's per-frame log caps durations at 255 ms; the
350 ms direct-control maximum comes from the engine's separate full-width
maximum. Its p95/p99 fall below that cap. In the same supported-HDA mux guest, three reference
runs give Doom 1,363.6 FPS median (1,353.2–1,363.6) and Chocolate Quake 834.4
(826.6–847.1). Initial references from the refused ICH9-audio configuration
are kept raw but excluded from these matched-configuration ranges.
These are nested-QEMU software-rendering results, not native performance or a
claim that Ship of Harkinian is suitable for softpipe. Native qualification,
retail data, installed-package integration, llvmpipe and JIT remain unrun.
The host packer/checker accepts the private qualification package (91 files,
438,641,664 archive bytes). It has not been installed or published publicly.
Publication is pending preferred-source/codec-notice closure.
