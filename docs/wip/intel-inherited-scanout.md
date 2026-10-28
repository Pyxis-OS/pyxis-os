# Intel inherited scanout: vela tearing

Status: inherited-state investigation accepted (chronium, 2026-10-28),
assigned to Codex alpha. The first implementation is an opt-in, read-only
inventory; GGTT/plane writes remain unapproved.

## Problem and evidence

Vela's applications supply complete frames, but the kernel uses its firmware
framebuffer copy path. The only synchronized physical-flip backend and hardware
timing observer are Renoir-specific. OSMesa double buffering protects the
application image; it does not synchronize the final copy with Intel scanout.
See [display presentation](../kernel/display.md#presentation-timing) and the
[native llvmpipe record](../development/experiments/llvmpipe.md).

The read-only Fedora capture identifies Intel `8086:b0a0`, revision 0, at
`0000:00:02.0`, driver `xe`; only eDP is connected. A subsequent `modetest -M xe
-p` snapshot has one active CRTC (DRM ID 151), framebuffer 563, 2880×1800 at
120 Hz, VRR off. Its primary plane is ID 35 and its cursor plane ID 145,
framebuffer 557. The other three CRTCs and 22 planes are inactive.

These are Linux DRM object IDs, not hardware register indices. The listed
LINEAR/tiled/compressed formats are capabilities, not evidence of the active
framebuffer's modifier. Linux has already initialized and modeset its display;
this snapshot cannot establish the route that firmware leaves to Pyxis.
The generic `Intel Xe2 Graphics` banner is not the display-IP version:
Linux identifies this PCI family as Panther Lake and reads display `GMD_ID`.

Raw captures and a compact decoded summary remain under
`/shared/present/vela-tearing/results/`; no raw dump is committed. KScreen's
headless query timed out; the DRM snapshot is sufficient for this reference.
No further Fedora capture is required for the next step.

## Native identity — 2026-10-28

The orchestrator-run PXE capture of #1548 at `60376e62d` passed the partial
identity check: PCI `00:02.0`, `8086:b0a0`, revision 0, D0; BAR0
`0x10012000000`, BAR2 `0x10000000000`; stable display GMD `0x07800004`
(**30.0, step 4**). GOP was `0x10000800000`, 1920×1080, pitch 7680,
8,294,400 bytes, RGB shifts 16/8/0. BAR2-relative placement is 8 MiB;
translation and active-plane ownership remain unproven. No hardware writes
were made. Raw filtered results remain with the sealed native packet under
`/shared/present/vela-tearing/inventory-native-20261028/results-vela/`.

This is a **PXE-path mode**, not a panel limit. Chronium reports the normal
NVMe firmware/Limine boot supplies 2880×1800. The installed menu generators
set no resolution; the deployed NVMe menu has not been inspected here.
Pinned Limine v12.9.3 (`e3aa138d383ca4fe061f05931b5a54497cd502f2`)
supports the per-entry line `resolution: 2880x1800x32`; it may select another
mode if the firmware does not offer that one. The next capture must report
its actual GOP and hardware timing instead of assuming either resolution,
Linux's 120 Hz mode, or an unchanged firmware route.

## Initial strategy — accepted (chronium, 2026-10-28)

Pursue an inherited-state backend, beginning with a
read-only native inventory. Preserve firmware mode, clocks, links, power state,
scalers and colour configuration. No general Intel modesetting or rendering
engine is part of this task. First establish the active pipe/plane, framebuffer
translation and timing; propose the exact write/ownership contract afterward.

Full Intel modesetting ownership is later work, likely with the Intel GPU
driver. It is outside the inherited-state inventory and any later bounded
scanout implementation.

This decision does **not** authorize unused-GGTT claims, page-table writes,
stolen-memory reuse or a plane-address update. If a spare surface needs any of
those, its authority, backing ownership, invalidation and retirement rules must
be settled when the native evidence establishes that need. Zero entries alone
are not proof that a GGTT range is available. No framebuffer-copy timing or
application FPS cap will be advertised as a tear-free guarantee.

## Task 1 — read-only inherited inventory

- Scope the initial native probe to PCI `8086:b0a0`, deriving BARs and display
  layout from the device and the reference's GMD decoding, not from the Fedora
  addresses or its banner. Reuse `display.inventory=1` for opt-in reporting;
  default boot has no new probe work or log lines.
- Follow the reference's power-domain guards. Read a register only where its
  accessibility under the inherited state is established, without requesting
  or enabling a power well, forcewake, clock, interrupt or firmware operation.
  An unproven/unsupported state stops that portion of the inventory and leaves
  the existing display path running.
- Record active pipe/transcoder/plane controls, format, stride, size, offsets,
  surface/live-surface and applicable scaler/cursor state. Relate the scanout
  surface to the GOP aperture and bounded GGTT entries. Report mismatches and
  unsupported layouts; do not manufacture a match from equal dimensions.
- Sample the reference's scanline/frame-timestamp fields with bounded waits
  and stability checks. This establishes whether a later confirmation rule can
  observe the actual inherited scanout; it grants no write permission.
- Build and boot the omitted/nonmatching paths in QEMU. QEMU cannot qualify
  these Intel registers. Deliver a matching kernel/archive pair for one
  orchestrator-run, hands-free native capture through UDP/Remote. No attended
  repeated samples, test image autorun or hardware writes are needed.

**What chronium can do after this task:** read one compact native report showing
whether Pyxis's inherited GOP surface, active Intel plane and translation agree,
with the evidence needed to choose a bounded flip implementation. This first
task does not yet remove tearing.

## Pinned source basis

Linux v6.19, commit `05f7e89ab9731565d8a62e3b5d1ec206485eeb0b`, is the initial
read-only source reference; Fedora's observed kernel was 7.2.10-200.fc44.
The source pin is not a claim that those kernels have identical initialization.

- [Display identification and GMD probe](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display_device.c):
  `ptl_desc`, `probe_gmdid_display` and the GMD-to-layout map.
- [Universal-plane readout](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/skl_universal_plane.c):
  `skl_plane_get_hw_state` first checks the enabled power domain;
  `skl_get_initial_plane_config` reads and interprets the inherited surface.
- [Xe initial-plane handling](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/display/xe_plane_initial.c):
  initial scanout backing reservation and the bounded early frame-timestamp wait.
- [GGTT implementation](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/xe_ggtt.c):
  page-table location/extent and the distinction between reading existing PTEs
  and allocating, programming and invalidating owned mappings.

## Task 1 implementation boundary

The first probe reports PCI identity, existing D0/memory decode, BAR0/BAR2,
GOP dimensions/pitch/colour shifts, and two `GMD_ID_DISPLAY` reads with a final
PCI/BAR stability check. The GMD page is temporarily mapped read-only, NX and
uncached; no PCI configuration or GPU register is written. All work and output
are behind `display.inventory=1`. An absent/nonmatching device is reported
without mapping its registers.

**Powered-state inventory remains unavailable, rather than guessed safe.**
The pinned reference exposes a prerequisite that a visible GOP image does not
establish: on display version 30, `intel_de_read()` can acquire a DMC wakelock
with a register write. Its `powered_off_ranges` include the pipe, transcoder,
plane, cursor and scaler registers (`0x60000..0x7ffff`). Its DC5/6 DMC-touched
list also includes `DC_STATE_EN`, power-well control registers and the DMC
register block. Reading those registers is therefore not an independently
proven write-free power guard. Linux's initial power setup disables DC states
with a write and then acquires power references; this probe does neither.

The native packet deliberately reports that limit and leaves those registers,
GGTT entries and timing samples unread. It can establish the actual firmware
GOP/PCI/GMD identity, but cannot yet establish the active plane, surface
translation, latch or retirement rule. No writes are authorized by these
partial observations. A later access contract requires source evidence and
chronium's decision before any wake/power operation is introduced.

Additional pinned references:

- [DMC wake-lock ranges and read protection](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_dmc_wl.c):
  `powered_off_ranges`, `xe3lpd_dc5_dc6_dmc_ranges` and `__intel_dmc_wl_take`.
- [Display reads](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_de.h):
  `intel_de_read` takes/releases the DMC wakelock.
- [Power-well transitions](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display_power_well.c):
  `gen9_set_dc_state` explains why disabling DC requires an MMIO write.

## Powered-state access — accepted (chronium, 2026-10-28)

Linux-style early DC0 admission and inherited-state reads are ordinary display
startup on the matched Intel device. There is no Intel inventory opt-in and no
`display.inventory.power` option. During development the packet image is the
gate. The existing `display.inventory` option remains for its independent
Renoir read-only inventory; Intel startup does not consult it.

Keep qualified inherited wells and parents held without clearing BIOS requests;
skip gated/unproven domains, with no plane/GGTT programming or forced PSR exit.
Release only our holds; uncertain outcomes fence further probing and retain
confirmed resources/records until reboot, with a report and no routine panic.

The PXE default entry now requests `resolution: 2880x1800x32`; the orchestrator
confirmed vela's early console reports 2880×1800. Future captures still record
actual GOP and timing, without reinterpreting the earlier 1080p identity run.

### Reference-backed primitives and their limits

The pinned Xe driver calls the shared i915 display probe. In
`intel_display_driver_probe_noirq`, power-domain hardware initialization occurs
before `intel_dmc_init`. For this generation, `icl_display_core_init` starts
with `gen9_set_dc_state(DC_STATE_DISABLE)`. Its documented hardware guarantee
is that the disabling MMIO write completes after the affected resources and
register state have been restored. `gen9_write_dc_state` contains the bounded
rewrite/readback workaround. DC3CO has an additional exit sequence and 200 µs
settling interval. This provides an early admission primitive; it does not
justify transplanting Linux's complete clock/PHY/DBUF initialization.
The reference's combo-PHY recovery path is inactive for Panther Lake; its
display-30.0 DC5/DC6 PSR workaround covers A0 up to, but excluding, B0.
Keep the initial powered-access profile scoped to the observed B0 device;
other steppings need their own prerequisite/erratum check.

A DMC wakelock is not a replacement for pipe-domain ownership. Linux enables
its wakelock protocol before requesting it, waits at most 5 ms for ACK, and
still holds the relevant domain and parents. Its source explicitly leaves
request-before-enable safety unresolved. Raw wakelock control access is an
intentional bootstrap path, so the broad DMC register range alone is not a
proof that the control cannot be accessed. However, neither GMD identity nor
an idle request bit proves inherited protocol initialization. Blindly asserting
REQ and hoping for ACK is not the recommended next step.

Linux's `hsw_power_well_sync_hw` copies an existing BIOS request into the DRIVER
request register before removing the BIOS request. A well stays on while any
requester holds it. The proposal borrows the additive hold but **omits BIOS
request removal**. It does not run the full enable/disable sequence for an
otherwise gated well, whose additional IRQ/VGA work is outside this slice.
The Xe3 hierarchy is PG1 → PGA/PGB/PG2, PG2 → PGC/PGD; PG0 has no software
request knob and PW1 is HW/DMC-managed. Domain and fuse checks must follow
that map, not an invented BIOS-request requirement for every ancestor.

### Ordinary matched-device admission

The qualified `8086:b0a0` / display-30.0 B0 device saves the inherited DC target
and uses the
reference's early DC-disable primitive at `DC_STATE_EN` (`0x45504`). Keep DC0
through the entire snapshot, then restore only the inherited target fields.
Preserve unrelated register bits through masked operations. Follow the
reference's rewrite/readback bounds and DC3CO exit handling. Admit the known
targets disabled, DC5, DC6 or DC3CO; refuse DC9, mixed/reserved targets or an
unqualified profile before the first write. The target-field mask is
`0x4000000b`. DC3CO exit separately clears status bit 29 and waits 200–210 µs;
that status bit is never saved/restored as a target. Successful restoration
restores sleep eligibility, not historical residency, counters or an identical
instantaneous power state.

This temporarily changes sleep eligibility; it does not set a pixel clock,
power up a new output, retrain a link or change the mode. Do not load/replace
DMC firmware, set `DMC_WAKELOCK_CFG`, use an unqualified wakelock request, or
perform GT forcewake. No DMC handshake is required for this early DC0 path.
Other devices/profiles remain on the existing firmware-framebuffer path; this
startup does not initialize an otherwise gated output.

### Retain only qualified inherited resources

Under that window, serialize a single kernel inventory
owner before AP startup, display initialization and any other Intel register
writer. Require the existing PCI D0/decode/BAR identity and reject an endpoint
already owned by another driver. Temporarily add only missing DRIVER requests
for the already-enabled BIOS-held wells needed by a candidate pipe and its
parents, following the additive part of Linux's handoff. Preserve BIOS, KVMR,
DEBUG and pre-existing DRIVER requests. Treat HW/DMC-managed ancestors by their
reference rules and verify the relevant state/fuse acknowledgements. Keep an
explicit journal of request bits actually added by this operation.

The permitted additional write location is `HSW_PWR_WELL_CTL2` (`0x45404`),
limited to the qualified PG1/PG2/PGA/PGB/PGC/PGD request fields and only where
the reference's parent/admission conditions hold. An off, unqualified or
unretainable domain is skipped; never enable an inactive pipe just to read it.
A sampled status bit without a retained lifetime is insufficient.

Read pipe/transcoder/plane/cursor/scaler state only under those lifetimes.
Keep all other MMIO mappings read-only. GGTT reads use Xe's separate raw-PTE
read path, under stable PCI/MMIO lifetime, bounded to the GOP and admitted
surface extents; they do not authorize PTE allocation, writes or invalidation.
Report raw unsupported tiling/DPT/format instead of guessing translation.
Repeated snapshots detect changing configuration and invalidate the affected
interpretation; zero PTEs never establish unused ownership.

Do not disable PSR/panel replay to manufacture moving timing. Use one bounded
40 ms observation window, following Xe's early frame-timestamp wait. Static
fields may reflect self-refresh: report timing unproven without treating that
as a dead display. Every report includes the actual GOP/mode and stable field
values; neither the 1080p PXE capture nor Linux's 120 Hz establishes NVMe-boot
timing. Mode/plane/GGTT programming remains a later decision.

### Bounded cleanup and explicit uncertain state

Use the reference's bounded DC rewrite/readback and
1 ms power-state/fuse waits. Acquisition failure authorizes no dependent
register reads. On success, release only our added request bits in reverse
dependency order while the original inherited holds are still established;
then restore the saved DC target with the reference's bounded confirmation.
Do not restore whole stale registers or clear somebody else's request.

On an uncertain transition, lost inherited hold or unconfirmed cleanup, stop
further probe mutations, retain any still-owned holds and the small access
record/mappings until reboot, and report `access cleanup unconfirmed`.
Report only holds/transitions positively confirmed; a failed DC write is
`state unknown`, not a claim that DC0 is retained. Do not guess restoration,
retry the inventory, take a late acknowledgement as
permission to release, or panic routinely. Existing firmware-framebuffer
presentation continues; a retained hold/DC0 state can cost power until reboot.
No scanout backing is allocated by this slice. This is the accepted Pyxis failure/ownership policy, not a claim that Linux
uses it. The software wait
bounds do not make a CPU MMIO transaction itself interruptible.

### Qualification

- Audit the exact allowed writes and matched acquire/release journal; QEMU
  checks nonmatching paths but cannot qualify Intel power handling.
- One hands-free vela capture through Remote/UDP with before/after DC target,
  request masks, acknowledgements, skipped-domain reasons, raw layout, bounded
  PTE summaries and timing observations. Every failure remains a visible
  unavailable/uncertain result, not an inferred successful inventory.
- Request 2880×1800×32 with the verified Limine per-entry option; record the
  actual returned GOP. Keep the prior 1080p capture separately labelled.
  No new kernel build is needed merely to request that bootloader mode.

**What chronium can do after this slice:** inspect a source-scoped, power-held
snapshot and decide the exact GGTT/plane write and retirement contract. It
still does not remove tearing or take full modesetting ownership.

Additional pinned Linux references:

- [Shared display probe ordering](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display_driver.c):
  `intel_display_driver_probe_noirq`, power admission before DMC initialization.
- [Power initialization and domain lifetimes](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display_power.c):
  `icl_display_core_init`, `intel_power_domains_init_hw`,
  `intel_display_power_get_if_enabled`.
- [Xe3 power-domain hierarchy](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display_power_map.c):
  `xe3lpd_power_wells`, HW/DMC-managed PW1.
- [DC admission, bounded waits and additive request handoff](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display_power_well.c):
  `gen9_set_dc_state`, `gen9_write_dc_state`, `tgl_disable_dc3co`,
  `hsw_power_well_sync_hw`, `hsw_wait_for_power_well_enable`.
- [Wakelock initialization and its ordering limit](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_dmc_wl.c):
  `intel_dmc_wl_enable`, `__intel_dmc_wl_take`.
- [Xe early timing observation](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/xe/display/xe_plane_initial.c):
  `intel_plane_initial_vblank_wait`.
- [Limine v12.9.3 configuration](https://github.com/Limine-Bootloader/Limine/blob/e3aa138d383ca4fe061f05931b5a54497cd502f2/CONFIG.md):
  protocol-specific `resolution`; unsupported modes can fall back.

The profile-specific prerequisite references are
[`intel_phy_is_combo`](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_display.c)
and
[`intel_psr_notify_dc5_dc6`](https://github.com/torvalds/linux/blob/05f7e89ab9731565d8a62e3b5d1ec206485eeb0b/drivers/gpu/drm/i915/display/intel_psr.c).
