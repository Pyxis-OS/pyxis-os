# USB enumeration and control transfers

Caelum addresses boot-present devices directly attached to the selected xHCI
controller, reads checked descriptors and prepares one provisional SCSI/Bulk-Only
transport. It does not yet establish LUN/media support or expose USB block access.
[The installation milestone](../wip/usb-installation.md#b-native-read-only-usb-storage)
tracks those remaining layers. QEMU is the temporary target; physical hardware is
unqualified.

## Boundaries and preparation

`kernel/usb/xhci.c` owns controller commands, contexts, endpoint rings, DMA and
transfer-event interpretation. `core.c` owns USB requests, descriptor traversal,
configuration/alternate selection and inventory completeness. `bot.c` matches
checked interface/endpoint facts to the initial SCSI/BOT profile; it has no bulk
exchange or SCSI implementation yet. No class is selected from a vendor ID.
Known unrelated classes, including HID, remain unbound. Their standard descriptor
structure is checked without configuring their endpoints or interpreting class
reports. Unknown/vendor classes and hubs make discovery incomplete, because this
initial implementation cannot classify their possible storage inventory.

`usb_prepare()` allocates retained per-port discovery records and one reusable
scratch descriptor buffer before AP startup. xHCI prepares input/output contexts,
an EP0 ring, control-data storage and two non-control rings for every advertised
root port at the same stage. The runtime worker allocates or maps nothing.
Resource exhaustion fails preparation or discovery explicitly; it cannot silently
skip a configuration or port that might change disk selection.

The private interfaces in `host.h` run on the current BSP controller worker.
This placement follows the scheduler/VM ownership contract; it is not a permanent
USB architecture requirement. Storage, unrelated-device records and requests
retain separate state despite sharing that worker and scratch buffer.

## Discovery and selection

Port setup and enumeration share one absolute thirty-second deadline. Each
controller command and control request also has a five-second limit bounded by
that deadline. Current choices live in `settings.h`, independently of RAM, image
size and descriptor values. The descriptor/control budget is initially 4 KiB.

Address Device publishes the device's output-context address through DCBAA and
lets xHCI assign the USB address. The core never sends SET_ADDRESS. EP0 starts
with the speed-defined packet size: low/full speed 8, high speed 64, SuperSpeed
512 bytes. The first eight device-descriptor bytes are checked before reading
the full descriptor. Full-speed devices may require Evaluate Context to update
EP0; SuperSpeed's wire value 9 means 512 bytes.

Every advertised configuration is inspected, even after finding a candidate.
Checks include descriptor lengths and totals, stable repeated headers, distinct
configuration values, interface/alternate identities, default alternates,
endpoint counts and addresses, speed-dependent packet/interval fields and
SuperSpeed companion structure. Interface numbering follows USB's consecutive
zero-based numbering rule. Class/vendor descriptors retain opaque contents but
must have valid traversal lengths. The matcher supports one interface with class
08, SCSI subclass 06, BOT protocol 50, and exactly two opposite bulk endpoints.
Composite storage, other protocols, streams and unsupported shapes remain
positively unsupported when the full inventory is otherwise classifiable.

The first supported configuration/alternate in descriptor order represents one
physical-device candidate. Additional configurations/alternates on that same
device are not additional disks. Incomplete inventory prevents selection;
multiple physical-device candidates fail as ambiguous. With one candidate,
SET_CONFIGURATION uses its actual configuration value, and a nonzero alternate
requires SET_INTERFACE with its actual interface/alternate values. The host then
configures the actual endpoint DCIs, packet sizes and bursts. SuperSpeed bulk
requires 1024-byte packets and preserves the descriptor's burst, including 15.
A failed selected-device setup cannot select another device.

The resulting transport remains provisional: GET_MAX_LUN, LUN 0 qualification,
SCSI commands, geometry, bulk exchange and block/mount integration are subsequent
tasks. The configured endpoints do not imply a supported or readable disk.

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

`discovery` and the controller's per-port/request records are available for GDB
inspection. They are internal state, not a public device registry.

See the [bring-up record](../development/usb-enumeration-bringup.md) for measured
behavior and limits. Context and transfer semantics follow the
[Intel xHCI 1.2b specification](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf),
particularly §§4.3, 4.6, 4.9, 4.11, 6.2, 6.4 and 7.2. Unbound class recognition
uses the [USB-IF class-code list](https://www.usb.org/defined-class-codes).
