# Early boot console

Caelum draws kernel log output directly on the boot framebuffer from shortly
after Limine handoff until the display presenter's first frame. Before that
frame, no space TTY is visible, so this console is the only on-screen record of
boot progress and early panics. It is for hardware bring-up, not a terminal.

## Lifetime

1. **Start.** The Limine adapter validates the framebuffer straight after the
   memory map, ahead of the command-line, ACPI and initrd checks. It then
   starts the console through Limine's write-combining direct map. Earlier
   failures are serial-only: an unsupported base revision, missing responses,
   executable placement and an invalid memory map.
2. **Rebind.** `paging_init` switches the console to the kernel's own
   framebuffer mapping immediately after the CR3 write, before the next log
   line. The direct map no longer exists at that point.
3. **Mirror and retain.** Every ordinary log line goes live to serial and the
   early console. Before space 0's initialized TTY attaches, log bytes are also
   retained in static storage. Attaching the TTY replays that prefix once under
   the log lock, then enables live TTY output. The TTY draws into an off-screen
   buffer that is not yet presented; replay does not duplicate serial output.
4. **Handoff.** Before its first framebuffer write, the presenter takes the log
   lock and retires the console. Afterwards the console never draws again, and
   the presenter logs `display: presentation started; early console retired`.

## Retained boot log

The current buffer holds 32 KiB without heap allocation. It keeps the beginning
when full and counts later discarded bytes, saturating the count at `SIZE_MAX`.
Replay writes the kept bytes directly to the TTY, followed by
`[early log truncated: N bytes dropped]` when necessary. The replay and TTY
selection save/restore caller interrupts and share the ordinary log lock;
concurrent output cannot interleave at the boundary. Capturing stops permanently
after the first attachment. Selecting a TTY again does not replay or restart it.
Emergency panic output bypasses capture and TTY replay.

This is byte retention, not terminal scrollback or a userspace-readable log.
Later output can still scroll the beginning off screen. A full buffer can cut
a line or escape sequence; the truncation notice starts on a fresh line. Use
info logging for native bring-up: PCI function/resource/capability details,
per-space initialization and ordinary task exits require `LOG_LEVEL=trace`.
Discovery summaries and warnings remain at info. Buffer capacity is an
implementation choice, not a required architectural minimum.

## Rendering

Rendering uses the Bizcat glyph data with fixed colours and does no heap
allocation. It does not use the TTY, escape sequences or framebuffer reads.

- Text is kept in a fixed BSS grid of at most 256 by 128 cells. Larger
  screens use a top-left region.
- Glyphs are doubled on framebuffers at least 2560 pixels wide.
- Scrolling redraws only cells whose character changes. Tabs become spaces,
  and unprintable bytes are drawn as `?`.

## Panic ownership

One atomic state leaves `ACTIVE` exactly once, by compare-exchange:

| State | Who draws |
| --- | --- |
| `ACTIVE` | Ordinary output, under the log lock |
| `PANIC` | Only the first panicking CPU |
| `RETIRED` | Nothing; the presenter owns the screen |

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
- **Panic claim.** `klog_panic_begin` claims the console for the first
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
- **Other panics.** Panics on other CPUs are serial-only. The owner re-enters
  through `panic()` after an exception report and keeps drawing. A fault raised
  while the owner is drawing stops its drawing, because the fault may come from
  the framebuffer mapping itself. In that case the original panic text is lost
  from the screen, but the fault report still reaches serial.
- **Handoff during a panic.** If the presenter's `log_begin()` fails, a panic
  is in progress and the presenter skips the frame without assuming it holds
  the lock. If retirement loses to a panic, the presenter never draws.

Panics after the handoff remain serial-only. See
[technical debt](../technical-debt.md#early-console-and-post-handoff-panics).

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
