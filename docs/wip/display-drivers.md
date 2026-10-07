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

- **One fixed framebuffer.** Limine passes the framebuffer that the firmware
  (GOP) set up, with its size fixed for the whole boot (`struct boot_framebuffer`
  in `include/kernel/boot.h`).
- **The presenter copies everything.** `space_present()` in `kernel/space.c` copies
  the space bar and then the whole active space into that framebuffer, about 60
  times a second. The source is the space's TTY buffer or a program's
  [mapped graphics buffer](../interfaces/graphics.md).
- **Every buffer has the boot layout.** Space buffers, the cursor row, the space
  bar and program graphics buffers all copy the boot framebuffer's width, pitch
  and pixel format. The graphics ABI promises fixed dimensions.
- **Panics write to the boot framebuffer** through the early console
  (`early_console_panic_begin()`).
- **QEMU devices:**
  - `scripts/run-qemu.sh` passes no display device, so q35 provides the
    standard VGA. That device already has the Bochs mode registers.
  - With `virtio-gpu-pci`, OVMF's GOP driver supplies the boot framebuffer. That
    framebuffer is ordinary RAM: nothing copies it to the screen once boot
    services exit.
- **The ThinkPad** has an AMD integrated GPU. Pyxis has no driver for it,
  so it keeps the boot framebuffer.

## Decisions

Agreed with the earlier VirtIO GPU direction:

- 2D presentation of the software-rendered screen; no 3D acceleration, shaders or
  compositor.
- Limine's framebuffer stays the boot-time output.
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

- [ ] **2. The interface, with the boot framebuffer as its first driver.**
  - The presenter writes through the interface. Nothing visible changes.
  - **Finish when:**
    - QEMU and the ThinkPad look and behave as before;
    - the presenter cost matches the task 1 baseline within run-to-run variation.

- [ ] **3. VirtIO GPU 2D at the boot size.**
  - Create a 2D resource, attach guest memory as its backing, set the scanout,
    then present by transferring to the host and flushing.
  - **Finish when:**
    - with `virtio-gpu-pci` and no VGA device, QEMU shows the space bar, spaces,
      the shell and Mandelbrot after boot;
    - a panic is visible;
    - the presenter cost is recorded against the task 2 figures;
    - the default standard-VGA run is unchanged.

- [ ] **4. Bochs.**
  - Set the mode through the Bochs display registers at the boot-option size
    (decision 3), on QEMU's standard VGA and on `bochs-display`.
  - **Finish when:**
    - QEMU boots at a size different from the firmware's, with a correct space
      bar, terminal and Mandelbrot;
    - an unsupported or missing size falls back to the boot framebuffer, with a
      message.

- [ ] **5. Live resizing.**
  - VirtIO GPU display-change events, plus resize handling across spaces,
    terminals and graphics programs, following the contract chosen in task 1.
  - **Finish when:**
    - resizing QEMU's GTK window changes the terminal's columns and rows in
      every space;
    - a running graphics program follows the task 1 contract, with no stale
      mapping;
    - a refused resize leaves the old size working.

## Task 1 investigation and proposal

Delivered against main `7bc196e` on 2026-10-07. Everything in this section is
**proposed**, except the inspected current behavior and measured baseline.
Checking task 1 means the investigation is delivered, not that its choices are
accepted or task 2 is authorized. Review the three decisions at the end.

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
| Prepare | Before AP startup, inspect supported PCI functions in discovery order, validate/map resources and allocate fixed queue/panic storage. Retain the boot layout/mapping; see the VirtIO fallback limit below. |
| Get target | Return a kernel pixel surface and immutable layout: address, width, height, pitch, channel shifts and geometry generation. No boot-protocol pointers. |
| Activate | On the BSP, finish initialization and present a first complete frame before publishing the selected driver. Restore firmware output on Bochs failure; VirtIO has the limit below. |
| Present | Finish the composed frame: drain WC stores for direct framebuffers; transfer and flush the VirtIO resource, checking responses and fences. |
| Check size | Read/coalesce pending VirtIO display information. Boot and Bochs report no runtime changes. |
| Prepare/commit/cancel size | Stage a replacement target without changing published layout; switch device scanout only after all space allocations are ready. Cancel frees unowned allocations; attached backing needs confirmed fenced detach/unreference or is retained until reboot. |
| Panic takeover/present | Claim emergency ownership and render/publish without allocation, locks, scheduling or the ordinary queue helper. Bounded failure preserves serial reporting. |

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

