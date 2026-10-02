# Initial xHCI controller

Caelum prepares a unique PCI xHCI function and reserves hardware slots for
boot-present, directly attached root-port devices. It does not yet address USB
devices, fetch descriptors, bind classes or expose a USB block backend. The
archive-backed shell and existing VirtIO block behavior remain available.
[Phase B](../wip/usb-installation.md#b-native-read-only-usb-storage) tracks those
remaining layers. QEMU is the temporary target; physical hardware is unqualified.

## Preparation and activation

`xhci_prepare()` runs on the BSP with interrupts disabled before AP startup.
Class/subclass/programming-interface matching preserves absent, ambiguous and
incomplete PCI inventory outcomes. The initial profile requires PCI xHCI 1.x,
a firmware-assigned, page-aligned memory BAR0 with decoding already enabled,
64-bit DMA, 4 KiB pages and disabled firmware MSI/MSI-X. It records the advertised
32- or 64-byte context stride; device contexts are a later task.

A staged [PCI claim](pci.md#driver-owned-resources) leaves firmware command state
intact for provisional access to BAR0's first 4 KiB. Every interpreted extended
capability body must fit that prefix and avoid defined register regions. Supported
Protocol capabilities associate root ports with protocol versions and Slot Type;
no QEMU port numbering or vendor identity selects a device.

If legacy ownership is present, request OS ownership, wait for BIOS release and
then disable legacy SMI sources. Wait for controller readiness, halt before
clearing bus mastering, reset, disable decoding and probe BAR sizes. Validate the
bootstrap extent and sized register/MSI-X regions before mapping them. Initial
handoff, halt and reset waits each have a one-second deadline. Unsafe failure
retains the claim; safe boot failure unwinds unpublished resources.

Preparation allocates port records, DCBAA, scratchpads when advertised, one
command-ring page, one event-ring page and an ERST allocation. All backing and
mappings exist before AP startup. CPU addresses and device physical addresses
remain distinct. Rings are 64-byte aligned and remain within a 64 KiB segment
boundary. The command ring ends with a Toggle Cycle Link TRB; the event ring uses
one ERST entry. The initial choice is 256 TRBs per ring, derived from the supported
4 KiB page and hardware TRB layout, with one command admitted at a time.

After scheduler setup, `xhci_start()` creates one BSP worker. It enables bus
mastering before programming rings: a halted controller may fetch the ERST as
soon as its base is written. It then enables interrupter zero, MSI-X entry zero
on static vector 39 and Run/Stop. IRQ work only acknowledges observed activity,
records notification and detaches/wakes the worker wait. The worker parses and
validates cycle-owned events, matches command completions to the admitted physical
TRB, advances dequeue and releases EHB after consumption. Empty polls do not
rewrite an unchanged dequeue pointer. Payload stores precede cycle publication
and the doorbell. Commands share an absolute five-second deadline.

## Root-port and failure ownership

The worker powers controllable ports, allows 20 ms after a power change and takes
one startup connection snapshot. Recognized USB 2 ports are reset when required;
USB 3 ports use their enabled link state. Setup shares a thirty-second deadline.
Connected unknown protocol versions remain explicitly unsupported. This snapshot
has only been qualified in QEMU: a physical USB 3 link can still be initializing
after controller reset. See [qualification limits](../technical-debt.md#xhci-hardware-profile-and-runtime-retention).

For each supported enabled port, Enable Slot uses its advertised Slot Type and
retains the checked returned slot ID with that port. No input/output device
context or Address Device command is needed in the Enabled state; DCBAA device
entries remain zero. These reservations exercise real commands and interrupts
without startup self-tests or a storage assumption. An unrelated mouse receives
its own controller reservation; descriptor/class binding remains pending.

A connection change after the snapshot retires that startup candidate. Loss of
an enabled reserved port runs Disable Slot after prior command completion, then
keeps that port retired until reboot. It does not stop unrelated ports. Later
insertion cannot inherit a reservation. Writes acknowledge only observed PORTSC
changes and preserve neutral power/wake fields; they never echo PED or reset bits.

Command timeout, invalid completion/event identity, controller error, failed
port setup or failed slot retirement stops the controller for this boot. The
worker attempts bounded halt, masks delivery and disables bus mastering. All
runtime claims, mappings, command state and DMA allocations remain until reboot,
including when halt succeeds. Masking interrupts or disabling bus mastering alone
does not establish returned DMA ownership. The shared VM contract supplies no
runtime unmapping or allocation path here.

The worker uses a ten-millisecond notification/health polling interval today.
Deadlines and polling/ring budgets are implementation choices in
`kernel/usb/xhci.c`, separate from image configuration. There are no unit-test
constants for RAM, disk/ESP sizes, port numbering or controller capacities.

The [bring-up record](../development/xhci-bringup.md) describes manual validation
and its limits. Register and Enabled-slot behavior follow the
[Intel xHCI 1.2b specification](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf),
particularly §§4.2, 4.5.3, 4.22, 5 and 6.
