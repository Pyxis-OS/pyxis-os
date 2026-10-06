# ACPI and space-bar widgets

Status: **direction, recorded 2026-10-06.** The owner chose uACPI as the ACPI
interpreter. This comes after the [system layout](system-layout.md) milestone.
Nothing here authorizes code or placeholder APIs; each part needs its own
decisions before it starts.

## Goal

Show small status widgets in the [space bar](../userland/init.md#space-bar). The
first is the ThinkPad's battery charge: a small box with 0–100. The same ACPI
work should also give Pyxis a real power-off.

## Today

- The kernel reads only static ACPI tables (`arch/x86_64/acpi.c`): the MADT for
  CPUs and interrupt routing, the FADT's legacy flags, the HPET and the PCI
  configuration space. There is no AML interpreter, so ACPI devices and methods
  cannot be used.
- On the T14, as on most laptops, the battery is an ACPI control-method battery.
  Its charge comes from AML methods that read the embedded controller.
- Powering off means `sync`, then holding the power button
  ([USB installation](../devices/usb-installation.md)). The native filesystem
  already plans to flush on clean shutdown once one exists
  ([native filesystem](native-filesystem.md)).
- The space bar is drawn by the kernel presenter (`kernel/space.c`): a 32-pixel
  strip with a chevron slot at each end and equal-width tabs between them.

## Direction

### 1. uACPI

Port [uACPI](https://github.com/uACPI/uACPI), an MIT-licensed ACPI
implementation designed to be embedded in small kernels, as a pinned vendor
dependency with its notices preserved.

- **Kernel glue:** physical memory mapping, I/O ports, PCI configuration access,
  interrupt installation, timing and sleeping, locking and logging. Each is
  implemented on Caelum's existing primitives, following the SMP rules: which
  CPU runs AML, and which locks it may hold.
- **First use, power-off:** enter the S5 sleep state after a clean filesystem
  flush. This replaces holding the power button, and a restart can follow the
  same path. The flush ordering belongs to the native filesystem's clean-shutdown
  rule.
- **Second use, the battery:** evaluate the battery's status and information
  methods (`_BST`, and `_BIF` or `_BIX`) to get the remaining and full
  capacities, and from them a percentage.
- **Later uses**, not part of the first slice: lid, power-button and AC-adapter
  events, thermal zones and sleep.

Reading the embedded controller's registers directly, at offsets taken from the
T14's DSDT, was considered and set aside: it is model-specific and works around
the firmware rather than through it.

### 2. Space-bar widgets

- **Area:** a fixed-width area at the right end of the space bar, inside the
  chevron, that widgets draw into. Tabs fit into the remaining width.
- **Battery box:** the first widget shows `0`–`100`, refreshed every few
  seconds, and is hidden when no battery is present. Charging or AC state and
  low-battery styling can come later.
- **Data source:** the kernel presenter reads the battery value directly. A
  general interface that lets userspace services publish widget text, such as
  a widget capability with short bounded text, is a later decision.

## Open questions

- **Execution context:** where AML runs (a BSP kernel worker or any CPU), and how
  ACPI interrupts (SCI) and embedded-controller events are routed.
- **Power-off authority:** which capability lets a session request power-off or
  restart, and who holds it.
- **Widget layout:** width, how many widgets, and what the bar does when tabs
  and widgets do not fit.
- **Validation:** QEMU's ACPI covers power-off but has no battery, so the
  battery needs native ThinkPad checks.

## Related

[Spaces](spaces.md), [later OS directions](later-os-directions.md) and the
[ThinkPad target notes](../targets/t14-gen1-amd/notes.md).
