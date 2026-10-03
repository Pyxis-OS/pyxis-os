# ThinkPad RTL8111 driver

Status: **five-task outline approved, 2026-10-03.** Approval covers the work and
completion points in that outline, not every detail below. Implementation,
ownership and reference choices remain proposals pending owner confirmation;
specific owner decisions are recorded separately. Driver implementation and task 1
have not started. [NIC passthrough](../development/thinkpad-nic-passthrough.md)
is complete.

## Goal and machine configuration

Proposed goal: use the ThinkPad's built-in RJ45 controller for Pyxis networking,
first through VFIO in QEMU/KVM, then on a native boot. Completion means the existing remote
terminal is reachable through this port, with ordinary loopback and VirtIO
operation preserved.

The owner confirmed these settings for the **built-in port profile**:

| Setting | Value |
| --- | --- |
| IPv4 address | `192.168.0.50` |
| Prefix | `/24` (`255.255.255.0`) |
| Gateway | `192.168.0.1` |

These are machine configuration. Proposed packaging: apply them through the
existing userspace network configuration authority and keep the QEMU user-network
profile separate. Configuration/profile packaging belongs to the separately
versioned userspace repository; publish any dependency PR before updating the
parent pin, following [SDK and repository integration](../development/sdk-and-repositories.md).

## Controllers and selection

