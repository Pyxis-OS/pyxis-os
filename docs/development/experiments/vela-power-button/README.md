# Vela power-button scope — assigned 2026-10-14

Implementation authorized; event-route default accepted 2026-10-14. The owner reported Linux's control-method
power button `PNP0C0C` (`PWRB`) and Intel HID `INTC10CC` 5-button array. These are
enumeration facts, not proof of which event route carries this machine's press.
This task follows [NVMe compatibility](../nvme-task1/README.md#vela-nvme-20-scope--assigned-2026-10-14)
and remains a separate PR. Current control: main `0068e4d2`.

## Bounded handling

- Discover present matching devices through uACPI, register worker-owned
  device Notify handlers, and retain their contexts for the namespace lifetime.
  Accept `PNP0C0C` Notify `0x80`; accept Intel power press `0xce`, ignore its
  `0xcf` release. Other notifications remain trace-only. Preserve the fixed
  power event for machines using it.
- Use only Intel's power-button setup, with advertised `_DSM` function 1 or
  the named `BTNL` method. Do not enable unrelated hotkeys/tablet functions.
  Confirm the simple-mode contract before admitting that interface. A setup
  failure leaves other power sources and boot working, with an explicit reason.
  The generic HID event path, if needed for power, reads the event index and
  acts only on the defined power index; it does not expose a new input API.
- Reuse the existing physical-access authority and clean power-off latch;
  no capability, forced-power shortcut, restart choice or sleep policy changes.
  Both sources coalesce while queued together. No speculative debounce interval.
- Suppress presses during startup setup, service SCI and drain queued Notify
  work before arming. Also drain deferred notifications while a power operation
  is still marked running before discarding its press latch on failure. Merely
  clearing the fixed-event latch would leave a deferred press queued to retry.
  Never clear an already-owned EC GPE just to initialize a button.
- Keep all AML, discovery, handlers and method calls on the existing BSP ACPI
  worker. Do not run AML in interrupt entry or bypass ACPI mapping/PCI rules.

## Owner decision — accepted 2026-10-14

Keep existing event sources and enable only a firmware-proven additional
runtime power-button GPE. `_PRW` is a candidate, not runtime-route proof:
require AML evidence that it carries the press, then enable its existing
handler with `uacpi_enable_gpe()`. Never guess a GPE number and report
**unqualified** when the route cannot be established. Do not call general
runtime-GPE finalization. No Vela AML route has yet been provided, so this
implementation must leave all additional GPEs disabled.

## Qualification and missing evidence

First capture the unchanged fixed-event QEMU baseline;
repeat the normal QEMU power-button and clean-shutdown checks after the change,
with GDB inspection of registrations and queued work. Stock QEMU does not model
vela's Intel HID interface or prove its AML route. No new test framework,
boot automation or speculative firmware writes.

Native evidence needed: vela's DSDT/SSDT button, Intel HID and GPE/EC method
routing (serial-free disassembly sections are sufficient), then one short
owner-present press with the Caelum log photographed. The existing trace
Notify record can establish whether the supported EC path already delivers it.
Keep the current recovery overlay and storage refusal/force behavior. No native
run or firmware change has been performed by Codex.

References, inspected for facts only; no GPL driver code is copied:
[ACPI control-method button example](https://uefi.org/specs/ACPI/6.5/04_ACPI_Hardware_Specification.html),
[ACPI wake routing via _PRW](https://uefi.org/specs/ACPI/6.5/05_ACPI_Software_Programming_Model.html#managing-a-wake-event-using-device-prw-objects),
[Linux v6.19 button driver](https://raw.githubusercontent.com/torvalds/linux/v6.19/drivers/acpi/button.c),
and [Linux v6.18 Intel HID driver](https://raw.githubusercontent.com/torvalds/linux/v6.18/drivers/platform/x86/intel/hid.c)
(power setup and press/release meanings). The pinned in-tree uACPI
`include/uacpi/event.h` distinguishes selective enable from whole runtime
initialization; its Notify implementation queues delivery as worker work.
