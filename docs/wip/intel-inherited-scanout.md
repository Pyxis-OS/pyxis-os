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