The built-in function is host `0000:05:00.0`, `10ec:8168`, PCI revision `0x15`.
The qualified guest assigned it `00:03.0`; driver selection must not depend on
that guest address. Dock Ethernet is another `10ec:8168` at host `02:00.0`, PCI
revision `0x0e`. Its different revision and shared IOMMU group are recorded in
[ThinkPad next steps](thinkpad-next-steps.md#2-ethernet-passthrough-to-qemu-then-a-driver).

**Owner decision: configuration chooses the interface.** The built-in port
profile binds its controller through a locally supplied MAC selector; the QEMU
user profile binds VirtIO. Discovering RTL8111 does not automatically switch away
from a configured VirtIO interface or its host-forwarded remote terminal.
Full and partial MAC bytes stay out of the repository and published captures.

Proposed controller model, pending confirmation:

- Keep driver state per controller, with explicit PCI, register, DMA and interrupt
  ownership. A global singleton is not the driver model.
- Enumerate all candidates. Diagnose unsupported variants individually; another
  NIC's presence must not prevent the supported built-in controller from working.
- Keep hardware support separate from the configuration binding above. Guest
  PCI addresses may differ from native addresses.
- Initially qualify the built-in controller. Supporting the dock adds its own
  hardware profile and qualification; it does not justify an exactly-one-NIC
  restriction.

The current stack has one external interface, `net0`, and direct VirtIO calls.
Proposed integration keeps per-controller state distinct from the selected
interface. Exposing multiple active interfaces also needs address, ARP and routing
ownership work; its scope remains a decision before implementation.

## Tasks

The five-task work/completion outline is approved. The ordering and task 1 capture
instructions below incorporate the requested PR review corrections; detailed
implementation proposals remain subject to confirmation.

- [ ] **1. Identify the hardware.** Begin with the owner's Fedora r8169
  `dmesg`/`ethtool`/`lspci` output, with MAC bytes removed. Record the chip name,
  TxConfig-derived MAC/XID, firmware reported by Linux, BARs and MSI/MSI-X
  capabilities. Then confirm the same XID and capabilities from Caelum before
  reset, driver DMA or interrupt enablement. Finish with a documented built-in
  hardware profile and proposed bounded preparation contract; PCI revision alone
  does not identify the MAC implementation.
- [ ] **2. Prepare the controller.** Implement ownership, quiescence, reset and
  PHY initialization with bounded waits and per-controller state. Account for
  native PXE firmware state as well as VFIO. Leave DMA and delivery disabled
  until worker activation. Finish with a known stopped/prepared state; missing
  or failed hardware leaves ordinary boot usable.
- [ ] **3. Integrate networking.** Replace direct VirtIO dependencies in Ethernet,
  ARP, IPv4 configuration/status and worker dispatch with a small driver-selection
  layer as a pure refactor, with VirtIO as its only implementation. Configuration
  chooses the interface. Finish with existing QEMU ping, UDP and TCP workloads
  preserving VirtIO behavior; capture matched runs before and after the refactor.
- [ ] **4. Implement Ethernet I/O.** Add owned RX/TX rings, coherent DMA ordering,
  validated completions and interrupts serviced through the existing BSP network
  worker. Connect RTL8111 to the selection layer from task 3. Finish with ARP and
  gateway ping through the built-in port profile and clear buffer ownership.
- [ ] **5. Qualify operation.** Exercise existing UDP/TCP tools, remote terminal,
  link changes and sustained traffic, then native boot. Finish with the built-in
  port working in VFIO and natively, including with the dock attached.

### Proposed task 1 owner capture

With the VFIO VM stopped, the owner can return the port to r8169, capture the
following output and restore the VFIO binding. Use Wi-Fi or dock Ethernet for
host connectivity. Replace `enp5s0` with the interface identified by `ip -br link`.

```sh
sudo driverctl unset-override 0000:05:00.0
sudo dmesg | rg -i 'r8169|rtl_nic'
ip -br link
sudo ethtool -i enp5s0
sudo lspci -vvv -s 0000:05:00.0
sudo driverctl set-override 0000:05:00.0 vfio-pci
```

Remove all MAC bytes before sharing or committing the capture. Linux's reported
firmware establishes what its driver used; firmware-free operation remains a
measurement rather than an assumption. These commands are task instructions,
not work performed for this planning PR.

## Proposed ownership and preparation constraints

The proposal follows the existing [PCI](../devices/pci.md),
[networking](../devices/networking.md), [SMP](../kernel/smp.md) and
[memory](../kernel/memory.md) contracts. Boot preparation
and allocation remain BSP-owned before AP startup; the network worker owns
protocol state and runtime ring service. Interrupt entry handles only the
necessary device acknowledgement/notification and wakes that worker; it does not
process packets, allocate or log.

Borrow valid RX bytes only while processing, then return the buffer to hardware.
TX copies into driver-owned storage; successful queueing does not mean delivery.
Failure and timeout do not return DMA ownership. Quiescence precedes boot failure
unwinding; runtime failures retain owned storage and shared mappings until reboot.
Exact ring sizes and initialization timings are implementation choices established
for the identified variant, not inherited VirtIO values or architectural limits.

The host reports I/O BAR0, 4 KiB memory BAR2 and 16 KiB memory BAR4.
`pci_size_bars()` sizes all memory BARs and skips I/O BARs; `pci_map_bar()` maps
any sized BAR, including BAR2 and BAR4. The proposal reuses those existing helpers.
Only xHCI's early `pci_map_bootstrap_bar0()` path is BAR0-specific; BAR2 does not
require a new mapping helper merely because its index differs. Safe claim,
quiescence, sizing and mapping ordering remains to be established for this NIC.
Task 1 confirms the interrupt capability and table BAR; the existing MSI-X helper
can map its table/PBA when applicable. The interrupt-mode choice remains proposed
until that capture is available.

After identification, settle any required firmware source, pin, packaging and
license, plus explicit PHY/power-management settings, reset/stop ordering and
bounded failure handling. Keep only the confirmed variant's necessary setup;
the plan does not choose a firmware-free path without qualification.

## Proposed validation and boundaries

Use ordinary builds, interactive VFIO boots and debugger inspection. Before
stack changes, capture a VirtIO baseline with existing ping/UDP/TCP workloads;
repeat matched runs afterward, recording revisions, configurations, commands,
samples and variation. Qualify real-NIC behavior separately rather than treating
different transports as a matched performance comparison. Native qualification
is run by the owner. No new benchmark or boot automation is assigned.

DHCP, broadcast discovery, reverse remote terminal, Wi-Fi, offloads, jumbo frames
and hot-plug remain separate work. The dock's profile and multiple active
interface routing remain explicit follow-ups/decisions, rather than accidental
restrictions in the built-in controller implementation.

## Proposed hardware references

Use pinned primary sources as hardware references, with provenance and licensing
recorded for any imported material. Linux v6.18 identifies the MAC implementation
from TxConfig/XID and contains variant-specific PHY/firmware setup:

- [r8169 controller identification and initialization](https://github.com/torvalds/linux/blob/v6.18/drivers/net/ethernet/realtek/r8169_main.c)
- [r8169 PHY configuration](https://github.com/torvalds/linux/blob/v6.18/drivers/net/ethernet/realtek/r8169_phy_config.c)
- [r8169 firmware format and interpreter](https://github.com/torvalds/linux/blob/v6.18/drivers/net/ethernet/realtek/r8169_firmware.c)

The exact MAC/XID, usable interrupt mode and firmware requirement remain unmeasured
in this plan until task 1's owner capture and Caelum confirmation. PCI revision
`0x15` is an observed identity, not proof of a particular Realtek MAC implementation.
