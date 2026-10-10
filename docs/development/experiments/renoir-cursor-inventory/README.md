# Renoir cursor inventory and software baseline

Task 1 of the [accepted cursor track](../../../wip/renoir-hardware-cursor.md),
2026-10-10. Read-only inventory and optional software counters are implemented;
native register evidence and motion costs await one owner-run boot. No cursor
writes, cursor backing allocation or new GPU write allowlist is implemented.
Existing opt-in flips still perform their qualified writes.

## Inputs and build

Fresh main `f4a79b69` after #665. Before code edits, a full source image with
`LOG_LEVEL=info LOG_UDP=1 DISPLAY_FLIP=1 DISPLAY_FLIP_METRICS=1
 DISPLAY_TIMING=off` was built and sealed in `build/cursor-baseline-main/`.
Pins: userland `6e12b5f4`, ports `7c33f3ff`, fs `b427df29`, lwIP `a1aadb91`.
Compiler: existing LLVM23.1.3 builder, no compiler rebuild or dependency change.

Code commit `2c2cfcba` adds the opt-in probe. The local native set is
`build/cursor-task1-native/`, with kernel/initrd/config, REVISION, INPUTS and
SHA256SUMS. It reuses verified unchanged SDK/userland/ports from the baseline;
the kernel and AMD license payload are rebuilt. These sets are not PXE staging.
The subsequent documentation commit does not change probe code. Luna may build
the submitted PR head instead; record its actual revisions and hashes.

```sh
make -j16 image LOG_LEVEL=info LOG_UDP=1 DISPLAY_FLIP=1 \
  DISPLAY_FLIP_METRICS=1 DISPLAY_TIMING=off DISPLAY_CURSOR_PROBE=1
```

`DISPLAY_CURSOR_PROBE=1` adds `display.cursor.probe=1`. Default zero adds no
probe output. The flag enables generic compose/copy counters and, only after
qualified flip preparation and full revalidation, reads the active route's
cursor inventory. Absent/refused Renoir reports unavailable without cursor
register reads. No global trace logging is needed.

## Inventory and source conclusions

Before AP startup, two read-only samples record all four inherited HUBP cursor
controls, addresses, size, position, hotspot, stereo, fetch scheduling and power;
DPP cursor/clock controls; OPP cursor locks; selected DPP MPC pending/taken/ACK;
and active OTG update, VSTARTUP, VUPDATE, keepout and position. Instance tables
are distinct. Only with the probe is BAR5 page zero additionally mapped read-only
UC for REFCLK/DP DTO; cursor pages use the existing retained mappings. The
existing three-register flip writer allowlist is unchanged.

Numeric ATOM dce_info v4.1–4.3 crystal metadata is copied from the validated VFCT
image before paging, with bounded table reads. No raw BIOS, EDID, serial or MAC
is logged. REFCLK, global hub timer and every DTO control/phase/modulo are read;
Linux's DCHUB reference arithmetic is labelled a candidate, not clock authority.
Pixel source/reference provenance and cursor power/address-domain interpretation
remain unqualified until the native values are assessed. Nothing programs clocks
or powers up a cursor block.

The 64 KiB storage candidate is recomputed after the owned spare extent using
the same memory-policy checks as scanout preparation, including reserved boot/EFI
coverage, the 9 MiB VGA prefix, 16 MiB tail guard and physical alias exclusions.
All four cursor enable bits must be disabled in stable samples before reporting
`UMA-policy-candidate=1`; it performs no reservation, mapping or store. This is
storage-policy evidence, **not old-cursor fetch retirement or complete allocation
authority**. Nonzero inherited addresses/update state need review even if disabled.
The captured prior layout predicts offset `0x10f0000`; never assume that result.

