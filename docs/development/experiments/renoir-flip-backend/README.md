# Renoir two-surface flip backend

Task 2 implements the [accepted bounded backend](../../../wip/renoir-flip-presentation.md).
Default is off. No native flip, memory-write safety, timeout recovery, panic
visibility or tear reduction is qualified by the QEMU checks below.

## Qualification and authority

`DISPLAY_FLIP=1` adds `display.flip=1`; `DISPLAY_FLIP_METRICS=1` independently
adds `display.flip.metrics=1`. Both default to zero. Metrics work on the disabled
ordinary path too, permitting paired measurements at info level. Default boot
adds no new lines. Flip enablement is only attempted on the GOP backend; Bochs
and VirtIO retain their existing paths. Failed preparation retains GOP copies.

Before AP startup, numeric VFCT/ATOM metadata is copied by the bootstrap adapter.
Require one unique already-D0/memory-enabled Renoir display, no other display,
one enabled progressive 1920x1080 OTG and one unsplit mono OPP/MPCC/HUBP path.
Require actual active dimensions, current master enable, scaler bypass and
matching recout/MPC/viewport, no stereo/update lock/GSL/triple buffering, disabled flip/flip-away interrupt
enables (read only, never changed), opaque MPCC global alpha/gain,
linear ARGB8888 with matching crossbar, no DCC/TMZ/VM translation, and stable
BAR/MC/DCN/GOP correspondence. Mask live request/clock/coefficient-bank status
from immutable comparisons. Generic INUSE is not the completion address.

The owner accepted the unsheared native GOP display as proof of a **7680-byte
effective stride**, 2026-10-09. Raw PITCH stays `0x780`; Linux normally programs
1920 pixels as `0x77f`. This first slice requires the observed raw value and GOP
1920x1080/7680/32-bit RGB shifts 16/8/0. It does not generalize the discrepancy.
Every format, pitch, tiling, blend and delay field remains firmware-owned.

The 512 MiB UMA bounds are recomputed from DCN/MMHUB/GC/NBIF and correlated
through stable BAR0 to original GOP offset zero. Require supported, uniquely
matched ROM2.2/master2.1/firmware3.2/usage2.1, posted-only capability1 and zero
reported firmware/driver allocations; unfamiliar/nonzero reservations refuse
this first slice rather than treating them as empty. DMCUB must be disabled,
with all windows disabled. Accepted pre-OS PSP/SMU residual risk remains.

Kernel memory policy requires reserved boot and UEFI type0 coverage, excludes
PMM RAM and existing kernel VM aliases, preserves max(GOP extent,9MiB VGA) and
the final16MiB guard (covering discovery/TMR and any applicable audited tail
reservation). Training or new firmware capabilities refuse qualification.
Round one spare to64KiB and recheck offset `0x900000`; no offset is assumed.
For this capture backing is `[0x900000,0x10f0000)`, payload ends `0x10e9000`.
Map direct UMA once RW/NX/WC, without a WB alias; do not map unknown BAR0 length.
Partial unpublished mappings unwind. After publication retain both surface
mappings/storage until reboot, including timeout and staging-allocation refusal.

