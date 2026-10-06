# ACPI power control and battery

Status: **milestone, agreed 2026-10-07.** It follows the
[system layout](system-layout.md) milestone. The owner chose uACPI as the ACPI
interpreter (2026-10-06) and accepted the three decisions below. Each task
starts when the owner says so.

## Goal

- **Clean power-off and reboot.** Today powering off means `sync system://` and
  `sync home://`, then holding the power button, and restarting is a power cycle.
- **Battery readings.** The test device is a laptop. On 2026-10-07 the ThinkPad
  switched itself off on low battery during a PXE boot attempt. It happened in
  firmware, before an Update was due to run.
- **Space-bar widgets.** The battery charge is the first widget: a small box
  showing 0–100 in the [space bar](../userland/init.md#space-bar).

## Today

- The kernel reads only static ACPI tables (`arch/x86_64/acpi.c`): the MADT for
  CPUs and interrupt routing, the FADT's legacy flags, the HPET and the PCI
  configuration space. There is no AML interpreter, so ACPI devices and methods
  cannot be used.
- On the T14, as on most laptops, the battery is an ACPI control-method battery.
  Its charge comes from AML methods that read the embedded controller.
- The native filesystem flushes periodically and on `sync`. It already plans to
  flush on clean shutdown once one exists
  ([native filesystem](native-filesystem.md)).
- The space bar is drawn by the kernel presenter (`kernel/space.c`): a 32-pixel
  strip with a chevron slot at each end and equal-width tabs between them.

## Decisions

Accepted by the owner on 2026-10-07:

1. **Execution: one owner.** A single BSP kernel worker owns uACPI, AML
   execution, the ACPI interrupt (SCI) and deferred ACPI work. Other kernel code
   asks it through BSP requests, like today's other single-owner services.
2. **Authority: a `power` capability** with power-off and restart rights. Boot
   init gives it to spaces whose configuration entry sets `power = true`. The
   installed `pyxis` space and the live Development space default to it, the
   same pattern as `network = true`. Native `poweroff` and `reboot` commands use
   it.
3. **What "clean" means at first:**
   - user programs are not asked to exit;
   - the kernel stops running user tasks, then flushes and checkpoints every
     mounted npfs pool and flushes the block devices;
   - only then does it power off (S5) or reset;
   - if a flush fails, it reports the failure and stays up rather than risk
     data.

   A protocol for asking services to stop comes later, with service supervision.

## Tasks

- [ ] **1. Bring in uACPI.**
  - Vendor [uACPI](https://github.com/uACPI/uACPI) (MIT) at a pinned revision,
    with its notices, under the kernel's vendor-dependency rules.
  - Implement its kernel interface on Caelum's primitives, following decision 1
    and the [SMP rules](../kernel/smp.md):
    - physical mapping, I/O ports and PCI configuration access;
    - locks, events and sleeping;
    - SCI installation through the MADT/FADT interrupt routing;
    - deferred work and logging.
  - Load the ACPI namespace at boot. No device is used yet.
  - **Finish when:** QEMU and the ThinkPad both boot with the namespace loaded and
    no AML errors, and the memory and boot-time cost is recorded.

- [ ] **2. Clean power-off and reboot.**
  - The `power` capability and its boot-init forwarding (decision 2), plus the
    native `poweroff` and `reboot` commands.
  - The shutdown sequence from decision 3. Power-off enters S5 through uACPI.
    Reboot uses the FADT reset register, with a documented fallback if a machine
    lacks one.
  - **Finish when:**
    - `poweroff` makes QEMU exit;
    - the ThinkPad turns off;
    - after `reboot` the firmware shows the Limine menu again;
    - files written without `sync` survive both;
    - the pool's journal is empty on the next boot;
    - a space without `power` is refused.

- [ ] **3. Battery reading and the battery widget.**
  - Read the battery through the AML methods `_BST`, and `_BIF` or `_BIX`: the
    remaining and full capacity, the charging state and whether AC is present.
  - Draw a fixed-width widget at the right end of the space bar, inside the
    chevron. It shows the percentage, refreshes every few seconds and is hidden
    without a battery. The kernel presenter reads the value directly.
  - **Finish when:** the ThinkPad shows a percentage that tracks charging and
    discharging and matches Linux's reading within a few percent, and QEMU,
    which has no battery, shows no widget.

- [ ] **4. Power button.**
  - A short press of the physical power button runs the same clean power-off as
    `poweroff`. Holding it remains the firmware's emergency path.
  - **Finish when:** a short press on the ThinkPad powers off cleanly, with an
    empty journal on the next boot.

## Proposals, not agreed

- **Battery-aware Update.** The installer warns, or refuses, when running on
  battery below a threshold without AC, as firmware updaters do. The 2026-10-07
  low-battery shutdown happened just before a planned Update.
- **Low-battery warning.** The widget changes style at a low level. Later, a clean
  power-off happens automatically at a critical level, before the firmware cuts
  power.
- **More widgets.** A general interface for userspace services to publish widget
  text, for example a widget capability with short bounded text.

## Open questions

- **Widget layout:** not a concern for now (owner, 2026-10-07). The resolutions
  Pyxis runs at leave room for several widgets. Details are settled with the
  task 3 implementer. If there are ever too many, mouse support could show the
  less important ones on click.
- **Later ACPI uses:** lid and AC-adapter events, thermal zones and sleep. These
  are not part of this milestone.

Reading the embedded controller's registers directly, at offsets taken from the
T14's DSDT, was considered and set aside: it is model-specific and works around
the firmware rather than through it.

## Related

[Spaces](spaces.md), [later OS directions](later-os-directions.md) and the
[ThinkPad target notes](../targets/t14-gen1-amd/notes.md).