Pinned Linux v6.19.10 commit `271f8eab9590b57a2ff0c8c9eee357723c4a85cb`:
[HUBP table](https://github.com/gregkh/linux/blob/271f8eab9590b57a2ff0c8c9eee357723c4a85cb/drivers/gpu/drm/amd/display/dc/hubp/dcn21/dcn21_hubp.c),
[definitions](https://github.com/gregkh/linux/blob/271f8eab9590b57a2ff0c8c9eee357723c4a85cb/drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_1_0_offset.h),
[masks](https://github.com/gregkh/linux/blob/271f8eab9590b57a2ff0c8c9eee357723c4a85cb/drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_1_0_sh_mask.h),
[DCCG](https://github.com/gregkh/linux/blob/271f8eab9590b57a2ff0c8c9eee357723c4a85cb/drivers/gpu/drm/amd/display/dc/dccg/dcn20/dcn20_dccg.c#L72-L91),
[hub reference](https://github.com/gregkh/linux/blob/271f8eab9590b57a2ff0c8c9eee357723c4a85cb/drivers/gpu/drm/amd/display/dc/hubbub/dcn20/dcn20_hubbub.c#L560-L590)
and [DTO readback](https://github.com/gregkh/linux/blob/271f8eab9590b57a2ff0c8c9eee357723c4a85cb/drivers/gpu/drm/amd/display/dc/dce/dce_clock_source.c#L1192-L1228).
Complete adapted-source MIT notices and provenance are in the bundled
[AMD NOTICE](../../../../arch/x86_64/amd/NOTICE).

The [cursor lock workaround](https://github.com/gregkh/linux/blob/271f8eab9590b57a2ff0c8c9eee357723c4a85cb/drivers/gpu/drm/amd/display/dc/hwss/dcn10/dcn10_hwseq.c#L2173-L2263)
uses pixel/reference timing and a 70 µs programming estimate. Its VUPDATE
candidate derives from blank-end minus VSTARTUP plus one with Linux's exact
positive/negative normalization; VUPDATE_PARAM alone is not that recipe. This
probe prints the candidate, never schedules a write. Our future validation and
transaction duration has not been qualified against its keepout.

**Task 2 blocker:** the [MPC lock helper](https://github.com/gregkh/linux/blob/271f8eab9590b57a2ff0c8c9eee357723c4a85cb/drivers/gpu/drm/amd/display/dc/mpc/dcn10/dcn10_mpc.c#L460-L465)
and cursor setters do not establish a transaction-correlated latch/disable/old
fetch retirement predicate from pending/taken/ACK. No cursor equivalent of the
flip's earliest-in-use address was found. Stable idle bits in this read-only boot
cannot prove image reuse, capture completion or safe disable; no guessed delay
or register readback replaces that proof. Task 2 needs separate assignment and
resolution of this evidence gap before writes, plus supported native clock,
power/address-domain and keepout evidence.

## Counter interpretation

`display-cursor-probe` emits cumulative totals every 120 successfully presented
frames alongside existing `display-flip-metrics` and `renoir-flip` metrics.
`compositions` counts frames receiving pixels into display storage; CPU scanout
copies count staged/flip surface copies. A timeout dual copy counts twice; a
direct-front path after staging allocation refusal has no separate scanout copy
and is unsuitable for this native baseline. Submitted/confirmed flips come from
the existing flip metrics, so enable `DISPLAY_FLIP_METRICS=1` too.

`reports` counts routed reports, including synthesized button changes, not raw
PS/2 packets. `relative-motion` counts nonzero dx/dy reports; `screen-moves` counts
reports actually changing the bounded physical position (locked relative input
changes no screen position). `moved-frames` counts successive visible **rendered**
snapshots with different positions; several moves can coalesce into one frame.
The pending frame's snapshot stays fixed while later input counters advance.

Use boundary differences, not whole-boot averages: frames/reports/moves and
compose/copy totals divided by delta frames or delta moves. Report ratios with
zero moves as unavailable. Compose/copy spans include clock/preemption overhead; wall elapsed also includes
sleeps and logging. Formatting this sample happens after its compose/copy spans,
but opt-in output can still perturb cadence. These are not CPU profiling or
per-event attribution. Idle output/caret cadence
and the normal periodic base recomposition remain active. More frames during
motion alone does not establish that input caused those compositions.

## Local checks

Clean-main full source image, probe image and default image build with the
existing builder; optional images reuse verified unchanged SDK/userland/ports.
Source/lifetime review confirms no cursor writer or allocation and unchanged
flip write fields and guards. Named register offsets/masks were checked against
the pinned headers; the negative VUPDATE normalization was corrected to Linux's
exact arithmetic. No tests, synthetic MMIO, fault injection or benchmark
infrastructure were added.

Manual QEMU 10.2.2, Q35/nested KVM, max CPU, four CPUs, 2 GiB, fresh OVMF variables,
relative PS/2, VirtIO-SCSI CD/RNG, no NIC/disk/USB/audio. Standard VGA 1280x800
checks the absent-Renoir probe path: OFF/unprepared, all 12 mapping slots zero,
and one gated unavailable line. Four routed motion reports changed physical
position four times but only two visible rendered positions, demonstrating
coalescing; normal frames continue composing/copying while the report counts
are unchanged. This is a counter sanity check, not a native cost result.

Default firmware, Bochs 800x600 and VirtIO boots keep probe counters disabled,
zero and silent, with no Renoir mappings. Shell/tab/ls checks pass, and software pointer/caret rendering is visible on
firmware/Bochs. VirtIO retains its separate cursor state; its QMP framebuffer
dump is not a visual check of that hardware cursor. QEMU has no DCN: it
cannot check the native register, clock, cursor-power or memory evidence.

## One native boot — Luna stages, owner moves the pointer

1. Ask the orchestrator to have Luna stage the set (or exact-head rebuild) as a
   temporary **Cursor inventory + software baseline** entry cloned from
   **Flip: on + metrics**. Preserve rollback and record kernel/initrd hashes.
   Its normal command line must include `display.flip=1 display.flip.metrics=1
   display.cursor.probe=1 display.timing=off log.udp=1`; LOG_LEVEL is info.
   Alpha does not stage PXE. Reboot once remotely; horse's UDP collector retains
   the startup dump. Keep the ThinkPad on AC and the existing 1920x1080 GOP mode.
2. Require the existing prepared HUBP/two-surface summary, a read-only cursor dump
   and continuing `backend=flip` confirmations. If preparation refuses, a failure
   occurs or timeouts appear, retain the masked log; do not call it a successful
   flip-mode baseline. The cursor remains software in every case.
3. Select the local Development terminal prompt, keeping klog output offscreen.
   Use the PS/2 touchpad/TrackPoint, leaving other pointers idle. Wait ten seconds,
   then use a host timer for **30s idle, 30s continuous motion, 30s idle, 30s motion**.
   Keep the pointer inside the same terminal body; no clicks, selection, wheel,
   edge clamping or space changes during these windows. Record the host UTC
   boundaries; sampling is about every 120 frames, so use interior complete
   metric intervals and retain that boundary uncertainty. Check the software
   I-beam follows motion and input/tearing behavior remains the usual flip path.
4. Retain paired cumulative cursor/display/flip metric lines at each boundary.
   Report repeated idle/motion ranges for frame/copy/confirmation rate, routed
   motion and screen moves, changed-position frames, compositions per move and
   mean elapsed compose/copy cost from interval differences. Preserve wait,
   validation, timeout and FAILED output. This becomes the task 2 control;
   do not combine the four windows into one average or claim hardware improvement.
5. Send the masked log location, exact revisions/hashes, window boundaries and
   owner observations. Do not send raw EDID/BIOS/serial data. The inventory report
   will assess actual cursor/clock/storage evidence and append the software
   measurements; native results are currently pending.
