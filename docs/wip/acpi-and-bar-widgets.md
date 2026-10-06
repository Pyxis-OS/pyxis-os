# ACPI power control and battery

Status: **milestone, agreed 2026-10-07.** It follows the
[system layout](../userland/system-layout.md) milestone. The owner chose uACPI
as the ACPI interpreter (2026-10-06) and accepted the three decisions below.
Each task starts when the owner says so.

## Goal

- **Clean power-off and reboot.** Today powering off means `sync system://` and
  `sync home://`, then holding the power button, and restarting is a power cycle.
- **Battery readings.** The test device is a laptop. On 2026-10-07 the ThinkPad
  switched itself off on low battery during a PXE boot attempt. It happened in
  firmware, before an Update was due to run.
- **Space-bar widgets.** The battery charge is the first widget: a small box
  showing 0–100 in the [space bar](../userland/init.md#space-bar).

## Today

- Early boot reads static ACPI tables directly (`arch/x86_64/acpi.c`): the MADT
  for CPUs and interrupt routing, the FADT's legacy flags and SCI, the HPET and
  the PCI configuration space. Task 1 adds the uACPI interpreter, which loads
  the namespace on a BSP worker ([ACPI](../kernel/acpi.md)). No ACPI device is
  used yet.
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

Accepted for task 1 on 2026-10-07:

4. **Firmware mappings** live in a kernel window reserved before AP startup.
   A page address is mapped at most once and never reused. ACPI and firmware
   memory is cached; reserved or unlisted memory is uncached; RAM, kernel and
   loader memory are refused.
5. **PCI configuration** may be read by AML through ECAM. Writes return an
   error and are logged. Revisit with ThinkPad evidence if AML needs them.
6. **Scope and failure.** Task 1 leaves GPEs and fixed events disabled. The
   embedded controller driver and GPE enabling move to task 3, and the power
   button's fixed event to task 4. If uACPI fails to initialize, boot continues
   without ACPI and logs why.

Accepted on 2026-10-07, after task 1:

7. **AC adapter.** Task 3 reads AC presence from the AC adapter device's `_PSR`
   method. The battery's charging bits do not say whether AC is connected.
8. **Userspace exposure** is a separate task 5, after the widget, so that task 3
   stays bounded. Task 5 settles the query's fields and authority.
9. **Notifications instead of polling** wait until after task 3. They depend on
   events being re-armed after an unclaimed SCI, which belongs with that work.

Accepted for task 2 on 2026-10-07:

10. **Stopping user tasks** is a reversible hold. Each user task stops at its next
    return to user mode and is parked; a failed flush releases them. After the
    final flush, the native filesystem refuses further pool changes until the hold
    is released, so a task inside a syscall cannot change a pool afterwards.
11. **`poweroff` and `reboot` are shell builtins.** The shell keeps the `power`
    handle and never forwards it to the programs it runs.
12. **Remote shells do not receive `power`,** even in a space that has it; the
    remote terminal server is unauthenticated on the LAN.

## Tasks

- [x] **1. Bring in uACPI.**
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
  - **Status:** done. QEMU with one and four CPUs and the ThinkPad (owner,
    2026-10-07) load the namespace without AML errors, refused mappings or
    refused PCI writes. Costs are recorded in [ACPI](../kernel/acpi.md#measurements).

- [ ] **2. Clean power-off and reboot.**
  - The `power` capability and its boot-init forwarding (decision 2), plus the
    native `poweroff` and `reboot` commands.
  - The shutdown sequence from decision 3. Power-off enters S5 through uACPI.
    Reboot uses the FADT reset register, with a documented fallback if a machine
    lacks one.
  - **Status:** implemented ([ACPI](../kernel/acpi.md#power-off-and-restart)).
    QEMU meets every check below with one and four CPUs; the ThinkPad checks
    are the owner's. The run script no longer passes `-no-reboot -no-shutdown`,
    so a guest power-off exits QEMU and a reset reboots it.
  - **Finish when:**
    - `poweroff` makes QEMU exit;
    - the ThinkPad turns off;
    - after `reboot` the firmware shows the Limine menu again;
    - files written without `sync` survive both;
    - the pool's journal is empty on the next boot;
    - a space without `power` is refused.

- [ ] **3. Battery reading and the battery widget.**
  - An embedded controller driver: the `EmbeddedControl` operation region
    handler, from the ECDT or the `PNP0C09` device, plus its query GPE. uACPI
    does not include one, and the battery methods read through it.
  - Enable GPEs once the embedded controller is ready (decision 6).
  - Read the battery through the AML methods `_BST`, and `_BIF` or `_BIX`: the
    remaining and full capacity and the charging state. Read AC presence from
    the AC adapter's `_PSR` (decision 7).
  - Draw a fixed-width widget at the right end of the space bar, inside the
    chevron. It shows the percentage, refreshes every few seconds and is hidden
    without a battery. The kernel presenter reads the value directly.
  - **Finish when:** the ThinkPad shows a percentage that tracks charging and
    discharging and matches Linux's reading within a few percent, and QEMU,
    which has no battery, shows no widget.
  - **Review notes from task 1, to settle in this task:**
    - an unclaimed SCI currently masks ACPI events until reboot; once the
      embedded controller's GPE is enabled, consider re-arming after a short
      delay or a counted limit instead;
    - record the "firmware window" figure before and after periodic battery
      reads; the ThinkPad already uses 2,127 of 16,384 pages after loading.

- [ ] **4. Power button.**
  - A short press of the physical power button runs the same clean power-off as
    `poweroff`. Holding it remains the firmware's emergency path.
  - Enable the power button's fixed event, or its control-method device on
    machines that use one (decision 6).
  - **Finish when:** a short press on the ThinkPad powers off cleanly, with an
    empty journal on the next boot.
  - The unclaimed-SCI note under task 3 applies here too: a spurious SCI must
    not silently disable the power button until reboot.

- [ ] **5. Battery and AC state for userspace.** (Decision 8.)
  - A read-only query of the battery and AC state, for example on the existing
    system-information capability, so that Fastfetch can show its Battery and
    Power Adapter modules and a later status UI has its data.
  - Its fields, units and authority are settled when the task starts.

After task 3, battery and AC changes can be signalled by ACPI notifications
instead of polling (decision 9).

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
