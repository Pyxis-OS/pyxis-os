# ThinkPad RTL8111 hardware profile

Task 1 of the [RTL8111 milestone](../wip/thinkpad-rtl8111.md), recorded
2026-10-03. This identifies the built-in port and proposes preparation ordering;
Caelum has no RTL8111 driver yet. Full and partial MAC bytes are omitted.

## Owner's Fedora capture

The owner supplied `dmesg`, `ip -br link`, `ethtool -i enp5s0` and
`lspci -vvv -s 0000:05:00.0` from Fedora kernel
`6.19.10-300.fc44.x86_64` with r8169 bound to the built-in port.

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
That observation does not establish a Pyxis power-management policy.

The dock's `0000:02:00.0` reported `RTL8168ep/8111ep`, XID `502`, link down.
It is a separate variant; this capture does not qualify its preparation or I/O.

## Caelum-side confirmation

The agent built and inspected main `e30204d3498252e851a139b8ab2218b05db418f7`.
Dependency pins were fs `f95e5a3`, userspace `d15d782`, ports `a50ae5c` and
lwIP `a1aadb9`. The ordinary image build passed with GCC 16.2.0:

```sh
make -j16 image PYTHON=build/hpet-config-venv/bin/python3 \
  CROSS_COMPILE=/home/chronium/opt/pyxis-cross/bin/x86_64-unknown-pyxis-
QEMU_DISPLAY=none MEMORY=2G CPUS=4 ACCEL=kvm VIRTIO_NET=0 \
  OVMF_CODE=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  OVMF_VARS=/usr/share/edk2/ovmf/OVMF_VARS.fd \
  VFIO_PCI=0000:05:00.0 scripts/run-qemu.sh debug
gdb -q build/caelum.elf
```

This used installed QEMU 10.2.2, KVM on the ThinkPad host, unlimited memlock,
fresh copied OVMF variables, VirtIO RNG enabled, xHCI disabled and no VirtIO net,
block or filesystem device. It was not a nested-VM measurement or performance run.
The host function was already bound to vfio-pci; no host binding was changed.

GDB stopped the BSP at `boot_start_cpus`, after PCI discovery and before Caelum
started APs. The complete inventory contained seven functions. Its RTL record
was `00:03.0`, `10ec:8168`, revision `0x15`, with no owner. ECAM was read through
Caelum's existing mapping; register reads used QEMU's physical-memory monitor.
No inferior function calls, PCI configuration writes, BAR probes, NIC resets,
DMA activation or interrupt enablement were issued by the inspection.
QEMU/OVMF/VFIO may already have changed hardware state before kernel entry.

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

To reproduce the inspected reads, first locate the guest function with
`monitor info pci`; its address and BAR assignments may change. For this boot:

```gdb
set pagination off
target remote localhost:1234
thbreak boot_start_cpus
continue
monitor info pci
set $cfg = 0xfffffe8050018000
x/1hx $cfg + 4
x/6wx $cfg + 0x10
x/1hx $cfg + 0x44
x/1wx $cfg + 0x50
x/1wx $cfg + 0x70
x/1hx $cfg + 0x80
x/3wx $cfg + 0xb0
monitor xp /1wx 0x380000008040
monitor xp /1bx 0x380000008037
monitor xp /1hx 0x38000000803c
detach
quit
```

After inspection, boot resumed through four online CPUs, userspace startup and
the loopback/TCP worker. QEMU and GDB were stopped. This confirms ordinary boot,
not RTL Ethernet operation.

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
No Linux code or firmware has been imported.

## Proposed bounded preparation for task 2

Preparation remains BSP-owned before AP startup, with independent state per
controller, following [PCI ownership](pci.md) and [SMP ownership](../kernel/smp.md).
The following is a proposed implementation sequence, not measured reset behavior:

1. Reserve each candidate with `pci_reserve_device`, preserving firmware command
   state. Existing claim validation rejects enabled MSI/MSI-X. Obtain checked
   provisional register access, read XID, and leave unsupported variants untouched.
2. Mask device sources and use the H quiescence/OOB sequence, then reset. Linux's
   `rtl_hw_init_8168g`, `rtl_enable_rxdvgate` and `rtl_hw_reset` are the reference:
   gate receive traffic, wait for FIFO drain, stop RX/TX, leave OOB mode, complete
   the shared-FIFO handshake, then wait for reset completion. Give every polling
   operation a monotonic deadline and propagate failure, including waits whose
   result Linux's wrappers discard.
3. After confirmed quiescence/reset, complete the claim to disable bus mastering
   and INTx; disable I/O and memory decoding with readback. Use `pci_size_bars`
   and `pci_map_bar` for BAR2/BAR4, retaining exclusive ownership throughout.
4. Validate provisional accesses against the sized register window. Use existing
   MSI-X discovery/mapping for the confirmed BAR4 table/PBA and check region
   disjointness. Propose MSI-X entry zero on the BSP; delivery is qualified in
   task 4. Re-enable memory decoding while keeping bus mastering, RX/TX and
   interrupt delivery disabled. Initialize PHY under the agreed firmware/power
   policy; rings and activation belong to task 4.

The pre-size access is the concrete PCI gap: `pci_map_bar` already supports sized
BAR2, but `pci_map_bootstrap_bar0` cannot use this controller's I/O BAR0.
Propose generalizing that provisional helper to a checked caller-selected memory
BAR, preserving its existing xHCI semantics and bounded 4 KiB prefix. Do not
probe BAR size while firmware can still DMA, or duplicate ordinary BAR mapping.

Cancel an unchanged reservation after unsupported identity or read-only failure.
After hardware writes, unwind only after confirmed safe stop with message
interrupts and DMA disabled. If halt/reset is uncertain, retain ownership and
mappings until reboot, report that controller unavailable and continue boot.
A timeout does not establish that hardware has relinquished DMA ownership.

Before task 2 implementation, settle firmware inclusion/source pin/license and
packaging, native D0/wakeup and initially disabled memory-decode handling, and
ASPM/clock-request/PHY power policy. Existing provisional mapping requires memory
decoding enabled; this VFIO boot does not settle other native initial states.
These choices and successful native handoff remain unqualified.
