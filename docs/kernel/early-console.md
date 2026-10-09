# Early boot console

Caelum draws kernel log output directly on the boot framebuffer from shortly
after Limine handoff until display handoff. Before the presenter's first
frame, no space TTY is visible, so this console is the only on-screen record of
boot progress and early panics. If Limine supplies no framebuffer, early output
stays serial-only until a display driver presents. It is for hardware bring-up,
not a terminal.

## Lifetime

1. **Start.** The Limine adapter validates the framebuffer straight after the
   memory map, ahead of the command-line, ACPI and initrd checks. It then
   starts the console through Limine's write-combining direct map. Earlier
   failures are serial-only: an unsupported base revision, missing responses,
   executable placement and an invalid memory map. An absent framebuffer is
   accepted; malformed supplied metadata remains fatal.
2. **Rebind.** `paging_init` switches the console to the kernel's own
   framebuffer mapping immediately after the CR3 write, before the next log
   line. The direct map no longer exists at that point.
3. **Mirror and retain.** Every ordinary log line goes live to serial and the
   early console. The static log ring retains bytes from the first output and
   continues after space 0's TTY attaches. Attaching the TTY replays retained text once under
   the log lock, then enables live TTY output. The TTY draws into an off-screen
   buffer that is not yet presented; replay does not duplicate serial output.
4. **Handoff.** The boot driver retires ordinary console output at its first
   presenter frame under the log lock. Device preparation withdraws direct
   output and retires the console before pre-AP PCI decoding/mode changes.
   Panic may reclaim a verified boot/Bochs target through the display interface;
   VirtIO stays serial-only. On its first frame the presenter logs
   `display: presentation started; early console retired`.

After a Bochs refusal that follows early-console retirement, serial stays live
but the screen may remain stale or blank until the first presenter frame shows
the Caelum TTY, including intervening retained logs.

## Retained boot log

The [kernel log ring](../interfaces/kernel-log.md) keeps the most recent lines
in 256 KiB of static storage, continuing after TTY attachment. Replay writes
retained text directly into the TTY, followed by
`[early log truncated: N lines dropped]` when needed. The replay and TTY
selection preserve caller interrupts and share the ordinary presentation lock;
concurrent normal output cannot interleave at the boundary. Selecting a TTY
again does not replay it.

Retention is separate from drawing and TTY scrollback. A program can read the
ring with `log` or follow it with `log -f`; later TTY output may still scroll
earlier text off screen. Panic capture is best effort and never waits for a
held ring lock. Info logging retains discovery summaries and warnings; PCI
function/resource/capability details, per-space initialization and ordinary
task exits require `LOG_LEVEL=trace`.

## Rendering

Rendering uses the Bizcat glyph data with fixed colours and does no heap
allocation. It does not use the TTY, escape sequences or framebuffer reads.

- Text is kept in a fixed BSS grid of at most 256 by 128 cells. Larger
  screens use a top-left region.
- Glyphs are doubled on framebuffers at least 2560 pixels wide.
- Scrolling redraws only cells whose character changes. Tabs become spaces,
  and unprintable bytes are drawn as `?`.

## Panic ownership

One atomic state leaves `ACTIVE` through handoff or panic. The first panic
claims either `ACTIVE` or `RETIRED` by compare-exchange; `PANIC` is terminal:

| State | Who draws |
| --- | --- |
| `ACTIVE` | Ordinary output, under the log lock |
| `PANIC` | Only the first panicking CPU |
| `RETIRED` | The display presenter; the panic path may take it back |

- **Ordinary output.** Each character's render step records the drawing CPU
  in `drawing_cpu`, then rechecks the state. Both steps are sequentially
  consistent, so a CPU that claims a panic either sees the record or is seen
  by the renderer.
  - CPUs are identified by their CPUID APIC ID, which needs no GS, LAPIC
    mapping or memory.
  - Long operations such as scrolling recheck the state per cell and abandon
    work once the console has left `ACTIVE`.
  - The renderer drains write-combining stores before clearing the record.
  - After the handoff, the step returns before identifying the CPU.
