# PCI discovery and owned resources

Caelum reads the firmware-configured PCI topology during BSP initialization,
before starting APs. Discovery produces a serial inventory and retains heap-backed
device records for driver lookup. Drivers claim their selected functions through
a separate resource path; ordinary boot does not require an optional device.

## Configuration access

The x86 adapter accepts one ACPI MCFG allocation for segment zero starting at
bus zero. It copies the aperture description while the boot firmware mappings
are available, then maps that range into the owned kernel root. The mapping is
supervisor-only, read-only, non-executable and uncached, with no retained HHDM
alias. Naturally aligned byte, word and dword reads address a function's 4 KiB
configuration page through ECAM. Claiming a function makes only its page
writable; the rest of the aperture remains read-only.

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
Device records live for the boot; allocation failure is reported as an incomplete
inventory while enumeration continues. Each record keeps the address, vendor and
device ID, class, subclass, programming interface, revision and header type that
discovery read. The inventory state is unavailable when there is no supported
configuration access, and otherwise complete or incomplete. The records and
state are fixed before user tasks start. The
[system-information PCI queries](../interfaces/system-information.md#pci-inventory)
expose them read-only for [lspci](../userland/lspci.md).

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

## Driver-owned resources

Resource preparation runs on the BSP with interrupts disabled, before AP startup.
`pci_claim_device` gives one stable, caller-owned `pci_claim` exclusive ownership
of an endpoint. It validates the conventional capability chain, rejects enabled
MSI/MSI-X, disables bus mastering and disables INTx. Configuration writes require
that claim. Command changes use 16-bit writes so the adjacent status register's
write-one-to-clear bits are not accidentally acknowledged.

The class selector, like the vendor/device selector, returns a device only for
one match in a complete inventory. It distinguishes absence, ambiguity and
incomplete discovery. [xHCI](usb-xhci.md) uses class/subclass/interface matching.

MMIO controllers can first call `pci_reserve_device`, preserving firmware
decoding and bus-master state while reserving software/configuration ownership.
`pci_map_bootstrap_bar` maps a checked, assigned, page-aligned 4 KiB prefix of a
caller-selected memory BAR with memory decoding already enabled. It
rejects out-of-range indices, I/O or unsupported memory BARs, and the upper half
of a paired 64-bit BAR; xHCI selects BAR0. This provisional mapping does not
establish BAR size. Its consumer bounds every access and confirms firmware
handoff/halt before `pci_complete_claim` disables DMA/INTx. Cancellation of an
uncompleted reservation unmaps CPU access without command writes or restoring
bus mastering. Failed completion retains ownership until safe quiescence is
confirmed. VirtIO keeps its immediate-claim reset path.

For identity reads that need temporary wake or memory decoding,
`pci_begin_mmio_probe` snapshots the reserved function's command and optional
PMCSR before mapping. It rejects duplicate or truncated power capabilities.
An already-D0 function with memory decoding enabled remains untouched even if
firmware bus mastering is enabled. Any normalization requires bus mastering
off. D3hot wake without `NoSoftRst` is rejected before writing because an
internal reset can discard firmware BAR assignments. Otherwise wake to D0,
wait 10 ms with the monotonic clock, verify the power state,
then enable memory decoding with readback. PME enable remains unchanged, and
PME status is never acknowledged. Generic command writes still require a
completed claim; this is a restricted PCI-owned exception.

Before canceling after unsupported identity or an early probe failure, the
caller uses `pci_restore_mmio_probe` to restore changed command/PMCSR fields
without setting bus mastering or acknowledging PME status. A failed restoration
retains the claim and mappings until reboot. A successful restoration returns
the saved command and writable PMCSR fields. The caller must perform no other hardware writes before
this cancellation path. Confirmed controllers continue to driver handoff and
ordinary claim completion, retaining the normalized power/decoding state.

The driver must confirm quiescence and disable address decoding before
calling `pci_size_bars`. DMA/IRQ controllers retain their reset/halt requirements.
The QEMU-only Bochs device has neither a DMA engine nor an interrupt source;
retiring CPU pixel writers makes its BAR probe quiescent without destroying the
firmware scanout mode. Sizing handles firmware-assigned 32-bit and paired 64-bit
memory BARs. Each probe restores both original halves before returning, including
on malformed size/alignment or overlapping BARs. Unassigned resources and obsolete
memory BAR encodings are rejected. I/O BARs stay untouched and are not mapped.
There is no resource reassignment or bridge-window programming.

`pci_map_bar` checks the requested extent against the sized BAR, rejects overlap
with non-reserved boot memory and existing ECAM/APIC/HPET mappings, then reserves
kernel virtual pages. `vm_map_mmio` installs supervisor RW/NX/uncached leaves;
it neither allocates nor owns the physical device frames. The caller keeps each
`pci_mapping` at a stable address for the claim's lifetime. Capability regions
sharing a physical page use the same uncached memory type.

`pci_map_display_bar` is a separate boot-only WC aperture path. It accepts a
sized display BAR containing the matching boot-framebuffer reservation and
preserves its existing mapping only when that framebuffer starts at BAR base.
It validates all existing WC leaves, creates only a missing suffix, and rolls
that suffix back on failure. Existing PCI owners' UC mappings must not overlap;
the full permanent aperture then excludes later PCI and direct VM UC mappings.
General `pci_map_bar` reservation and UC checks remain intact. Successful display
leaves are retained until reboot, outside the claim's releasable mapping list;
BAR2 registers still use ordinary UC mapping.

Partial mapping failure removes installed leaves and releases the reservation.
`pci_release_device` unwinds boot preparation: it removes all owned mappings,
restores original address decoding with bus mastering and INTx kept disabled,
and returns the function's ECAM page to read-only. BAR assignments stay unchanged.
It never frees MMIO through the PMM. Empty page tables may remain for reuse under
the existing VM policy. This is not runtime teardown or hot-unplug: it must not
be used after starting DMA, and a device reset cannot be undone. A driver that
enabled MSI-X must disable it before releasing the claim or table mappings.
Any later bus-master-enable write permanently excludes boot release, even after
the bit is cleared again; runtime resources remain retained until reboot.

`kernel/pci/msix.c` discovers one disabled MSI-X capability, validates disjoint
sized table/PBA extents, maps them and prepares entry zero under function/entry
masks. It routes a static BSP vector, enables with readback and masks before
disabling. Owning drivers check overlap against their register regions before
mapping and retain responsibility for device-specific source/vector-index setup.

## Initial virtio-fs consumer

`virtio_fs_pci_prepare` selects the first discovered modern filesystem function
(`1af4:105a`). OVMF may leave bus mastering enabled, so preparation first uses the
VirtIO PCI configuration-access capability to write zero to device status and
wait for reset confirmation, with a one-second monotonic deadline. This reaches
the advertised common configuration byte before probing BAR sizes. The indirect
access selector fields are restored afterward.

Once reset is confirmed, preparation disables decoding, sizes BARs and validates
the common, notification, ISR and filesystem configuration extents, plus the
MSI-X table and pending-bit array. Required regions must fit assigned memory BARs
and not overlap each other. Reserved BAR numbers and unknown VirtIO capability
types are ignored; the first supported instance is selected. Larger capability
structures are accepted without using their unknown fields.

Common and ISR mappings cover the needed register prefixes. Notification and
device configuration mappings retain their advertised ranges for queue offsets
and optional negotiated fields. Memory decoding is then enabled, and status
must still read zero. The ISR is not read merely for diagnostics because that
would acknowledge interrupts. Preparation failures log a diagnostic and unwind
without preventing the existing OS from booting.

Shared VirtIO reset, capability validation, mappings and queue inspection live
in `kernel/virtio/transport.c`; generic MSI-X mechanics live in `kernel/pci/msix.c`.
Each driver keeps its own
`virtio_pci_transport` at a stable address for the claim's lifetime. Feature
policy, queue storage, activation and workers remain driver-owned. The
[network transport](networking.md#virtio-net-transport) uses these same
mechanisms with an independent claim and MSI-X vector.

### Feature negotiation and queue inspection

Preparation continues by setting `ACKNOWLEDGE`, then `DRIVER`. It reads the first
two device feature words and accepts only `VIRTIO_F_VERSION_1`; absence of that
bit is an error. Other offered features are left unselected, including packed
queues, indirect descriptors, event indices, platform DMA translation and
filesystem notifications. This is the modern split-queue baseline for the
current QEMU platform, not support for arbitrary hardware or IOMMU configurations.
The driver sets `FEATURES_OK` and reads status back before proceeding.

The filesystem tag and request-queue count are read between matching configuration
generation values, retrying for at most one second. The tag may occupy all 36 wire
bytes; the retained copy has a separate terminator. The request count must be
nonzero and leave room for the high-priority queue within the transport's queue
count. Only the high-priority queue and first request queue are inspected. Since
filesystem notifications are not negotiated, those are queues zero and one.

Each must be disabled and offer a nonzero power-of-two split-queue size. Its
notification offset, multiplied with widened arithmetic, must leave room for a
16-bit notification within the mapped notification region. Maximum sizes and
notification addresses are retained for later queue setup; this stage does not
write queue sizes, addresses, enable bits or notification registers.

Success leaves `ACKNOWLEDGE | DRIVER | FEATURES_OK` set and `DRIVER_OK` clear.
`filesystem.negotiated` records that boundary; it does not mean the device is
running. Bus mastering, INTx and MSI-X remain disabled. MSI-X entries are counted
and mapped at this point; masked routing setup follows. No queue storage or DMA
buffers exist yet.

Negotiation, inspection, routing or queue-setup failure disables MSI-X, marks `FAILED` and
resets through the mapped status register, with a one-second deadline. Confirmed
reset and MSI-X disable use the resource unwind above. If either cannot be
confirmed, the driver retains the claim and mappings until reboot rather than
exposing the failed device for reuse; DMA stays disabled and normal boot
continues. This is boot-only cleanup, not recovery of in-flight filesystem
requests.

### Masked MSI-X routing

The first virtio-fs device owns `APIC_VIRTIO_FS_VECTOR`, statically handled by
the IDT dispatcher. Its MSI-X entry zero carries a fixed, edge-triggered message
to the BSP's physical xAPIC ID. There is no interrupt remapping, dynamic vector
allocator or legacy INTx fallback.

Preparation enables MSI-X under its function mask, masks every table entry,
programs entry zero and reads the message fields back. It then maps configuration
changes, queue zero and queue one to that entry, checking each selection. VirtIO
vector registers contain table indices, not x86 vectors; the device may reject a
selection by returning `NO_VECTOR`. Unused queues retain their reset mappings.
Both the function and entry masks remain set after successful preparation.

The BSP handler records pending activity, detaches and wakes any worker waiter,
then the arch dispatcher sends APIC EOI. It does not read the VirtIO ISR byte:
that register is unused under MSI-X. The handler neither processes queues nor
allocates, logs or switches tasks.

The transport interrupt wait is for the sole BSP kernel worker with IF=1 and
no locks held. It disables interrupts around checking pending activity and
publishing its task wait, then parks through the ordinary event-wait API. BSP
affinity and IF=0 serialize the worker/IRQ handoff; a spinlock is unnecessary
for these two participants. Activity arriving before publication remains pending,
and the task wait remembers a wake before parking finishes. Return consumes the
activity flag and restores IF=1. The worker must recheck all relevant queue and
configuration state: one interrupt can cover several completions.

Queue storage is allocated and programmed before AP startup, while DMA and
interrupt delivery remain disabled. After task initialization the BSP worker
enables bus mastering, sets `DRIVER_OK`, unmasks entry zero and finally unmasks
the function, reading back each transition. Queue publication and completion use
explicit coherent-DMA barriers. The [transport and host setup](virtio-fs.md)
describes the real FUSE session exchange and worker lifetime.

Boot failure disables MSI-X before reset and resource release. Runtime failure
masks delivery, disables MSI-X and bus mastering, and requests reset with a
one-second deadline. It retains the claim, queue storage and mappings until
reboot, regardless of reset success, preserving shared kernel mappings. The
worker detaches its waiter before exiting. A mask cannot retract an interrupt
already sent to the APIC; the static handler state remains valid for that late
delivery. Runtime unmapping and reconnection are not implemented.

The reference is
[VirtIO 1.4](https://docs.oasis-open.org/virtio/virtio/v1.4/cs01/virtio-v1.4-cs01.pdf),
especially initialization (§3.1), PCI configuration (§4.1.4) and the filesystem
device (§5.11).

## Inspection and next step

An ordinary `make run` prints the inventory before SMP startup. QEMU's monitor
`info pci` provides an independent view of device addresses and assigned BARs.
`make debug` allows inspection at `pci_discover` before any device discovery
reads occur. ECAM page permissions can also be inspected through the recursive
page tables. Break at `boot_start_cpus` to inspect completed preparation before
APs start; use direct memory inspection rather than inferior function calls
under KVM (see [GDB](../development/gdb.md)).
Read MMIO registers individually at their documented byte, word or dword width.
A bulk structure read can combine neighboring registers into accesses the device
does not support, producing misleading values even with correct field offsets.

The [init-managed mount](virtio-fs.md#init-mount-and-delegation) exposes the
optional export as `host://` through directory/file capabilities with the
selected init's grants. The
[host setup](virtio-fs.md#start-the-host-service) documents the opt-in daemon and
socket used by `make run`/`make debug`. Ordinary archive-only boot needs neither.
