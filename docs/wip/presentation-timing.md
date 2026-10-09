# Presentation timing and buffering

Investigation, 2026-10-09, at Pyxis `f4c5cdbc`, userland `9ea770b9`, ports
`8b918c46`; the owner reports a tear line with ordinary Quake at about 72 fps on
the ThinkPad's 60 Hz panel. The three [original owner decisions](#owner-decisions)
were accepted as the defaults on 2026-10-09.

**Status:**
- **Step 1, RAM staging for boot and Bochs:** merged in #610, with its
  [measurements and native steps](../development/experiments/presentation-staging/README.md).
- **Completed-frame handoff:** merged in #618, after three decisions accepted
  2026-10-09 ([below](#completed-frame-handoff)), with its
  [measurements and native steps](../development/experiments/frame-handoff/README.md).
- **Step 2, native timing:** all three defaults accepted 2026-10-09;
  observer and guarded-copy implementation delivered for review. The
  [interface/limits](../kernel/display.md#read-only-renoir-firmware-timing) and
  [qualification record](../development/experiments/renoir-presentation/README.md)
  include exact native steps. Timed copies remain off by default until native
  qualification. The read-only
  [Fedora Renoir reference](../development/experiments/renoir-linux-timing/README.md)
  records the active OTG, panel blank window, idle/animation counters, disabled
  PSR/Replay and private WC copy cost. No Pyxis timing probe is implemented.
- **Step 3, program timing/pacing:** remains a later assignment.

**Recommendation:** stage composition in RAM first, then investigate read-only
Renoir timing and whether a blank-started native copy stays ahead of display fetch.
Neither RAM double buffering nor a 60 Hz sleep alone promises tear-free output.
Actual AMD page flipping needs a separate native display-engine task.

## Today's path

[The presenter](../../kernel/space.c) runs every 16,666,667 ns, drops missed
cadences and repaints the whole screen: navigation, selected graphics/TTY,
selection/caret and pointer. There is no kernel damage protocol.

| Backend | Storage and copies | When pixels become visible |
| --- | --- | --- |
| ThinkPad boot framebuffer | Compose a complete frame in WB RAM, then copy visible rows top-down to the WC firmware front surface. Allocation failure retains direct composition. | The display engine reads that memory while CPU writes it. No blank/flip synchronization. |
| Bochs | Same RAM staging and final full-frame copy into WC BAR0; mode setup fixes offsets at zero. | Device/frontend samples the currently selected aperture page, independently of copying. |
| VirtIO 2D | One guest-RAM resource backing; compose there, full TRANSFER_TO_HOST_2D, initial SET_SCANOUT, then RESOURCE_FLUSH, with fenced control completion. | Transfer populates a host resource; flush requests its display update. Completion is not a monitor timestamp. |

Until the [frame handoff](#completed-frame-handoff), DISPLAY_PRESENT selected a
continuously sampled writable mapping; later writes could appear without another
call. Frame leases protected lifetime, not pixel immutability. SDL drew
off-screen, then copied dirty rectangles into this mapping. Quake scaled and
palette-expanded directly into the mapping and paces its loop at 72 Hz;
timedemo is uncapped. Thus two mixtures were possible: application writes
racing presenter reads, and front-buffer writes racing panel scanout. The
handoff removes the first. The 72/60 mismatch adds repeats/drops; even equal nominal rates drift.

## What each backend can do

- **VirtIO/QEMU:** the [VirtIO GPU specification](https://docs.oasis-open.org/virtio/virtio/v1.3/virtio-v1.3.html)
  provides transfer, flush and resource selection, not vblank or a vsync-latched
  flip. [QEMU 10.2.2](https://github.com/qemu/qemu/blob/v10.2.2/hw/display/virtio-gpu.c#L397-L625)
  copies into a host Pixman resource, exposes its pixels as the display surface,
  and calls dpy_gfx_update on flush. Complete immutable frames and alternating
  populated resources can prevent guest partial uploads into the selected
  resource. Host tearing still depends on frontend/compositor: [GTK queues a
  redraw](https://github.com/qemu/qemu/blob/v10.2.2/ui/gtk.c#L359-L405), without
  proving host scanout completion. No physical-tearing observation was made here.
- **Bochs:** [DISPI X/Y offset panning exists](https://github.com/qemu/qemu/blob/v10.2.2/hw/display/vga.c#L509-L717),
  so multiple VRAM pages can be selected if they fit. It is not a documented
  retrace-latched flip. Standard VGA's [retrace emulation](https://github.com/qemu/qemu/blob/v10.2.2/hw/display/vga.c#L266-L296)
  is either read-driven toggling or virtual-clock timing, not host monitor phase;
  [display-only bochs-display](https://github.com/qemu/qemu/blob/v10.2.2/hw/display/bochs-display.c#L143-L215)
  samples offsets at UI update and has no VGA status block. Keep its unsynchronized
  capability honest; no vblank claim from polling VGA status.
- **ThinkPad GOP:** the framebuffer descriptor supplies memory/format/geometry,
  not vertical timing or a second scanout allocation. Firmware-mode-preserving
  timing observation is plausible; swapping ordinary RAM addresses is not.

**Minimal AMD timing task:** Linux's AMD-authored [Renoir resource map](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/display/dc/resource/dcn21/dcn21_resource.c#L69-L101)
uses DCN 2.1 offsets/base indices and four OTGs. Its [timing-generator code](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/display/dc/optc/dcn10/dcn10_optc.c#L621-L665)
reads OTG_STATUS_POSITION (OTG_VERT_COUNT), OTG_STATUS_FRAME_COUNT and, for
blank detection, OTG_STATUS.OTG_V_BLANK; configured OTG_V_BLANK_START_END and
OTG_V_TOTAL define the interval. A bounded native observer could identify AMD
PCI/BAR MMIO, map only required registers UC, locate the active OTG for the
firmware eDP/GOP surface, and correlate counters with monotonic time. Do not
assume OTG0 or infer the panel phase from any moving counter. Validate rollover,
mode/pipe association and self-refresh/clock-gating behavior without changing
mode, clocks, interrupts or panel state. Stalled/unknown counters mean unavailable.
This is a small AMD-specific component, even though it need not modeset.

**Flips are more than one unqualified register write.** AMD's published
[DCN description](https://docs.kernel.org/gpu/amdgpu/display/dcn-overview.html#global-sync)
explains double-buffered surface registers and VUPDATE/VREADY synchronization.
The [HUBP programming path](https://github.com/torvalds/linux/blob/v6.12/drivers/gpu/drm/amd/display/dc/hubp/dcn10/dcn10_hubp.c#L330-L386)
sets flip type and writes DCSURF_PRIMARY_SURFACE_ADDRESS_HIGH before the low
address that latches the update. Correct Renoir use additionally needs the
active HUBP/OTG routing, GPU-address translation, scanout-compatible allocation,
pitch/tiling/DCC state, update locks, completion/retirement and firmware/panic
handoff. CPU physical memory is not automatically a valid scanout address.
AMD's [GPUOpen manuals index](https://gpuopen.com/amd-gpu-architecture-programming-documentation/)
mainly documents shader ISAs; it does not establish this firmware handoff.
The Linux register/source material supports a read-only probe, not a proved
safe Renoir flip recipe. Defer register writes and GPU acceleration.

## Cost, pacing and bounded delivery

1920×1080×4 is **8,294,400 bytes (7.91 MiB)** per tightly packed copy, or
497.7 MB/s of destination writes at 60 Hz, before source reads. One additional
kernel RAM staging frame costs 7.91 MiB; staging adds a RAM copy, not a free speedup.
Current full repaint already pays the front-buffer write. Damage-only copies
would write roughly the changed fraction, but need damage for navigation,
cursor old/new positions, caret and all source changes; moving Quake usually
changes most pixels. Introduce that only after correctness and measurement.

**Native Pyxis front-copy time is unknown.** The Linux reference's private full
WC copy exceeds its computed panel blank window, but a uniform top-down copy
would outrun scanout by about 19×: row completion near `s + 0.77r` µs versus
scanout near `465 + 15.0r` µs, with start delay `s` after blank begins and row
number `r` from the top. This is
analysis, not measured tear-free output or GOP/Pyxis qualification. The constraint
is start latency below about 465 µs minus display-fetch lead and wake/interrupt/
safety margins, with every row remaining ahead of fetch despite stalls; the
whole copy need not finish in blank. Existing [copy qualification](../kernel/display.md#qualification-and-cost)
reports 0.640 ms median for an entire 1280×800 presenter in nested KVM, not a
1920×1080 ThinkPad WC blit. Native evidence is owner-observed improvement after
rep movsq, not isolated bandwidth. Measure the actual RAM→WC copy including its
final store fence; a 16.67 ms refresh period is not the available blank window.
Even 8 GB/s would take about 1.04 ms for the payload alone (illustrative, not
measured). Qualify actual copy start latency, per-row write visibility and
progress against display fetch natively before promising tear-free output;
retain a labelled unsynchronized fallback until then.

First software task: one allocated WB staging frame for boot/Bochs, compose
all overlays there, then a complete bounded row/pitch-correct front copy. VirtIO
already composes into attached RAM backing; reuse it for the existing transfer/
flush path, rather than add a redundant RAM copy. Preflight resize allocation
and preserve leases, capture/cursor fences, uncertain-device retention and the
direct panic target. Staging confines direct scanout writes to the final blit;
any shorter duration must be measured. It still samples mutable application pixels. A separately bounded
completed-frame handoff for SDL/Quake is required before promising atomic frames:
render/publish distinct slots, retain submitted pixels until acknowledged reusable,
keep at most one pending frame, and identify replacement/drop/failure explicitly.

Timing should reach programs through a native display observation/wait contract:
geometry generation, frame sequence, monotonic timestamp/period and whether the
source is software cadence, command completion or verified hardware timing.
Buffer retirement and actual display timing are different events. SDL's optional
vsync setting must use supported timing or report unsupported; a timer cap is
pacing. Quake can use display pacing instead of its unrelated 72 Hz sleep while
keeping timedemo uncapped. Avoid unbounded queueing, output-lock/IF=0 waits and
changes to hidden-space/focus/input semantics.

Before/after: same ThinkPad mode, AC power, workload and revision; manually record
composition/copy+fence P50/P95/max, missed intervals, bytes/damage fraction, blank
width/wake lateness if available, application cadence and input latency. Compare
idle TTY/cursor and ordinary moving Quake separately from timedemo. Short camera
clips/owner observations verify visible tearing; FPS or screenshots alone cannot.
Check QEMU VirtIO/Bochs separately and qualify resize/capture/panic/lifetime paths.
The original proposal performed no hardware access; the linked Linux reference
now supplies read-only timing evidence. Native Pyxis qualification remains later work.

## Owner decisions

Accepted 2026-10-09, all as the defaults.

1. **Software first:** default RAM staging/full-frame presentation, followed by
   explicit completed-frame handoff for SDL/Quake; no tear-free promise from
   staging alone. Damage tracking remains later work.
2. **ThinkPad timing:** default a read-only Renoir OTG probe next; blank-timed
   copies only if native measurements show margin. Defer page-flip writes to a
   separate display-engine contract and task.
3. **Program policy:** default optional low-latency presentation pacing with
   bounded pending frames and truthful timing capability; keep uncapped mode
   available. No unconditional 60 Hz cap or pretend SDL vsync.

## Completed-frame handoff

Accepted by the owner on 2026-10-09, all three as the defaults:
1. **Three slots per session:** current, pending and being rendered, so SUBMIT
   never waits.
2. **Newest frame wins:** a SUBMIT while a frame is pending replaces it, and
   the reply reports the drop explicitly.
3. **Continuous sampling retired:** SDL2, Quake and the other graphics programs
   moved to SUBMIT in the same change, with userland and ports PRs.

The contract is in [graphics](../interfaces/graphics.md#slots-and-frame-handoff).

## Step 2: read-only Renoir timing

Proposal, 2026-10-09, based on main `f6d82ef9`, including #615. This does not
reopen #608's read-only observer, conditional timed copies or deferred flips.
The owner accepted the beam-racing interpretation on 2026-10-09: start latency
and sustained lead over display fetch matter; the copy need not finish in blank.
All three defaults below were **accepted 2026-10-09**, after #622 merged, and
step 2 implementation was authorized. Timed copies remain off by default until
native qualification; program timing APIs and page flips remain later tasks.

### Observer and firmware boundary

Keep Renoir register knowledge in its own unit under `arch/x86_64/amd/`, with
the presenter/backend integration under `kernel/display/`. Consume copied
`boot_info` framebuffer metadata; Limine response details stay in the adapter.
Prepare mappings on the BSP before AP startup, then observe from the sole BSP
presenter, IF=1. Keep the mappings stable until reboot; no new polling service.

The first scope is the firmware boot-framebuffer backend and a unique unclaimed
AMD `1002:1636` display endpoint in a complete PCI inventory. Require one display
function, already enabled memory decoding and D0; absence, ambiguity, another
owner, unusable power/decoding or an unsupported device means unavailable.
Read the assigned BAR5 address, walking BAR pairs so an upper half cannot be
mistaken for BAR5. Require its expected non-prefetchable 32-bit memory encoding,
nonzero aligned base and non-overflowing register extent. Never assume the
Fedora address, PCI location or active OTG number.

Map only BAR5 pages at offsets `0x13000` and `0x14000`, supervisor-only,
read-only, NX and UC. Reserve virtual space through VM, map UC, and remove write
permission before the observer accesses or publishes the mapping. Existing
`vm_protect` preserves the UC cache bits. Exclude RAM, the rounded GOP framebuffer
physical extent, WC aliases and other reserved MMIO users; unwind an unpublished
partial mapping without freeing device frames. Allocation failure loses timing,
not presentation. No userspace mapping or delegated register authority.

Normal PCI claim/completion, BAR sizing and temporary MMIO wake helpers are
unsuitable: they can write command, BAR or power registers. Do not use them,
make configuration writable, enable interrupts, request runtime resume, execute
ACPI resource methods, or issue firmware commands. Reading an assigned BAR does
not reveal its length. The two-page span is an audited **Renoir register window**,
not an independently sized native BAR; #615's 512 KiB is Linux resource evidence.
Do not publish a guessed BAR size. Extending to unknown GPUs/resource layouts
needs separate evidence rather than a write-and-restore probe.

Read only aligned 32-bit registers in the documented allowlist. For OTG0–3,
instance stride is `0x200` bytes. The
[reference's table](../development/experiments/renoir-linux-timing/README.md#otg-and-panel-self-refresh)
gives CONTROL, V_TOTAL, V_BLANK_START_END, STATUS_POSITION,
STATUS_FRAME_COUNT and STATUS. Horizontal geometry additionally needs H_TOTAL
(`0x13fa8` for OTG0), H_BLANK_START_END (`0x13fac`); INTERLACE_CONTROL
(`0x14010`) and V_TOTAL_MIN/MAX/CONTROL exclude unsupported variable timing.
Use AMD's [offsets](https://github.com/torvalds/linux/blob/v6.19/drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_1_0_offset.h),
[masks](https://github.com/torvalds/linux/blob/v6.19/drivers/gpu/drm/amd/include/asic_reg/dcn/dcn_2_1_0_sh_mask.h)
and [timing implementation](https://github.com/torvalds/linux/blob/v6.19/drivers/gpu/drm/amd/display/dc/optc/dcn10/dcn10_optc.c)
as the source; no underflow/status-clear or firmware-state registers.

Find exactly one OTG with CONTROL master-enable, then validate stable,
progressive H/V active dimensions against GOP width/height, totals greater than
active dimensions, in-range blank endpoints and positions, and consistent
STATUS.V_BLANK. Active dimensions come from the programmed blank endpoints;
do not rescale width merely because horizontal DIV_BY2 is set. Reject
unsupported adaptive totals, multiple enabled OTGs or mismatching geometry.
This is a sole-output geometry/timing correlation, not independently decoded
HUBP addressing, tiling or routing. Multi-display/routing discovery remains out.

Use a bounded initial observation, at most 250 ms wall time with coarse sleeps
between samples rather than a continuous busy loop, requiring at least four
advancing frame transitions and samples inside/outside blank. Derive period from
monotonic time and the low 24-bit frame counter; handle modulo rollover and
missed samples without synthesizing hardware transitions. Bracket non-atomic
reads and reject samples crossing an edge or inconsistent mode snapshot.
Check the mode fingerprint and advancement during ordinary presenter sampling.
Changed mode/enable state, implausible reads or no advance for three measured
periods (at least 50 ms) invalidate timing. Requalification accumulates samples
across ordinary cadences, at most one extra read bracket per cadence and one
250 ms observation attempt per second; it adds no waiting loop to a frame.
The same checks must pass before restoring availability. Never wake a stalled generator or
infer PSR from a stall. Linux's advancing counters and unsupported PSR/Replay
are encouraging evidence; firmware/GOP behavior must be checked independently.

### Starting the staged copy

First qualify observation with existing unsynchronized copies. On the native
mode, measured period / V_TOTAL gives line time; blank width uses modulo totals.
The reference blank wraps from vertical count 1108 through zero to 28, so count
zero is neither blank start nor the top visible row. Use programmed endpoints
and STATUS together. Edge timestamps have a measured uncertainty interval,
including at least one scanline when using vertical position alone.

After native margin qualification, use a predicted blank edge to phase the
presenter's own cadence. Compose before that edge (initial wake lead 2 ms,
checked against measured composition/wake tails), sleep if early, then start
polling about 125 µs before the predicted edge. A single fine-poll window is
bounded to 250 µs elapsed time and 64 read brackets, with interrupts enabled.
Space bracket starts by at least 4 µs so fast reads cannot exhaust the count
before the predicted edge; the elapsed bound includes MMIO and clock cost.
Re-read actual position/status immediately before the first front write; a
prediction alone never authorizes a timed copy. If already inside blank, accept
only a conservatively measured remaining start budget. No IRQ is installed:
an unconfigured interrupt cannot be relied on, and enabling one would violate
the no-writes boundary. Cadence-only polling can miss the 465 µs window; full
refresh-period spinning would consume a BSP core and is not proposed.

Use the accepted initial **eight-line fetch/safety guard**, about 120 µs at the reference
mode, plus measured timestamp/read/first-write uncertainty. It is a qualification
guard, not a claimed AMD fetch-depth specification. Admission requires the
upper bound of copy-start delay plus that guard/uncertainty to be less than
measured blank width. Measure the first actual front write, not entry to frame
end. The roughly 0.77 µs/row versus 15.0 µs/row model explains why later rows
gain lead; native copy stalls and WC visibility still need qualification. Total
copy/fence P50/P95/max alone do not establish per-row visibility or tear-free
output. Keep the current top-down, pitch-correct copy and final store fence.

If composition/wake is late, the poll bound expires, timing is unavailable or
the start guard fails, **copy once immediately, labelled unsynchronized**.
Do not hold the frame for another blank or build a retry queue. Record the
reason/missed target and return to the next cadence without catching up. A
late start does not by itself invalidate an otherwise advancing hardware clock.
No staging means no timed full-frame copy; ordinary fallback remains available.

No sleep/spin under output locks, allocator locks or IF=0. While waiting, publish
no direct-front writer: retain the private staged frame, then reacquire the
existing panic writer/recheck ordering immediately before copying. Panic can
take its direct target without waiting for a sleeping presenter; abort normal
work if it claims ownership. Preserve frame leases, newest-pending-frame policy,
cursor-inclusive captures and their completion rules. Capture completion still
means composition/copy/fence success, not physical display completion.

At 60.02 Hz, 250 µs of fine polling per frame is at most about 1.5% additional
BSP busy time before read/deadline-check overshoot; measure actual spin time,
MMIO/clock cost, coarse wake lateness, composition, copy/fence and input service
delay. This adds no framebuffer traffic beyond the existing full copy. Sleeping
must not add a second refresh of latency; phase the next composition deadline,
and keep the current software cadence on unsupported backends.

### Truthful capability and qualification

Keep a private distinction between a validated hardware timing source and a
copy admitted within its start guard. Hardware timing means a matching,
advancing OTG correlated with monotonic time, with a bounded edge estimate; it
does not mean a page flip, exact scanout-completion timestamp or tear-free copy.
An unsynchronized fallback can still have a valid hardware timing source. On
loss of validation, stop attributing timestamps/sequences to hardware and report
unavailable. Detailed samples are bounded diagnostics, not per-frame normal logs.

Step 3 later can use the accepted geometry/sequence/monotonic period/source
contract with that capability and its uncertainty. No new program-facing ABI,
SDL vsync claim, Quake pacing change or unconditional application cap in step 2.
QEMU boot/Bochs report software cadence; VirtIO fence completion remains command
completion, never a physical vblank timestamp. They do not gain AMD timing.

Implementation/qualification breakdown:

- [ ] **2a — observer:** PCI/RO-UC mapping, active OTG validation and bounded
  counter diagnostics. Capture the ordinary-copy baseline before changing its
  scheduling. QEMU boot, Bochs and VirtIO must exercise unavailable timing with
  ordinary rendering, input, capture, resize where supported and panic retained.
- [ ] **2b — native timing/copy gate:** the owner runs the observer-only build
  on the ThinkPad at native GOP mode, AC power. Capture raw/decoded mode and
  OTG identity, bracket uncertainty and frame/blank/position advancement during
  idle TTY/cursor and moving Quake. Compare the same mode/workload/revisions
  with a candidate timed-copy build; require measured positive start margin
  under the proposed guard before enabling timed copies by default.
- [ ] **2c — qualification and references:** record start-delay P50/P95/P99/max,
  late/fallback counts, spin/read cost, composition and copy/fence distributions,
  representative copy-progress stalls and input service delay in matched runs.
  The owner takes matched short camera clips of steady-turning ordinary Quake
  before/after, with uncapped timedemo kept separate. A screenshot or FPS is not
  a tearing check. If the margin/visibility/clip evidence is insufficient, close
  only observer qualification and retain labelled unsynchronized copies; record
  the missing native evidence and revisit point. Rewrite implemented contracts
  into the display reference; step 3 and page flips remain unassigned.

### Implementation and native gate

`display.timing=observe` is the default: observation/start distributions with
unsynchronized copies. `off` provides the same-revision copy baseline; `blank`
explicitly opts the trusted boot into native qualification. The Make setting
`DISPLAY_TIMING` accepts only these three values. No program can change this
boot policy. Native qualification remains pending; no tear-free claim is made.

The observer/guarded-copy source and QEMU unavailable path are implemented;
the native checkboxes above remain open for the owner's ThinkPad batch. The
record explains dense startup versus conservative sparse requalification,
the IRQ-atomic first pixel, diagnostic overhead and source-reviewed limits.
Step 3 remains unassigned.

### Step 2 owner decisions

Accepted 2026-10-09, all three as the defaults:

1. **Observer boundary:** use the single-device/single-enabled-OTG scope and
   audited two-page Renoir window above, RO/NX/UC, with no BAR-sizing, configuration
   or power writes. Validate GOP geometry/counters or report unavailable. Accept
   the explicit native BAR-length/routing evidence limits; broader discovery is
   later work.
2. **Copy start and late policy:** after native margin qualification, use the
   coarse sleep plus 250 µs/64-bracket fine poll, initially an eight-line guard
   plus measured uncertainty. Copy immediately and label unsynchronized when
   late/unqualified; do not wait another refresh. Preserve IF=1/input/panic bounds.
3. **Capability and delivery gate:** hardware timing describes validated counter
   observation, independently of copy-start success. Deliver observer-first,
   qualify native starts/progress and before/after camera clips before enabling
   timed copies; insufficient evidence leaves the unsynchronized path. Program
   timing APIs and page flips stay later tasks.