Current inspection corrects the summary above: **panics after presenter handoff
are serial-only today**, including standard VGA. `early_console_retire()` makes
RETIRED terminal. Tasks 2–4 must extend this rather than assume an existing
post-handoff panic renderer. The boot mapping remains reserved, even when retired
as normal output; it is not a fallback screen after another device owns scanout.
VirtIO transport preparation resets the device before BAR sizing, destroying
GOP scanout resources. Even before reset, its firmware framebuffer is RAM that
OVMF no longer transfers after ExitBootServices. Retaining this mapping cannot
promise live early output or visible fallback after failed VirtIO activation;
that interval/failure is serial-only until a driver resource is established.
This is a device limit, unlike Bochs firmware-mode restoration.

Keep the early console until the driver's resources and the first screen are
ready. Retire its ordinary writer under the log lock before any mode/scanout
change. A separate atomic display ownership gate coordinates normal mutation,
layout publication and the first panic claim. Publish an immutable emergency
descriptor only when its backing and mappings are valid; initialization and
resize must keep an old valid descriptor or exclude takeover during their short
mutation phase. Do not expose half-programmed modes. If Bochs activation fails, restore
the old mode and bind emergency rendering to that restored target. If VirtIO
activation fails after reset, retain serial diagnostics without claiming the
old RAM framebuffer is visible.

A panic revokes normal rendering. If it interrupted this CPU's renderer, that
continuation is abandoned. On another CPU it boundedly waits for the active
mutation/store interval to finish; timeout stays serial-only. Reuse the early
console's static glyph grid, first-owner rule and recursion/fault guard. Its
renderer needs a layout descriptor independent of Limine. Boot and Bochs render
directly into their current mapped screen and fence stores.

For VirtIO, reserve a **1024×768×4 = 3,145,728-byte** panic surface plus separate
rings and command/reply storage before AP startup. After exclusive takeover,
reset and confirm reset, renegotiate VERSION_1 and program the emergency rings
as **control queue 0**. Recreate the 2D resource, attach the reserved backing,
transfer, set scanout and flush through bounded polled commands. Reset destroys
host resources, so a resource created at boot cannot simply be reused. Render
the panic text in RAM before the final transfer/flush; exception output that
precedes `panic()` must also be published. The panic may use its own fixed size.

This is an explicit emergency exception to BSP/IF=1 normal device ownership.
Do not restart or modify the ordinary `struct virtqueue`: its helper retains
failed queue storage until reboot. Keep all old DMA backing retained. If takeover,
reset or device commands fail, retain serial output and halt; visible output on
a broken device cannot be promised. Task 2 establishes direct panic takeover;
task 3 adds this VirtIO emergency path, with debugger inspection of the normal
queue and emergency resource ownership.

### Hardware constraints

VirtIO 2D requires VERSION_1, the control queue, GET_DISPLAY_INFO, RESOURCE_CREATE_2D,
RESOURCE_ATTACH_BACKING, TRANSFER_TO_HOST_2D, SET_SCANOUT and RESOURCE_FLUSH.
Use checked response types/lengths and fences for completion that changes resource
or backing ownership. Select the first enabled scanout, retain its index and
ignore other outputs for this milestone. Task 3 creates it at boot pixel size;
task 5 follows the selected output's reported size. With no usable output,
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
`display_size=WIDTHxHEIGHT` in the kernel boot command line, supplied by a Make
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

Allocate the candidate target/resource, every space's new TTY framebuffer, bar
and cursor scratch before publishing anything. Serialize space creation and
geometry changes on the BSP; verify the registry/generation before scanout switch and again before logical
commit. If a space was appended during the device wait, restore old scanout and
restart preparation with the new registry; never omit that space from the swap.
Keep old buffers until all presenter leases and device commands are finished.
VirtIO panic descriptors refer to the fixed emergency backing, not runtime
resize surfaces; direct boot/Bochs targets do not resize in this milestone. No AP may retain an old TTY pixel pointer outside the output
lock. Stage allocations with IF=0 according to VM ownership; do not hold the
output lock across allocation or device commands.

Proposed TTY policy: preserve rasterized whole cells **without reflow**. On
shrinking height, drop enough top rows to keep the cursor's row visible; copy
remaining rows from that origin. On growth keep their positions. Preserve only
whole columns that fit, fill newly exposed cells/margins with the TTY background,
translate/clamp the cursor, and clear pending wrap. Keep colours, tab width and
escape-parser state. Text cropped horizontally or dropped vertically is lost:
there is no retained text grid or scrollback today. This avoids adding another
terminal storage model to the driver milestone.

