# Intel inherited scanout: vela tearing

Assigned to Codex alpha (chronium, 2026-10-28). The current slice admits and
observes inherited display state. Plane/GGTT programming and retirement remain
unapproved; this does not yet remove tearing.

## Accepted scope (chronium, 2026-10-28)

Start with inherited-state scanout, preserving firmware mode, pixel clocks,
links, scalers and colour programming. Full Intel modesetting is later work,
likely with the Intel GPU driver. Mesa/OSMesa double buffering alone does not
synchronize the kernel's final firmware-framebuffer copy with scanout; see
[display presentation](../kernel/display.md#presentation-timing).

The initial read-only identity probe established the device/profile but could
not prove powered-register accessibility. The accepted next step supersedes
its opt-in boundary: **Linux-style early DC0 admission and reads are ordinary
startup on the matched Intel device.** During development the packet image is
the gate. No `display.inventory.power` option exists. `display.inventory`
remains for the separate Renoir read-only probe; Intel startup ignores it.

## Evidence and profile

Fedora's read-only capture identifies Intel `8086:b0a0`, revision 0, at
`0000:00:02.0`, driver `xe`. It has one active 2880×1800@120 Hz CRTC (DRM 151,
FB 563), primary plane 35 and cursor 145; VRR is off, other pipes/planes inactive.
These are post-Linux-modeset objects, not firmware pipe selectors or proof of
the active framebuffer modifier. Raw captures remain under
`/shared/present/vela-tearing/results/`.

The orchestrator-run PXE identity capture at #1548 `60376e62d` passed with no
hardware writes: stable display GMD `0x07800004` (**30.0, raw step 4/B0**),
BAR0 `0x10012000000`, BAR2 `0x10000000000`, D0. GOP was
`0x10000800000`, 1920×1080, pitch 7680, 8,294,400 bytes, RGB shifts 16/8/0.
The aperture-relative 8 MiB offset did not establish translation or ownership.
Filtered results remain in
`/shared/present/vela-tearing/inventory-native-20261028/results-vela/`.

Chronium reports normal NVMe boot gets 2880×1800. Installed menu generators do
not set a resolution; the deployed NVMe menu has not been read here. Pinned
Limine v12.9.3 supports the per-entry `resolution: 2880x1800x32`, with fallback
if unavailable. The orchestrator added it to the PXE default and confirmed
2880×1800 on vela's early console. Every subsequent capture must record actual
GOP and timing; neither the old 1080p capture nor Linux's 120 Hz proves the
firmware route/timing.

## Power/access contract

The initial startup profile is PCI `8086:b0a0`, display 30.0/B0. GMD is read
directly as in Linux's identification path. Unsupported profiles leave the
existing framebuffer path running, without power writes. Unmatched devices
receive no Intel register work or output.

The boot-only owner runs before AP startup/display initialization, with no
other Intel driver owning the endpoint. Existing PCI D0/decode/BARs must remain
usable. It saves a known inherited DC target (disabled, DC5, DC6 or DC3CO),
then uses the reference's early DC-disable primitive. DC9, mixed/reserved
targets and unknown profiles refuse before the first write.

Only two control locations may be written:

| Register | Permitted changes |
| --- | --- |
| `DC_STATE_EN` (`0x45504`) | Target fields `0x4000000b`; separately clear status29 for the reference's DC3CO exit. Never restore status as a target. |
| `HSW_PWR_WELL_CTL2` (`0x45404`) | Add/remove only this operation's qualified PG1/PG2/PGA/PGB/PGC/PGD DRIVER requests. |

The DC-disable write synchronously restores affected rails/register state;
Linux's bounded rewrite/readback workaround confirms settling (seven matching
reads, at most 100 rewrites). DC3CO exit additionally waits 200–210 µs. Keep DC0
through observation and cleanup. Restore only the saved target fields;
restored eligibility permits asynchronous re-entry, not reversal of residency,
counters or transient exit effects. Other current register fields survive
masked updates.

This does not load/replace DMC firmware, initialize the wakelock protocol,
perform GT forcewake, set clocks, reset the GPU, retrain links, enable an
output or change the mode. Linux initializes power hardware before its own
DMC; a blind wakelock request is not substituted for that bootstrap. The
reference leaves request-before-enable ordering unresolved.

