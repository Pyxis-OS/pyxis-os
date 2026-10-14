# IdeaPad Slim 5 16IPH11 notes

The third hardware target and the first Intel machine, host name `vela`. Linux
observations were collected on 2026-10-14 with Fedora 44 KDE and kernel
7.2.10-200.fc44.x86_64 ([inventory](ideapad-inventory.txt); serials, UUIDs, GUIDs
and MACs redacted). They are enumeration snapshots, not Pyxis qualification.

## Platform

Lenovo type `83S6` (board `LNVNB161216`), BIOS `T6CN25WW`, Secure Boot disabled
from the factory. An Intel Core Ultra 7 356H (Panther Lake H) with 16 logical
CPUs and no SMT, and 32 GB of DDR5-5600 as two 16 GB SO-DIMMs (two slots, up to
64 GB). The 16-inch panel is a 2880x1800 120 Hz OLED with Samsung S-Stripe
subpixels; Linux drives it at 12 bits per channel, but exposes no EDID through
the eDP connector. There is no built-in Ethernet.

The 1 TB SanDisk SN7100S (2242, DRAM-less, NVMe 2.0) holds Fedora in its first
half: a 1 GB ESP, a 2 GB `/boot` and a 511 GB btrfs root and home. The other
510 GB is unallocated for Pyxis.

## CPU topology

The MADT lists only x2APIC entries (type 9), all enabled, with APIC IDs:

| Group (inferred) | APIC IDs |
| --- | --- |
| 4 performance cores | 0, 8, 16, 24 |
| 8 efficient cores | 32, 34, 36, 38, 40, 42, 44, 46 |
| 4 low-power efficient cores | 64, 66, 68, 70 |

The grouping is inferred from the ID spacing and the 356H's 4+8+4 core
configuration; neither Linux nor Pyxis has enumerated core types here. All IDs
are below 256, so 8-bit IO-APIC and MSI destinations reach every CPU.

## Linux reference behaviour

- Firmware enables x2APIC and locks it on; Linux enables interrupt remapping in
  x2APIC mode (DMAR present).
- Linux disables the HPET ("HPET dysfunctional in PC10") and trusts the TSC
  frequency from CPUID leaf 0x15: a 38.4 MHz crystal times 96, 3686.4 MHz.
- The GOP framebuffer is at physical `0x10000800000`, inside the graphics BAR
  above 1 TiB. Its size, 2880 x 1800 x 4 bytes, is not a whole number of pages.
- The power button is a control-method device (`PNP0C0C`, `PWRB`), with an
  Intel HID 5-button array (`INTC10CC`) beside it.
- Both xHCI controllers place their extended capability list 32 KiB into the
  register BAR and offer MSI (8 messages) but no MSI-X.

## Pyxis status

Boots from the owner's installed USB stick. As of 2026-10-14:

| Area | Status |
| --- | --- |
| Boot, x2APIC, 16 CPUs | Works: Limine 12.9.3, [x2APIC](../../kernel/smp.md) and CPUID 0x15 timers |
| Display | Firmware framebuffer, 2880x1800, 8-bit XRGB |
| Keyboard | Works over PS/2 |
| Touchpad | Not supported: ELAN I2C-HID on DesignWare I2C |
| USB | Works with the xHCI capability window and MSI fallback (#848, in review) |
| NVMe | Refused: the driver accepts only NVMe 1.3 and 1.4 (in progress) |
| Power button | Ignored: only the ACPI fixed event is handled (queued) |
| Audio | HD Audio with a Conexant SN6140 codec; not yet tried |
| Wi-Fi and Bluetooth | Realtek RTL8922AE and a Realtek USB Bluetooth radio; unsupported |
| Network | None built in; the Steam Deck dock's AX88179A needs a CDC-NCM driver ([task 9](../../wip/steam-deck.md#9-dock-ethernet)) |

## Bring-up, 2026-10-14

1. Limine panicked before Caelum: the kernel did not request x2APIC, which the
   firmware cannot disable. Fixed by always requesting x2APIC when the CPU
   supports it (#835).
2. A black screen with no output: Caelum's framebuffer check rounded the
   framebuffer up to whole pages and required Limine's reservation to cover the
   rounded range. Fixed by checking the exact bytes and, separately, that no
   allocatable RAM shares the boundary pages (#847). Limine was updated to
   12.9.3 at the same time (#846).
3. A panic calibrating the local APIC timer: the 8254 PIT appears not to count.
   The PIT path was removed; Intel uses CPUID 0x15 for the TSC and the APIC timer,
   other machines measure once against the HPET (#847).
4. The rescue space only: both xHCI controllers were refused because their
   capability list lies beyond the 4 KiB bootstrap window and they lack MSI-X
   (#848). With that fixed, the stick's pool mounts and the configured spaces
   start.

Each step was a hardware assumption that every earlier machine happened to
share; see [unchecked hardware assumptions](../../technical-debt.md#unchecked-hardware-assumptions).

## Network boot

Firmware PXE through the Steam Deck dock reaches DHCP, but TFTP stalls: the
firmware requests `blksize 1468` and `windowsize 4`, dnsmasq cannot refuse the
window size, and the dock drops the bursts. Boot from the USB stick until a
different TFTP server or the RTL8156 adapters are tried.