Switch scanout only when old presentation is drained and replacement allocations
are complete. Keep old TTY pointers/geometry live during device waits. After
confirmed scanout success, acquire the output lock with IF=0, copy the *latest*
old pixels into candidates and swap all TTY pointers/dimensions plus the geometry
generation before unlocking. Copying before the device wait would lose AP output
written during that wait. This one-time full copy pauses writers; measure its
duration, including all spaces, rather than claiming a short critical section.
No device wait under the lock. Then compose and transfer the candidate screen;
a brief stale/blank screen during the transition is permitted. Publish geometry
wakeups after logical commit. If switching fails, restore the old
scanout before resuming; an unresponsive device is a terminal driver failure,
not an ordinary refused allocation. Existing device-owned memory stays retained.

Recompute the bar viewport/widget placement and pointer bounds. Keep space
identity, selection and input capture. Terminal dimensions are
`width / font_width` and `(height - bar_height) / font_height` in every local
space, including inactive spaces. Existing remote terminal sessions retain their
own rows/columns, input/output queues and size policy.

Expose geometry and generation atomically through console SIZE and a display
size query. Extend native readiness with a coalesced RESIZED condition comparing
a caller-supplied observed generation, plus input READABLE for local consoles and
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
the parent pins them. These are task 5 consumers, not prerequisites for task 2.

### Mapped graphics lifetime

Keep every acquired mapping's address, extent, pitch and dimensions fixed until
the owner explicitly replaces or releases it. A screen resize changes the
space's destination and size generation, not that mapping. Until adaptation,
copy the top-left intersection and fill uncovered destination pixels; never read
beyond the old buffer. No kernel scaling and no implicit application termination.

Add an owner-only REPLACE operation with an expected geometry generation. Through
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
VirtIO adds `width * height * 4` bytes and the fixed 3 MiB panic surface.
During a resize both old and candidate surfaces/TTY sets coexist. Each graphics
owner additionally retains its old mapping, and an explicit replacement briefly
needs old plus new backing. Budget page rounding, physical-page lists, queues
and VM records as well; no allocation is assumed to succeed. One in-flight frame
bounds retired pixel backing rather than accumulating old geometries. For the
measured four spaces and 8×16 font, 1280×800 VirtIO uses about 22.14 MiB including
the panic surface; preparing 1920×1080 adds about 38.96 MiB, a 61.10 MiB peak
before graphics mappings, page lists, rounding, queues and metadata. One graphics
owner adds 3.75 MiB at the old size, and 7.68 MiB during explicit replacement.
These are calculated budgets, not measured allocation totals or fixed limits.

### QEMU qualification plan

Propose `QEMU_VIDEO=std|virtio|bochs`, default `std`, independent of `QEMU_DISPLAY`.
`scripts/run-qemu.sh` adds the exact device arguments; Make forwards the setting
to run/debug and their USB counterparts. Reject unknown choices before launch.
The image's DISPLAY_SIZE and the host's QEMU_VIDEO have separate lifetimes.

| Selection | Device arguments | Required observation |
| --- | --- | --- |
| std | `-vga std` | Default unchanged; task 2 direct-copy baseline, task 4 boot-option size and missing/invalid-size fallback. |
| virtio | `-vga none -device virtio-gpu-pci,disable-legacy=on` | Sole VirtIO display, boot-size UI/shell/Mandelbrot and post-handoff panic in task 3; task 5 resize while shell/Kilo/graphics are idle or drawing. |
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
none of the proposed drivers or resize behaviors have been implemented/qualified.

### Three decisions for review

1. **TTY preservation:** use cell-aligned raster cropping, keeping the cursor row,
   without reflow or scrollback. It loses cropped text but keeps task 5 bounded.
   Retaining a character grid would enable a richer resize policy and add memory
   plus changes to every terminal drawing operation.
2. **Graphics adaptation:** stable old mappings plus explicit owner REPLACE,
   clipping until adaptation; Mandelbrot/Doom adapt at rendering checkpoints.
   This costs overlapping allocations but avoids dangling pointers and lets a
   slow or allocation-limited program keep running.
3. **Panic:** permit fixed 1024×768 emergency VirtIO output with a 3 MiB reserve,
   and serial-only fallback on unsafe takeover or hardware failure. Direct
   framebuffer panic output is restored in task 2. Reserving runtime-sized panic
   backing would increase resize peak memory and its failure cases.

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
  reset destroys resources; host geometry produces DISPLAY events.
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
