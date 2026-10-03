# ThinkPad RTL8111 hardware profile

Hardware identified in task 1 of the [RTL8111 milestone](../wip/thinkpad-rtl8111.md),
recorded
2026-10-03. Caelum prepares the identified built-in controller with RX/TX, DMA
and delivery disabled; Ethernet I/O is pending. Full and partial MAC bytes are
omitted.

## Owner's Fedora capture

| Property | Observed value |
| --- | --- |
| Built-in host function | `0000:05:00.0`, `10ec:8168`, revision `0x15` |
| Subsystem | Lenovo `17aa:5081` |
| Chip / XID | `RTL8168h/8111h` / `541` |
| Linux firmware version | `rtl8168h-2_0.0.2 02/26/15` |
| Link | 1 Gbps, full duplex, RX/TX flow control |
| BAR0 | I/O, 256 bytes, assigned `0x2000` |
| BAR2 | 64-bit non-prefetchable memory, 4 KiB, assigned `0xfd504000` |
| BAR4 | 64-bit non-prefetchable memory, 16 KiB, assigned `0xfd500000` |
| MSI at `0x50` | Disabled, one message, 64-bit address, no per-vector masking |
| MSI-X at `0xb0` | Enabled, four entries; table BAR4 + `0x0000`, PBA BAR4 + `0x0800` |
| Power / PCIe | D0; ASPM L1, clock power management and L1.1/L1.2 enabled |

Linux reported that it could not disable ASPM because the OS lacked ASPM control.

The dock's `0000:02:00.0` reported `RTL8168ep/8111ep`, XID `502`, link down.
It is a separate variant; this capture does not qualify its preparation or I/O.

## Caelum-side identification before driver preparation

