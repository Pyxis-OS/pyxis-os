# Intel inherited scanout: vela tearing

Assigned to Codex alpha (chronium, 2026-10-28). The startup slice admits and observes inherited display state. Synchronized
flips are authorized under the accepted contract below; implementation and
native qualification are pending, so this does not yet remove tearing.

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

The startup inventory permits writes to only two control locations:

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
**PSR and PSR2 are disabled in the captured state.** Pinned Linux's
`intel_psr_regs.h` defines both `EDP_PSR_ENABLE` and `EDP_PSR2_ENABLE` as
bit 31 (`0x80000000`), not bit 0. The exact decoding is:

| Captured register | Relevant field | Decoded result |
| --- | --- | --- |
| `PSR_CTL=0x00000001` | enable bit 31 | Clear: PSR disabled. Low nibble 1 is the idle-frame setting. |
| `PSR2_CTL=0x00004811` | enable bit 31 | Clear: PSR2 disabled. The remaining bits are retained timing/configuration fields. |
| `PSR_STATUS=0x04000000` | state bits 31:29; link bits 27:26 | State 0 (idle), link 1 (full on). Bit 26 is not an enable flag. |
| `PSR2_STATUS=0x04000000` | state bits 31:28 | State 0 (idle), not deep sleep. |
| `TRANS_DP2_CTL=0` | panel-replay enable bit 30 | Clear: replay disabled. |

Nonzero control/status words therefore do not mean self-refresh is enabled.
Preserve these words, including their timing fields; this captured path needs
no PSR-exit operation. A different boot with an enable bit set needs the
separate source/sink handling described in decision 2 below.

The earlier `GOP-source-candidate=0` compared untranslated addresses. Linux's
`detect_bar2_integrated()` explains the relevant newer integrated layout:
DM GGTT PTE addresses are offsets from GSMBASE, DSM begins at 8 MiB, and its
CPU I/O aperture begins at BAR2+8 MiB. Thus GGTT virtual `0xfec00000` with
first PTE `0x00800003` maps the same *starting device-memory offset* as
GOP `0x10000800000 = BAR2+0x00800000`. The last PTE `0x01bc6003` also agrees.
Zeros at GGTT virtual 8 MiB say nothing about that high virtual mapping.

**All-page equivalence is now measured.** The orchestrator-run follow-up at
`4c3164d8207bba63b8a49adf00bcd2ddcf5f0292` returned
`INTEL GOP ALIAS PASS pages=5063/5063 stable=1 cleanup=confirmed`.
Every PTE in active byte range `[0xfec00000,0xfffc6800)` was present, DM set,
and mapped consecutively to device-memory offsets `0x00800000` through
`0x01bc6000`. Both full snapshots matched and the final layout stayed stable:
`GOP-DM-alias=1 matches=5063`, `GOP-source-candidate=1`. Thus plane 1 and the
GOP CPU aperture expose the same framebuffer backing in this boot. The old
zero candidate was the untranslated-address comparison, not different memory.
The format/PSR readings above were unchanged and cleanup again confirmed.
Evidence: `/shared/present/vela-tearing/alias-native-20261028/results-vela/`.
This proves the alias, not authority to allocate or overwrite nearby stolen
memory, nor a flip latch/retirement rule.

## Tasks and qualification

- [x] Record inherited strategy, read-only identity and accepted access contract.
- [x] Native PCI/GOP/GMD identity capture; no hardware writes.
- [x] Implement ordinary matched-device admission and powered-state readout.
- [x] QEMU nonmatching startup with VirtIO-VGA and VGA configurations, Remote
  echo, no Intel work/output, and the independent Renoir option retained.
- [x] One hands-free 2880×1800 vela capture at `ab5e3445b`: stable readout,
  inherited DRIVER holds, PGD skipped and confirmed unchanged power controls.
  Native limits and the complete-PTE follow-up are recorded above.
  QEMU does not qualify Intel MMIO.
- [x] Native complete-plane alias check at `4c3164d82`: all 5063 PTEs matched
  in both snapshots, layout stable and cleanup confirmed.
- [x] Decide flip backing, synchronous operation/power lifetime and retirement
  (chronium, 2026-10-28).