Software-only PCI reservation gives kernel-exclusive ownership without command,
BAR, power, interrupt or firmware writes. Map only audited UC register pages,
initially RO. Make the qualified HUBP page writable only after all prerequisites;
the write helper asserts exactly its flip control, primary high and primary low.
No full-BAR map, reset, blank, mode, clock, power, VM or firmware command occurs.
The [pinned IRQ table](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/irq/dcn21/irq_service_dcn21.c#L180-L237)
identifies the enable-mask semantics; no IRQ sequence is imported or executed.

## Presentation, failure, capture and panic

The three producer RAM slots and application lease rules stay unchanged. Compose
into WB staging, service input independently, validate the free surface, claim
the direct writer, copy visible rows to the WC back, fence, revalidate and clear
only the immediate-flip mask `0x2` in flip control. Preserve all other fields.
Write primary high first and low last; low triggers. Recheck panic before low.

Release writer ownership before polling. Keep one request and retain the staged
image/application/pointer/capture snapshots; process keyboard and pointer input
between1ms sleeping polls. Completion is a stable pending-clear plus primary and
earliest-in-use equal to the request. A50ms wall-clock deadline and bounded polls
stop GPU writes on timeout. Both surfaces remain pinned and receive fenced,
unsynchronized copies of this and later staged images. Revalidate identity/layout
and membership in the owned set; a lost identity makes display unavailable.
This first slice retains dual copying until reboot even if a late latch arrives.

Capture publishes only after confirmed flip or a completed fenced fallback copy.
The cursor is software-composed in both paths. Panic drains the existing writer
claim and paints both permanent possible fronts with no MMIO, waits, allocation
or GPU writes. A failed identity or undrained remote writer stays serial-only.
Native panic remains unqualified. If staging allocation fails, cancel before any
flip and keep ordinary direct rendering; unpublished GPU ownership is never implied.

The active backend skips counter-derived timing/blank-copy preparation. Refused
and disabled paths keep today's timing policy. Metrics are explicit info-level
qualification output, including composed/copy elapsed means/maxima, completion
waits and read-only validation costs. They include preemption/clock overhead;
completion timestamps bound observation, not photons. No per-frame default logs.

## Source and licence

Pinned Linux v6.19.10 commit `271f8eab9590b57a2ff0c8c9eee357723c4a85cb`, AMD DC
MIT definitions and only the adapted mono sequence/completion/state decoding:
[HUBP21](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hubp/dcn21/dcn21_hubp.c#L594-L723),
[HUBP1 completion](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hubp/dcn10/dcn10_hubp.c#L752-L777),
[HUBP2 latch](https://github.com/gregkh/linux/blob/v6.19.10/drivers/gpu/drm/amd/display/dc/hubp/dcn20/dcn20_hubp.c#L744-L781).
Complete notices/provenance in [AMD NOTICE](../../../../arch/x86_64/amd/NOTICE)
and [LICENSING](../../../../LICENSING.md), retained by images and kernel bundles.
No Linux DRM/BO/VM infrastructure or build-time upstream fetch.

## QEMU record

Baseline main `54dbe1c4`, userland `48ba9bad`, ports `7dbe43c5`, fs `b427df29`,
lwIP `a1aadb91`; existing LLVM23.1.3 builder. Full source baseline image built.
Kernel ELF SHA256 `042b94fe2fba292108c6f9a44e6bd4faba128e6b4dd5559492a6025fec6edcd2`;
ISO `9e4623cdfb85597b358cfa6596c3ecba3d93f4ee02aa21cffc8bb1f8d16de1cb`.
QEMU10.2.2/Q35/nested KVM/max/four CPUs/2GiB/fresh OVMF, relative PS/2,
VirtIO-SCSI CD/RNG, no NIC/disk/USB/audio. Baseline std VGA uses DISPLAY_BOOT,
1280x800; tab switching and `ls` work, caret/I-beam visible.

Changed-image final checks and exact-head CI are recorded in the PR. QEMU has no
DCN2.1 and cannot exercise positive memory proof, writes or flip completion.
No synthetic device, tests, fault injection or boot/output automation was added.

## Paired native qualification — Luna stages, owner judges

Use one submitted revision, same kernel/initrd, same GOP mode and boot configuration,
AC/battery setting and workload. Info logging in both; no global trace. Alpha
prepares sealed local sets and asks the orchestrator to have Luna stage them;
Alpha does not change PXE staging. The laptop is in its PXE loop, with remote
reboot and existing UDP capture on horse.

1. Disabled: build `make -j16 image LOG_LEVEL=info LOG_UDP=1 DISPLAY_TIMING=off
   DISPLAY_TIMING_METRICS=0 DISPLAY_INVENTORY=0 DISPLAY_FLIP=0 DISPLAY_FLIP_METRICS=1`.
   Save kernel/initrd/limine.conf and checksum manifest as `build/native-flip-off/`.
   Confirm `display.flip` is absent, `display.flip.metrics=1 display.timing=off
   log.udp=1` present. Luna stages this set, retaining the previous rollback set.
2. Enabled: same inputs with `DISPLAY_FLIP=1`, sealed `build/native-flip-on/`.
   Confirm the only new command-line option is `display.flip=1`; kernel/initrd
   checksums must match the disabled set. Luna stages it after the disabled run.
3. Per boot, capture startup and at least60s idle/moving-pointer metrics. Enabled
   must report prepared with recomputed spare0x900000, inherited raw0x780/effective
   7680, correct addresses and many matching confirmations with no timeout/loss.
   Refusal is a safe negative result: retain its numeric metrics dump, do not
   force-enable. Timeout/unavailable is not qualification success; restore off.
4. In Development, run the same moving native Quake scene/demo on both boots,
   then play `chocolate-quake` with its unchanged72Hz frame-sleep pacing on both.
   The owner reports about three simultaneous drifting tear lines in Chocolate
   Quake with current unsynchronized copies, versus native Quake's single tear;
   this is a baseline observation, not a flip result. Keep the workload/pacing
   matched, take camera clips of both games, and judge tearing by eye.
   Compare info-level
   compose/copy costs, flip pending duration, validation costs and poll counts.
   Check ordinary keys/motion, tabs, text selection, space/layer changes,
   Quake lock, Super+Esc and fresh-click relock for perceived delay/regression.
5. Take `screenshot` from remote while a visible local cursor/selection is shown;
   verify cursor-inclusive image and no unconfirmed-frame publication. Run
   ordinary shutdown/reboot. Retain masked log, revision, image manifest and
   owner's visual/input observations; do not commit raw serials/MACs/EDID.

No native timeout injection or panic trigger is authorized here. Report those
paths as source-reviewed/QEMU-unexercisable, not measured. Task3 remains separate.

- [x] Bounded two-surface backend and private memory ownership implemented.
- [x] Capture, panic and bounded pending/fallback paths implemented.
- [ ] Owner native paired qualification; success is not assumed.