GDB stopped the BSP at `boot_start_cpus`, before AP startup, with the RTL function
unclaimed; ECAM and QEMU monitor `xp` reads issued no hardware writes.
Use `monitor info pci` to locate the BARs, then read TxConfig at BAR2 + `0x40`;
guest assignments may change. [PR #359](https://git.internal/PyxisOS/pyxis-os/pulls/359)
records the build, dependency pins, boot configuration and debugger procedure.

| Property | Observed guest value |
| --- | --- |
| BAR0 | I/O `0x6000`, 256 bytes |
| BAR2 | Memory `0x380000008000`, 4 KiB |
| BAR4 | Memory `0x380000004000`, 16 KiB |
| TxConfig, BAR2 + `0x40` | `0x57100f80`; `(value >> 20) & 0xfcf` gives `0x541` |
| ChipCmd, BAR2 + `0x37` | `0x00`: RX/TX enable and reset bits clear |
| Interrupt mask, BAR2 + `0x3c` | `0x0000` |
| PCI command | `0x0007`: I/O, memory and bus mastering enabled; INTx disable clear |
| PM at `0x40` | PMCSR `0x0008`, D0 |
| MSI at `0x50` | Control `0x0080`: disabled, one message, 64-bit address |
| MSI-X at `0xb0` | Control `0x0003`: disabled, four entries, function mask clear |
| MSI-X table / PBA descriptors | `0x00000004` / `0x00000804`: BAR4 + `0x0000` / `0x0800` |
| PCIe at `0x70` | Link control `0x0000` in the guest |

BAR extents came from QEMU `info pci` and matched the owner's Fedora sizes;
Caelum did not size them. Guest PCIe state is virtualized and does not prove
native ASPM has been disabled. RX/TX being clear in this boot does not prove
firmware DMA is quiescent on every boot; PCI bus mastering was still enabled.
Interrupt capability presence does not qualify delivery.

## Source interpretation

Linux v6.18's [chip table and probe](https://github.com/torvalds/linux/blob/v6.18/drivers/net/ethernet/realtek/r8169_main.c)
map XID `541` to `RTL_GIGA_MAC_VER_46` and firmware
`rtl_nic/rtl8168h-2.fw`. XID `502` maps to MAC version 51 with different
DASH/CMAC handling. Match TxConfig-derived identity before variant-specific
writes; the common PCI device ID and PCI revision are insufficient.

The H [PHY sequence](https://github.com/torvalds/linux/blob/v6.18/drivers/net/ethernet/realtek/r8169_phy_config.c#L793)
applies firmware before analog tuning and hardware-derived calibration.
The [firmware interpreter](https://github.com/torvalds/linux/blob/v6.18/drivers/net/ethernet/realtek/r8169_firmware.c)
executes register operations, rather than uploading an opaque DMA image.
Fedora's success with firmware does not qualify operation without it.
The implementation uses these pinned sources as hardware-programming references;
no Linux source or firmware is imported. The Linux reference files are GPL-2.0-only;
the original Caelum implementation follows the repository's MPL-2.0 licensing.
PHY wake/reset behavior also follows the v6.18
[Realtek PHY reference](https://github.com/torvalds/linux/blob/v6.18/drivers/net/phy/realtek/realtek_main.c)
and [PHY reset reference](https://github.com/torvalds/linux/blob/v6.18/drivers/net/phy/phy_device.c).

## Controller preparation

Preparation remains BSP-owned before AP startup, with independent state per
controller, following [PCI ownership](pci.md) and [SMP ownership](../kernel/smp.md).
`rtl8111_prepare` runs before AP startup. It enumerates retained `10ec:8168`
functions, allocating independent stable state for each candidate:

1. Reserve each candidate with `pci_reserve_device`, preserving firmware command
   state. Existing claim validation rejects enabled MSI/MSI-X. Obtain checked
   provisional BAR2 access and read XID. Unsupported variants receive no
   variant-specific writes; temporary PCI probe changes are restored before
   cancellation, under the accepted exception below.
2. Mask device sources and use the H quiescence/OOB sequence, then reset. Linux's
   `rtl_hw_init_8168g`, `rtl_enable_rxdvgate` and `rtl_hw_reset` are the reference:
   gate receive traffic, wait for FIFO drain, stop RX/TX, leave OOB mode, complete
   the shared-FIFO handshake, then wait for reset completion. Give every polling
   operation a monotonic deadline and propagate failure, including waits whose
   result Linux's wrappers discard.
3. After confirmed quiescence/reset, complete the claim to disable bus mastering
   and INTx; disable I/O and memory decoding with readback. Use `pci_size_bars`
   and `pci_map_bar` for sized resources, retaining exclusive ownership throughout.
4. Validate and retain the provisional register prefix against the sized BAR2.
   Use existing MSI-X discovery/mapping for the confirmed BAR4 table/PBA and
   check region disjointness. Keep MSI-X disabled and function-masked without routing a vector;
   routing and delivery belong to task 4. Re-enable memory decoding while keeping
   bus mastering, RX/TX and interrupt delivery disabled. Initialize PHY under the accepted firmware/power
   policy; rings and activation belong to task 4.

The generalized `pci_map_bootstrap_bar` provides the checked assigned memory-BAR
prefix needed before sizing; xHCI still selects BAR0. The NIC selects BAR2 and
retains that bounded 4 KiB mapping after size validation, avoiding a duplicate
register alias. BAR probing never runs while firmware can still DMA.

PHY preparation waits 20 ms after power-up and confirms any existing reset
before tuning. It applies H-family analog calibration without a firmware script,
then confirms a PHY reset and restarts autonegotiation, preserving speed/duplex
and pause advertisements. PFM, ALDPS, PLL power saving and EEE advertisement
are disabled. Link completion is not required: an unplugged cable is valid.
MAC, ERI and PHY polls propagate timeout/readback failures, with finite monotonic
budgets; PHY reset has a 600 ms budget. Packet filters, rings and activation
remain task 4 work.

Restore temporary PCI probe changes and cancel after unsupported identity or
preparation failure before variant-specific writes.
After hardware writes, unwind only after confirmed safe stop with message
interrupts and DMA disabled. If halt/reset is uncertain, retain ownership and
mappings until reboot, report that controller unavailable and continue boot.
A timeout does not establish that hardware has relinquished DMA ownership.

## Accepted task 2 choices

The owner accepted these defaults in PR #359; they are the current direction and
may be revised by the owner:

- **Firmware-free first.** Task 4 measures link and sustained traffic without
  `rtl8168h-2.fw`. Only if those measurements show it is needed, add the pinned
  linux-firmware file, its redistribution license and a small interpreter in a
  focused PR.
- **Power.** Disable ASPM and CLKREQ in the endpoint's PCIe Link Control during
  preparation; run the PHY at full power. Further power policy is deferred.
- **Native initial state.** If PMCSR is not D0, move to D0 and wait 10 ms;
  disable PME and wake. If memory decoding is off, enable it with bus mastering
  still off before provisional access. An inconsistent state, including bus
  mastering enabled with decoding disabled, leaves that controller unavailable
  while ordinary boot continues.
- **Identification exception.** The owner permitted temporary PCI wake/decode
  before reading XID only with bus mastering off. Restore the prior command and
  writable PMCSR fields if unsupported or probing fails; restoration failure
  retains ownership. Reject D3hot wake without `NoSoftRst` before writing, since
  a reset could discard firmware BAR assignments. PME/wake and variant-specific
  policy changes occur only after XID `541` is confirmed.

These choices do not establish successful firmware-free operation or native
handoff. Task 2 was explicitly authorized after PR #359 merged. Native
initial-state transitions and cold-start firmware-free traffic remain unqualified; task 4
measures I/O and task 5 includes owner-run native qualification.