- **Before handoff.** `klog_panic_begin` claims the console for the first
  panicking CPU, then checks for a render step in progress.
  - **On this CPU.** The step was interrupted and never resumes, so the owner
    takes over at once.
  - **On another CPU.** That CPU may only be delayed, so the owner requires the
    step to finish, including its store fence, before touching the shared
    state. If the bounded wait expires, the panic stays serial-only and no
    owner is recorded.
  - Once it owns the console, the owner clamps the cursor and draws without
    the lock, as the only writer. A half-finished update on its own CPU can
    leave stale characters, but never out-of-range writes.
- **After handoff.** The first claimant closes the physical display's panic
  gate permanently. The BSP presenter publishes its APIC ID before rechecking
  that gate, so either it refuses the frame or the claimant sees the writer.
  Screen copies check the gate between chunks of at most 64 KiB; completion
  drains WC stores before clearing the writer record. A panic on the writer's
  CPU fences its abandoned stores and proceeds. Another CPU boundedly waits for
  the record to clear; timeout stays serial-only and future frames are refused.
  This bounds unchecked copy size, not scheduler or host delays.
  - The kernel retained the driver layout before AP startup; no boot-response
    pointer, heap, GS, lock, VM mutation or scheduler operation is needed.
  - After takeover, reset the static text grid and clear the current screen.
    Keep panic ownership unpublished until this finishes, so a fault during
    reset reports on serial without recursive rendering.
  - The panic starts at the top-left, independent of the active space/graphics.
    It uses the driver's pitch and format, including on physical hardware that
    still uses the boot framebuffer. A selected VirtIO GPU returns no panic
    target and performs no device operation on the panic-reporting path; its
    early console is retired before AP startup. Normal device work observes the
    gate and abandons its queue without releasing DMA storage. An AP claim does
    not retract a normal operation already past its last gate check.
- **Other panics.** Panics on other CPUs are serial-only. The owner re-enters
  through `panic()` after an exception report and keeps drawing. A fault raised
  while the owner is drawing stops its drawing, because the fault may come from
  the framebuffer mapping itself. In that case the original panic text is lost
  from the screen, but the fault report still reaches serial.
- **Handoff during a panic.** If the presenter's `log_begin()` fails, a panic
  is in progress and the presenter skips the frame without assuming it holds
  the lock. If retirement loses to a panic, the presenter never draws.

