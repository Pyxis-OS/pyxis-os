# Presentation timing and buffering

Investigation, 2026-10-09, at Pyxis `f4c5cdbc`, userland `9ea770b9`, ports
`8b918c46`; the owner reports a tear line with ordinary Quake at about 72 fps on
the ThinkPad's 60 Hz panel. All three decisions below were accepted as the
defaults on 2026-10-09.

**Status:**
- **Step 1, RAM staging for boot and Bochs:** merged in #610, with its
  [measurements and native steps](../development/experiments/presentation-staging/README.md).
- **Completed-frame handoff:** three decisions accepted 2026-10-09
  ([below](#completed-frame-handoff)); implemented for review, with its
  [measurements and native steps](../development/experiments/frame-handoff/README.md).
- **Timing and pacing:** later steps. The read-only
  [Fedora Renoir reference](../development/experiments/renoir-linux-timing/README.md)
  measures the panel mode/blank window and private WC copy cost; protected OTG
  and PSR/Replay reads are pending host access. No Pyxis timing probe is implemented.

**Recommendation:** stage composition in RAM first, then investigate read-only
Renoir timing and whether a native copy fits the measured blank interval.
Neither RAM double buffering nor a 60 Hz sleep alone promises tear-free output.
Actual AMD page flipping needs a separate native display-engine task.

## Today's path

[The presenter](../../kernel/space.c) runs every 16,666,667 ns, drops missed
cadences and repaints the whole screen: navigation, selected graphics/TTY,
selection/caret and pointer. There is no kernel damage protocol.

| Backend | Storage and copies | When pixels become visible |
| --- | --- | --- |
| ThinkPad boot framebuffer | Limine's GOP-derived front surface is WC. Composed spans go directly through memcpy into live scanout; no complete compositor backbuffer. | The display engine reads that memory while CPU writes it. No blank/flip synchronization. |
| Bochs | Same direct-copy path into WC BAR0; mode setup fixes offsets at zero. | Device/frontend samples the currently selected aperture page, independently of copying. |
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

**Native copy time is unknown.** Existing [copy qualification](../kernel/display.md#qualification-and-cost)
reports 0.640 ms median for an entire 1280×800 presenter in nested KVM, not a
1920×1080 ThinkPad WC blit. Native evidence is owner-observed improvement after
rep movsq, not isolated bandwidth. Measure the actual RAM→WC copy including its
final store fence; a 16.67 ms refresh period is not the available blank window.
Even 8 GB/s would take about 1.04 ms for the payload alone (illustrative, not
measured). A blank-timed copy is eligible only when its tail latency plus wake,
interrupt and safety margins fits the measured remaining blank interval.
Otherwise it can still tear; retain a labelled unsynchronized fallback.

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
No native timing measurement, QEMU boot or register access was performed here.

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
