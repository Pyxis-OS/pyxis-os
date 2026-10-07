# Display drivers and resizing

Status: **milestone, agreed 2026-10-07.** It replaces the parked VirtIO GPU
direction that was in [desktop and graphics](desktop-graphics.md). Codex
implements it; each task starts when the owner says so. It runs alongside the
[ACPI milestone](acpi-and-bar-widgets.md).

## Goal

- **Display drivers.** Caelum can drive a display device itself, instead of
  only the framebuffer that firmware set up before boot. There are three drivers:
  1. the boot framebuffer;
  2. VirtIO GPU 2D;
  3. the Bochs display interface of QEMU's standard VGA.
- **One small interface.** It is shaped by those three drivers, not designed
  ahead of hardware Pyxis does not drive yet.
- **Live resizing.** A display size change reaches the space bar, every space,
  terminal rows and columns, and graphics programs.
- **A base for physical GPUs.** These come after this milestone (see
  [After the milestone](#after-the-milestone)).

## Today

- Boot-framebuffer, VirtIO GPU 2D and Bochs drivers present through the physical
  display interface. Live resizing remains tasks 5a–5b.
- The sole BSP presenter composes the bar and active TTY/graphics space about
  60 times a second. Boot output copies directly; VirtIO copies into kernel RAM,
  transfers the full frame to the host and flushes. Buffers use the selected
  immutable layout; application graphics mappings remain fixed for this boot.
- With a directly writable Limine framebuffer, the early console mirrors serial
  until retirement. Without one, early output is serial-only. Boot/Bochs panics
  can reclaim the direct screen; VirtIO panics stay serial-only.
- `QEMU_VIDEO=std` retains q35's default VGA. `virtio` removes VGA and attaches
  the modern VirtIO GPU. The qualified OVMF VirtIO GOP is PixelBltOnly and Limine
  supplies no framebuffer; the driver queries initial geometry before AP startup.
- The ThinkPad has no native AMD GPU driver and keeps its boot framebuffer.
  The owner confirmed normal task-2 output in PR #471; native panic is untested.

## Decisions

Agreed with the earlier VirtIO GPU direction:

- 2D presentation of the software-rendered screen; no 3D acceleration, shaders or
  compositor.
- Limine's framebuffer stays the boot-time output when present; absent output
  uses serial until the selected driver presents.
- Display pixel size is separate from terminal rows and columns.
- A resize never silently invalidates a program's mapping, and no step assumes a
  resize allocation succeeds.

Accepted by the owner on 2026-10-07:

1. **Proposal first.** The first task is an investigation, delivered as a docs PR
   and reviewed before any code. The implementation order then follows the task
   list below.
2. **Ownership: one screen.** At boot the kernel claims the first supported
   display device and retires the boot framebuffer. Without a supported device,
   the boot framebuffer remains the driver. Multiple monitors are out of scope.
3. **Who picks the size:**
   - VirtIO GPU follows the size QEMU reports, including window resizes;
   - Bochs has no such signal, so it uses a size from a boot option;
   - a runtime command to change the size can come later.

Follow-up from [PR #463 review](https://git.internal/PyxisOS/pyxis-os/pulls/463),
carried into the next task at the owner's request on 2026-10-07:

4. **TTY contents:** preserve rasterized whole cells around the cursor, without
   reflow or scrollback. Cropped text is lost.
5. **Graphics mapping:** keep it stable until the owner explicitly replaces or
   releases it. Clip to the current screen until the application adapts.
6. **Panic output:** boot-framebuffer and Bochs panics use direct screen writes.
   VirtIO panics are serial-only. Do not add an emergency VirtIO queue, device
   reset path, 3 MiB reserve or exception to BSP ownership.
7. **Bochs boot option:** `display.size=WIDTHxHEIGHT`, matching the dotted kernel
   option names. Task 4 adds it to the boot command-line table in
   [init](../userland/init.md#boot-command-line).
8. **Resize delivery:** split task 5 into kernel geometry/cropping (5a) and
   readiness, mapping replacement and application adaptation (5b).

Task-3 boot clarification accepted by the owner on 2026-10-07:

9. **Missing boot framebuffer:** support an all-zero absent framebuffer, with
   serial-only early output. A detected VirtIO GPU uses a bounded GET_DISPLAY_INFO
   command before AP startup to obtain initial geometry. This explicitly permits
   the sole BSP to use the queue helper with IF=0 for that boot-only query, without
   scheduler waits. Runtime queue ownership remains the BSP presenter with IF=1.

Task-4 failure policy accepted by the owner on 2026-10-07:

10. **Unverified Bochs restoration:** missing or unsupported sizes keep firmware
    output. If an attempted mode fails and firmware restoration cannot be verified,
    stop boot with a serial panic; do not publish an uncertain direct target.

Review follow-up from merged [PR #473](https://git.internal/PyxisOS/pyxis-os/pulls/473):
when VirtIO is selected, failure does not try a separate VGA firmware screen in a
hand-built mixed-device VM. Record that limit; multiple monitors/failover remain
outside this milestone. Full-frame idle cost stays accepted for now and is to be
remeasured after resizing; changed-region work remains a separate follow-up.

## Tasks

- [x] **1. Proposal.** A docs PR that updates this document with:
  - **The interface:** its operations, derived from what the three drivers need;
    where it and the drivers live; and which task owns each driver under the
    [SMP rules](../kernel/smp.md). Prefer plain code over callback frameworks.
  - **Handoff:** how a driver takes the screen from the early console and the boot
    framebuffer, and how a panic still reaches the screen with each driver.
  - **Presentation cost:** a baseline of today's presenter, and whether full
    frames or changed regions are sent to each device.
  - **Resizing:**
    - what happens to the space bar, space buffers and TTY contents;
    - how terminal rows and columns change and who is told;
    - what happens to a mapped graphics buffer, such as Doom's or Mandelbrot's;
    - what happens when an allocation fails.

    Remote terminals keep their own geometry.
  - **Testing:** the QEMU configuration for each device and how
    `scripts/run-qemu.sh` selects it.
  - **Open decisions:** at most about three per round, each with a proposed
    default.

- [x] **2. The interface, with the boot framebuffer as its first driver.**
  - The presenter writes through the interface. Ordinary output is unchanged.
  - Restore panic takeover of the boot framebuffer after presenter handoff.
  - **Status:** done. See the task 2 results below. On 2026-10-07 the owner ran
    main after the #467–#470 merges on the ThinkPad over PXE. The screen looked
    as before. The battery widget rose on AC, later dropped a point, and kept its
    background colour. Quake's `timedemo demo1` gave 667.1 fps, against 634.2
    fps on 2026-10-04. Both are single runs. A native panic was not triggered.
  - **Finish when:**
    - QEMU and the ThinkPad look and behave as before during normal operation;
    - a post-handoff panic is visible on the boot framebuffer;
    - the presenter cost matches the task 1 baseline within run-to-run variation.

- [x] **3. VirtIO GPU 2D at the initial size.**
  - Create a 2D resource, attach guest memory as its backing, set the scanout,
    then present by transferring to the host and flushing.
  - **Status:** implemented and qualified below, including framebufferless boot.
  - **Finish when:**
    - with `virtio-gpu-pci` and no VGA device, QEMU shows the space bar, spaces,
      the shell and Mandelbrot after boot;
    - a panic reaches serial without resetting or submitting to VirtIO GPU;
    - the presenter cost is recorded against the task 2 figures;
    - the default standard-VGA run is unchanged.

- [x] **4. Bochs.**
  - Set the mode through the Bochs display registers at the boot-option size
    (decision 3), on QEMU's standard VGA and on `bochs-display`.
  - **Status:** implemented and qualified below on `display/bochs`, based on
    main `164b463`.
  - **Finish when:**
    - QEMU boots at a size different from the firmware's, with a correct space
      bar, terminal and Mandelbrot;
    - an unsupported requested size falls back with a message; a missing size
      keeps the boot framebuffer without an ordinary log line;
    - a post-handoff panic is visible in the selected Bochs mode.

- [ ] **5a. Kernel live resizing and size queries.**
  - VirtIO display-change events, transactional target/space/TTY resizing and
    raster cropping, with atomic geometry/generation queries.
  - Update the userland query wrappers for changed reply layouts in the same
    delivery; adaptive consumer behavior stays in 5b. Publish dependency PRs
    before updating parent pins.
  - Preserve existing graphics mappings and clip them to the new destination.
  - **Finish when:**
    - resizing QEMU's GTK window changes terminal rows/columns in every space;
    - console SIZE and the display query return the new geometry/generation;
    - a refused resize leaves the old geometry usable;
    - graphics mappings retain their original address and extent.
  - Consumers see the new size on their next query. Idle readers and running
    graphics programs do not yet adapt automatically.

- [ ] **5b. Resize readiness and graphics adaptation.**
  - RESIZED readiness and input readiness, owner-only display REPLACE, and the
    libterm/Mandelbrot consumers in userland and Kilo/Doom in ports, with focused
    dependent PRs.
  - **Finish when:**
    - a blocked shell line editor and Kilo redraw at the new size without a key;
    - Mandelbrot and Doom adapt at render/frame checkpoints without stale maps;
    - replacement allocation failure leaves the old mapping/session usable;
    - remote terminals keep their independent geometry.

## Task 1 investigation and proposal

Delivered against main `7bc196e` in merged PR #463 on 2026-10-07. The
owner-directed review follow-up above supersedes the original VirtIO panic
proposal. This section records the investigation/design; implementation results for
tasks 2–4 and the pending resize work are identified separately. No driver or resize
implementation is claimed by checking task 1.

### Device interface and ownership

Keep the existing per-space `display` capability separate from the physical
screen. Add `include/kernel/display.h` and `kernel/display/` for screen selection,
layout and the three drivers; put x86 register access and aperture mapping in
`arch/x86_64/`. Extend the Makefile's source selection when code starts.
Dispatch through a three-value driver enum and explicit switches, without an
operations table. No public physical-device capability or userspace modesetter.

The shared interface has these operations:

| Operation | Contract |
| --- | --- |
| Prepare | Before AP startup, inspect supported PCI functions in discovery order, validate/map resources and allocate fixed normal queue storage. Retain the boot layout/mapping; see the VirtIO fallback limit below. |
| Get target | Return a kernel pixel surface and immutable layout: address, width, height, pitch, channel shifts and geometry generation. No boot-protocol pointers. |
| Activate | On the BSP, finish initialization and present a first complete frame before publishing the selected driver. Restore firmware output on Bochs failure; VirtIO has the limit below. |
| Present | Finish the composed frame: drain WC stores for direct framebuffers; transfer and flush the VirtIO resource, checking responses and fences. |
| Check size | Read/coalesce pending VirtIO display information. Boot and Bochs report no runtime changes. |
| Prepare/commit/cancel size | Stage a replacement target without changing published layout; switch device scanout only after all space allocations are ready. Cancel frees unowned allocations; attached backing needs confirmed fenced detach/unreference or is retained until reboot. |
| Panic takeover | Boot/Bochs claim direct output and render without allocation, locks or scheduling. VirtIO remains serial-only. Bounded takeover failure preserves serial reporting. |

The existing `space_present_task` is the sole normal device worker on the BSP;
it owns activation, frame submission, completion polling and resize sequencing.
IRQ handlers record activity and wake that task; the architecture dispatcher
sends EOI. The worker handles configuration/events. The BSP
request executor still owns graphics acquisitions, explicit mapping replacement
and release, with IF=0 and the process parked outside its private root/stack.
AP console output remains under the existing output lock. Driver work must not
hold that lock while waiting for hardware. Yield/sleep while awaiting normal
VirtIO completions, with a finite command deadline, rather than blocking BSP
services in a polling loop.

A frame lease keeps its target/layout alive across preemption and submission.
The presenter copies application pixels into kernel screen backing before
lending it to VirtIO; application pages are never direct scanout DMA backing.
Do not compose into backing while a transfer still reads it. Keep the current
single-buffer tearing contract and drop missed frames rather than accumulating
a queue of stale frames. One in-flight presentation is enough.

Task 2 keeps the firmware pitch/format and direct copy destination, preserving
its cost. VirtIO uses tightly packed B8G8R8X8 (shifts 16/8/0); Bochs uses 32-bit
pixels with the same shifts and pitch `width * 4`. Space/graphics backing follows
the selected layout; the common layout is not forced onto the boot framebuffer.

### Handoff and panic

Task-1 inspection found that **panics after presenter handoff were serial-only**,
including standard VGA: `early_console_retire()` made RETIRED terminal. Task 2
now permits direct boot-framebuffer takeover; task 4 extends this to Bochs. The boot mapping remains reserved, even when retired
as normal output; it is not a fallback screen after another device owns scanout.
VirtIO transport preparation resets the device before BAR sizing, destroying
firmware scanout resources. Task 1 incorrectly assumed OVMF supplies a writable
VirtIO boot framebuffer: the qualified firmware exposes PixelBltOnly GOP and
Limine does not export it. Early output is therefore serial-only. A retained
firmware descriptor, if another firmware supplies one, still cannot promise live
fallback after reset. This differs from Bochs firmware-mode restoration.

For direct-screen drivers, keep the early console until handoff. VirtIO retires
it before AP startup, once transport preparation has invalidated firmware output.
Retire its ordinary writer under the log lock before mode/scanout changes. A separate atomic display ownership gate coordinates normal mutation,
layout publication and the first panic claim. For boot/Bochs, publish an
immutable panic descriptor only when its mappings and mode are valid. During
Bochs activation, retain the old descriptor or exclude takeover during the
short mode-mutation phase. Do not expose half-programmed modes. If Bochs
activation fails, restore the old mode and bind panic rendering to that target. If VirtIO
activation fails after reset, retain serial diagnostics without claiming the
old RAM framebuffer is visible.

A panic revokes normal rendering. If it interrupted this CPU's renderer, that
continuation is abandoned. On another CPU it boundedly waits for the active
mutation/store interval to finish; timeout stays serial-only. Reuse the early
console's static glyph grid, first-owner rule and recursion/fault guard. Its
renderer needs a layout descriptor independent of Limine. Boot and Bochs render
directly into their current mapped screen and fence stores.

VirtIO panics remain serial-only through the VM's existing serial console.
There is no emergency surface, queue, reset/renegotiation or AP device operation.
The normal queue keeps its BSP ownership and failed-storage lifetime rules.
Physical display engines can use direct scanout memory like boot/Bochs; the
VirtIO transfer/flush requirement does not shape their panic contract.

### Hardware constraints

VirtIO 2D requires VERSION_1, the control queue, GET_DISPLAY_INFO, RESOURCE_CREATE_2D,
RESOURCE_ATTACH_BACKING, TRANSFER_TO_HOST_2D, SET_SCANOUT and RESOURCE_FLUSH.
Use checked response types/lengths and fences for completion that changes resource
or backing ownership. Select the first enabled scanout, retain its index and
ignore other outputs for this milestone. Task 3 creates it at the initial pixel
size: the boot framebuffer's dimensions when present, otherwise the bounded
pre-AP query's result. Task 5a follows later reported size changes. With no usable output,
activation fails rather than silently selecting a different monitor later.

Resource backing can be a list of physical page extents from BSP-owned VM RAM.
The existing `dma_buffer_allocate/release` helper is pre-AP only: use it for fixed
queue storage, not live-resize frame backing. Complete detach/unreference before
freeing resource pages. If command ownership is uncertain, retain those pages
until reboot, as the other VirtIO drivers do. Never turn a physical address into
a direct-map pointer.

QEMU standard VGA and `bochs-display` share PCI ID `1234:1111`, framebuffer BAR0
and a 4 KiB register BAR2. DISPI uses 16-bit accesses at `0x500 + 2 * index` in
BAR2. Validate ID, resource extents and memory decoding before access. Propose
`display.size=WIDTHxHEIGHT` in the kernel boot command line, supplied by a Make
`DISPLAY_SIZE` image setting, empty by default. It applies only to Bochs.
Accept exact 32-bit modes at least 64×64, width divisible by eight, with zero
scanout offsets and checked pitch/extent within both BAR0 and reported VRAM.
Also require room for the bar and at least one text cell. Save firmware registers,
use NOCLEARMEM and verify exact readback. Reject malformed/missing/unsupported
sizes before mode writes; restore saved registers on a failed mode change.
If restoration cannot be verified, report activation failure explicitly rather
than claiming a working fallback. No guessed closest mode.

The boot framebuffer lies inside Bochs BAR0. `pci_map_bar()` currently rejects
that overlap and ordinarily maps resources uncached, while the boot aperture is
write-combining. Add a display-specific, validated aperture mapping path before
AP startup, retaining one WC mapping for that physical range. Keep BAR2 uncached.
Do not weaken general PCI overlap checks or create UC/WC aliases. All MMIO/panic
mappings remain valid for the boot, respecting shared kernel-mapping rules.

### Presentation cost and changed regions

Today's presenter redraws the navigation bar and copies `pitch * height` bytes
every tick, even if nothing changed. A visible cursor adds one text-row copy
into scratch backing and glyph composition, without an extra screen pass.
Applications can write their selected buffer without another PRESENT call, so
calls alone cannot establish dirty rectangles.

Propose **full frames for all three drivers**. Boot/Bochs keep direct copies;
VirtIO composes into RAM, then transfers and flushes the full rectangle. This
needs neither framebuffer reads nor damage tracking across AP TTY writers and
unannounced application writes. Changed-region tracking stays outside these
tasks; measured VirtIO costs can justify a separate follow-up. Record composition
CPU time, transfer/flush completion latency and achieved frame rate separately.
PRESENT to userspace keeps meaning selection, not completion or vblank.

Measured baseline: unchanged main `7bc196e`, userland `6e00700`, ports
`42f8374`, fs `b427df2`, lwIP `a1aadb9`, checked-in kernel configuration,
GCC 16.2.0 `-O2`, info logging. No tracked source changes at build time.
The bundle metadata records modified state because an untracked investigation
build log was then in the worktree root; it has since moved under `build/`.
Ordinary `make -j16 image` completed. QEMU
10.2.2 plus the documented AHCI read-cancellation fix, source `983d31c61557`,
q35, nested KVM, `-cpu max`, four cores/one thread each, 256 MiB, default standard
VGA, `-display none`, entropy enabled, no NIC/filesystem/block device. Raw OVMF
CODE/VARS from `/usr/share/OVMF`, with fresh copied variables on each boot.

GDB observed 1280×800, pitch 5120, shifts 16/8/0, 8×16 font, all four spaces
created, active idle Caelum TTY with cursor. A hardware breakpoint at
`space_present` and `finish` bracketed whole calls after warmup. Read HPET at
`0xfffffe80402020f0` directly at both stops; multiply tick difference by 10 ns
(the reported period is 10,000,000 fs). Repeated reads at a stopped breakpoint
were identical, so debugger think time is excluded by QEMU's stopped clock.
This measures guest elapsed time, including preemption and host scheduling/trap
effects, not isolated CPU cycles or device completion. No profiler code was added.

| Fresh boot | Five consecutive sampled calls, ms | Median, ms |
| --- | --- | --- |
| A | 1.26859, 1.28734, 2.42505, 1.09169, 1.03116 | 1.26859 |
| B | 1.32022, 2.04427, 1.42819, 1.69831, 1.06153 | 1.42819 |

The medians differ by 12.6%; these small debugger samples establish an initial
comparison envelope, not a performance guarantee. The framebuffer screen write
is 4,096,000 bytes per call, or **234.375 MiB/s at the nominal 60 Hz**; this is
calculated traffic, not measured throughput. Cursor scratch adds 81,920 bytes of
RAM copying plus composition. Medians occupy about 7.6–8.6% of the 16.67 ms frame
budget. Graphics/GTK/physical-host timings and uninstrumented achieved cadence
remain unmeasured. Refresh this exact profile after ACPI task 3 and use matched
runs before judging task 2 regression; GTK and owner-host results belong in
separate profiles.

Reproduction after boot/warmup, stopped on the BSP in the presenter:

```gdb
hbreak space_present
continue
set $start = *(unsigned long long *)0xfffffe80402020f0
finish
p (*(unsigned long long *)0xfffffe80402020f0 - $start) * 10
continue
```

Repeat the last four commands manually for each sample. This address/scale is
specific to the inspected kernel/QEMU HPET; inspect its mapping and period when
they change. No allocator or clock function calls are injected into the guest.
The built ELF SHA-256 is
`68d1151c0581b861ae7d6695a9c9497e7cb8dbca6e33d7e77805589889921ce2`;
OVMF CODE SHA-256 is
`da1f94a2f51db93fe2b2d67c21a44c32d4bdf77a42f281aeb4abb31004bf518a`,
and original VARS is
`6ed987af3a3c155be71665f510eae3e007eda9b8b94afd59d45e91c4a11565cc`.


### Resize transaction and terminal contents

Handle only the most recent enabled geometry of the selected VirtIO output.
Acknowledge an observed DISPLAY event before querying fresh information, then
recheck events to avoid losing a newer change. Zero/disabled or too-small output
is refused while retaining the last valid geometry. No repeated allocation loop
at 60 Hz for an unchanged refused size; retry on a fresh host event. Preparation
failure logs once per refused request. A newer request replaces pending work.

The resize transaction has five steps:

1. **Prepare.** Allocate the candidate target/resource, every space's new TTY
   framebuffer, bar and cursor scratch without changing published state. Stage
   allocations with IF=0 according to VM ownership. Do not hold the output lock
   across allocation or device commands. Cancel unowned allocations directly;
   attached backing needs confirmed fenced detach/unreference before freeing.
2. **Verify the registry.** Serialize space creation and geometry changes on the
   BSP. Check that the registry and current geometry still match preparation;
   otherwise restart preparation with the current spaces. Drain old presentation
   before changing scanout.
3. **Switch scanout.** Keep old TTY pointers/geometry live during device waits.
   After confirmed device success, recheck the registry. If a space appeared
   during the wait, restore old scanout and restart preparation. On switch
   failure, restore old scanout before resuming. An unresponsive device is a
   terminal driver failure; retain device-owned backing until reboot.
4. **Commit under the output lock.** With IF=0, copy the latest old TTY pixels to
   candidates, then swap all TTY pointers/dimensions and the geometry generation
   before unlocking. Copying before the device wait would lose AP output during
   that wait. No AP retains an old pixel pointer outside this lock. This full
   copy pauses writers; measure the duration across all spaces. Never wait for
   hardware under the lock. Release old backing only when all presenter leases
   and device commands have finished. Boot/Bochs panic targets do not resize.
5. **Present and wake.** Compose and transfer the candidate frame; a brief
   stale/blank screen during transition is permitted. Task 5b publishes geometry
   wakeups after logical commit. In task 5a consumers learn on their next query.

The TTY preserves rasterized whole cells **without reflow**. On shrinking
height, drop enough top rows to keep the cursor's row visible and copy remaining
rows from that origin. On growth keep their positions. Preserve only whole
columns that fit; fill new cells/margins with the TTY background. Translate/clamp
the cursor and clear pending wrap. Keep colours, tab width and escape-parser
state. Cropped or dropped text is lost: no text grid or scrollback exists today.

Recompute the bar viewport/widget placement and pointer bounds. Keep space
identity, selection and input capture. Terminal dimensions are
`width / font_width` and `(height - bar_height) / font_height` in every local
space, including inactive spaces. Existing remote terminal sessions retain their
own rows/columns, input/output queues and size policy.

Expose geometry and generation atomically through console SIZE and a display
size query in task 5a. In task 5b, extend native readiness with a coalesced
RESIZED condition comparing a caller-supplied observed generation, plus input
READABLE for local consoles and
acquired keyboard sessions. Wait registration must check generation and subscribe
under the same lock; a change between query and sleep is immediately ready.
Readiness reserves no input and multiple readers keep existing input arbitration.
A resize wake is separate from input bytes, key/focus events and SIGWINCH.
Console READ/WRITE rights permit geometry observation; display DRAW permits its
size observation, never mode changes. Update wait-interest layouts and all
in-tree consumers together rather than preserving old ABI layouts.

`libterm` must wake a blocked line editor, re-query size and redraw its retained
prompt/input with the new width; preserve text and cursor, even when the line
now exceeds screen capacity. Kilo recomputes viewport/status rows and redraws on
the same notification without requiring a keypress. Query-only programs see the
new size on their next query. Publish corresponding userland/ports PRs before
the parent pins them. These are task 5b consumers, not prerequisites for task 2.

### Mapped graphics lifetime

Keep every acquired mapping's address, extent, pitch and dimensions fixed until
the owner explicitly replaces or releases it. A screen resize changes the
space's destination and size generation, not that mapping. Until adaptation,
copy the top-left intersection and fill uncovered destination pixels; never read
beyond the old buffer. No kernel scaling and no implicit application termination.

Task 5b adds an owner-only REPLACE operation with an expected geometry generation. Through
the existing parked-process BSP loan, allocate/map a zeroed candidate of the
current size at a disjoint address. If generation changed or allocation/mapping
fails, return a retry/error and leave the old session/mapping intact. On success,
return the new descriptor, retire the old user mapping within that explicit call,
and retain old backing while a presenter lease exists. After return the old
pointer is invalid. Keep a visible session visible; use the newly selected blank
buffer until the owner redraws. Do not require RELEASE/ACQUIRE and temporarily
lose exclusive ownership. PRESENT still selects and does not freeze pixels.

Mandelbrot waits for display resize together with keyboard input, replaces at a
render checkpoint and recomputes aspect before redrawing. Doom checks geometry
at its frame boundary, replaces and recomputes integer scale/letterboxing of
its fixed game frame. At a size too small for scale 1 it retains the old mapping
and clipped display until the screen grows again; it keeps running. No game
engine resolution change. Replacement allocation failure keeps rendering into
the old buffer and waits for another resize or an explicit later retry.

For N spaces, a local TTY costs approximately `pitch * (height - bar_height)`
bytes per space; navigation/cursor storage adds `pitch * (bar_height + font_height)`.
VirtIO adds `width * height * 4` bytes for the normal screen surface.
During a resize both old and candidate surfaces/TTY sets coexist. Each graphics
owner additionally retains its old mapping, and an explicit replacement briefly
needs old plus new backing. Budget page rounding, physical-page lists, queues
and VM records as well; no allocation is assumed to succeed. One in-flight frame
bounds retired pixel backing rather than accumulating old geometries. For the
measured four spaces and 8×16 font, 1280×800 VirtIO uses about 19.14 MiB. Preparing
1920×1080 adds about 38.96 MiB, a 58.10 MiB peak before graphics mappings, page
lists, rounding, queues and metadata. One graphics
owner adds 3.75 MiB at the old size, and 7.68 MiB during explicit replacement.
These are calculated budgets, not measured allocation totals or fixed limits.

### QEMU qualification plan

Propose `QEMU_VIDEO=std|virtio|bochs`, default `std`, independent of `QEMU_DISPLAY`.
`scripts/run-qemu.sh` adds the exact device arguments; Make forwards the setting
to run/debug and their USB counterparts. Reject unknown choices before launch.
The image's DISPLAY_SIZE and the host's QEMU_VIDEO have separate lifetimes.

| Selection | Device arguments | Required observation |
| --- | --- | --- |
| std | q35 default VGA, no added video arguments | Default unchanged; task 2 direct-copy baseline, task 4 boot-option size and missing/invalid-size fallback. |
| virtio | `-vga none -device virtio-gpu-pci,disable-legacy=on` | Sole VirtIO display, boot-size UI/shell/Mandelbrot and serial-only panic in task 3; tasks 5a/5b resize while shell/Kilo/graphics are idle or drawing. |
| bochs | `-vga none -device bochs-display` | Exact boot-option size, UI/graphics/panic and checked fallback in task 4. |

For live resizing use `QEMU_DISPLAY=gtk,gl=off,zoom-to-fit=on`, four CPUs and
256 MiB as an initial nested-KVM comparison profile. QEMU 10.2.2 sends GTK
geometry after **one second without further geometry updates**; this is host
coalescing, not a promised kernel latency. Window scaling on std/Bochs is not
mode negotiation. Include one-CPU fallback and owner-run ThinkPad confirmation
for task 2. Wait for merged ACPI task 3 before changing the presenter, then
refresh the baseline on that revision to include the battery widget.

Use ordinary builds, interactive boots and debugger inspection: initial/retired
resource ownership, mapping extents, inactive-space dimensions, resize refusal,
release/exit during a preempted frame and panic takeover. Do not add boot tests,
fault-injection features or output automation. Task 1 measures existing output;
task 2 implements the boot driver below and task 3 implements VirtIO. Bochs mode
setting is recorded in task 4; resizing remains unimplemented.

### Investigation sources

Current Pyxis evidence: `kernel/space.c`, `kernel/object/display.c`,
`kernel/fb/tty.c`, `kernel/fb/early_console.c`, `kernel/pci/resources.c`,
`include/kernel/virtio/queue.h`, `include/kernel/mm/dma.h`,
`arch/x86_64/paging.c`, `userspace/libterm/line.c`,
`userspace/mandelbrot/main.c` and `ports/doom/doomgeneric_pyxis.c`.

Hardware/protocol evidence (QEMU pinned to the measured release):

- [VirtIO 1.4, initialization and GPU sections 3.1/5.7](https://docs.oasis-open.org/virtio/virtio/v1.4/cs01/virtio-v1.4-cs01.pdf):
  queues, display events, scatter backing, resource commands and fences.
- [QEMU GPU command/reset implementation](https://github.com/qemu/qemu/blob/v10.2.2/hw/display/virtio-gpu.c)
  and [display events](https://github.com/qemu/qemu/blob/v10.2.2/hw/display/virtio-gpu-base.c):
  reset destroys firmware resources and prevents a visible boot-framebuffer
  fallback; host geometry produces DISPLAY events.
- [GTK geometry](https://github.com/qemu/qemu/blob/v10.2.2/ui/gtk.c) and
  [console debounce](https://github.com/qemu/qemu/blob/v10.2.2/ui/console.c):
  actual drawing-area size and delayed host notification.
- [Standard VGA interface](https://www.qemu.org/docs/master/specs/standard-vga.html),
  [DISPI definitions](https://github.com/qemu/qemu/blob/v10.2.2/include/hw/display/bochs-vbe.h),
  [VGA mode fixups](https://github.com/qemu/qemu/blob/v10.2.2/hw/display/vga.c) and
  [Bochs validation](https://github.com/qemu/qemu/blob/v10.2.2/hw/display/bochs-display.c):
  BARs, exact mode readback and retained framebuffer contents.

## Working rules

- **The space bar:** ACPI task 3 draws a battery widget in it (`kernel/space.c`).
  It is expected to merge before this milestone reaches the presenter. If it has
  not, wait for it rather than changing the space bar in parallel.
- **Diagnostics:** output needed only to check a driver goes to `ktrace` or is
  removed before review. At most one ordinary log line per device.
- **Measurements:** use existing tools, record nested-VM figures as such, and
  give the revisions and QEMU configuration.

## Task 2 implementation and validation

Branch `display/boot-framebuffer` is based on main `30e127b`, after merged ACPI
battery task 3 (#464) and the kernel log-ring/readers change (#465). It carries
all four review follow-ups from #463. Code is commit `58e0024`; subsequent
commits update the documentation. No public ABI or submodule pin changes.

The implemented interface is `display_init`, `display_layout`,
`display_begin_frame`, `display_copy`, `display_end_frame` and
`display_panic_target`. Only the boot driver exists. Its immutable descriptor
copies the validated boot metadata before AP startup and retains the existing
mapping, pitch and format. There is no new frame allocation or mode change.
Enum dispatch and additional operations wait until another real driver needs
them; no unimplemented driver value or callback table is introduced.

Every physical presenter write, including bar and cursor row, goes through the
interface. Copying checks a permanent panic gate between at most 64 KiB chunks;
end drains WC stores before clearing the writer's CPUID APIC ID. A first panic
may claim RETIRED, fence its interrupted local writer or boundedly await another
CPU's writer, then clear/reset the static renderer and publish panic ownership.
No GS, heap, log lock, VM mutation or scheduler operation is used for takeover.
Allocation and graphics-session snapshot/release behavior is unchanged.

### Refreshed presenter cost

Configuration and measurement method match task 1: QEMU `983d31c61557`
(10.2.2 plus the documented AHCI fix), q35, nested KVM, four CPUs, 256 MiB,
standard VGA, headless display, entropy enabled, no NIC/export/disk, fresh raw
OVMF variables, 1280×800/pitch 5120 and the idle Caelum TTY with cursor.
Use the same HPET reads and manual GDB function entry/finish samples after warmup.

| Source | Five whole-call samples, ms | Median, ms |
| --- | --- | --- |
| Main `30e127b`, after ACPI task 3 | 1.12753, 1.14340, 1.03834, 1.40985, 1.08155 | 1.12753 |
| Task 2 code `58e0024` | 1.46491, 1.07368, 1.04355, 1.19968, 1.42558 | 1.19968 |

The median difference is +6.4%, within the initial task-1 run variation (12.6%)
and with overlapping sample ranges. This small elapsed-time/debugger comparison
supports no material regression in this profile; it is not a CPU-cycle result,
GTK cadence measurement or native performance claim. Task-2 timing was collected
from the matching source before committing it; the committed code was rebuilt
and used for later runtime checks. The baseline branch head was `52cb6f0`, whose
only change from main was the carried review documentation.

Source builds used `make -j16 image`. The changed kernel then used verified
unchanged SDK/userland/ports bundles from that build (`PREBUILT="sdk userspace
ports"`); newer main's log ABI required rebuilding them rather than using the
older task-1 SDK. Kernel builds have no warnings; upstream ports emitted their
existing build warnings. Pinned inputs: userland `bb66de52`, ports `42f83748`,
fs `b427df29`, lwIP `a1aadb91`.

### Runtime results

- Four-CPU boot: the bar, cursor, space navigation, Development shell and
  Mandelbrot display correctly. Escape releases graphics and restores the TTY;
  GDB then observes no owner, frame or user mapping. The app's 1280×768 backing
  has the original pitch/format and its normal snapshot reference lifetime.
- BSP panic after handoff: the first claim closes the gate, resets the screen
  with no published owner, then publishes APIC 0 and displays the panic text.
- AP panic while the BSP has a real frame lease: stopped in the first physical
  copy with writer APIC 0, redirected idle APIC 1 to the existing panic function.
  GDB held the BSP until the panic gate was closed, then released both CPUs.
  The BSP cancelled its copies, fenced and cleared the record; the AP completed
  takeover and showed the panic. A later BSP `display_begin_frame()` returned
  false, retaining the screen. This exercised real recorded-writer handoff,
  with debugger-controlled timing, not a naturally occurring exception race.

- Single-CPU boot: the same bar, cursor, space navigation and Development
  shell are visible. Interrupting a real recorded BSP frame reaches panic reset
  with the gate closed and local writer APIC 0; no remote wait is needed.
- Fault during panic reset: on that single-CPU boot, GDB changed the renderer
  address to unmapped zero before clear. One kernel page-fault report and the
  fatal panic reached serial, no screen owner was published, and the CPU halted
  without recursively rendering the exception. This was a debugger-simulated
  bad mapping; no test or injection code was added.

- Bounded-wait expiry: GDB replaced the writer record with a non-clearing APIC
  1 while redirecting the BSP to panic, holding the other CPUs stopped. The poll
  limit expired, the gate stayed closed, the owner remained unset and panic text
  reached serial. This is a simulated stuck-writer record, not a measured native
  scheduling timeout.

The owner subsequently confirmed normal ThinkPad output in merged PR #471,
completing task 2 as recorded above. Native panic remains untested.
No compiler-container rebuild was needed. Task-2 QEMU/GDB jobs were closed.

## Task 3 implementation and validation

Branch `display/virtio-gpu` starts from merged main `800f979` (#471), which
records the owner's task-2 ThinkPad check. The driver is in
`kernel/display/virtio_gpu.c`; integration adds explicit enum dispatch, an IRQ
route and `display_start`/availability handling. Public graphics ABI and
submodule pins are unchanged. The implementation is commit `fe66d2d`.

### Boot and protocol

The qualified sole-GPU profile has no VGA and no RAMFB. Installed raw OVMF
`edk2-ovmf-20260508-8.fc44` exposes [PixelBltOnly GOP](https://github.com/tianocore/edk2/blob/b03a21a63e3b/OvmfPkg/VirtioGpuDxe/Gop.c#L254-L262).
Pinned [Limine v12.9.0 accepts directly writable pixel formats](https://github.com/Limine-Bootloader/Limine/blob/34fe53c3d27d229ea25c28fc3e04db362543715b/common/drivers/gop.c#L64-L106),
so it supplies no framebuffer. This corrects task 1's GOP assumption and led to
accepted decision 9. The adapter keeps absent metadata all zero; it still rejects
malformed supplied metadata and does not invent a framebuffer reservation.

Before AP startup the driver claims/maps the modern PCI device, negotiates only
VERSION_1, prepares MSI-X under the function/entry masks and allocates a temporary
control queue. With no firmware layout, a fenced GET_DISPLAY_INFO command polls
with IF=0 and a one-second deadline, without a task wait. Configuration/control
routes are NO_VECTOR. After selecting the first enabled nonzero output, it
confirms reset and MASTER disable before releasing temporary queue/control
storage. It retains the PCI claim's permanent DMA-started latch, renegotiates,
restores masked normal vector routes and prepares new queue/backing storage.
No bootstrap queue is reused after AP startup.

The normal queue has at most 16 descriptors and one command in flight. The
cursor queue stays disabled. Backing is page-rounded kernel VM RAM; an explicit
pre-AP physical page list attaches it to one B8G8R8X8 2D resource. Commands and
replies occupy separate persistent DMA storage beside that list. Application
mapping pages are never attached to the device.

The sole BSP presenter with IF=1 enables DMA/IRQ delivery, checks the selected
output, creates the resource and attaches backing. Each complete composed frame
uses TRANSFER_TO_HOST_2D and RESOURCE_FLUSH; only the first frame adds SET_SCANOUT,
after its transfer. Every command checks descriptor completion, exact reply
length/type and matching fence/header fields. Pending runtime requests use IRQ
wakeups and finite timed sleeps; the BSP request service can continue running.
No allocation or VM mutation occurs in presentation.

A command/device failure permanently stops normal presentation, masks interrupts,
disables MASTER and attempts bounded reset. Runtime storage and PCI records
remain until reboot, even after successful reset. ACQUIRE/PRESENT then return
unavailable; RELEASE still tears down mappings. A selected GPU's preparation
failure also cannot claim visible firmware fallback after transport reset. If
neither a usable initial layout nor firmware layout exists, initialization
panics on serial. Layout must fit navigation and at least one terminal row
(current font minimum 80×48); too-small geometry fails explicitly. The
boot-framebuffer path remains selected when no supported GPU is present.

The panic-reporting path closes the display gate and returns no target, without
VirtIO submission, reset or device-register access. Normal work observing the
gate abandons its queue and retains storage. This does not retract an operation
already past its last check when an AP claims panic; no global CPU-stop protocol
is claimed. No emergency queue, reserve or panic log replay was added. The
last-lines replay suggestion from #468 remains an owner-decision follow-up for
remote debugging or direct-screen panic output.

### Runtime observations

Ordinary `make -j16 image` built current main and its SDK/userland/ports. Changed
kernel builds reused those verified unchanged bundles with
`PREBUILT="sdk userspace ports"`; they completed without kernel warnings.
Pinned inputs are userland `d81475f7`, ports `48911d63`, fs `b427df29` and lwIP
`a1aadb91`. No compiler-container rebuild or dependency PR is required.

Interactive QEMU used `983d31c61557` (10.2.2 plus the documented AHCI fix), q35,
nested KVM, `-cpu max`, 256 MiB, headless output, entropy enabled, no NIC/export/
block disk and fresh copied raw OVMF variables. The VirtIO profile used exactly
`-vga none -device virtio-gpu-pci,disable-legacy=on`.

- Four CPUs: GDB before AP startup observed an all-zero boot framebuffer, a
  completed geometry query, a fresh empty 16-descriptor queue, normal DMA disabled
  and a 1280×800/pitch-5120 RAM target. First presentation completed the fenced
  transfer/scanout/flush, bound output 0 and returned all descriptors.
- The bar and all four spaces displayed; navigation selected the Development
  shell. Mandelbrot rendered correctly; Escape restored the prompt and bar.
  Afterwards GDB observed no queue loans and no driver failure.
- One CPU, final implementation source: framebufferless geometry query and
  first presentation completed at the same layout, with all 16 descriptors free.
  The bar, cursor, four spaces and Development shell were visible, navigation
  worked and the driver remained free of errors.
- Standard VGA: the selected driver stayed BOOT with the original pitch/format.
  The bar, space navigation, shell and Mandelbrot looked as before; Escape
  restored the TTY. The launcher adds no video arguments for the default profile.
- A manual BSP redirect into the existing panic function after handoff printed
  `Caelum panic: fatal kernel exception` on serial. The display gate closed,
  screen owner stayed unset, and GPU fence ID 8123, available/used indices 8122
  and device status 15 were unchanged across the panic. This is a debugger-driven
  panic, not a naturally occurring exception.

All task-specific QEMU/GDB jobs were closed. Launcher syntax and invalid
`QEMU_VIDEO` rejection were checked; documentation links and diff whitespace
were reviewed.

Malformed replies, allocation failure and uncertain reset paths were reviewed,
not fault-injected. GTK resizing and uninstrumented frame cadence are not
qualified by this task. Geometry stays fixed until task 5a.

### Presenter cost

Use task 1's direct HPET reads (10 ns/tick), manual GDB stops after warmup and
idle Caelum TTY with cursor. Task 3 splits entry-to-`display_end_frame` composition
from end-to-return transfer/flush; `space_present` tail-calls end, so its finish
returns directly to `space_present_task`. No profiler code was added. Values
include preemption and host/trap scheduling, not just CPU cycles or host GPU time.

| Source/stage | Five elapsed samples, ms | Median, ms |
| --- | --- | --- |
| Task 2 `58e0024`, standard VGA, whole call | 1.46491, 1.07368, 1.04355, 1.19968, 1.42558 | 1.19968 |
| Fresh main `800f979`, standard VGA, whole call | 1.55327, 1.24939, 2.68717, 1.23738, 2.15198 | 1.55327 |
| Task 3, standard VGA, whole call | 1.48476, 1.19591, 1.22034, 1.30288, 1.22494 | 1.22494 |
| Task 3, VirtIO RAM composition | 2.76871, 1.56492, 2.14839, 1.45731, 1.29583 | 1.56492 |
| Task 3, VirtIO transfer/flush completion interval | 2.25720, 1.67253, 0.89907, 0.83809, 1.25917 | 1.25917 |
| Task 3, VirtIO whole call | 5.02591, 3.23745, 3.04746, 2.29540, 2.55500 | 3.04746 |

VirtIO's whole-call median is about 2.54 times the older task-2 median and 1.96
times the refreshed same-main baseline. It occupies about 18.3% of the nominal
16.67 ms frame budget. RAM composition is close to the refreshed direct-copy
median; transfer/flush adds a separate cost. Each frame copies and transfers
4,096,000 bytes, each stage calculated at 234.375 MiB/s for nominal 60 Hz.
The unchanged standard-VGA path measured 1.22494 ms, with ranges overlapping
task 2 and the refreshed baseline; no regression or speedup is established.
These small nested-VM debugger samples do not establish achieved throughput,
GTK cadence or native performance. Full-frame transfer stays the selected
bounded implementation; damage tracking is a measured follow-up, not this task.

## Task 4 implementation and validation

Branch `display/bochs` starts from main `164b463`, after merged PRs #472–#476.
Implementation is commit `29f14d2`, with a one-pass inventory walk in `cbad116` and explicit pre-AP parser ownership
in `4b57bd0`. The owner authorized task 4 after reading
#473 and selected serial panic when
firmware restoration cannot be verified. The #473 mixed-device failure and idle
cost notes are carried into limits/technical debt; no failover or damage tracking
is added. The QEMU results and material limits are recorded below.

The kernel parses native boot options once before display/AP initialization,
retaining typed options and strings in static storage. Boot init consumes that
same result later; UDP-log enabling keeps its previous late activation point.
`display.size` value refusal belongs to the driver, while duplicate keys and
unknown/missing-value options preserve fatal parsing. `DISPLAY_SIZE` assembles
normal/rescue/installer command lines independently of `QEMU_VIDEO`.

Display selection walks supported PCI functions in discovery order. Missing
Bochs size keeps its boot screen rather than trying a later device. VirtIO
selection still has no separate-device fallback after reset. All layout and
mapping changes finish before AP startup and initial space allocation.

Hardware access lives in `arch/x86_64/bochs_display.c`; native selection/layout
lives in `kernel/display/bochs.c`. The driver supports QEMU PCI `1234:1111`
VGA/display-other functions with revision-2 register extensions. It checks
DISPI identity, assigned BAR0/BAR2, exact register extent and VRAM capacity.
Mode dimensions follow hardware bounds, width alignment and the common space
bar/font fit predicate. There is no guessed closest mode or runtime resizing.

Before PCI decoding or mode changes, display handoff retires early drawing and
withdraws direct panic output. Bochs has no DMA/IRQ source; retiring CPU writes
makes its temporary BAR probe quiescent without destroying firmware mode.
The driver verifies BAR/decoding restoration, saves firmware DISPI and byte-order
fields, and switches with NOCLEARMEM. It verifies width, height, BPP, enable,
virtual width/height, offsets, bank and byte order. Standard VGA derives virtual
height from VRAM/pitch; the display-only device stores it explicitly. Failure
restores and verifies the saved state before allowing direct firmware output.
Unverifiable restoration leaves direct output withdrawn and panics on serial.

The display-only aperture helper maps BAR0 WC at the existing fixed framebuffer
address. Boot pixels must start at BAR0; nonzero placement refuses mode setting.
Existing leaves are verified and unchanged; only missing suffix leaves are added,
with rollback on failure. BAR2 stays UC. Existing PCI owners' mappings must not
alias the new aperture; later PCI/ACPI UC mapping also rejects it. The permanent
aperture is not a releasable VM/PCI mapping and remains until reboot. No general
PCI overlap check or mutable-address restriction is relaxed.

Normal/panic copies use the selected immutable direct target and the existing
writer gate/store fences. There is no device operation after AP startup. Current
limits are in [technical debt](../technical-debt.md#bochs-boot-mode-scope-and-aperture-retention):
an enabled firmware DISPI mode is required (no legacy VGA state restoration),
and claims/register mappings/aperture are retained after preparation/refusal.

### Validation

Current main and SDK/userland/ports built with `make -j16 image`; subsequent
kernel builds use verified matching `PREBUILT="sdk userspace ports"`. Pinned
inputs: userland `1b153b16`, ports `e18117d5`, fs `b427df29`, lwIP `a1aadb91`.
No dependency changes or compiler-container rebuild. Changed kernel builds
complete without warnings.

Interactive QEMU uses `983d31c61557` (10.2.2 plus the AHCI fix), q35, nested KVM,
`-cpu max`, four CPUs, 256 MiB, headless output, entropy, no NIC/export/block disk
and fresh raw OVMF variables. The firmware mode is 1280×800.

- Standard VGA, `DISPLAY_SIZE=1024x768`: GDB before AP startup observed the
  selected 1024×768/pitch-4096 target and preserved 1280×800 boot metadata,
  BAR0 16 MiB and BAR2 4 KiB extents. Source review confirms WC pixel
  and UC register mapping paths. All spaces, bar, Development shell and
  Mandelbrot displayed; Escape restored the prompt. A post-handoff BSP redirect
  into the existing panic function cleared the selected 1024×768 screen and
  printed the panic at top-left, also on serial; owner APIC 0 was published.
- `bochs-display` with no VGA, `DISPLAY_SIZE=800x600`: before AP startup, GDB
  observed the selected 800×600/pitch-3200 target, direct panic enabled and the
  display-only register variant. The bar, all spaces, shell and Mandelbrot
  displayed correctly, Escape restored the TTY, and post-handoff BSP panic
  reached serial and the selected 800×600 direct target.

- Default standard VGA with missing `DISPLAY_SIZE`, four CPUs: BOOT remains
  selected at 1280×800/pitch 5120, no Bochs claim/aperture is created, and the first
  frame is visible. The initial missing-size info message moved to trace in #485
  review; explicit malformed, too-small and unsupported requests still warn.
- Standard VGA with `DISPLAY_SIZE=garbage`, four CPUs: malformed-value refusal
  keeps the same BOOT target without a Bochs claim; boot continues and presents.
- `bochs-display` with `DISPLAY_SIZE=16000x12000`, one CPU: capacity refusal
  reports VRAM/BAR overflow, keeps the 1280×800 BOOT target and presents normally.
  Individual 16-bit MMIO reads confirmed width 1280, height 800 and enable 65;
  no DISPI mode or WC aperture was installed.

### Matched presenter cost

Matched idle Caelum TTY/cursor samples at 1280×800 use task 1's direct HPET/GDB
entry/finish method, 10 ns/tick, after startup/warmup. Both boots use standard VGA,
the same font/pitch/format, four CPUs and the profile above. The new boot selects
Bochs at that same size; initial mode setup is outside the measured call.

| Source | Five whole-call samples, ms | Median, ms |
| --- | --- | --- |
| Main `164b463`, boot driver | 1.14367, 1.52225, 3.14138, 1.47683, 1.20291 | 1.47683 |
| Task 4 `29f14d2`, Bochs driver | 1.12928, 1.23989, 1.50068, 1.25868, 1.24480 | 1.24480 |

The medians differ by -15.7%, with overlapping sample ranges. No regression is
observed; these small nested-VM/debugger samples do not establish a speedup.
They measure guest elapsed time including preemption/host/debugger effects,
not isolated CPU cycles, achieved cadence or native performance. Full-frame
traffic stays 4,096,000 bytes/call, calculated at 234.375 MiB/s for nominal 60 Hz.
No profiler, benchmark infrastructure or optimization is added. The later
inventory-loop cleanup changes boot selection only, outside these measurements.

Allocation failure, register-readback failure and failed restoration were
inspected, not fault-injected. Direct panic tests use manual GDB register redirects,
not naturally occurring exceptions. Installer/rescue configuration propagation
and literal command-line limits were reviewed; USB boots are not repeated for
this display task. The final VirtIO regression boot selects its 1280×800 RAM
layout without a boot framebuffer, completes the first fenced presentation,
returns all queue ownership, and displays the normal UI. The updated boot-option
ownership guard runs before AP startup. No source tests or boot automation were
added. All task-specific QEMU/GDB jobs were closed.

## After the milestone

- **Physical GPUs.** A driver for real hardware, investigated against an Intel
  integrated GPU passed through from horse, or natively on the ThinkPad over PXE
  with [remote debugging](remote-debugging.md). The owner prepares the physical
  GPUs. Loading vendor firmware blobs is acceptable (owner, 2026-10-07).
  Passing the ThinkPad's GPU through VFIO is ruled out: its IOMMU group also
  holds the PSP, both USB controllers and the audio devices.
- Programs that change the resolution, multiple monitors, vblank timing and
  double buffering stay separate work. The
  [desktop and graphics direction](desktop-graphics.md) covers the compositor
  and SDL2.

## Related

[Mapped graphics buffers](../interfaces/graphics.md),
[early console](../kernel/early-console.md), [PCI](../devices/pci.md),
[VirtIO queues](../devices/virtio-queues.md), QEMU's
[VirtIO GPU documentation](https://www.qemu.org/docs/master/system/devices/virtio/virtio-gpu.html)
and [implementation](https://github.com/qemu/qemu/blob/master/hw/display/virtio-gpu-base.c),
and the
[visible-work sequence](storage-and-terminal-agenda.md#4-native-terminal-sessions-multiplexer-and-navigator).
