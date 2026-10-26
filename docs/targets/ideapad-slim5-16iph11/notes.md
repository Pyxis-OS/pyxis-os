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

The [coexistence task-3 scope](../../wip/nvme-coexistence.md#3-installer-coexistence-mode)
uses vela as a second target. The inventory records Fedora shim firmware paths,
not Fedora-through-Limine qualification or ESP contents. Exact GPT free extents
and FAT geometry still need read-only capture; no native writes are assigned.

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

Boots from the owner's installed USB stick. Status as of 2026-10-20; the
2026-10-14 bring-up observations are kept below as historical evidence:

| Area | Status |
| --- | --- |
| Boot, x2APIC, 16 CPUs | Works: Limine 12.9.3, [x2APIC](../../kernel/smp.md) and CPUID 0x15 timers |
| Display | Firmware framebuffer, 2880x1800, 8-bit XRGB |
| Keyboard | Works over PS/2 |
| Touchpad | Works: ELAN precision touchpad over HID-over-I2C on DesignWare I2C, polled; native move, click, drag and two-finger scroll qualified 2026-10-16. GPIO interrupt delivery is later work ([milestone](../../wip/vela-i2c-touchpad.md)) |
| USB | Works with the xHCI capability window and MSI fallback (#848) |
| NVMe | [#853](https://git.internal/PyxisOS/pyxis-os/pulls/853) admits newer controllers by the NVM contract; owner read-only namespace/GPT check passed, writes remain disabled |
| Power button | Works through the `INTC10CC` power event: a held press runs orderly power-off, native qualification 2026-10-14; short taps are ignored by the firmware ([ACPI](../../kernel/acpi.md#power-off-and-restart)) |
| Audio | Plays through the Conexant SN6140 on HD Audio (#881); Chocolate Quake has sound |
| Wi-Fi and Bluetooth | Realtek RTL8922AE and a Realtek USB Bluetooth radio; unsupported |
| Network | None built in; the Steam Deck dock's AX88179A is covered by the [USB CDC-NCM driver](../../devices/usb-cdc-ncm.md), which has not been qualified on vela |

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
share; see [unchecked hardware assumptions](https://git.internal/PyxisOS/pyxis-os/issues/1335).

## USB startup race, 2026-10-15

Owner native evidence on main `2cadd127`: the SanDisk stick was missing on both
right-side USB-A ports while both controllers reported complete, so installed
boot entered rescue. A trace build instead found `0781:55a9` on `0:14.0` root
port 11 (USB 3.2, speed-id 4), completed BOT with 240,328,704 blocks and reached
the configured `home://` prompt. Extra console output changed timing.

The never-merged info-level diagnostic `ffb3675d` buffered PORTSC/TSC records,
printing only after both controllers' snapshot/+100/+500/+1000 ms samples.
Owner photo/OCR: root 11 had CCS=0, PLS=RxDetect at about 8 ms relative to the
snapshot base, so it was skipped; by +100 ms it had CCS=1, PED=1, PLS=U0 and
speed-id 4, unchanged at +500/+1000 ms. Enumeration finished at about 121 ms
without a root device. Empty root 12 stayed in RxDetect. The raw `00001203`
decodes CSC=0/PRC=0; OCR's change-bit labels do not alter the CCS transition.

This exposed another hidden hardware assumption: HCRST/RUN was treated as proof
that attached links had settled. The accepted fix is a 100 ms minimum, extended
for active training/reset up to 400 ms, before the one boot snapshot; see
[the USB contract](../../devices/usb-enumeration.md#root-port-startup-boundary).
The cap is policy, not a specification guarantee for every retry/tolerance.
Native qualification of the fix is pending; the diagnostic is evidence of the
race, not a passed fix check. No diagnostic sampling code is merged.

## Audio, 2026-10-15

The controller (`00:1f.3`, `8086:e328`) reports class `04/01`, not HD Audio's
`04/03`, and has no PCIe capability, so it needed an explicit
[quirk entry](../../devices/hda.md#admission-and-controller-quirks): Intel DEVC
No Snoop, TCSEL, Immediate Command transport and CGCTL handling, following
Linux's Panther Lake-H profile. Three owner runs on the stick's rescue entry
found two more assumptions that held on QEMU and the ThinkPad:

1. `8981a442`: initialization failed closed because the whole CGCTL dword did
   not read back after restoring MISCBDCGE. The bit never sticks on this
   controller (`before=0 written=40 after=0`); Linux writes it without reading
   it back. It is now a logged warning (`0873ad23`).
2. `0873ad23` / `9e0d4d29`: audio became ready, but every playback start was
   refused: SDnCTL read back with the interrupt enables set and RUN clear.
   `9e0d4d29` made the refusal fail only that playback; the engine stayed
   available across three attempts.
3. `18511fa1`: RUN is polled for up to 1 ms; it read back after about 5.2 µs,
   and both playbacks started. The same build also wrote the stream tag as a
   dword, so the run does not prove which change mattered; the late RUN latch
   matches both logs and Intel's own driver, which polls for it.

See the [vela section](../../devices/hda.md#vela-intel-panther-lake-h-and-conexant-sn6140)
for the evidence. The owner then played Chocolate Quake with sound through the
speakers ("as expected, not great", as under Linux), so real SDL audio works
natively. Comparing the headphones' V-shaped sound under Linux needs real music
and stays open until a music player exists.

## Network boot

Firmware PXE through the Steam Deck dock reaches DHCP, but TFTP stalls: the
firmware requests `blksize 1468` and `windowsize 4`, dnsmasq cannot refuse the
window size, and the dock drops the bursts. Boot from the USB stick until a
different TFTP server or the RTL8156 adapters are tried.
