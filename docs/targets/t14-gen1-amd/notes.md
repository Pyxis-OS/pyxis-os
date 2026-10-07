# ThinkPad T14 Gen 1 AMD inventory notes

Reviewed Linux observations collected on 2026-10-02 with Fedora 44 and kernel
6.19.10-300.fc44.x86_64:

- [Undocked inventory](thinkpad-inventory-undocked.txt), starting at 20:57:52 +03:00.
- [Docked inventory](thinkpad-inventory-docked.txt), starting at 21:08:38 +03:00.

These are Fedora enumeration snapshots, not Pyxis hardware qualification or
storage-integrity measurements. The original comparison used main `9efe4a5`;
the support notes below include the later multi-controller/USB 2 hub work.

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

## CPU topology

Linux `lscpu` reports one socket, six cores with two threads each (12 CPUs) and one NUMA node. It
lists six L1/L2 instances and two 8 MiB L3 instances in total, meaning two core complexes
(CCXs) of three cores. The base clock is 2.1 GHz, with frequency boost enabled.

A native Pyxis boot on 2026-10-05 reported these APIC IDs in startup order. Pyxis CPU
indices follow that order, and the sibling and CCX columns are inferred:

| Pyxis CPU | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| APIC ID | 0 | 1 | 2 | 3 | 4 | 5 | 8 | 9 | 10 | 11 | 12 | 13 |
| Core (inferred) | 0 | 0 | 1 | 1 | 2 | 2 | 3 | 3 | 4 | 4 | 5 | 5 |
| CCX (inferred) | 0 | 0 | 0 | 0 | 0 | 0 | 1 | 1 | 1 | 1 | 1 | 1 |

The inference follows AMD's usual APIC ID layout: bit 0 selects the SMT thread, and the
gap at 6–7 starts the second CCX. Neither Fedora capture records per-thread APIC or
core IDs, and that Pyxis revision did not read CPUID topology leaves. The grouping is therefore
consistent with the hardware, not enumerated by it. It means:

- SMT siblings are adjacent Pyxis CPUs.
- Pyxis CPU 1 shares a core with the BSP (CPU 0).

