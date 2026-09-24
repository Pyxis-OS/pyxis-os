# PCI discovery

Caelum reads the firmware-configured PCI topology during BSP initialization,
before starting APs. The current implementation produces a serial inventory;
it does not bind drivers, activate devices or keep a device registry.

## Configuration access

The x86 adapter accepts one ACPI MCFG allocation for segment zero starting at
bus zero. It copies the aperture description while the boot firmware mappings
are available, then maps that range into the owned kernel root. The mapping is
supervisor-only, read-only, non-executable and uncached, with no retained HHDM
alias. Naturally aligned byte, word and dword reads address a function's 4 KiB
configuration page through ECAM.

The complete advertised bus range is mapped, although enumeration visits only
reachable buses. Q35's 256 MiB aperture requires 128 leaf page-table pages
(512 KiB); it does not allocate 256 MiB of RAM backing. The mapping lives at
`PCI_ECAM_BASE`, outside the VM allocation area, and is inherited by private
address spaces through the existing shared kernel mappings.

Missing MCFG, multiple allocations/tables, a nonzero segment or starting bus,
an unsupported address extent, or overlap with non-reserved boot memory disables
PCI discovery with a diagnostic. Existing ACPI checksum and table-access
failures retain their fatal firmware-validation behavior. There is no legacy
configuration-port fallback or ACPI AML interpreter.

## Inventory

Discovery starts at bus zero, checks the multifunction bit before probing
additional functions, and follows configured PCI bridges. Secondary buses must
advance beyond their primary bus and fit within both the parent bridge range
and the MCFG aperture. Repeated buses and overlapping sibling bridge ranges
are diagnosed. The work queue is bounded by PCI's 256 possible bus numbers;
there is no fixed device-count registry or recursive traversal stack.

Each function reports its address as `segment:bus:device.function`, identity,
class/subclass/programming interface, revision and header type. Bus and device
numbers are hexadecimal. Endpoint and PCI-bridge headers are supported.
The inventory decodes I/O, 32-bit memory and paired 64-bit memory BAR addresses,
including prefetchability. It walks conventional capability headers and, for
PCI Express functions, extended capability headers, detecting invalid pointers
and cycles. Capability bodies remain for their owning drivers to interpret.

Discovery never writes configuration registers, probes BAR sizes, enables bus
mastering, resets devices or changes bus numbers. A zero BAR is omitted because
a read alone cannot distinguish an unimplemented BAR from an unassigned one.
Expansion ROMs and bridge forwarding windows are not inventoried. Unsupported
headers, BAR types and malformed topology/capability chains mark the inventory
incomplete while other reachable functions are still inspected.

## Inspection and next step

An ordinary `make run` prints the inventory before SMP startup. QEMU's monitor
`info pci` provides an independent view of device addresses and assigned BARs.
`make debug` allows inspection at `pci_discover` before any device discovery
reads occur. ECAM page permissions can also be inspected through the recursive
page tables; no configuration writes are needed for this check.

Resource sizing, writable configuration access, BAR mapping and driver ownership
are the next [milestone task](wip/virtio-fs.md#focused-task-list). Interrupts,
VirtIO queues and host filesystem access are not implemented by discovery.