### Qualified inherited wells

DC0 alone is insufficient for independently gated pipes. Qualify each selected
well and its parents with hardware state, fuse distribution and an inherited
BIOS or pre-existing DRIVER request. Preserve existing requests; add only a
missing DRIVER request where BIOS already holds the enabled well. Record
exactly which bits were added. A hardware-on bit without a retained request
is insufficient. PG0 has no software request knob; PW1's HW/DMC-managed label
is not permission to invent a reversible root hold. Skip an unproven ancestor
and its dependents.

Xe3's paths are PG1→PGA/PGB/PG2 and PG2→PGC/PGD. Transcoders A/B use their
reference root domain; C/D additionally require PG2. PIPESRC is pipe-domain
state even though its address is in the transcoder range. No gated well is
enabled just to complete the report. BIOS/KVMR/DEBUG and inherited DRIVER
requests are never cleared.

Read layout only under the qualified domain lifetimes. The readout includes
transcoder timings/route, active plane control/format/stride/size/offset,
programmed/live surface, joiner, cursor and scaler state. Unsupported
nonlinear/DPT/compressed/rotated/protected layouts remain raw observations;
no guessed framebuffer reconstruction. A linear GOP-source candidate is a
register relationship, not flip or backing-ownership qualification.

Xe's GGTT raw-PTE reader is separate from DMC register protection. Read the complete admitted linear-plane span through read-only GGTT MMIO;
compare every PTE twice. For the matched Panther Lake device, compare each
present DM entry with the GOP BAR2-relative device-memory offset, ignoring only
the documented PAT fields. This is a translation check, not a CPU-physical or
allocation-ownership claim. No PTE
allocation/write/invalidation, stolen-memory reuse or unused-range claim is
permitted. Zero PTEs do not establish ownership.

Use one bounded 40 ms frame-timestamp/scanline observation window. Do not force
PSR or panel-replay exit to make counters move. Static observations may reflect
self-refresh and do not prove a dead display or a usable future flip latch.

### Cleanup and uncertain outcome

State/fuse waits use the reference's 1 ms bound. Acquisition failure permits no
dependent read. Before releasing owned requests, confirm inherited holds and
identity; release owned children before parents, then restore the saved DC
target with bounded confirmation. Do not restore stale whole registers or
release a pre-existing request.

A lost inherited hold, uncertain transition or unconfirmed cleanup fences
further probing and retains the remaining confirmed holds and access record/
mappings until reboot. No late acknowledgement permits reclamation, no probe
retry occurs, and there is no routine panic. Report unknown state honestly;
a failed DC transition is not described as confirmed DC0. Existing framebuffer
presentation continues. Retained holds/DC0 can increase power consumption
until reboot. No scanout backing is allocated. Software wait bounds do not
make a stalled CPU MMIO transaction interruptible.

## Native startup qualification (2026-10-28)

The orchestrator-run packet at `ab5e3445b2b6c410509dc4d923b93fb937298b80`
returned `CAPTURED cleanup=confirmed mode=2880x1800 pipes=7 stable=1`.
The single active route is pipe/transcoder A, plane 1: 2880×1800, pitch 11520,
20,736,000 bytes, programmed and live GGTT surface `0xfec00000`. Timing totals
are 3080×3840; scanline and frame-timestamp values progressed. This does not
establish a pixel clock or refresh rate; Fedora's 120 Hz is a different setup.

PG1/PGA/PGB/PG2/PGC were already DRIVER-held; all `added=0`. PGD was skipped
as unqualified. BIOS `0x5405` and DRIVER `0xfc0f` were unchanged after cleanup.
The inherited DC target was already zero, and both confirmations required zero
rewrites. This qualifies the observed inherited-hold/read/release path. It does
not exercise adding a BIOS-derived request, leaving a deep DC state, or an
uncertain transition/cleanup. No plane/GGTT programming occurred. Raw evidence:
`/shared/present/vela-tearing/startup-native-20261028/results-vela/`.

### Format, PSR and the initial framebuffer

