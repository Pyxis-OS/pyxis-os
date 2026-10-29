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
- [x] Matched 2880×1800 OpenArena presentation baseline before implementation;
  one native sample and its counter limits are recorded below.
- [x] Implement source-qualified GGTT ownership, owned UMA buffers and synchronous
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

## Accepted setup contract (chronium, 2026-10-29)

The buffer/power/retirement decisions above remain accepted. The following
source-qualified setup effects are also authorized. The baseline preceded the
development backend below; native flip qualification remains pending.

1. **Auxiliary GT access for cache validation:** permit
   only the reference bootstrap access needed to verify graphics PAT indices 0–3,
   with only the conditional PAT3 programming exception below: GT forcewake kernel request (`0xa188`, write-mask bit 16
   and request bit 0; acknowledgement at `0xdfc`), the firmware-shared MCR
   semaphore (`0xfd0`, read-acquire/write-1 release), and selector (`0xfd4`,
   group 0/instance 0, multicast bit 31) around read-only PAT registers `0x4800` through `0x480c`.
   Preserve other forcewake owners; follow Linux's multicast selector handoff,
   rather than inventing a selector save/restore. Keep waits bounded and apply
   the accepted fence/retention rule to uncertain ownership/cleanup. Verify an
   inherited UC/noncoherent PAT entry; do not assume Linux initialized the
   firmware's table. Independently identify graphics/media stepping. If the
   media-A0 CPU GGTT-write workaround applies, allow only its documented dummy
   media-GMD write after each 63 PTE writes. No GT reset, firmware load, engine
   submission or generic GT TLB invalidation. PAT0–2 remain read-only; the
   conditional PAT3 write below supersedes only that part of the earlier
   no-PAT-programming boundary. Existing display
   DC0/well holds do not substitute for this GT access protocol.
