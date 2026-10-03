# USB enumeration and control transfers

Caelum addresses boot-present root devices and supported USB 2 hub descendants on every prepared xHCI
controller and publishes an immutable read-only boot snapshot for native
[lsusb](../userland/lsusb.md). It does not configure a storage transport or expose
USB block access. [The installation milestone](../wip/usb-installation.md#b-native-read-only-usb-storage)
tracks class/media work. Physical hardware is unqualified.

## Boundaries and preparation

`kernel/usb/xhci.c` owns controller commands, contexts, endpoint rings, DMA and
transfer-event interpretation. `core.c` owns USB requests, descriptor traversal,
inventory completeness and immutable observation publication. There is no BOT
matcher or class-transfer path in the inventory slice. No class is selected
from a vendor ID.
Classes other than supported hubs remain unbound. Their standard
descriptor structure is checked without configuring endpoints or interpreting
class reports. [USB 2 hubs](usb-hubs.md) are configured for boot traversal;
unsupported hubs or uninspected descendants make inventory partial.

`usb_prepare()` allocates retained root/descendant discovery records, one reusable
scratch descriptor buffer and a bounded interface-record arena per controller
before AP startup. xHCI prepares input/output contexts,
an EP0 ring and control-data storage for every advertised root port at the same
stage, plus a bounded descendant pool in one retained DMA arena. The runtime worker allocates or maps nothing.
Resource exhaustion fails preparation or discovery explicitly; it cannot silently
skip a configuration or connected port while claiming complete observation.

The private interfaces in `host.h` run on the current BSP controller worker.
This placement follows the scheduler/VM ownership contract; it is not a permanent
USB architecture requirement. Each controller and its worker have separate
discovery state and scratch storage;
waits can interleave without lending one controller's buffer to another.

## Descriptor inspection

Port setup and enumeration share one absolute thirty-second deadline. Each
controller command and control request also has a five-second limit bounded by
that deadline. Current choices live in `settings.h`, independently of RAM, image
size and descriptor values. The descriptor/control budget is initially 4 KiB.

A device whose controller speed mapping is unclassified remains an unidentified,
incomplete observation. Its boot detail says `unsupported device speed` before
any Address Device command or descriptor request is issued; this is a support
limit rather than a failed descriptor transfer.

Address Device publishes the device's output-context address through DCBAA and
lets xHCI assign the USB address. The core never sends SET_ADDRESS. EP0 starts
with the speed-defined packet size: low/full speed 8, high speed 64, SuperSpeed
512 bytes. The first eight device-descriptor bytes are checked before reading
the full descriptor. Full-speed devices may require Evaluate Context to update
EP0; SuperSpeed's wire value 9 means 512 bytes.

Every advertised configuration is inspected within the controller startup deadline.
Checks include descriptor lengths and totals, stable repeated headers, distinct
configuration values, interface/alternate identities, default alternates,
endpoint counts and addresses, speed-dependent packet/interval fields and
SuperSpeed companion structure. Interface numbering follows USB's consecutive
zero-based numbering rule. Class/vendor descriptors retain opaque contents but
must have valid traversal lengths. Validated interface records retain
configuration value, interface number, alternate,
class, subclass, protocol and endpoint count. A malformed configuration rolls back
its interface records while retaining checked device identity. The current interface
arena holds 512 records per controller; exceeding it gives partial inventory. This
is a resource choice in `settings.h`, not a hardware or database requirement.

Only supported hubs receive SET_CONFIGURATION and xHCI Slot hub metadata.
There is no SET_INTERFACE, non-control endpoint binding or BOT device selection. The unused matcher, bulk-endpoint setup
and bulk-ring allocations were removed. Class transfers belong with their first
consumer and an explicit resource-preparation policy. Selection across multiple
controllers needs its own storage contract; observation does not choose a disk.

## Snapshot publication

The pre-AP registry retains all discovered PCI USB host functions. Unsupported
host interfaces and failed xHCI preparation remain controller records. Every
connected startup root port observed by a running controller is retained, including
unidentified ports when inspection fails. No zero VID/PID is presented as a checked
identity unless IDENTIFIED is set. Unreadable descriptors, exceeded budgets,
unsupported speeds and uninspected hub descendants make the aggregate observation incomplete.

Each controller finishes within its startup deadline. Only after all entries finish
does the BSP compute global controller/device/interface indices and publish the
snapshot with release ordering. Readers on any CPU acquire that publication before
copying immutable records. Until then, the root query reports initializing and zero
counts. Publication does not grant transfer/reset authority, and later runtime
removal or controller failure does not alter the snapshot. See
[system information](../interfaces/system-information.md#usb-inventory).

## Control-request ownership

Each device admits one EP0 request with a monotonically increasing ticket
identity. Submission captures setup/outbound bytes into retained storage and
never stores a caller's read destination. A client owns the ticket until one
successful take or abandonment. A stale ticket cannot collect or alter a newer
request. A rejected collection preserves the original ticket.

Setup, optional Data and Status stages are separate TDs. Payloads, later stage
cycle bits and event identities are ready before Setup becomes visible; the
slot doorbell follows publication. Data Stage short-packet events record actual
bytes and do not complete the request. Only successful Status, after prior events
on interrupter zero, retires the sequence for ordinary reuse. Collection copies
only actual successful inbound bytes. Failure leaves the destination untouched.

Wait timeout does not consume or cancel the request. Abandonment removes client
ownership while active hardware work retains its ring span/data buffer; successful
terminal completion can later retire it. A request deadline, early transfer error
or unexpected removal during active work stops the controller and retains the
unresolved span. Halt, interrupt masking or disabling bus mastering alone cannot
justify recycling it. All runtime backing remains until reboot, including after
successful completion. Idle port removal still retires that device independently.

`inventory` and each controller's private discovery/per-port/request records are
available for GDB inspection. Only the copied observation records cross the ABI.

See the [inventory bring-up record](../development/usb-inventory-bringup.md) and
[earlier control-transfer record](../development/usb-enumeration-bringup.md) for measured
behavior and limits. Context and transfer semantics follow the
[Intel xHCI 1.2b specification](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf),
particularly §§4.3, 4.6, 4.9, 4.11, 6.2, 6.4 and 7.2. Unbound class recognition
uses the [USB-IF class-code list](https://www.usb.org/defined-class-codes).
