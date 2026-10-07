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

- [ ] **1. Proposal.** A docs PR that updates this document with:
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

## Working rules

- **The space bar:** ACPI task 3 draws a battery widget in it (`kernel/space.c`).
  It is expected to merge before this milestone reaches the presenter. If it has
  not, wait for it rather than changing the space bar in parallel.
- **Diagnostics:** output needed only to check a driver goes to `ktrace` or is
  removed before review. At most one ordinary log line per device.
- **Measurements:** use existing tools, record nested-VM figures as such, and
  give the revisions and QEMU configuration.

## After the milestone

- **Physical GPUs.** A driver for real hardware, investigated against either an
  Intel integrated GPU passed through from horse, or a GPU passed through VFIO
  to a VM on a headless Fedora machine. The owner prepares the physical GPUs and
  the passthrough.
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
