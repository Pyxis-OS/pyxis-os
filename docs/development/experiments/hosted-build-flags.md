# Hosted build flags: pre-decision comparison

2026-10-25, nested QEMU investigation for the
[hosted-flags proposal](../../wip/hosted-build-flags.md). SDK and recipe defaults
are unchanged; these are temporary target-compiler overrides, not an implemented
SDK split or native qualification.

## Inputs and method

Parent `c02a2bfae996` (0.0.9), userland `81ee85f9b9b3`, ports `4d07f4c1cf95`;
existing Clang 23.1.3 builder `pyxis-llvm23.1.3-3d69e83`. Fresh baseline SDK/runtime
archives are held fixed. Target compiler wrappers append only `-fhosted` for B;
Mesa additionally has explicit O2/O3 wrappers. All other flags, CPU target,
upstream math policy and SDK header precedence remain fixed. Host generators use
native tools. Builds finish and their containers stop before timings start.

A/B source builds cover SDL2, Mesa, ioquake3, Lua, Mbed TLS, picohttpparser,
sbase and native userland programs. C++ is already hosted: ImGui's identical C++
objects are relinked against the four SDL/Mesa profiles. The public hosted Clang
and pure-Go Git binaries are identical A/B controls, not rebuilt LLVM or predicted
C-mode gains. SHA dispatch/library objects are also held fixed; the sbase caller
is rebuilt A/B. A later implementation must repeat checks with rebuilt SDK
libraries.

q35/KVM, `-cpu host`, four CPUs, 4 GiB, modern VirtIO network/RNG/GPU, UTC RTC;
960x752 display with 960x720 drawable area. OpenArena uses 320x240 at integer 3x;
ImGui is 1:1. One guest and one timed workload at a time. No native/PXE action.
The live fixture combines the public 0.0.8 archive with current kernel, programs,
configuration and genuine current release metadata (both supported boot ABIs);
public ABI header diff is empty. This is an explicitly mixed qualification image.

Ordinary Lua `pyxis.monotonic_ns`/`pyxis.run` measures existing tools. OpenArena's
shipped `demo088-test1` supplies its own complete 3,398-frame timedemo and frame
CSV; ImGui uses its existing FPS HUD. No native benchmark API is added. Raw logs,
compiler probes, argv, fixtures and host scripts stay under
`/shared/present/hosted-flags/`.

## Render matrix

Three interleaved complete timedemos per profile; one setup/warmup run discarded.
All measured runs exited zero and returned 3,398 frames.

| Profile | Seconds, median [min–max] | FPS from median time | Frame p95, ms |
| --- | --- | --- | --- |
| Current C, O2 | 274.5 [274.0–275.7] | 12.38 | 107 |
| Hosted C, O2 | 258.4 [258.1–260.4] | 13.15 | 101 |
| Current C, O3 | 277.7 [276.0–283.9] | 12.24 | 109 |
| Hosted C, O3 | 256.3 [255.6–261.4] | 13.26 | 100 |

Hosted O2 takes 5.9% less engine time than current O2 (6.2% more FPS).
Hosted O3 takes only 0.8% less time than hosted O2, with overlapping ranges;
current O3 is slower than current O2 here. This supports explicit O2 pending
native evidence, not an automatic release-O3 speedup. These compare the combined
port C-mode changes, not Mesa in isolation. Frame percentiles pool each profile's
three integer-millisecond CSVs; rounded engine FPS is not used to calculate ratios.

ImGui FPS HUD, three independent launches per profile, 30 seconds after focus
preparation; median [min–max]: current O2 **30.4 [30.4–30.4]**, hosted O2
**31.8 [31.7–32.0]**, current O3 **30.5 [30.3–30.6]**, hosted O3
**31.8 [29.2–32.5]**. All twelve exit receipts are zero and screenshots are
retained unedited. These are rolling HUD readings, not complete frame-time logs;
the low hosted-O3 point is retained. No stable O3 advantage appears here either.
An invalid QMP typing attempt never launched the demo and is excluded.

## CLI controls

Three interleaved A/B samples per row, milliseconds; medians [min–max].

| Workload | A: current | B: hosted |
| --- | --- | --- |
| Package install, 32 MiB | 4043.14 [4037.68–5491.25] | 4046.90 [4040.77–4093.58] |
| Package verify, 32 MiB | 226.43 [225.91–566.69] | 226.95 [226.57–227.35] |
| Clang compile, unchanged binary | 2945.26 [2930.81–3864.11] | 2960.31 [2954.47–2975.53] |
| Git HTTPS clone, unchanged binary | 116.66 [111.69–116.95] | 111.13 [109.37–114.04] |
| SHA-NI, 64 MiB | 57.84 [57.00–86.28] | 58.48 [57.93–58.59] |
| Portable SHA, 64 MiB | 167.44 [167.18–168.55] | 164.21 [164.14–191.21] |

No material package/SHA gain appears with the runtime held fixed. The unchanged
Clang/Git controls vary too; their ratios are not effects of C-mode changes.
First A samples include larger outliers, retained in the ranges rather than
silently removed. Package installation uses six never-installed
versions of identical 32 MiB/128-file public data, after a version-zero warmup.
Install includes download, verification, extraction and set publication; it is
not an isolated decompression measurement. `pkg verify` is measured separately.
The test harness declares package authority and runs each exact native program
entry; no caller manufactures grants.

Clang compiles a public 10,000-function C input with `-O2 -c` and checks identical
object hashes. Git clones a public 256-file local HTTPS fixture with a synthetic
fixture CA. Git uses its normal bundle declaration: a raw entry does not
implicitly inherit its private service constructor. The binary is byte-identical
to the published 0.0.9 Git package. Initial wrong-destination
and missing-constructor fixture attempts failed and are excluded, not counted
as slow or successful clones. All six final clones and compiles succeeded;
compile object hashes match. SHA checks identical 64 MiB input/hash with SHA-NI
enabled, then in a
separate guest with CPUID SHA-NI disabled for portable dispatch (confirmed in
the QEMU CPU-model record). Both modes returned the expected digest in all runs.

## Attribution and limits

Static object relocation counts support the expected hosted mechanism: at O2,
Mesa's TGSI executor loses all eight `sqrtf` and eight `fabsf` call sites, and its
sampler loses two `sqrtf` and 60 `fabsf` call sites. Small-copy calls also decrease.
`floorf` remains a libcall for the selected baseline ISA despite intrinsic IR.
These counts are not runtime profiles. O3 increases hot-object text size; that
alone does not establish an instruction-cache cause for a timing difference.

The earlier attended vela/Fedora SoH observations (5.2/6.2 FPS) used different
scene presentation and are not a matched gap attributable to compiler flags.
SoH and the selected defaults still need coordinated native vela A/B with the
same scene/save, resolution, frame cap, audio and CPU-power policy. No private
assets or settings were inspected here. Nested results do not establish native
speedups or CPU-placement effects.
