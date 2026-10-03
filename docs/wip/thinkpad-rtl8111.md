# ThinkPad RTL8111 driver

Status: **accepted plan, 2026-10-03.** Driver implementation has not started.
[NIC passthrough](../development/thinkpad-nic-passthrough.md) is complete. The
first task identifies the actual controller implementation before choosing its
initialization sequence. Work proceeds through focused tasks and PRs; later
hardware-dependent details are settled from that identification.

## Goal and machine configuration

Use the ThinkPad's built-in RJ45 controller for Pyxis networking, first through
VFIO in QEMU/KVM, then on a native boot. Completion means the existing remote
terminal is reachable through this port, with ordinary loopback and VirtIO
operation preserved.

The owner's B0-22 static profile is:

| Setting | Value |
| --- | --- |
| IPv4 address | `192.168.0.50` |
| Prefix | `/24` (`255.255.255.0`) |
| Gateway | `192.168.0.1` |

These are machine configuration, not driver constants. Apply them through the
existing userspace network configuration authority. Keep the QEMU user-network
profile separate. Configuration/profile packaging belongs to the separately
versioned userspace repository; publish any dependency PR before updating the
parent pin, following [SDK and repository integration](../development/sdk-and-repositories.md).

## Controllers and selection

The built-in function is host `0000:05:00.0`, `10ec:8168`, PCI revision `0x15`.
The qualified guest assigned it `00:03.0`; driver selection must not depend on
that guest address. Dock Ethernet is another `10ec:8168` at host `02:00.0`, PCI
revision `0x0e`. Its different revision and shared IOMMU group are recorded in
[ThinkPad next steps](thinkpad-next-steps.md#2-ethernet-passthrough-to-qemu-then-a-driver).

Accepted direction:

- Keep driver state per controller, with explicit PCI, register, DMA and interrupt
  ownership. A global singleton is not the driver model.
- Enumerate all candidates. Diagnose unsupported variants individually; another
  NIC's presence must not prevent the supported built-in controller from working.
- Keep hardware support separate from interface selection. Prefer the supported
  RTL8111 transport over VirtIO when both are present. Configuration binds the
  intended port through stable identity such as its MAC, rather than a remapped
  guest PCI address.
- Initially qualify the built-in controller. Supporting the dock adds its own
  hardware profile and qualification; it does not justify an exactly-one-NIC
  restriction.

The current stack has one external interface, `net0`, and direct VirtIO calls.
That is an existing limitation, not a new driver contract. The integration task
must keep per-controller state distinct from the selected interface. Exposing
multiple active interfaces also needs address, ARP and routing ownership work;
its scope is a separate decision before that task's implementation.

## Tasks

- [ ] **1. Identify the hardware.** Use existing PCI inventory, QEMU monitor and
  GDB inspection to record BARs, MSI/MSI-X capabilities, permanent MAC and the
  TxConfig-derived MAC/XID. Perform identification before reset, driver DMA or
  interrupt enablement. Establish the exact initialization/PHY sequence and
  firmware requirements. Finish with a documented built-in hardware profile and
  a bounded preparation contract; do not infer the MAC implementation from PCI
  revision alone.
- [ ] **2. Prepare the controller.** Implement ownership, quiescence, reset and
  PHY initialization with bounded waits and per-controller state. Account for
  native PXE firmware state as well as VFIO. Leave DMA and delivery disabled
  until worker activation. Finish with a known stopped/prepared state; missing
  or failed hardware leaves ordinary boot usable.
- [ ] **3. Implement Ethernet I/O.** Add owned RX/TX rings, coherent DMA ordering,
  validated completions and interrupts serviced through the existing BSP network
  worker. Finish with real transmitted/received frames and clear buffer ownership.
- [ ] **4. Integrate networking.** Replace direct VirtIO dependencies in Ethernet,
  ARP, IPv4 configuration/status and worker dispatch with a small driver-selection
  layer. Bind configuration to the selected controller, applying the accepted
  preference above. Finish with ARP and gateway ping using the B0-22 profile.
- [ ] **5. Qualify operation.** Exercise existing UDP/TCP tools, remote terminal,
  link changes and sustained traffic, then native boot. Finish with the built-in
  port working in VFIO and natively, including with the dock attached.

## Ownership and preparation constraints

Follow the existing [PCI](../devices/pci.md), [networking](../devices/networking.md),
[SMP](../kernel/smp.md) and [memory](../kernel/memory.md) contracts. Boot preparation
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

The host reports I/O BAR0, 4 KiB memory BAR2 and 16 KiB memory BAR4. The existing
bootstrap PCI helper maps BAR0 only. Task 1 must resolve the required memory BAR
and safe access ordering; preparation may need a focused extension for that BAR.
Select one verified interrupt mode after capability inspection, reusing MSI-X
support when applicable. Do not assume the card exposes MSI-X from its PCI ID.

After identification, settle any required firmware source, pin, packaging and
license, plus explicit PHY/power-management settings, reset/stop ordering and
bounded failure handling. Keep only the confirmed variant's necessary setup;
the plan does not choose a firmware-free path without qualification.

## Validation and boundaries

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

## Hardware references

Use pinned primary sources as hardware references, with provenance and licensing
recorded for any imported material. Linux v6.18 identifies the MAC implementation
from TxConfig/XID and contains variant-specific PHY/firmware setup:

- [r8169 controller identification and initialization](https://github.com/torvalds/linux/blob/v6.18/drivers/net/ethernet/realtek/r8169_main.c)
- [r8169 PHY configuration](https://github.com/torvalds/linux/blob/v6.18/drivers/net/ethernet/realtek/r8169_phy_config.c)
- [r8169 firmware format and interpreter](https://github.com/torvalds/linux/blob/v6.18/drivers/net/ethernet/realtek/r8169_firmware.c)

The exact MAC/XID, usable interrupt mode and firmware requirement remain unmeasured
for this card until task 1. PCI revision `0x15` is an observed identity, not proof
of a particular Realtek MAC implementation.
