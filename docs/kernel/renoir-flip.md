# Renoir flip presentation

The opt-in Renoir backend presents a complete frame by changing the existing
GOP pipe's scanout address at the display engine's synchronized flip point. It
keeps the firmware mode, clocks, power and interrupts, and applies to one
verified Renoir `1002:1636` driving one progressive 1920x1080 pipe. Two linear
scanout surfaces alternate: the original GOP surface and one kernel-owned spare.
`DISPLAY_FLIP=1` adds `display.flip=1`; omitting it keeps the ordinary GOP copy.
Boot, Bochs and VirtIO use their own paths. The [display reference](display.md#opt-in-renoir-flips)
summarizes the backend; the [backend record](../development/experiments/renoir-flip-backend/README.md)
holds the checks, measurements and native steps, and the
[inventory](../development/experiments/renoir-flip-inventory/README.md) the
read-only evidence it rests on.

Normal flips and tear-free native Quake and Chocolate Quake play were qualified
by the owner on the ThinkPad at `9254f5c8` (2026-10-10): 3120 submitted and
confirmed flips, no timeouts or failures, negligible perceived latency. Panic
visibility was qualified natively the same day with a deliberate panic probe
(below). Timeout recovery and unreported capture and input scenarios are not
qualified; see [technical debt](../technical-debt.md#renoir-flip-backend-qualification).

## Authority

This is the first Pyxis AMD GPU register **write** path, accepted by the owner on
2026-10-09 for this bounded backend only. The BSP display driver exclusively
owns the device, its mappings and the surface-address writes. Programs supply
pixels through DRAW and the [RAM frame handoff](../interfaces/graphics.md#slots-and-frame-handoff);
no BAR mapping, address capability or GPU command interface is delegated.

The write allowlist is one qualified HUBP's synchronized flip control and its
primary surface address high/low pair. There is no modeset, clock, power, VM,
firmware command, reset, GSL, display-interrupt or OTG-blank write. A demonstrated
need for more returns to the owner.

## Preconditions

Before AP startup the adapter copies numeric VFCT/ATOM metadata. The backend
requires one unique already-D0, memory-enabled Renoir display with no other
display, and one enabled progressive 1920x1080 OTG whose route
`OTG <- OPP <- MPCC <- HUBP` is unsplit and mono, found by following
`HUBP_VTG_SEL` and cross-checking every stage rather than equating instance
numbers (OTG0/OPP0/MPCC0/HUBP0 on the ThinkPad). Exactly one scanout path's live
primary and in-use address must translate to the GOP framebuffer.

State is snapshotted twice and validated before each submission:

- linear 32-bit ARGB8888 with a matching crossbar, DCC off, TMZ off, VMID 0 with a
  verified direct framebuffer-aperture translation, no stereo, update lock, GSL,
  hardware triple buffering, rotation or mirroring;
- opaque MPCC alpha and gain, and matching viewport, RECOUT and MPC rectangles;
- inherited scaler state: DSCL mode 1, AutoCal `0x100` and exactly unity H/V luma
  ratios (the owner accepted the correctly displayed unscaled panel as evidence,
  2026-10-10), compared in full against the boot snapshot every time;
- flip and flip-away interrupt enables read only, never changed.

The effective stride is 7680 bytes. The register's raw pitch value `0x780` is kept
(Linux normally programs 1920 pixels as `0x77f`); the owner accepted the unsheared
display as evidence on 2026-10-09, and this does not generalize to other firmware
modes. Ambiguity or a changed pipe refuses the backend and never repairs firmware
state; a refusal keeps today's GOP copy.

## Flip path and completion

The writer follows the DCN 2.1 mono sequence adapted from Linux v6.19.10 AMD DC:
copy the completed composite into the free surface, fence the write-combined
stores, revalidate ownership, set `SURFACE_FLIP_TYPE=0` by a masked update, then
write the address **high first, low last**; the low write latches the update.
One request is outstanding at a time.

Completion needs both pending clear and a stable earliest-in-use address equal to
the request; a clear bit alone is not enough. Only that confirmation lets the
previous front become the next back surface. While a flip is pending the
presenter makes 1 ms sleeping polls with a 50 ms (at most 50 poll) deadline,
services input between polls and composes nothing new while both surfaces are
busy. A poll timestamp bounds the transition; it is not a vblank or photon time.

| HUBP register (DWORD, BASE_IDX 2) | 0 | 1 | 2 | 3 |
| --- | --- | --- | --- | --- |
| PRIMARY_SURFACE_ADDRESS | 0xeb28 | 0xee98 | 0xf208 | 0xf578 |
| PRIMARY_SURFACE_ADDRESS_HIGH | 0xeb2c | 0xee9c | 0xf20c | 0xf57c |
| FLIP_CONTROL | 0xeb6c | 0xeedc | 0xf24c | 0xf5bc |
| SURFACE_EARLIEST_INUSE | 0xeb94 | 0xef04 | 0xf274 | 0xf5e4 |
| SURFACE_EARLIEST_INUSE_HIGH | 0xeb98 | 0xef08 | 0xf278 | 0xf5e8 |

Renoir's register segment 2 starts at DWORD `0x34c0`; the BAR byte offset is
`(segment + register) * 4`, and only the needed pages are mapped. FLIP_CONTROL
holds update lock `0x1`, flip type `0x2`, pending `0x100`, stereo mode `0x3000`
and stereo sync `0x10000`.

## Pending-poll validation

PENDING polls take two observations of flip control and earliest-in-use low/high
(six MMIO reads). Stable pending-bit/address observations are hints: they can
only keep the request waiting. Both observed addresses must belong to the owned
pair and non-status control must match the inherited value under the existing
pending/immediate exclusions. An unexpected observation remains latched for
FAILED even if subsequent full samples recover; a torn light sample is not
claimed as proof of persistent hardware ownership loss.

READY and FALLBACK always run full device, route, immutable-layout and owned-set
validation. Submission independently revalidates after the fenced back copy.
Possible completion or either timeout bound also requires the full check. Full
validation therefore guards the first back-surface pixel write, every GPU
submission, front retirement, capture publication, timeout fallback and every
fallback copy. Completion verifies stable primary **and** earliest addresses
against the request with pending clear. Light reads authorize no write, release,
reuse or capture publication.

Layout-loss detection during the write-free PENDING interval is bounded by
candidate completion or the existing 50 ms/50-poll deadline; the owner accepted
this cadence on 2026-10-10. One outstanding request, 1 ms sleeps, pinned FAILED
surfaces, status masks and the three-register write allowlist are unchanged.

Metrics-gated reports retain full-poll validation mean/max/count/total and add
submission-validation and light-observation mean/max/count/total. Cumulative
observation elapsed per confirmed frame includes BSP clock, interrupt and
preemption overhead, excludes sleeping between polls, and is not separately
profiled CPU execution time. The [poll-cost record](../development/experiments/renoir-poll-cost/README.md)
records the owner-run ThinkPad A–B–A–B qualification: BSP observation elapsed
per frame fell from 3.393–3.415 ms to 1.116 ms (about 67%), with zero timeouts
or FAILED and tear-free native Quake/Chocolate Quake. The owner found B more
responsive. These whole-boot metrics include preemption and are not CPU profiling;
B2 had one reported maximum of 48.121 ms, close to the unchanged 50 ms deadline,
whose cause and frequency cannot be determined from the final totals.

## Surfaces and memory

The validated 512 MiB UMA range is owned by the kernel driver except the GOP/VGA
storage, every reported firmware, driver and live-client reservation, applicable
discovery, training and stolen reservations, and the last-16-MiB guard `[0x1f000000,
0x20000000)`. The owner accepted this Linux-derived exclusion standard on
2026-10-09 in place of an explicit firmware allocator handoff, and accepted the
residual pre-OS PSP/SMU risk. The spare is one 64 KiB-aligned region at VRAM
offset `0x900000` (rounded backing `[0x900000,0x10f0000)`), mapped write-combined
on the BSP before publication and excluded from PMM allocation. The ThinkPad's
GPU framebuffer range is `[0xf400000000,0xf420000000)` and the direct CPU UMA range
`[0x810000000,0x830000000)`. Translations and exclusions are recomputed at every
boot, and unfamiliar or nonzero reservations refuse the backend. Backing stays
until GPU retirement is established.

## Presenter

The three RAM slots stay producer storage and never attach to the GPU. SUBMIT
still returns before scanout and replaces an unconsumed pending frame. The
presenter composes navigation, TTY or graphics, selection, caret and the software
cursor into the existing WB staging frame, then copies the whole image to the free
scanout surface; this moves the full copy off the fetched surface and does not
remove it. While both surfaces are front or pending, the staged snapshot is kept,
input keeps flowing and the next compose waits for confirmation or fallback.
Capture publishes only after a confirmed flip or a completed unsynchronized
fallback copy, so an unconfirmed flip never counts as presented, and screenshots
keep the visible cursor.

## Failure, fallback and panic

A timeout cancels nothing. The backend stops address writes, pins every surface
that may be fetched and, while the in-use and pending addresses stay in the
verified owned set, copies each later image to all possible fronts, paying two
copies and possibly tearing but never showing stale contents. It returns to the
single GOP target only after that surface's use is confirmed with no request
outstanding. If routing or address identity becomes unprovable the backend stops
and display is reported unavailable.

The offscreen copy, its store fence and the control/address transaction take part
in the direct-writer claim and the panic recheck. The claim is released before
asynchronous polling, with the pending snapshot kept apart. Panic drains an
already entered transaction before owning the pixels, never resumes an interrupted
low-address write, polls no flip and issues no GPU command from the faulting CPU;
it paints every owned surface that may be scanned. A mode or power change outside
the verified scope can still defeat visible panic.

Native panic visibility (owner, ThinkPad, 2026-10-10): an unmerged probe,
`probe/flip-panic` at `b50a2482`, panics from the BSP input path on
Ctrl+Alt+Shift+P only with `probe.panic_key=1`. Booted with `display.flip=1
display.flip.metrics=1 probe.panic_key=1`, the panic message was visible both
during native Quake and at the shell after quitting it.

## What it replaced

The counter-derived period and phase scheduling, CPU blank-copy admission and
beam-racing waits of the [read-only timing observer](display.md#read-only-renoir-firmware-timing)
failed native qualification on 2026-10-09 and are not flip authorities. A qualified
flip backend skips that observer; disabled and refused paths keep it. Read-only PCI
identity, D0 and BAR checks, bounded uncached mappings and sole-OTG mode checks
live on inside the one claimed driver.

## Provenance

The adapted mono flip ordering, completion predicate, state decoding and register
definitions come from AMD DC in Linux v6.19.10 under its MIT notices, preserved
with the tag, source paths and modifications in the upstream notice and in
[LICENSING.md](../../LICENSING.md); no unrelated DRM, BO or VM code is copied. The
optc blank helpers are not vendored.

## Limits

- Opt-in, Renoir only, one firmware layout; any other layout or reservation
  refuses.
- QEMU has no DCN 2.1: it checks refusal, the unchanged boot, Bochs and VirtIO
  paths, lifetimes and input, not register writes, VRAM ownership, completion,
  native panic or tearing.
- Timeout recovery, remote screenshots and some input scenarios are
  unqualified. A framebuffer screenshot or an FPS figure does not
  establish a tear-free panel.
- Three surfaces, display interrupts and OTG blanking are deferred.
- Light polling is qualified on the ThinkPad, with whole-boot cost measurements.
  Per-game cost splits, the cause of the 48.121 ms confirmation-wait maximum and
  separately profiled CPU execution time are not established.