2. **UMA DMA admission:** initially admit new UMA only
   when the relevant VT-d remapper is proven to have translation disabled;
   otherwise retain GOP copy presentation and report why flips are unavailable.
   Read DMAR scopes and remapper status, without changing firmware/IOMMU policy.
   The saved vela DMAR table names a dedicated IGD remapper at `0xfc800000`
   for PCI `0:2.0`; its live translation state remains unmeasured. DM-set stolen
   scanout proves no identity-DMA relationship for DM-clear system pages. Pyxis
   currently has no IGD IOMMU mapping implementation; the
   [IOMMU idea](later-os-directions.md#hardware-dma-isolation) remains outside
   this slice. Absence of a complete remapper-coverage proof also refuses admission.
3. **Presentation baseline versus unattended OLED blanking:** authorize a bounded visible baseline/candidate run, with kernel
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

## Native presentation baseline and remaining tearing (2026-10-29)

Orchestrator-run baseline A on vela, AC via USB-C charger without a dock:
kernel `397a22bff`, userland `8cefca40`, ports `18b56bf`; 2880×1800 GOP and
engine canvas, llvmpipe/default workers, HWP applied on 16 CPUs, existing copy
backend, confirmed Intel startup cleanup. The fixed demo completed 1803 frames
in 115.9 s (15.6 fps); engine frame times min/mean/max/stddev were
41.0/64.3/535.0/11.7 ms. One native sample, not a statistical comparison.
Evidence: `/shared/present/vela-tearing/flip-baseline-20261029/results-vela-A/`.

The existing presenter counters at frame 1800 report composition 2.182 ms and
final copy 4.036 ms per presented frame. Interior deltas over frames 120–1800
are approximately 2.171 ms composition and 4.033 ms copy. These counters run in
the presenter, while engine timing also includes simulation, rendering, OSMesa
readback, SDL scaling/submission and waits; work can overlap across CPUs. Thus
copy is numerically about 6.3% of the engine's 64.3 ms mean, but subtracting it
does not measure pure rendering time. Flips still require a complete staged
copy into the inactive back buffer; this work promises synchronization, not
removal of all copy cost.

Chronium saw no tearing in that low-cadence game run, but reports clear tearing
while typing in the terminal. The exact 2026-10-27 attended image (`d99630dc`,
ports `bf08f5da`) already contained OSMesa double buffering and explicit swaps:
the audit folded the extension into another patch before that image, rather
than removing it. The old/current engine source contents and SDL/Mesa swap
implementations match; only SDL CPU-count configuration changed in the relevant
port paths. Kernel presentation sources are unchanged through this baseline.
Neither build synchronizes the final GOP copy. A copy into a live scanout can
therefore tear when it intersects visible scanning; complete software frames
alone do not prevent that. The visual difference between those unmatched runs
has not been attributed to a specific cause.

The next matched visibility check includes ordinary terminal character output
and scrolling, plus a low-resolution OpenArena run. The 32-pixel bar leaves a
2880×1768 content area; use 360×221 for an exact 8× SDL viewport without changing
its scaling policy. A nominal 320×200 would not be integer-scaled in that content
area. Retain the native-resolution run separately for the presentation-cost
comparison. No physical keyboard input is fabricated by the output script.

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
- [VRR timing readout](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_vrr.c): display30 fixed-refresh timing generator, VMIN/VMAX/FLIPLINE and guardband-derived vblank.
- [Plane register definitions](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/skl_universal_plane_regs.h): 8888/channel order, alpha and display-30 SURF async selection.
- [CRTC update](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_crtc.c) and [vblank timing](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_vblank.c): safe update window and completion arming.
- [Xe GGTT](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/xe_ggtt.c): table extent/location and raw inherited-PTE reader.
- [PHY selection](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display.c) and [PSR workarounds](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_psr.c): Panther Lake has no combo-PHY recovery and this B0 is outside the A0–before-B0 DC5/DC6 workaround.
- [Pinned Limine configuration](https://github.com/Limine-Bootloader/Limine/blob/e3aa138d383ca4fe061f05931b5a54497cd502f2/CONFIG.md): per-entry resolution request and fallback.

Additional pinned-source references for setup/completion:

- [PAT table and reads](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/xe_pat.c), [forcewake](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/xe_force_wake.c), and [MCR steering](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/xe_gt_mcr.c): powered/serialized PAT admission.
- [Display pinning](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/display/xe_fb_pin.c) and [workaround rules](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/xe_wa_oob.rules): fixed linear mappings and media-stepping qualification.
- [Intel TGL Display Engine, pp. 198–200, 207, 280–282](https://cdrdv2-public.intel.com/705833/intel-gfx-prm-osrc-tgl-vol-12-display-engine.pdf): sticky event recording/delivery and synchronous flip completion.

## Development flip backend

`CONFIG_INTEL_INHERITED_FLIP=y` enables preparation in a development image;
its default is off. `display.flip=0` retains copying. The matched Intel route
must have a sole enabled pipe A/plane 1, disabled hardware cursor/other planes,
unchanged linear XRGB8888 geometry, disabled PSR/replay and fixed refresh
(including display30's fixed-refresh VRR timing-generator mode). Admission proves
the full GOP alias, relevant VT-d translation disabled and an inherited GGTT-reachable PAT entry
UC/noncoherent under the bounded reference GT access protocol. Refusal leaves
copy presentation when ownership is confirmed; uncertainty fences access.

Two new PMM-backed WC buffers receive fixed owned GGTT mappings. The presenter
copies a complete immutable composite into an inactive buffer, then arms one
synchronous SURF update outside the context-latency keepout. A selected fresh
plane flip-done event, expected programmed/live address, changed completion
timestamp, frame progress and exactly one flip-start increment must agree.
Hardware latch time is bounded even when CPU observation is delayed. The first
owned-buffer flip retires GOP before one confirmed return to it; subsequent
frames alternate the two UMA backs. Failed confirmation never authorizes reuse,
a late completion or copying into an uncertain front. Frame skipping and frozen
software captures keep their existing handoff; no per-frame PTE updates occur.

Qualified display/DC0 ownership, mappings and scanout storage remain retained
until reboot, including uncertain outcomes. This costs idle power and about
40 MiB of backing at the measured mode; follow-up is [#1565](https://git.internal/PyxisOS/pyxis-os/issues/1565).
Healthy panic presentation covers all three retained surfaces; a fenced backend
uses the existing serial-only panic path. Neither failure-path nor optical
tear-free behavior is natively qualified yet. QEMU cannot emulate this Intel
register path, so its nonmatching boots cannot establish latch/retirement.

Validation at code head `b4a86176e`: full default-off and enabled kernel builds
pass with the unchanged `8bdf136` compiler container. Nested KVM/Q35, host+invtsc,
4 CPUs/8 GiB, VirtIO-VGA with `display.flip=1` and VGA with `=0` each pass Remote
echo, ordinary terminal glyph/scroll completion, the fixed 1803-frame 360×221
llvmpipe demo and ordinary presentation metrics; neither admits Intel access or
panics. These are correctness checks, not timing or Intel-MMIO qualification.
An initial packet fixture invoked the workload from multiple spaces; it was
corrected to Development-only startup before both passing checks.

The hands-free candidate uses one common image and changes only
`display.flip=0/1` between boots, preserving the baseline engine and data. It
includes terminal output/scrolling, the exact 8× low-resolution run and the
native-resolution cost run. Its collector requires actual flip admission,
initial owned flip/GOP return, repeated correlated completions and zero timeouts;
an ordinary fallback is a reported refusal. Packet:
`/shared/present/vela-tearing/flips-native-20261029/`. Native execution remains
with the orchestrator; this record does not claim that its checks have run.

### First flip A/B admission correction (2026-10-29)

The orchestrator's common-image A completed terminal output/scrolling and both
1803-frame demos: 360×221 at 47.1 fps (38.3 s), native canvas at 15.6 fps
(115.6 s). B refused before any GGTT/plane programming. Its inherited values
were CONF `c0000000`, plane CTL `84000008`, COLOR `2000`, MISC2 `8000`,
IIR `1`, VRR_CTL `a40007f7`. MISC2's plane selector (bits 2:0) is zero;
IIR bit 0 is outside the selected fault mask; the format matches the earlier
inventory. The bit-31 VRR-generator rejection was incorrect for display30.

Pinned Linux `intel_vrr_always_use_vrr_tg()` uses the VRR timing generator for
fixed refresh on display30+. `intel_vrr_get_config()` distinguishes fixed
refresh through equal nonzero decoded VMIN/VMAX/FLIPLINE, uses VMIN for VTOTAL
and subtracts the guardband for vblank start. A set VRR_CTL enable bit alone
does not imply variable refresh. The earlier “VRR off” observation described
Fedora's post-modeset CRTC property; the Pyxis inventory did not capture these
firmware timing-generator registers. Their range is not inferred from the
control word and must be measured on the next admission.

The corrected backend reads and reports those inherited registers, admits
fixed refresh using the same Linux readout, retains them unchanged, and uses
the derived update boundary for keepout. Unequal timing limits or CMRR retain
the copy path because the fixed-cadence proof does not cover them. No timing,
VRR, push or mode register is written. Evidence remains under
`/shared/present/vela-tearing/flips-native-20261029/results-vela-A/` and
`results-vela-B/`; the refused B is not a completed flip qualification.

### Bounded setup after the r2 boot stall

The orchestrator's r2 boot left the early console frozen and never became
Remote-ready; no target log was captured, so the precise stalled instruction
is unobserved. Source review found no pre-AP flip-done wait: SURF submission
happens later in the presenter. The concrete pre-AP cost was the full GGTT
import: 2048 lazy page mappings each called the kernel physical-alias scan,
walking roughly 139000 initrd pages again (about 285 million page queries,
each walking scratch-mapped page tables). This runs before AP/network startup.
It is finite but plausibly explains the prolonged early-console silence.

The import now retires its small read-only inventory windows, validates the
whole 8 MiB physical span once, and reserves/maps it as one read-only CPU range.
The same physical-exclusion proof remains; only selected owned PTE pages later
become writable. Setup has a five-second software deadline, including bounded
mapping, import and scanout-allocation loops, with phase/refusal messages.
Existing timing qualification (200 ms), forcewake ACK (50 ms), MCR semaphore
(10 us), power settling and runtime flip completion remain bounded. These
software bounds assume individual MMIO instructions and the qualified clock
continue to make progress; no bus-hang watchdog is introduced.

A refusal before SURF keeps ordinary copying. After partial auxiliary/GGTT
setup, it may retain resources/holds and fence register writes while preserving
CPU copies only if the unchanged active GOP front is re-proven. After an
ambiguous SURF completion, the accepted fence/retention rule still applies:
an ordinary copy into an uncertain front is not a safe fallback. The scheduler
and Remote do not depend on a flip-done interrupt. Native qualification remains
open. The collector now persists its initial result, reports no beacon/Remote
readiness within its configurable 60-second default, and saves interruption
results even before a connection exists.

### R3 native prerequisite result and cache-policy decision

R3 boots and cleans up normally. Captured VMIN=VMAX=FLIPLINE=`0xeff`
confirms fixed refresh. The measured period 16663 us is approximately60.01Hz:
firmware uses htotal3080/vtotal3840, while Fedora's captured120Hz mode uses
htotal3080/vtotal1920 at essentially the same709.6MHz pixel clock. No refresh
change is authorized here; the inherited mode is preserved. Chronium accepts
this inherited 60 Hz mode for now (2026-10-29); 120 Hz belongs to later full
Intel modesetting ownership.

DMA admission is measured with GPU DRHD `0xfc800000`, GSTS `0x40000000` twice:
translation-enable bit31 clear. GraphicsGMD `0x07804000` is30.1A0; mediaGMD
`0x07800004` is30.0B0, so the media-A0 workaround is inapplicable. The next
refusal is exactly inherited graphicsPAT3=`0`, expected`0x3c`. Forcewake is
released and display cleanup confirmed; no scanout backing or plane write
is reached. Native flips remain unqualified.

Pinned xe `xe_pat_init_early()` chooses its Xe2/Xe3 table for graphics30;
`xe_pat_init()` programs that table during GT initialization under forcewake,
using multicast graphics writes. The display pin path selects `XE_CACHE_NONE`
(index3 in xe's table). Its value`0x3c` means L3UC/L4UC/no-snoop/compression-off;
zero means L3WB/L4WB/no-snoop/compression-off, not uncached. CPU PAT5/WC and
SFENCE do not establish a GPU cache invalidation protocol. Accepting zero
could therefore produce stale scanout on reuse; this is an unqualified cache
contract, not evidence of an invalid address or a guaranteed crash. On this
profile Linux classifies PAT as INSTANCE0, so the selected(0,0) read is not a
guessed fused-off target.

**Accepted (chronium, 2026-10-29), option 1:** retain no PAT writes; inventory
GGTT-reachable entries0..3 and reuse an existing UC/noncoherent/compression-off
entry if present, encoding its index in only the new UMA PTEs. Leave all entries
and the inherited GOP PTEs unchanged. If none qualifies, log the reason and
keep ordinary copy presentation; never refuse the boot. Only PAT3 was
captured, so another usable entry must not be assumed.

The implementation reads each of entries0..3 twice under the existing bounded
forcewake/MCR protocol and chooses the first whose L3/L4 policies are UC,
coherency is no-snoop and compression is disabled. CLOS/no-promote do not
change that uncached policy and are preserved. The selected index is encoded
only in the two new UMA back buffers; table and GOP PTEs remain unchanged.
The existing phase timestamps remain enabled for the next native capture.

Alternatives not selected:

- Permit PAT programming as a revision of the accepted no-write boundary.
  Prefer a single proven-unused reachable slot over xe's entire32-entry GT
  initialization. Before any write, establish its consumers, required GT/cache
  quiescence and scope; unused GGTT entries alone do not prove no PPGTT user.
  Do not rewrite the index selected by the inherited GOP. This requires a
  separate write/ownership decision after the evidence, not a silent table
  replacement under the display-only owner.
- Keep copying and defer the missing PAT initialization to fuller GPU ownership.
  Safe with today's data, but this hardware remains without synchronized flips.

Changing CPU WC to UC, or simply accepting cacheable PAT0, is not a substitute
for a device-cache contract. A more aggressive cacheable/flush path requires
reference-proven device cache maintenance and is outside this slice.

### Startup latency follow-up

R3 still showed the early console for multiple seconds. Its packet autostarts
terminal output and both OpenArena demos; the collector only observes them.
The orchestrator's Remote reboot explains the reported reset. The retained
capture contains only Intel lines, not a fault log, so it cannot establish a
separate application fault. Future collectors retain a masked full kernel log
and available application/phase logs before returning an admission refusal.

The remaining source-visible work repeats physical-alias scans for display
and GT register windows and imports the entire GGTT before checking PAT.
The implementation now batches the exact existing MMIO spans into one kernel
mapping walk per group, validates contiguous inventory PTE windows once,
checks PAT before the full import, and removes the duplicate40ms timing
observation when flip timing qualification follows. No exclusion or ownership
rule changes. Development phase logs include target t-us/elapsed-us/step-us,
plus the presentation handoff timestamp. Native phase attribution remains to
be measured; no guess about forcewake/DC0 consuming their timeout is recorded
as a measurement.

### PAT option-1 qualification

A host check compiles the actual selector from `gt_profile.c`: UC in each of
indices0–3 selects and encodes that index without changing the physical PTE
address; first-qualified ordering and CLOS/no-promote preservation pass.
Compression-enabled, coherent, L3/L4-cacheable, all-zero and all-ones tables
are refused. This is CPU-only policy/encoding evidence, not a GPU readback.
The next native packet must capture all four real values before claiming a
usable inherited entry or synchronized flips.

### R4 native result and register-window cost — 2026-10-29

R4 boots with confirmed cleanup but still uses ordinary copying:
PAT0=PAT1=PAT2=PAT3=`0`. There is no reachable uncached policy to reuse.
Evidence: `/shared/present/vela-tearing/flips-native-20261029-r4/results-vela-B-r4/`.
The native phase deltas are register windows376.914ms, inherited alias202.709ms,
GT identity windows187.890ms, GT cache windows188.020ms; copy-backend-ready
is1175.593ms from display preparation start. GT forcewake acknowledgment is
0.580ms, PAT reads0.267ms and GT cleanup0.292ms. These do not identify a
hardware wait of190ms.

Source attribution: display GMD and remaining register windows each perform a
full kernel physical-alias scan; GT's two identity windows share one scan, and
its remaining cache windows share another. Each scalar page query repeatedly
maps/unmaps page-table scratch slots, invalidating them at each descent.
The window pages themselves are mapped once, not repeatedly. GOP inspection
and VT-d admission add their own scans. VA reservation uses a linked free-range
list, not a bitmap; PMM starts its bitmap search at its maintained first candidate.

A disposable, never-merged observer called through GDB measured the components
with in-guest monotonic timestamps. One matched run per version, nested KVM,
invariant TSC, 8GiB guest, same569479680-byte archive. This is not a native boot
measurement. Baseline registered candidate pages157569, fixed157570 (one extra
kernel page); both have20 VM ranges,40960 PMM bitmap words and first candidate831.

| Component | Before | Leaf-batched query |
| --- | ---: | ---: |
| VA reservation, one page | 0.150ms | 0.152ms |
| PMM allocate one frame | 0.139ms | 0.047ms |
| MMIO map one page | 0.148ms | 0.049ms |
| Protect that page RO | 0.141ms | 0.048ms |
| Alias query, one physical span | 175.304ms | 0.626ms |
| Alias query, sixteen spans | 183.257ms | 1.921ms |
| Sixteen warmed reserve/map/RO/unmap/release cycles together | 0.133ms | 0.132ms |

These samples attribute the long path to alias queries, not PMM or VA allocation;
cold single-call differences are not allocator performance claims. A32GiB guest
could not start (`Cannot allocate memory`), so RAM-size scaling on vela was not
measured. No VM-host memory policy was changed.

The correction is in shared MM/paging: scan each present leaf table through one
scratch mapping, skip absent ancestors' complete spans, and release the short
snapshot guard between leaf tables. Every present frame is checked against the
same requested physical extents; the structural owner, reserved-memory/PCI/GOP
exclusions, corruption checks and cleanup remain. No cached alias proof, retained
scratch pointer, allocator change or hardware write is introduced. This removes
per-page scratch invalidations without weakening the mapping checks.

CPU-only QEMU/GDB checks pass for full2048-page RO/NX/UC spans, native-size5063-page
WC backing/zeroing/cleanup, mapped-RAM aliases in either vector order, zero/overflow
refusal, six first/last leaf slots across three tables, byte4095 intersections,
adjacent absent frames, and an empty8GiB reservation. The observer is excluded
from the final kernel. Small evidence is in
`/shared/present/vela-tearing/window-cost-20261029/`. Native startup cost after
this correction and native flips remain unqualified.

### What the zero PAT entries mean

An index selects the contents of a GPU PAT entry; index0 is not inherently WB.
For the matched graphics30/Xe2-style encoding, value0 is L3WB, L4WB, no-snoop,
compression-off, CLOS0 and promotable. Thus all four reachable indices currently
select that same cacheable policy. This does not establish why firmware left
them zero or what the unreachable entries contain. CPU x86 PAT/WC is a separate
mechanism; CPU stores/SFENCE alone do not supply GPU cache invalidation.

Pinned Linux `xe_pat.c` selects the Xe2 table for graphics30 and programs it at
GT initialization. Reachable entries0/1/2 are respectively`0x0c/0x0e/0x0f`
(L3WB/L4UC with no-snoop/one-way/two-way coherence); entry3 is`0x3c`
(L3UC/L4UC/no-snoop/compression-off). Display `xe_fb_pin.c` uses
`XE_CACHE_NONE`, which this profile assigns to index3. Firmware's zero table
therefore does not satisfy the cache policy Linux establishes for new display
buffers.

**Accepted (chronium, 2026-10-29), narrowed option 2:** program ONLY graphics
PAT entry3 to Linux xe's display value`0x3c` (L3/L4UC, no-snoop, compression-off),
leaving entries0–2 unchanged. This explicitly revisits the earlier no-PAT-write
boundary. Before writing, prove that no existing present GGTT PTE, including
GOP's complete span, and no other known consumer selects index3. Use the
reference forcewake/MCR protocol and log before/after. A failed proof makes no
PAT write and keeps ordinary copying; it must not refuse boot.

The MM batching foundation is split into [#1587](https://git.internal/PyxisOS/pyxis-os/pulls/1587),
based on main, with its own CI. #1563 is linearly based on that published head.
Merge order: #1587, then #1563. The MM bodies have no remaining diff in #1563.

### Remaining prerequisite: non-GGTT PAT consumers

The accepted conditional PAT3 write is not yet implemented: the available
readout does not prove absence of inherited PPGTT consumers. Pinned xe stores
PPGTT roots in logical context state (`xe_lrc.c:1005–1010`) and selects PPGTT
through context-descriptor privilege (`1513–1521`); the GGTT scan alone does not
cover those mappings. `GUC_STATUS.GS_MIA_IN_RESET` is a GuC-reset indication,
not an all-consumer certificate. GSC can survive GT reset and D3hot, and its
loaded-status read itself needs GSC forcewake beyond the current GT-only holds.

A tempting legacy shortcut is invalid: the Gen12 PRM, Vol2c Part1, printed
page1036, limits `GFX_MODE` bit9 to ring scheduling; execlist mode selects through
context descriptors. The bit also does not take effect until the page-directory
registers are programmed. A zero read therefore cannot certify GGTT-only access.
No graphics30 replacement certificate was found in the pinned xe code. These
are source limits, not a measurement that vela actually has an active PPGTT user.

**Accepted (chronium, 2026-10-29):** retain copying and extend a
bounded read-only inventory to the fused engine/context and GuC/GSC consumers,
with the additional Linux-qualified forcewake holds explicitly listed before
implementation. No reset, stop-ring, firmware command or submission. Keep the
PAT3 write conditional; unestablished consumer ownership still means no write.
This explicitly extends the earlier GT-only power/access fields. It does not
relax the separate condition on programming PAT3. A complete inherited
consumer proof may require a later GT takeover rather than status sampling.

References at Linux `05f7e89ab9731565d8a62e3b5d1ec206485eeb0b`:
[context roots and descriptors](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/xe_lrc.c),
[GSC lifetime](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/xe_gsc.c),
and [Gen12 PRM, Vol2c Part1](https://cdrdv2-public.intel.com/703046/intel-gfx-prm-osrc-tgl-vol-02-c-command-reference-registers-part-1.pdf).
R5 is the newly authorized observation packet, not a write-capable flip run.
It collects this missing evidence while retaining ordinary copying.


### Implemented bounded consumer inventory (r5)

Rebased on main including merged MM query PR#1587. The development packet is
the gate; no new boot option or public ABI is introduced. For the matched
graphics30.1/media30.0 topology, read CCS presence from`0x9114[19:16]` and
media presence from`0x389140[7:0,19:16]` under the corresponding GT forcewake.
One means present. Include RCS0, BCS0 and BCS8, all fuse-present CCS0–3,
VCS0–7 and VECS0–3, and GSCCS0. Do not apply Linux software CCS reductions or
its own missing-firmware filter to inherited state. Media GSI translation
applies only below`0x40000`; engine bases and GSC HECI1 remain untranslated.

Each present engine is read twice under its matching acknowledged domain:
head, tail, control, MI_MODE, execlist status low/high, current LRCA and mode.
Both GuC status registers and GSC FWSTS1 are read twice. Logs retain raw values
and a stability observation; no idle/reset/loaded bit is treated as proof that
all inherited PPGTT consumers are absent. No context-memory pointer is followed.

Only kernel request bit0 with write-mask bit16 is written on these controls:

| Domain | Control | Acknowledge |
| --- | --- | --- |
| Main/media GT | `0xa188` / `0x38a188` | `0xdfc` / `0x380dfc` |
| Main render | `0xa278` | `0xd84` |
| Present media VDBOX n | `0x38a540+4*n` | `0x380d50+4*n` |
| Present media VEBOX n | `0x38a560+4*n` | `0x380d70+4*n` |
| Media GSC | `0x38a618` | `0x380df8` |

Preserve inherited kernel requests; release only added requests, children before
parents. ACK waits keep the Linux50ms bound. Unknown acquisition/cleanup fences
and retains mappings/holds until reboot; ordinary CPU-copy boot may continue
with a proven unchanged GOP. CPU mapping refusal cleans up confirmed resources.
Other consumer/status windows are RO/NX/UC and map once with batched alias proof.

This **read-only observation slice intentionally ends before write admission**,
even if a usable inherited PAT entry happens to exist: no PAT, GGTT PTE or plane
write is issued. It returns to copying after confirmed GT cleanup. PAT3=0x3c
remains authorized only after the independent complete-consumer proof succeeds;
this inventory does not fabricate that proof from raw samples. No reset,
submission, firmware command, modeset or arbitrary context-memory read occurs.

R5 is hands-free, immediately blanks the OLED on presentation, has no game or
benchmark autorun and leaves NVMe writes off. It includes a log reader rebuilt
for current main's log wire format; the other exercised file/launch ABI remains
unchanged from the qualification archive. The Remote collector saves masked
kernel/Intel logs and checks topology, two samples per present engine, GuC/GSC
records and joined release/cleanup. `CAPTURED` means those observations arrived;
it is not a flip, absence or optical-tearing qualification.

Qualification before the native inventory: actual-code host checks pass for
all20 potential engines, zero/single/full fuse masks and the write whitelist
(rejecting PAT, engine mode/control and GuC command writes). CPU-only QEMU/GDB
checks pass for25 RO/NX/UC inventory windows, idempotent reuse, capacity refusal
without mutation and two complete cleanup cycles; no synthetic MMIO window is
dereferenced. Independent pinned-source review found no topology, offset,
parent-domain or cleanup defect. These checks do not emulate GT hardware or
qualify native forcewake/status reads; r5 supplies those observations.