- [ ] Matched 2880×1800 OpenArena presentation baseline before implementation.
- [ ] Implement source-qualified GGTT ownership, owned UMA buffers and synchronous
  correlated flips; no reuse after uncertain completion.
- [ ] Hands-free vela qualification packet and matched presentation comparison.

**What chronium can do after this slice:** inspect the inherited route and
translation evidence and decide a bounded flip contract. It does not yet make
presentation tear-free. Access limitations are tracked in [#1549](https://git.internal/PyxisOS/pyxis-os/issues/1549).

## Synchronized flips — accepted (chronium, 2026-10-28)

The complete-PTE native observation establishes that GOP and plane 1 share
stolen backing through different address spaces in the captured boot. Retest
that relationship at admission; do not allocate a new "initial front" merely
because the untranslated addresses differ.

### Decision 1: starting surface and owned back buffers

**Accepted (chronium, 2026-10-28):** after every active PTE and the layout match, retain
the inherited GOP mapping/backing as the initial front, preserving its entire
GGTT span. Allocate two new, pinned, kernel-owned UMA back buffers at the same
pitch/format; map their device-visible addresses into explicitly owned GGTT
slots with DM clear. The inherited stolen range remains reserved, including
after the first flip; do not overwrite its PTEs or allocate adjacent stolen
storage. A failed alias check leaves the existing copy backend running.

The GGTT address allocator must import inherited mappings and reference-defined
firmware reservations under exclusive device ownership; zero PTEs alone are
not allocation authority. The inherited span crosses Linux's GuC-specific
`0xfee00000` allocator ceiling, which is not permission to truncate it. New
UMA mappings require a proven DMA/IOMMU address contract, CPU WC/cache-alias
handling and the inherited PAT interpretation. No guessed identity DMA or
Linux-initialized PAT indices. If those prerequisites cannot be established,
report the missing contract before broadening this slice. No stolen-memory allocator is authorized.

### Decision 2: synchronous inherited-mode operation and power lifetime

**Accepted (chronium, 2026-10-28):** one pending, synchronous update on the qualified
pipe A/plane 1, leaving mode, clock, link, colour and scaler setup unchanged.
Preserve PSR/replay controls and their retained timing fields: their enable
bits (PSR/PSR2 bit 31, replay bit 30) are clear in the measured boot. Nonzero
`PSR=1`, `PSR2=0x4811` and status `0x04000000` do not contradict this; see the
field decoding above. If a future inherited setup has them enabled, keep
software presentation until a
separate source/sink exit contract is accepted; do not write PSR enable bits
in isolation. Display 30's async selector is SURF bit 0; keep it clear.

For an active flip backend, hold the qualified power domains and DC0 continuously
through all outstanding transactions, rather than releasing startup's holds
between frames. Preserve every inherited request and release only owned ones.
The idle-power cost is accepted for now; runtime power optimization is later work. The
exclusive display owner serializes submissions and validates the inherited
layout/access before writes. Derive the reference scanline keepout from qualified
clock/timing and VRR state before the final SURF arm; defer/coalesce a late
frame. A changing frame is composed/copied into an inactive buffer, with
frame skipping and frozen software capture semantics preserved.

Implementation is assigned to Codex alpha (chronium, 2026-10-29). Capture the
matched presentation baseline first. This is a development-packet write path first. Ordinary startup remains the
qualified inventory plus existing copy path until native flip qualification
and the activation decision. Exact GGTT update/invalidation and plane-register
write masks must be source-qualified before the first write; the current two
power-control write allowance does not silently authorize them. In particular,
Xe GGTT writes include platform workarounds and translation-ordering machinery;
copying PTEs and writing SURF alone is not an implementation plan.

### Decision 3: completion, retirement and uncertainty

**Accepted (chronium, 2026-10-28):** establish a transaction-correlated hardware completion
rule before the first plane write. Linux arms its vblank event inside the update
critical section and waits for flip completion; two equal SURFLIVE reads do
not establish the same guarantee. Qualify the applicable flip-done indication,
vblank progress and expected live surface together, including exactly when old
backing is no longer fetched. Keep the inherited front and both owned buffers
pinned through that proof; admit no second pending flip.

On timeout or ambiguous completion, fence further plane/GGTT writes and retain
every possibly referenced mapping/backing until reboot. Do not reclaim on a
late observation, guess which buffer is free, disable the pipe, or resume
copying into an uncertain front. Continue device/input service and report the
fenced display. Software fallback is available before the first write, or after
a separately confirmed return to the inherited surface; it is not an uncertain
mid-flip recovery. Integrate panic output with the same retained-buffer state
before enabling this backend, so the fallback never targets released storage.

### First task and qualification

The all-page alias capture is complete. First finish the source-level
mapping/cache/update contract, then prepare owned buffers and a single correlated flip/return under
the accepted write list. If the applicable hardware completion or GGTT-access
contract is absent, stop there for the next decision; no speculative native
write is justified by the current inventory.

QEMU checks only non-Intel behavior and software buffer/state ownership. A
hands-free vela packet then checks the one-flip/return proof before repetitive
presentation, followed by desktop/mux/game frame changes, idle frame skipping
and capture/panic behavior. Record native timing and power state independently
of Fedora's modeset. A native tear-free claim waits for this evidence.

**What chronium can do after the first task:** inspect a measured whole-buffer
GOP alias and the exact owned-buffer/write/retirement contract, or one reported
prerequisite that prevents a safe first flip.

## Setup decisions exposed by source qualification (pending)

The accepted buffer/power/retirement decisions stand. No new flip writes have
been implemented. The first implementation step exposed these additional
boundaries, which need decisions before their effects are introduced.

1. **Auxiliary GT access for cache validation. Recommended default:** permit
   only the reference bootstrap access needed to verify graphics PAT index 3,
   not program it: GT forcewake kernel request (`0xa188`, write-mask bit 16
   and request bit 0; acknowledgement at `0xdfc`), the firmware-shared MCR
   semaphore (`0xfd0`, read-acquire/write-1 release), and selector (`0xfd4`,
   group 0/instance 0, multicast bit 31) around the PAT read at `0x480c`.
   Preserve other forcewake owners; follow Linux's multicast selector handoff,
   rather than inventing a selector save/restore. Keep waits bounded and apply
   the accepted fence/retention rule to uncertain ownership/cleanup. Verify an
   inherited UC/noncoherent PAT entry; do not assume Linux initialized the
   firmware's table. Independently identify graphics/media stepping. If the
   media-A0 CPU GGTT-write workaround applies, allow only its documented dummy
   media-GMD write after each 63 PTE writes. No GT reset, firmware load, engine
   submission, PAT programming or generic GT TLB invalidation. Existing display
   DC0/well holds do not substitute for this GT access protocol.
2. **UMA DMA admission. Recommended default:** initially admit new UMA only
   when the relevant VT-d remapper is proven to have translation disabled;
   otherwise retain GOP copy presentation and report why flips are unavailable.
   Read DMAR scopes and remapper status, without changing firmware/IOMMU policy.
   The saved vela DMAR table names a dedicated IGD remapper at `0xfc800000`
   for PCI `0:2.0`; its live translation state remains unmeasured. DM-set stolen
   scanout proves no identity-DMA relationship for DM-clear system pages. Pyxis
   currently has no IGD IOMMU mapping implementation; adding one is outside this
   slice. Absence of a complete remapper-coverage proof also refuses admission.
3. **Presentation baseline versus unattended OLED blanking. Recommended
   default:** authorize a bounded visible baseline/candidate run, with kernel
   idle blanking at 900 s and host collection deadline 840 s; restore the usual
   immediately-blank standing PXE image and reboot through Remote after capture.
   This is a run-sheet exception, not a new shipped blanking default. The normal
   unattended `display.idle_blank=0` collapses blank generations, so whole-frame
   skipping suppresses the presentation workload being compared. No owner
   interaction is required. The timeout still blanks a hung/uncollected run.

### Completion source result (no additional policy decision)

Use a freshly recorded primary-plane flip-done event, not `PIPE_FLIPCNT` as a
retirement event: Intel documents that counter as incrementing at flip start.
The pinned Linux display-30 path uses pipe IIR bit 3 for plane-1 flip done.
IMR controls event recording; IER controls CPU delivery, so selected-bit polling
can keep CPU delivery disabled. Clear only the selected sticky bit to establish
a clean baseline, arm one synchronous SURF update, then require the fresh event,
latched expected SURFLIVE, flip-done timestamp and coherent frame progress.
`PIPE_MISC2`'s flip-information selector must select plane 1; it is itself double
buffered. Keepout uses normalized scanline and the reference 100 µs interval
computed from qualified timing/clock; raw scanline is not a memory-fetch frontier.

The public TGL/RKL register narrative is supported by the pinned Linux PTL
register/mask path, not a claim that a PTL flip has already passed natively.
Qualification must show one confirmed flip and return before repeated updates.
Two fixed, newly created GGTT mappings stay pinned; no per-frame PTE remapping.
Linux's DE-only linear display pin path does not call generic GT invalidation,
so no GuC/TLB-control sequence is inferred from a different consumer.

### Matched baseline definition

Use the existing fixed 1803-frame OpenArena demo (SHA-256
`c02c5a071f520939a33dc00ffd05c8e4f7b9c190b85e9289203cab685c1d8d9f`),
llvmpipe/default workers and an engine canvas of 2880×1800. Record that separately
from physical scanout 2880×1800 and the space's 2880×1768 content area; SDL
aspect-fits the canvas into 2828×1768 with 26-pixel side margins. The older
320×240 timedemo result is not this baseline. Reuse `display.flip.metrics=1`
for cumulative compose/copy statistics; those include startup/session frames,
not just game rendering. No new benchmark framework is needed.

## Pinned references

Linux v6.19 `05f7e89ab9731565d8a62e3b5d1ec206485eeb0b` is the source reference,
not a claim of identical initialization to observed Fedora 7.2.10-200.fc44.

- [Display identification](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display_device.c): GMD probe/layout, pipe/scaler fuses.
- [Shared display probe](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display_driver.c): power hardware initialization before DMC initialization.
- [Display power](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display_power.c) and [power wells](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display_power_well.c): early DC0, settling, inherited request handoff, parent/fuse waits.
- [Xe3 power map](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display_power_map.c): domain dependencies and HW/DMC-managed ancestors.
- [DMC wakelocks](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_dmc_wl.c): powered/DMC-touched ranges and ordering caveat.
- [Plane readout](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/skl_universal_plane.c) and [Xe initial plane](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/display/xe_plane_initial.c): power-guarded layout and bounded early timestamp wait.
- [Stolen device-memory layout](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/xe_ttm_stolen_mgr.c): `detect_bar2_integrated()` GSM-relative DM PTEs and BAR2 CPU access.
- [PSR register definitions](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_psr_regs.h): enable, idle and link-state fields.
- [Plane register definitions](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/skl_universal_plane_regs.h): 8888/channel order, alpha and display-30 SURF async selection.
- [CRTC update](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_crtc.c) and [vblank timing](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_vblank.c): safe update window and completion arming.
- [Xe GGTT](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/xe_ggtt.c): table extent/location and raw inherited-PTE reader.
- [PHY selection](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display.c) and [PSR workarounds](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_psr.c): Panther Lake has no combo-PHY recovery and this B0 is outside the A0–before-B0 DC5/DC6 workaround.
- [Pinned Limine configuration](https://github.com/Limine-Bootloader/Limine/blob/e3aa138d383ca4fe061f05931b5a54497cd502f2/CONFIG.md): per-entry resolution request and fallback.

Additional pinned-source references for setup/completion:

- [PAT table and reads](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/xe_pat.c), [forcewake](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/xe_force_wake.c), and [MCR steering](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/xe_gt_mcr.c): powered/serialized PAT admission.
- [Display pinning](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/display/xe_fb_pin.c) and [workaround rules](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/xe_wa_oob.rules): fixed linear mappings and media-stepping qualification.
- [Intel TGL Display Engine, pp. 198–200, 207, 280–282](https://cdrdv2-public.intel.com/705833/intel-gfx-prm-osrc-tgl-vol-12-display-engine.pdf): sticky event recording/delivery and synchronous flip completion.
