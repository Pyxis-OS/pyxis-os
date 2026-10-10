# Renoir flip presentation

**Design accepted 2026-10-09. Task 2 implemented and natively qualified 2026-10-10.**
Read-only task 1 is complete. The owner qualified normal flips and game play at
`9254f5c8`; default remains off. Task 3 and polling optimization are not assigned.
The owner redirected presentation step 2 after native batch 2 failed on main
`11d35fa6`: inaccurate counter-derived periods, excessive uncertainty and worse
tearing/input delay in blank-copy mode. The separate observer safety fix keeps
ordinary software cadence, immediate copies and trace-only repeated losses.
The observer safety fix does not qualify native timed copies; native flip results
are recorded separately below.

Present complete frames by changing the existing GOP pipe's scanout address at
the display engine's synchronized flip point. Keep its mode, clocks, power,
firmware and interrupt configuration. Begin with one verified Renoir
`1002:1636`, one progressive 1920x1080 pipe and two linear scanout surfaces:
the original GOP surface and one independently owned spare. Three surfaces are
a later option, not hardware triple-buffer enablement.

## Authority: accepted boundary

This is the first Pyxis AMD GPU register **write** path. It overturns #622's
read-only boundary for the bounded backend only, as accepted in decision 1
below on 2026-10-09. Task 2 is implemented and its normal path natively qualified; the backend stays opt-in.
The separate timing observer remains read-only.

The kernel's BSP display driver exclusively owns the device, mappings and
surface-address writes. Programs continue supplying pixels through DRAW and
the existing RAM frame handoff; no BAR mapping, address-setting capability or
GPU command interface is delegated. Keep Limine in its adapter, hardware under
arch, and allocation/VM mutation under the existing BSP memory contracts.

The accepted first write allowlist is one qualified HUBP's synchronized flip
control and primary address high/low pair. No modeset, clock, power, VM setup,
firmware command, reset, GSL reconfiguration or display interrupt writes.
Optional OTG blanking is described below but deferred by the accepted scope.

## Identify the inherited pipe

