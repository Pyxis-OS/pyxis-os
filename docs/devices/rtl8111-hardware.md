# ThinkPad RTL8111 hardware profile

Hardware identified in task 1 of the [RTL8111 milestone](../wip/thinkpad-rtl8111.md),
recorded 2026-10-03. Caelum prepares the identified built-in controller with RX/TX, DMA
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
This DASH controller is on the motherboard even when undocked; the dock/adapter
provides its RJ45 jack.
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
   operation a monotonic deadline and propagate failure. If FIFO drain fails
   after confirmed supported-XID ownership, perform one recovery reset, require
   reset/RX/TX bits clear and repeat the gate/drain checks with fresh deadlines
   before continuing OOB setup. This handles residual running state after an
   active VFIO guest exits. A failed recovery retains ownership; it never enables
   DMA or lends new buffers, and never treats a timeout as quiescence. The
   original PCI command stays unchanged until the confirmed halt allows claim
   completion to disable bus mastering.
3. After confirmed quiescence/reset, complete the claim to disable bus mastering
   and INTx; disable I/O and memory decoding with readback. Use `pci_size_bars`
   and `pci_map_bar` for sized resources, retaining exclusive ownership throughout.
4. Validate and retain the provisional register prefix against the sized BAR2.
   Use existing MSI-X discovery/mapping for the confirmed BAR4 table/PBA and
   check region disjointness. Re-enable memory decoding while keeping bus mastering,
   RX/TX and interrupt delivery disabled. Initialize PHY under the accepted
   firmware/power policy, then prepare the I/O resources described below.

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
budgets; PHY reset has a 600 ms budget. Activation follows explicit binding.

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

The owner subsequently reported native UEFI/PXE preparation success in
[PR #362](https://git.internal/PyxisOS/pyxis-os/pulls/362): XID `541` reached the
prepared state and XID `502`, with DASH enabled, was identified and released
without variant-specific writes. This is owner-reported evidence of that
firmware handoff, not a measurement of every initial power/decode state.
VFIO link and initial traffic are measured in task 4; cold-start firmware-free
reliability and owner-run native I/O qualification remain task 5 work.


## Ethernet I/O

After supported-XID identification, capture the boot-loaded IDR MAC without
logging it. This is the identity observed at boot, not a guarantee that the
previous owner left the factory address unchanged. Retain supported candidates
and their known identity even if later preparation fails. Identified unsupported
XIDs are excluded from selection; failure to identify a candidate or allocate
its state makes the RTL inventory incomplete.

Boot preparation allocates independent RX/TX rings through `dma_buffer_allocate`:
32 descriptors and 32 fixed 2048-byte buffers per ring, 68 KiB each. The descriptor
bank occupies the first page; physical DMA addresses and CPU mappings remain
distinct. These capacities are implementation choices. Complete bounded H EPHY,
ERI FIFO/filter and MAC timer tuning, disable checksum/VLAN offloads, program
ring addresses high then low, and route MSI-X entry zero to BSP vector 40 under
function/entry masks. Other table entries stay masked. Unselected controllers
retain this storage with DMA, RX/TX and interrupt delivery disabled.

Only the BSP network worker activates the selected controller after BIND. Enable
bus mastering, then RX/TX, program TxConfig after TX enablement, and unmask
MSI-X/device sources after checking activation. Accept own-unicast and broadcast
frames, without multicast hash filtering. Link state comes from PHYstatus and
link-change interrupts; lack of carrier does not invalidate preparation.

Interrupt entry masks device sources, acknowledges observed W1C status and wakes
the worker. The worker drains at most one ring's worth of completions per pass,
yields when saturated and reenables sources after draining. Pending status stays
latched while masked, closing the completion/sleep race without periodic polling.
The mask includes RX/TX completion/error and link change for MAC46.

TX copies into the next CPU-owned buffer, zero-pads short frames to 60 bytes,
publishes metadata before OWN, and kicks the normal queue. OWN clearing permits
reuse; it does not establish delivery. Pending TX has a five-second completion
budget. RX observes OWN clear before acquiring payload bytes, validates the
fixed address, EOR, flags and byte bounds, strips the four-byte FCS, lends valid
bytes synchronously to Ethernet and reposts afterward. Error, oversized and
fragmented frames are dropped without accessing payload. The reference's
`RxMaxSize = 0x4000` is a permissive length filter; each descriptor's 2048-byte
capacity bounds DMA, with oversized packets split across descriptors.

Runtime failure masks delivery, disables bus mastering, requests reset and
checks completion with a finite deadline. Neither timeout nor reset frees or
reposts outstanding storage. Claims, rings and shared mappings remain until
reboot, and configuration cannot switch or fall back to another controller.
A response that stops TX also stops the enclosing RX service before reposting.

Register and descriptor facts follow the pinned
[Linux v6.18 MAC46 start sequence](https://github.com/torvalds/linux/blob/v6.18/drivers/net/ethernet/realtek/r8169_main.c#L3371-L3430),
[common start](https://github.com/torvalds/linux/blob/v6.18/drivers/net/ethernet/realtek/r8169_main.c#L3898-L3962)
and [completion handling](https://github.com/torvalds/linux/blob/v6.18/drivers/net/ethernet/realtek/r8169_main.c#L4534-L4678).
The public [RTL8111B/8168B register datasheet](https://people.freebsd.org/~wpaul/RealTek/RTL8111B_8168B_Registers_DataSheet_1.0.pdf)
corroborates descriptor capacity/chaining and TX-enable ordering for the older B
variant; H behavior is checked against the pinned driver and physical VFIO boot.
No Linux code or firmware blob is imported. Undocumented tuning values remain
variant programming data rather than new architectural contracts.