`PLANE_CTL=0x84000008` selects enabled, linear 8888, RGBX clear;
`COLOR_CTL=0x2000` has plane alpha disabled. Together with GOP channel shifts
16/8/0 this is **XRGB8888, not a deep-colour plane**. Preserve unknown CTL bit 3.
`PSR_CTL=0x1` and `PSR2_CTL=0x4811` both have enable bit 31 clear;
`PSR_STATUS=0x04000000` decodes idle/full-on link, and PSR2 state is idle.
`TRANS_DP2_CTL=0` has panel replay disabled. Nonzero status/control words do
not establish enabled PSR. This capture needs no PSR-exit operation.

The earlier `GOP-source-candidate=0` compared untranslated addresses. Linux's
`detect_bar2_integrated()` explains the relevant newer integrated layout:
DM GGTT PTE addresses are offsets from GSMBASE, DSM begins at 8 MiB, and its
CPU I/O aperture begins at BAR2+8 MiB. Thus GGTT virtual `0xfec00000` with
first PTE `0x00800003` maps the same *starting device-memory offset* as
GOP `0x10000800000 = BAR2+0x00800000`. The last PTE `0x01bc6003` also agrees.
Zeros at GGTT virtual 8 MiB say nothing about that high virtual mapping.

**All-page equivalence is not yet measured.** The captured firmware code read
only the edges. The active byte range is `[0xfec00000,0xfffc6800)`, covering
5063 pages; every page must map consecutively to device-memory offsets
`0x00800000` through `0x01bc6000`, present with DM set. The follow-up readout
checks every entry twice, reports matched/total pages and the first mismatch,
and corrects the address-space comparison. Even a complete match establishes
an alias, not permission to allocate or overwrite nearby stolen memory.

## Tasks and qualification

- [x] Record inherited strategy, read-only identity and accepted access contract.
- [x] Native PCI/GOP/GMD identity capture; no hardware writes.
- [x] Implement ordinary matched-device admission and powered-state readout.
- [x] QEMU nonmatching startup with VirtIO-VGA and VGA configurations, Remote
  echo, no Intel work/output, and the independent Renoir option retained.
- [x] One hands-free 2880×1800 vela capture at `ab5e3445b`: stable readout,
  inherited DRIVER holds, PGD skipped and confirmed unchanged power controls.
  Native limits and the follow-up complete-PTE check are recorded below.
  QEMU does not qualify Intel MMIO.
- [ ] Decide GGTT/plane programming, backing authority and retirement after the
  actual powered-state evidence. No writes implied by completing the inventory.

**What chronium can do after this slice:** inspect the inherited route and
translation evidence and decide a bounded flip contract. It does not yet make
presentation tear-free. Access limitations are tracked in [#1549](https://git.internal/PyxisOS/pyxis-os/issues/1549).

## Pinned references

Linux v6.19 `05f7e89ab9731565d8a62e3b5d1ec206485eeb0b` is the source reference,
not a claim of identical initialization to observed Fedora 7.2.10-200.fc44.

- [Display identification](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display_device.c): GMD probe/layout, pipe/scaler fuses.
- [Shared display probe](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display_driver.c): power hardware initialization before DMC initialization.
- [Display power](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display_power.c) and [power wells](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display_power_well.c): early DC0, settling, inherited request handoff, parent/fuse waits.
- [Xe3 power map](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display_power_map.c): domain dependencies and HW/DMC-managed ancestors.
- [DMC wakelocks](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_dmc_wl.c): powered/DMC-touched ranges and ordering caveat.
- [Plane readout](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/skl_universal_plane.c) and [Xe initial plane](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/display/xe_plane_initial.c): power-guarded layout and bounded early timestamp wait.
- [Xe GGTT](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/xe_ggtt.c): table extent/location and raw inherited-PTE reader.
- [PHY selection](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display.c) and [PSR workarounds](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_psr.c): Panther Lake has no combo-PHY recovery and this B0 is outside the A0–before-B0 DC5/DC6 workaround.
- [Pinned Limine configuration](https://github.com/Limine-Bootloader/Limine/blob/e3aa138d383ca4fe061f05931b5a54497cd502f2/CONFIG.md): per-entry resolution request and fallback.
