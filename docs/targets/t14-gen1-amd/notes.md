# ThinkPad T14 Gen 1 AMD inventory notes

Reviewed Linux observations collected on 2026-10-02 with Fedora 44 and kernel
6.19.10-300.fc44.x86_64:

- [Undocked inventory](thinkpad-inventory-undocked.txt), starting at 20:57:52 +03:00.
- [Docked inventory](thinkpad-inventory-docked.txt), starting at 21:08:38 +03:00.

These are Fedora enumeration snapshots, not Pyxis hardware qualification or
storage-integrity measurements. Pyxis implementation comparisons below refer to
main revision `9efe4a5`.

## Platform and docking

The machine is Lenovo type `20UES1YD34`, with BIOS `R1BET87W` version 1.56,
an AMD Ryzen 5 PRO 4650U (six cores, twelve threads), and two reported 16 GB DDR4
memory devices configured at 2667 MT/s. Fedora booted through 64-bit UEFI with
Secure Boot disabled. A TPM 2.0 and an AMD IOMMU are present; Linux reports
translated DMA domains. These observations do not establish Pyxis DMA behavior.

The complete PCI listings are identical docked and undocked. Docking adds USB
hubs, Billboard/HID interfaces and dock audio, without adding PCI functions.
Both captures already contain two Realtek Ethernet functions, Intel AX200 Wi-Fi,
Renoir graphics/audio, the NVMe controller and the card reader.

## USB controllers and observed port routes

| PCI function | Observed controller | Linux USB 2 bus | Linux USB 3 bus |
| --- | --- | --- | --- |
| `07:00.3` | AMD `1022:1639`, xHCI 1.10 | 1 | 3 |
| `07:00.4` | AMD `1022:1639`, xHCI 1.10 | 4 | 5 |
| `06:00.0` | Renesas `1912:0015`, xHCI 1.00 | 6 | 7 |
| `02:00.4` | Realtek `10ec:816d`, EHCI | 2 | None |

The stick snapshots identify a SanDisk `0781:55a9`, reported as 114.6 GiB with
512-byte logical and physical sectors. Every tested attachment negotiates
5000 Mbit/s and Linux selects `uas`.

| Snapshot label | PCI xHCI | Linux USB path | Intervening hubs |
| --- | --- | --- | ---: |
| Laptop USB-A Right, undocked and docked | `07:00.3` | `3-2` | 0 |
| Laptop USB-A Left, undocked | `07:00.4` | `5-2` | 0 |
| Laptop USB-C Left (Power), undocked | `07:00.3` | `3-1` | 0 |
| USB-A Dock 1 | `07:00.4` | `5-1.2.1` | 2 |
| USB-A Dock 2 | `07:00.4` | `5-1.2.2` | 2 |
| USB-A Dock 3 | `07:00.4` | `5-1.2.3` | 2 |
| USB-A Dock 4 | `07:00.4` | `5-1.3` | 1 |
| USB-C Dock 1 | `07:00.4` | `5-1.1` | 1 |
| USB-C Dock 2 | `07:00.4` | `5-1.4` | 1 |

These paths come from the per-socket `lsusb -t` and `sda` sysfs snapshots.
Linux bus numbers and paths describe this capture, not stable kernel identifiers
or xHCI hardware port numbers.

The dock's USB 3 root-facing hub at `5-1` negotiates 10000 Mbit/s; the second
hub at `5-1.2` negotiates 5000 Mbit/s. Its USB 2 branch is `4-1` then `4-1.2`,
with dock audio at `4-1.2.4`. A Billboard/HID device also appears at `1-1` on
the other AMD controller. Thus both AMD controllers participate in docking,
although every tested dock storage socket routes through `07:00.4`.

The undocked baseline has the fingerprint reader (`06cb:00bd`, vendor-specific
interface) at `4-3`, AX200 Bluetooth at `4-4`, and the camera at `6-2`.

## Consequences for current Pyxis USB support

- [Controller selection](../../../kernel/usb/xhci.c) requires a unique PCI xHCI
  function before checking controller capabilities. The three observed xHCI
  functions would produce an ambiguous selection if Caelum discovers the same
  inventory. Moving the stick alone does not resolve that limitation.
- [Current enumeration](../../devices/usb-enumeration.md) handles directly
  attached root-port devices. Dock storage needs unsupported hub traversal;
  connected hubs make inventory incomplete even if a directly attached disk is
  also found on the selected controller.
- The vendor-specific fingerprint interface on `07:00.4` is another potential
  incompleteness source under current classification policy. The undocked
  baseline on `07:00.3` has no attached peripheral, making undocked USB-A Right
  a useful proposed first qualification route once controller selection is
  addressed. This is a recommendation, not an agreed controller-selection policy.
- Linux choosing UAS does not by itself prove BOT availability. The separate
  [SanDisk target observation](../../wip/usb-installation.md#known-target-and-missing-evidence)
  records BOT alternate 0 and UAS alternate 1 for the owner's `0781:55a9` target.
  These inventories contain no full stick configuration/endpoint descriptors.
- [Native USB block access](../../devices/usb-enumeration.md) remains pending.
  Fedora enumeration and the provisional Pyxis BOT endpoint setup do not
  establish readable media under Pyxis.

## Device condition and evidence limits

The internal SK hynix NVMe reports SMART health passed, zero critical warnings,
zero media/data-integrity errors and zero error-log entries, with 2% endurance
used. This is the device's reported health at capture time, not an independent
integrity check. Its unusually large reported power-cycle count is a counter
observation; these snapshots do not explain it.

The Renesas driver reports a missing `renesas_usb_fw.mem`, explicitly falls back
to ROM, and subsequently enumerates both root buses and the camera. That warning
does not establish an unusable controller. Seven fingerprint-reader resets are
recorded; their cause is not established. No stick address-assignment or
enumeration failure appears in the supplied USB sections.

Both captures share historical activity from the same Linux boot. In particular,
the undocked log includes earlier dock connections, while its timestamped
baseline has no dock hubs. Use the per-socket snapshots for attachment state;
do not interpret the whole boot log as a fresh undocked or docked boot.

The captures do not establish the stick's USB 2 companion routes, direct routing
through the remaining laptop USB-C socket, USB-C orientation behavior, firmware
ownership handoff or pre-OS controller state. Linux MSI-X settings describe the
running Linux driver, not firmware settings presented to Caelum.

## Next bring-up steps

The owner's order after reaching a native shell (2026-10-03) is recorded in
[ThinkPad next steps](../../wip/thinkpad-next-steps.md): entropy, then Ethernet.