That scheduler treated all 12 as independent CPUs. In the
[SMP task-4a native check](../../development/experiments/smp-task4a/README.md#native-thinkpad-check-owner-run),
four concurrent compute clients finished as two near the single-client time and two
about 1.8× slower. That fits two clients sharing one core, and the
[task-8 check](../../development/experiments/smp-task8/README.md#native-thinkpad-check-owner-run)
repeated it. The current [topology-aware tie-break](../../kernel/smp.md#placement-and-migration)
reads CPUID. The [native check on 2026-10-07](../../development/experiments/core-placement/README.md#native-thinkpad-check-owner-run)
confirmed these APIC IDs and SMT shift 1, with core keys
`0, 0, 1, 1, 2, 2, 4, 4, 5, 5, 6, 6`; they retain the APIC gap, unlike the
dense inferred labels in the historical table.

## ACPI embedded controller and battery

From the owner's `acpidump` under Fedora (2026-10-07), disassembled with ACPICA
20260408:

- There is no ECDT. The embedded controller is `\_SB.PCI0.LPC0.EC0`
  (`PNP0C09`), with data port 0x62, command and status port 0x66 and GPE 0x03.
  It has no `_GLK`.
- `EC0._REG` sets `H8DR`. Until then the firmware reaches the EC through SMI
  calls (`RBEC`, `WBEC`, `MBEC`); afterwards through `EmbeddedControl` fields.
- `BAT0` (`PNP0C0A`) has `_BIX` and `_BIF`. `_BST` takes the power unit from the
  package that `_BIX` fills, and `_BIX` may sleep up to ten seconds while the
  battery reports busy. `AC` (`ACPI0003`) has `_PSR`.
- `EC0` defines about 50 `_Qxx` query methods.

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

- [Controller discovery](../../../kernel/usb/xhci.c) now inspects each discovered
  xHCI independently. The three observed functions are no longer an ambiguous
  inventory selection; their capabilities and native firmware state remain unqualified.
- [Current enumeration](../../devices/usb-enumeration.md) handles directly
  attached root-port devices and supported USB 2 hub descendants. The observed
  SuperSpeed dock storage routes still need unsupported SuperSpeed hub traversal;
  uninspected descendants make inventory incomplete even if a directly attached
  disk is also observed.
- Vendor-specific fingerprint interfaces are valid unbound observations. The undocked
  baseline on `07:00.3` has no attached peripheral, making undocked USB-A Right
  a useful proposed first native qualification route. This is a validation
  recommendation, not a controller-selection policy or baked-in port map.
- Linux choosing UAS does not by itself prove BOT availability. A separate
  2026-10-02 Linux inspection found BOT at alternate 0 and UAS at alternate 1
  for the owner's `0781:55a9` stick; Pyxis later
  [selected BOT natively](usb-bringup.md#2026-10-04-read-only-storage-and-usb-3-hub-follow-up).
  These inventories contain no full stick configuration/endpoint descriptors.
- Fedora enumeration does not establish Pyxis support. Native
  [USB storage](../../devices/usb-storage.md) and the
  [first native installation](usb-bringup.md#2026-10-05-first-native-installation-c4)
  are recorded separately.

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

## Native status

Booted over PXE or from an installed USB stick, Caelum runs the normal system on
all 12 CPUs with the full 32 GB. The first owner-reported
[native USB inventory](usb-bringup.md) records the three xHCI controllers, the
USB 2 dock subtree and the first native installation.

- **Clock:** the 32-bit HPET runs [software-extended](../../kernel/timekeeping.md#software-extension-sampling-and-support-limit)
  (`clock: HPET 32-bit counter, software-extended, period=69841278 fs`). The
  owner's `date` check on 2026-10-03 spanned two of the counter's ~300 s wraps,
  and on 2026-10-07 the owner kept a native session running for more than
  15 minutes with the clock holding.
- **Keyboard:** works through PS/2. The scan-set query ID is optional, because
  this controller returns none after selecting set 2; see the
  [keyboard contract](../../devices/keyboard.md).
- **Entropy:** RDSEED with RDRAND as fallback, since there is no virtio-rng; see
  [randomness](../../devices/randomness.md).
- **Ethernet:** the built-in port through the [RTL8111 driver](../../devices/rtl8111.md),
  with [DHCP](../../devices/dhcp.md). The
  [NIC passthrough reference](../../development/thinkpad-nic-passthrough.md)
  covers developing it in QEMU first.
- **Remote work:** the [reverse terminal and UDP kernel log](../../development/remote-debugging.md)
  replace serial, which this machine lacks.
- **Power:** [ACPI](../../kernel/acpi.md) power-off, restart, power button and
  battery. On battery the firmware throttles the CPU hard: Quake `timedemo demo1`
  drops from about 660–690 fps on AC to about 400. State AC or battery for
  native performance figures, and compare on AC.
- **Display:** the boot framebuffer. Occasional tearing in Doom is accepted,
  because firmware framebuffers have no vsync and nothing is page-flipped.

Not supported: NVMe (the internal disk), Wi-Fi, the dock-facing Ethernet,
suspend and resume, and TSC as a clock source.

## Network controllers

The machine has two Realtek RTL8111-family controllers (`10ec:8168`):

| Port | Function | Revision | IOMMU group | Notes |
| --- | --- | --- | --- | --- |
| Built-in RJ45 | `05:00.0` | 0x15 | 15, alone | PXE port; TxConfig XID `541`, supported. |
| Dock Ethernet | `02:00.0` | 0x0e | 12, shared | RTL8168ep, XID `502`, unsupported. The group also holds two UARTs (`02:00.1`, `02:00.2`), an IPMI interface (`02:00.3`) and an EHCI controller (`02:00.4`). |

The second controller is on the motherboard; the dock only provides its jack.
AX200 Wi-Fi is `03:00.0`. The router reserves a fixed address for the built-in
port; its static profile is in the
[driver reference](../../devices/rtl8111.md#selection-and-machine-configuration).

## DASH management controller (parked)

The dock-facing controller is an AMD DASH management controller. On 2026-10-07
the owner reached it, and then decided not to rely on it for boot logs: the
setup is fragile, and the [UDP kernel log](../../development/remote-debugging.md)
covers the need. The findings, if it is revisited:

- **Web interface:** HTTPS on port 664 at the controller's own LAN address,
  whether Fedora is asleep or awake. Remote Control offers only power on, power
  off and reset; the Battery page shows presence and health, not charge.
- **AMD DASH CLI:** discovery on HTTP port 623; enumeration and text redirection
  need HTTPS on 664 with digest authentication and the CLI's option to accept
  the self-signed certificate.
- **Serial over LAN:** two text-redirection services, both disabled by default:
  Telnet on port 87 and SSH on port 57. `textredirection connect` without `-t`
  prompts for an instance and activates it; the client must connect within
  about 20 seconds or the service switches off again. Telnet works with the
  DASH account. SSH does not: the server offers only
  `diffie-hellman-group1-sha1` and refused connections after a first failed
  attempt.
- **The forwarded UART is `ttyS4`:** `02:00.1`, `10ec:816a`, I/O `0x3200`, at
  115200 baud. Without a session it drains at about 80 bytes per second, so a
  kernel log writer must never wait on it.
- **Earlier attempts** (2026-10-03) found no DASH settings in the BIOS and no
  reply while powered off; Linux's r8169 signals "OS driver active" to the
  firmware for this variant. The powered-off laptop once woke during scans,
  plausibly through Wake-on-LAN, unconfirmed.

Caelum reads only this controller's XID and leaves it untouched. A writer for
`0x3200` would find the UART by PCI ID, never wait for a session, and replay the
log ring once found. Caelum's serial output goes only to COM1 (`0x3f8`).