Master-enabled OTG alone is insufficient. Find exactly one scanout path whose
live primary/in-use address translates to the GOP framebuffer. Follow
`DCHUBP_CNTL.HUBP_VTG_SEL` to its OTG, then cross-check OTG/OPP/MPCC/HUBP routing;
do not equate instance numbers. Linux exposes the
[HUBP selector](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hubp/dcn10/dcn10_hubp.c#L1307-L1312),
[ODM data sources](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/optc/dcn20/dcn20_optc.c#L220-L240)
and [MPCC route](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/mpc/dcn20/dcn20_mpc.c#L543-L556).
The ODM getter includes a VBIOS stale-count workaround; its source must be
followed rather than interpreting a single count bit in isolation.

Require one enabled HUBP, one unsplit/unstereo plane, matching viewport and
unscaled output, no rotation/mirroring, no pending foreign flip, update locks
clear and no GSL dependency. Snapshot all relevant state twice and validate it
before each submission. Ambiguity or a changed pipe refuses the backend; it
does not repair the firmware setup.

The HUBP [state getter](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hubp/dcn20/dcn20_hubp.c#L1278-L1327)
provides format, viewport, swizzle, compression and addresses. Accept only
documented linear 32-bit RGB with DCC off, matching GOP channel shifts, pitch,
width and height. Pitch is pixels minus one in the
[register programming](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hubp/dcn10/dcn10_hubp.c#L163-L196);
for four-byte pixels Linux normally decodes pixels plus one to the byte stride.
For the inventoried firmware mode, retain raw `0x780` and use GOP 7680 bytes: the
owner accepted the unsheared native display as evidence of its effective stride
on 2026-10-09. Do not reinterpret or rewrite that register. Check
[HUBPRET crossbar](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hubp/dcn10/dcn10_hubp.c#L236-L272)
and compositor alpha behavior as well as format: a matching format number alone
does not establish RGB/BGR ordering or opaque GOP semantics. Tiled, compressed,
scaled, rotated, multi-plane and VM-dependent surfaces are outside this slice.
Retain firmware format/pitch/tiling and blend programming without rewriting it.

## Exact DCN 2.1 flip path

Source is Linux **v6.19.10** AMD DC, whose relevant files carry MIT permission
notices. The [Renoir HUBP table](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hubp/dcn21/dcn21_hubp.c#L815-L835)
uses `hubp21_program_surface_flip_and_addr`, inherited `hubp1_is_flip_pending`
and `hubp2_read_state`.

The [DCN2.1 writer](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hubp/dcn21/dcn21_hubp.c#L594-L664)
programs flip type/stereo fields, VMID, TMZ, metadata/secondary/chroma addresses,
then primary **high first, low last**. Its mono
[graphics preparation](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hubp/dcn21/dcn21_hubp.c#L696-L723)
does not need the video-only DMUB workaround. The inherited
[latch rule](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hubp/dcn20/dcn20_hubp.c#L744-L781)
identifies the primary low write as the trigger when surface-update lock is
unused. Preserve that ordering.

For this smaller mono/uncompressed slice, require VMID 0 with a verified direct
framebuffer-aperture translation, TMZ off, both stereo fields zero, no GSL
dependency and hardware triple buffering disabled. Metadata/chroma/secondary
planes must be provably inactive under the preserved format/DCC state; their
addresses need not be zero if inactivity is established. Unproved translation
or inactive-plane state refuses the backend. Preserve these fields rather than
transplanting the full Linux writer's resets. Copy the completed
composite into the free surface, fence WC stores, revalidate ownership/state,
set `SURFACE_FLIP_TYPE=0` by a masked update, then write its GPU address high
and low. Preserve unrelated control bits. The driver records one requested
address and accepts no second flip until it resolves the first.

Linux's [completion predicate](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hubp/dcn10/dcn10_hubp.c#L752-L777)
requires both pending clear and earliest-in-use address equal to the request.
Read a stable address/control tuple; a clear pending bit alone is insufficient.
Only this confirmation permits writing the previous front as the next back.
Keep the software input/presentation deadline independent. While a flip is
outstanding, use 1 ms sleeping polls with a 50 ms wall-clock deadline (at
most 50 polls), no display interrupt and no blank-start spin. Check pending at
the earlier poll/presenter wake, without composing a new image while both
surfaces are busy. Ordinary-cadence-only polling could miss the in-use transition
and add a frame; polling cost and wakeups must be measured natively. Keep
servicing input between polls. A poll timestamp bounds the transition; it is not
an exact vblank or panel-photon timestamp.

[DCN2.1 offsets](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_1_0_offset.h#L2178-L2239)
use BASE_IDX 2. [Renoir segment 2](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/include/renoir_ip_offset.h#L1367-L1371)
starts at DWORD `0x34c0`; BAR byte offset is `(segment + header offset) * 4`.
Map only needed pages, not an assumed full BAR extent.

| HUBP register | Instance 0 | Instance 1 | Instance 2 | Instance 3 |
| --- | --- | --- | --- | --- |
| PRIMARY_SURFACE_ADDRESS | 0xeb28 | 0xee98 | 0xf208 | 0xf578 |
| PRIMARY_SURFACE_ADDRESS_HIGH | 0xeb2c | 0xee9c | 0xf20c | 0xf57c |
| FLIP_CONTROL | 0xeb6c | 0xeedc | 0xf24c | 0xf5bc |
| SURFACE_EARLIEST_INUSE | 0xeb94 | 0xef04 | 0xf274 | 0xf5e4 |
| SURFACE_EARLIEST_INUSE_HIGH | 0xeb98 | 0xef08 | 0xf278 | 0xf5e8 |

The [field masks](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_1_0_sh_mask.h#L9190-L9345)
include update-lock `0x1`, flip type `0x2`, pending `0x100`, stereo mode
`0x3000` and stereo sync `0x10000`. Address fields are low 32/high 16 bits;
this does not establish allocation alignment or CPU/GPU address identity.

### Optional OTG blank

Linux's [optc1 blank/unblank helpers](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/optc/dcn10/dcn10_optc.c#L390-L479)
set blank-data enable, clear DE mode and disable blank-data double buffering;
blank confirmation requires enable plus current blank state. Unblank also
clears OTG underflow status, a write that must be recorded explicitly if adopted.
Blanking output does not prove HUBP fetch has stopped or cancel a pending flip.
The accepted first slice does not use it; a demonstrated need returns to the owner
before extending the write allowlist.

## Extra surface memory: accepted exclusion standard

The owner accepted the Linux-derived exclusion rule on **2026-10-09** after
[task 1's native inventory](../development/experiments/renoir-flip-inventory/README.md),
replacing the requirement for an explicit firmware allocator handoff. Treat the
validated 512 MiB UMA range as kernel-driver-owned except GOP/VGA storage,
all reported firmware/driver/live-client reservations, applicable
IP-discovery/training/stolen reservations and the last-16-MiB guard. Exclude any
newly identified reservation in full. The owner accepts the pre-OS PSP/SMU
residual risk; disabled DMCUB and zero ATOM usage do not prove those clients absent.

The native route is `OTG0 <- OPP0 <- MPCC0 <- HUBP0`. GPU framebuffer range is
`[0xf400000000,0xf420000000)`, direct CPU UMA is
`[0x810000000,0x830000000)`. Stable BAR0 `0x860000000` correlates the GOP-advertised
allocation to VRAM `[0,0x7e9000)`, primary/earliest GPU address `0xf400000000`.
BAR0 length is unknown; address correlation does not confer its entire extent.

Keep the 9 MiB low stolen/GOP prefix, all applicable reservation extents and
the guard `[0x1f000000,0x20000000)`. One 64 KiB-aligned spare at VRAM offset
`0x900000`, rounded backing `[0x900000,0x10f0000)`, is the accepted candidate for
this capture. Task 1 did not allocate it; task 2 maps it privately after rechecking exclusions.
Use WC direct-UMA mapping; avoid conflicting
WB/WC aliases, retain exclusion from PMM allocation and allocate/map on the BSP
before publication. Keep backing until GPU retirement is established.
No arbitrary RAM, reset, PSP/SMU/DMCUB request or MC reprogramming is authorized.

Task 1 read bounds/identity-validated ACPI VFCT/VBIOS ATOM
`vram_usagebyfirmware` and `firmware_info`, UEFI descriptors, display route/state
and direct MC/DCN translation registers. Its
[source/reservation report](../development/experiments/renoir-flip-inventory/README.md#linux-pre-initialization-reservations-on-this-renoir)
records Linux's fixed reservations and later driver BOs distinctly. Linux's
host scratch helper is not a VRAM allocation; later PSP/SMU BOs do not locate
all pre-OS firmware storage. No raw firmware/EDID/serial data is logged or committed.

The owner resolved the effective-stride prerequisite for task 2 on 2026-10-09:
the unsheared GOP image means a 7680-byte effective stride. Preserve raw `0x780`;
Linux's pixels-minus-one convention would normally write `0x77f`, and this does
not establish a general decoder for other firmware modes. The first backend
requires precisely this width/height/pitch/format, recalculates translations and
exclusions at boot, and refuses other layouts or unfamiliar/nonzero reservations.

The owner accepted the inherited firmware scaler as-is on 2026-10-10: the
correctly displayed unscaled panel proves this sampling state, and address-only
flips never change it. Require captured DSCL mode1/AutoCal0x100, exactly unity
H/V luma ratio fields and matching viewport/RECOUT/MPC rectangles. Do not require
chroma ratios/inits or tap controls to reproduce Linux's programming. Compare
the complete active scaler state against the boot snapshot on every validation,
including all ratios, phases, taps, boundary, two-tap, replicate and coefficient
bank fields. These fields remain read-only. This acceptance does not assert that
any particular chroma or two-tap field is unused, and does not generalize to a
new firmware layout. Native positive flips still require qualification.

Task 2 is authorized. `DISPLAY_FLIP=1` adds `display.flip=1`; omission is off.
Default remains today's GOP copy. The [task-2 record](../development/experiments/renoir-flip-backend/README.md)
covers qualification gates, lifetime, QEMU evidence and paired native steps.

## Presenter and the three-slot application handoff

The [existing three RAM slots](../interfaces/graphics.md#slots-and-frame-handoff)
remain producer storage and are never attached to the GPU. SUBMIT still returns
before scanout and replaces an unconsumed pending frame; it does not become a
flip wait. The presenter takes a whole submitted frame with its current lease,
composes navigation, TTY/graphics, selection, caret and software cursor into
the existing WB staging frame, then copies the complete image into the free
WC scanout surface. This moves the existing full copy off the currently fetched
surface; it does not remove the copy or introduce GPU rendering.

The compositor-current application frame and hardware-front surface are
different objects. The application lease protects source backing during the
copy; hardware retirement concerns only driver-owned scanout surfaces. Preserve
the current latest-frame/drop contract without inventing a fourth producer slot
or exposing scanout buffers. While two scanout surfaces are front/pending,
preserve the staged submitted snapshot, keep processing input, and wait to
compose/copy again until confirmation or fallback. Application pending frames
can still be replaced while the presenter waits.

Publish the completed capture snapshot only after confirmed flip, or after a
completed unsynchronized fallback copy. Screenshots retain the visible software
cursor. An unconfirmed flip cannot masquerade as a presented frame. A possible
third surface adds a driver-owned ready back image; one hardware request still
stays outstanding and stale ready work is replaced. It does not enable the
separate hardware triple-buffer control.

## Failure, fallback and panic

Before any write, absent memory proof or incompatible/ambiguous state retains
today's unsynchronized GOP copy unchanged. Never switch to a partly prepared
driver or change format/pitch to make a probe pass.

A timeout does not cancel a flip. Disable further address writes and retain
every potentially fetched surface; do not free/recycle them on a guessed
completion. If the stable in-use and pending addresses remain members of the
verified owned set, continue ordinary unsynchronized CPU copies to all possible
fronts (two with the default), so a late latch cannot display stale contents.
This fallback may pay two copies and tear; it preserves updates without another
GPU write or hardware-phase wait. Keep checking read-only identity/layout.
Return to a single original GOP target only after its use is confirmed and
there is no unresolved request; otherwise keep both allocations pinned until
reboot. If routing/address identity itself becomes unprovable, stop this backend
and report display unavailable; no safe visible fallback can be promised then.

Offscreen copying, its store fence and the short control/high/low/readback
transaction participate in the direct-writer claim and panic recheck. Recheck
before the low-address trigger; publish no new transaction after panic has
claimed output. Release the writer before asynchronous completion polling,
retaining the pending composite/capture snapshot separately. Panic must drain
an already entered transaction before owning its pixels; a timed-out foreign
writer leaves panic on serial rather than bypassing the claim. An interrupted
local submission never resumes to trigger the low write after panic.

Panic stops normal submissions through the existing direct-writer claim. It
does not poll for a flip or issue GPU commands from a faulting CPU. Copy panic
output into every known possibly scanned owned linear surface, retaining all
aliases/backing. Adapting the private panic writer to those bounded targets is
part of implementation, not something today's single-target helper already
does. Mode/power changes outside the verified scope can still defeat visible
panic and remain an explicit limitation.

## What stays, retires and is vendored

Keep read-only PCI identity/D0/BAR validation, bounded UC mappings, sole-OTG
mode checks, raw counter diagnostics and failure reporting. Fold these into
one claimed driver rather than letting the old unclaimed observer compete with
it. Retire counter-fitted period/phase as a scheduling authority, CPU blank-copy
admission and all beam-racing waits. Flip-pending plus in-use confirmation drives
surface retirement; program timing APIs remain a later task.

During authorized implementation, vendor only the adapted mono flip ordering,
completion predicate, required state decoding and exact register definitions
from pinned v6.19.10 AMD DC. Keep optc blank helpers separate/deferred. Preserve
each source/header's complete AMD copyright and MIT permission notice,
record the tag/commit, source paths and modifications in an upstream notice,
and add the entry to LICENSING.md. The relevant
[HUBP source notice](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hubp/dcn21/dcn21_hubp.c#L1-L24)
and [register notice](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_1_0_offset.h#L1-L24)
permit this; do not copy unrelated Linux DRM/BO/VM infrastructure under an assumed
MIT umbrella. Task 1 retains its adapted read-only definitions and notices;
task 2 adds the narrowly allowed mono write path, default off.

## Tasks and native qualification

1. **Read-only inventory and memory evidence — complete.** Native builds
   `5e342488` and `0f6baec6` establish route/state/translation/ATOM/UEFI evidence;
   the owner accepted the exclusion standard and PSP/SMU risk on 2026-10-09.
   **What the owner sees:** the [inventory report](../development/experiments/renoir-flip-inventory/README.md),
   no allocation or GPU writes; task 2 preserves the owner-qualified effective stride.
2. **Qualified two-surface backend — complete.** Private allocation/ownership,
   fenced offscreen copies, mono flips, bounded completion polling, capture,
   timeout fallback and panic are implemented. At `9254f5c8`, the owner reported
   3120 submitted/confirmed flips, zero timeouts/FAILED, no tearing in native
   Quake or Chocolate Quake and negligible perceived latency. Boot/Bochs/VirtIO
   retain their own paths; default remains GOP copy.
   **What the owner sees:** the [native record](../development/experiments/renoir-flip-backend/README.md#native-success--2026-10-10),
   explicit remaining qualification limits and an opt-in backend ready to merge.
3. **Native qualification and references.** Owner's ThinkPad: same revision and
   unchanged mode, info logging, opt-in disabled/enabled paired boots. Record
   source/reservation evidence, register snapshots before/after, requested versus
   confirmed addresses, pending durations/timeouts, CPU compose/copy costs and
   BSP poll/wakeup cost and service of input. Take matched native Quake and Chocolate Quake72Hz play camera clips; check cursor/selection,
   lock/unlock, spaces/layers, screenshots and ordinary shutdown. A framebuffer
   screenshot or FPS does not establish a tear-free panel. Include Chocolate
   Quake: on GOP copy it showed about three drifting tear lines at once, more
   than native Quake ([SDL game ports](../technical-debt.md#sdl-game-ports-native-qualification)).
   Keep optional blank
   and timeout recovery unqualified until actually exercised and recorded.
   **What the owner sees:** matched native completion/camera/input/cost results,
   an explicit qualification outcome and references documenting remaining limits.

QEMU has no DCN 2.1. It can check refusal/unavailable behavior, unchanged
boot/Bochs/VirtIO, frame/capture lifetime and ordinary input; it cannot qualify
register writes, VRAM ownership, flip completion, native panic or tear reduction.
No synthetic DCN device, new tests or fault injection is implicit in this plan.

## Accepted flip-backend decisions — 2026-10-09

1. **First GPU writes authorized for the bounded backend only:** kernel-exclusive,
   one verified mono HUBP, synchronized flip control plus primary address
   high/low, after all read-only prerequisites pass. No OTG blank in the first
   slice; no modeset/clock/power/VM/firmware/interrupt changes. This replaces
   #622's read-only rule for that backend.
2. **Spare memory under the accepted Linux-derived exclusion rule:** the
   validated 512 MiB UMA range is owned by the kernel driver minus GOP/VGA,
   reported firmware/driver/live-client and applicable discovery/training/stolen
   reservations, plus the last-16-MiB guard. One 64 KiB-aligned spare at offset
   `0x900000` is the candidate for this capture. The owner accepts PSP/SMU
   residual uncertainty. This 2026-10-09 criterion replaces explicit allocator
   handoff, not the requirement to validate translation/layout before writes.
3. **Two scanout surfaces and bounded completion:** unchanged three-slot producer
   handoff, one outstanding flip, 1 ms sleeping polls with a 50 ms deadline only
   while pending, preserving the independent ordinary input/presentation
   deadline. Timeout stops GPU writes, pins possible fronts and falls back to
   unsynchronized copies to the known owned set; unknown routing makes display
   unavailable. Three surfaces, interrupts and blanking are deferred.

Tasks 1 and 2 are complete; normal native flips and game play are qualified on
the recorded firmware layout. Default remains off. Task 3 still requires the
owner's explicit assignment; timeout/panic/capture and unreported input checks
remain unqualified.

## Proposed cheaper steady-state polling

Separate follow-up, proposed only; no polling change in task 2. The native run
measured 369 µs mean validation elapsed, 3.9 ms maximum and about 8 polls per
flip, or roughly 3 ms cumulative validation elapsed per frame. Metrics include
preemption/clock overhead; neither observed wait nor this estimate establishes
an optimization gain.

Recommended default: while PENDING, read a stable flip-control/earliest-in-use
tuple as a hint for scheduling the full check. Keep one outstanding request,
1 ms sleeping polls and the 50 ms/50-poll bound. Full device/route/immutable
layout/owned-set validation remains mandatory before a back-surface pixel
write, before GPU submission, before front retirement or capture publication,
and before timeout fallback or any fallback copy. Confirmation must still
verify stable primary **and** earliest addresses equal the request with pending
clear. Light reads alone never authorize a write, reuse or release. Full-check
failure or unknown ownership remains FAILED with surfaces pinned. No new masks,
write authority, allocations or interrupt/firmware changes are proposed.

This moves repeated full pending-poll checks to the frame's guarded actions;
expect fewer full checks plus small tuple reads, with matched native cost/input
qualification required. **Owner decision before implementation:** accept delayed
layout-loss detection during write-free PENDING, bounded by candidate completion
or the existing 50 ms/50-poll deadline, while retaining full validation at every
action above. That detection cadence is proposed, not accepted. Measure validation
and poll cost separately at the same revision/options/workloads; repeat both
games and confirm retirement, capture, fallback and FAILED guards remain intact.