A failed remote-writer takeover or framebuffer fault remains serial-only. See
[technical debt](../technical-debt.md#early-console-and-post-handoff-panics).

## Physical display interface

[`display.h`](../../include/kernel/display.h) separates the physical screen
from per-space graphics capabilities. `display_init()` retains a valid boot
layout and prepares the first supported VirtIO or Bochs driver before AP startup. Without a
boot framebuffer, a bounded pre-AP GPU query supplies the initial dimensions.
Spaces obtain dimensions/format through
`display_layout()`; no space retains a boot framebuffer descriptor. The boot
driver retains the existing mapping and makes no new allocation or mode change.

The sole BSP presenter pairs `display_begin_frame()` with `display_end_frame()`
and sends every physical write through `display_copy()`. A failed begin writes
nothing. `display_start()` activates the chosen driver once on that task.
VirtIO end submits a full transfer and flush and waits for validated fenced
responses without blocking other BSP tasks. End also runs after cancelled
copies, fences stores, then releases
physical ownership before graphics snapshot cleanup. Bochs selects its exact
initial mode before AP startup, with direct panic output withdrawn while PCI
decoding or mode registers change. It publishes the immutable selected target
only after readback, or returns to a verified firmware target on failure.
Unverifiable restoration halts boot on serial. The bar, cursor composition,
full-frame cadence and userspace slot/SUBMIT lifetime are unchanged.

## Serial

COM1 is checked with a loopback self-test, whose output is not transmitted.
Transmit waits are bounded by a fixed poll count, because no clock exists that
early. An absent port, or one that stops accepting bytes, is latched
unavailable for the rest of boot, so it cannot stall boot or the screen. A port
that later recovers is not retried.

## Breadcrumbs

Existing lines already mark Limine, the descriptor tables, paging, the clock,
the APIC timer, the keyboard, the initrd, SMP and spaces. The following lines
were added so that the last visible line narrows down a hang:

- `early console: WxH framebuffer`
- `mm: kernel VM and heap ready`
- `PCI: discovery starting` and `PCI: discovery complete`
- `tasks: scheduler and BSP request queues ready`
- `devices: starting virtio, block and native filesystem workers` and
  `devices: workers started`
- the handoff line

## Validation

QEMU 10.2.2 under nested KVM, four CPUs, 256 MiB, at 1280x800, with GDB
attached. Panics and the fault were triggered by temporary, uncommitted code
selected from GDB.

- **Before the paging switch:** the start line and the panic were visible.
- **After the switch:** the full boot log through `mm: kernel VM and heap
  ready` and the panic were visible.
- **Kernel page fault after the switch:** the full exception report and
  `Caelum panic: fatal kernel exception` were visible.
- **Fault while drawing a panic** (the console address set to an unmapped page
  from GDB): drawing stopped after the first character. Serial showed one
  exception report and the final panic, and the CPU halted with no recursion.
- **Normal boot:**
  - Stopped at the handoff, the early console showed the boot log scrolled to
    the end of userspace start.
  - GDB showed the state change from `ACTIVE` to `RETIRED`, with the address in
    the kernel's own mapping.
  - Afterwards, 248 early-console calls produced no `draw_cell` calls.
- **No serial device** (`-serial none`): the loopback test marked serial
  unavailable, no kernel text reached the host, and boot and the handoff
  completed normally.
- **Render step in progress when a panic is claimed** (after review). GDB
  stops all CPUs together, so it cannot pause one renderer while another CPU
  panics. Instead, at the claim, GDB set `drawing_cpu` to the value such a
  renderer leaves behind.
  - Another CPU (APIC 1) that never finishes: the wait expired, no owner was
    recorded, no `draw_cell` call occurred, and the panic appeared on serial
    only.
  - Another CPU that finishes during the wait: the claim was observed inside
    the wait loop. After the record cleared, the panic was drawn.
  - The panicking CPU itself (APIC 0): the wait loop was not entered, the owner
    was APIC 0 and the panic was drawn.
  - The fault-while-drawing case and a normal boot and handoff were rerun with
    the same results as above.
- **Not exercised:** a real concurrent race between CPUs. Only the recorded
  states were simulated.

## Display takeover validation, task 2

On code `58e0024`, QEMU 10.2.2 plus the documented AHCI fix (`983d31c61557`),
nested KVM, q35, standard VGA, 256 MiB, raw Fedora OVMF and headless display:

- Normal four-CPU boot, navigation, cursor, shell, Mandelbrot and release back to
  the TTY retain the boot layout. A single-CPU boot also shows the normal UI.
- A post-handoff BSP panic displays text after clearing the screen. The owner is
  unset through reset, then APIC 0 owns drawing.
- A post-handoff AP panic closes the gate while the BSP has a real recorded
  frame. After GDB releases the BSP, its cancelled frame fences and drops the
  record; APIC 1 owns the screen. A later BSP frame acquisition returns false.
- A same-CPU interrupted writer reaches reset without a remote wait. Changing
  the renderer address to unmapped zero there produces one serial page-fault
  report and fatal panic, without an owner publication or recursive drawing.
- A debugger-simulated non-clearing remote writer expires the bounded wait,
  leaves the gate closed and prints the panic on serial without a screen owner.

Panics were triggered by redirecting an existing kernel context to `panic()`
with an existing read-only format string, using breakpoints and register writes.
No debugger-injected function call, test hook or fault-injection code was added.
The AP handoff used debugger-controlled real writer/gate transitions; timeout
and bad mapping were simulated state changes. The owner confirmed normal
ThinkPad output in PR #471; native panic and naturally occurring concurrent
failures remain unmeasured. Presenter timing is
recorded in the [display milestone](../kernel/display.md#qualification-and-cost).
