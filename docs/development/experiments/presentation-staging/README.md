# Presentation RAM staging

Measured on 2026-10-09 for step 1 of the accepted
[presentation-timing plan](../../../wip/presentation-timing.md): boot and Bochs
frames compose in a RAM staging frame
([behaviour](../../../kernel/display.md#ram-staging)).

## Method

**Timing patch.** A local patch, not committed, timed every presented frame
with the monotonic clock. It is `/shared/present/timing.py TREE`, applied
identically to both builds.
- **Composition:** from a successful frame begin until the frame end is
  entered: navigation, graphics or TTY, overlays and the software pointer.
- **End:** from there until after the store fence. On boot and Bochs that is
  the staging copy, or nothing on main. On VirtIO it is transfer, flush and
  pointer.
- **Reporting:** every 600 frames (10 s) the patch logs P50/P95/max.
- **Clock cost:** nested QEMU has no usable TSC, so each figure includes about
  35 µs of HPET reads. That is the whole end phase on main's boot and Bochs.

**Configuration.**
- **QEMU:** 10.2.2 with the local AHCI fix, nested KVM on the development VM,
  q35, 4 CPUs, 8 GiB, 1280x800, display off.
- **Builds:** main `c1e5f3b` against this branch.
- **Bochs:** used `DISPLAY_SIZE=1280x800`. A 1024x768 run confirmed the
  bochs-display device drove the screen.
- **Workloads:** each build and backend took the last three 10 s summaries of
  two workloads:
  - the Development shell idle at its prompt with the caret visible;
  - `quake` playing its attract-mode demos.

## Results

P50 / P95 / max in ms; ranges cover the three summaries.

| Backend, workload | Main composition | Main end | Staging composition | Staging end |
| --- | --- | --- | --- | --- |
| Boot (std VGA), idle | 0.31–0.32 / 0.51–0.53 / 0.91–1.66 | 0.035 / 0.12 / 0.16–0.53 | 0.31–0.32 / 0.53 / 1.09–1.40 | 0.26 / 0.42–0.43 / 0.80–0.93 |
| Boot (std VGA), Quake | 0.27 / 0.50–0.51 / 0.82–1.60 | 0.035 / 0.12 / 0.21–1.46 | 0.27 / 0.49–0.52 / 0.87–1.31 | 0.25 / 0.42–0.45 / 0.65–1.40 |
| Bochs, idle | 0.32 / 0.52–0.54 / 0.97–1.43 | 0.035 / 0.12 / 0.15–0.30 | 0.28–0.31 / 0.48–0.54 / 1.10–1.59 | 0.22–0.25 / 0.39–0.44 / 0.75–1.25 |
| Bochs, Quake | 0.27 / 0.49–0.52 / 0.96–3.35 | 0.035 / 0.12–0.13 / 0.15–0.25 | 0.28–0.29 / 0.53–0.59 / 0.93–1.60 | 0.26–0.27 / 0.43–0.45 / 0.84–1.56 |
| VirtIO, idle | 0.26–0.30 / 0.49–0.51 / 0.66–1.40 | 0.54–0.56 / 0.92–1.10 / 2.32–4.20 | 0.29–0.30 / 0.51–0.52 / 0.63–1.85 | 0.57–0.67 / 1.03–1.09 / 1.92–2.66 |
| VirtIO, Quake | 0.27 / 0.50–0.54 / 1.01–1.36 | 0.57–0.59 / 1.03–1.12 / 2.02–3.92 | 0.28–0.31 / 0.56–0.90 / 1.50–1.86 | 0.56–0.69 / 1.09–1.26 / 2.29–3.42 |

- **Boot and Bochs:** composition is unchanged. In QEMU the "scanout memory"
  is ordinary guest RAM, so composing into it costs the same as into staging.
  The new end phase is the 4 MB copy, about 0.22 ms at P50 after the clock
  reads. It takes the frame from about 0.35 to about 0.57 ms at P50, out of
  16.7 ms.
- **VirtIO:** no code change. The differences are run-to-run spread.
- **Natively,** composition into write-combining memory and the copy should
  both look different. That is the owner's run below.

**Checks** (QEMU, this branch):
- **Quake:** its demos rendered correctly through staging, and the boot and
  Bochs shells looked normal.
- **Capture:** `screenshot` captured the staged boot framebuffer correctly,
  pointer included.
- **Panic:** with the presenter stopped by GDB inside a staged frame's end, a
  `panic` call drew its message on the front buffer through the unchanged
  direct target.
- **Allocation failure:** not induced; that fallback path was reviewed in code
  only.

## Native steps for the owner

On the ThinkPad, wired and on AC, at the panel's native mode. Use two PXE
builds:
- main `c1e5f3b` with the timing patch;
- this branch with the same patch.

Apply it with `/shared/present/timing.py TREE` before `make -j16 image`; I can
stage each build in turn.

1. **Idle:** leave the Development shell idle at its prompt for 40 s, then run
   `log` and note the last three `present-timing` lines.
2. **Moving Quake:** run `quake` and let the demos play, or play `e1m1` while
   turning steadily. After 40 s, quit and note the last three
   `present-timing` lines.
3. **Tearing:** during steady turning in ordinary Quake, take a short camera
   clip on each build, slow motion if the phone has it, and note whether a
   horizontal tear line still appears.

**What to expect.** Staging should remove partially drawn overlays from the
panel, such as a half-updated caret, pointer or navigation bar. It doesn't
synchronize the copy with scanout, and Quake still renders into a buffer the
presenter reads, so a tear line may well remain. That is what the
completed-frame handoff and the later timing steps address.

## Limits

- Staging costs 7.91 MiB at 1920x1080 and one extra full-frame copy per
  frame.
- The QEMU figures don't predict the native copy into write-combining scanout
  memory.
- No damage tracking: every frame copies the whole screen, as before.
