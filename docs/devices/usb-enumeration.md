# USB enumeration and control transfers

Caelum addresses boot-present root devices and supported hub descendants on every prepared xHCI
controller and publishes an immutable read-only boot snapshot for native
[lsusb](../userland/lsusb.md). Supported storage also receives a
[BOT/SCSI probe](usb-storage.md) and kernel block registration.
[USB installation](usb-installation.md) summarizes the storage path end to end.
[Owner-reported ThinkPad observations](../targets/t14-gen1-amd/usb-bringup.md)
cover specific native profiles; broader hardware and recovery qualification remain pending.

## Boundaries and preparation

`kernel/usb/xhci.c` owns controller commands, contexts, endpoint rings, DMA and
transfer-event interpretation. `core.c` owns USB requests, descriptor traversal,
inventory completeness and immutable observation publication. `bot.c` owns
storage matching, SCSI commands and BOT recovery. `hid.c` selects checked boot
keyboard/mouse interfaces and interprets their fixed reports; the AX200 binder
supplies its private HCI profile. Supported HID can bind one keyboard and one
mouse per device, including distinct endpoints in a composite device. Other
classes remain unbound observations. [USB hubs](usb-hubs.md) are configured for
boot traversal and bounded runtime topology recovery, with USB 2/USB 3
notification streams for supported hub and USB 2 HID admission;
unsupported hubs or uninspected descendants make inventory partial.

`usb_prepare()` allocates retained root/descendant discovery records, one reusable
scratch descriptor buffer and a bounded interface-record arena per controller
before AP startup. xHCI prepares input/output contexts,
an EP0 ring and control-data storage for every advertised root port at the same
stage, plus a bounded descendant pool in one retained DMA arena. Runtime topology
preparation adds fresh device/context/control slices and hub notification streams,
and the core retains separate runtime discovery records and scratch/interface
storage. A separate 32-claim HID budget per controller covers boot claims and runtime
attempts, including failed and retired generations. Storage and HCI pools remain
separate. The runtime worker allocates or maps nothing. Resource exhaustion fails
preparation or refuses admission explicitly; it cannot silently skip a boot
configuration or connected port while claiming complete observation.

The private interfaces in `host.h` run on the current BSP controller worker.
This placement follows the scheduler/VM ownership contract; it is not a permanent
USB architecture requirement. Each controller and its worker have separate
discovery state and scratch storage;
waits can interleave without lending one controller's buffer to another.

## Root-port startup boundary

After RUN and root-port power preparation, each owning worker waits at least
100 ms before its single boot-presence snapshot. It then extends the wait only
while a powered, supported root reports active PR (hot or warm reset), or a USB3
root reports Polling, Recovery or HotReset. The total settle cap is 400 ms.
Workers sleep between checks and drain their own events without acknowledging
PORTSC; the ordinary snapshot still owns change acknowledgment and generation
capture. The thirty-second enumeration deadline includes settling. One trace
summary per controller records elapsed time and the ending reason; plain builds
add no log chatter.

PRC/WRC/CSC are latched changes, not work in progress; WPR always reads zero.
RxDetect can mean an empty port, USB2 Polling requires the existing software
reset, and Resume requires software action, so these do not extend settling.
The register meanings are in [xHCI 1.2b](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf),
sections 4.15.2.1, 4.19.2, 4.19.5.1 and 5.4.8. The accepted 400 ms cap is a
boot-latency policy near the nominal 360 ms Polling.LFPS timeout; it is not a
worst-case HCRST-to-U0 guarantee. Timer tolerance alone permits roughly 540 ms
for that substate, with detection and retries adding time ([USB-IF timing table
and polling-retry test](https://www.usb.org/sites/default/files/USB%203%202%20Link%20Layer%20Test%20Specification%202024%2003%2003.pdf),
Table 4-1 and TD7.40). The minimum and cap are tunable settings, not ABI.

**Hidden hardware assumption removed:** HCRST/RUN completion does not imply
that every already attached SuperSpeed device has finished training. On
[vela](../targets/ideapad-slim5-16iph11/notes.md#usb-startup-race-2026-10-15),
a device in RxDetect at the old snapshot reached U0 before +100 ms but was
permanently omitted. The owner accepted this bounded adaptive settle on
2026-10-15. Ports connected at the eventual snapshot count as boot-present. A
link that begins later, or trains beyond the cap, can still be absent from this
immutable snapshot. Supported later connections use the separate bounded
runtime topology path below.

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
with the speed-defined packet size: low/full speed 8, high speed 64,
SuperSpeed and SuperSpeedPlus 512 bytes. The first eight device-descriptor bytes are checked before reading
the full descriptor. Full-speed devices may require Evaluate Context to update
EP0; both USB 3 speed categories require wire value 9 for 512 bytes.

Every advertised configuration is inspected within the controller startup deadline.
Checks include descriptor lengths and totals, stable repeated headers, distinct
configuration values, interface/alternate identities, default alternates,
endpoint counts and addresses, speed-dependent packet/interval fields and
SuperSpeed companion structure for both USB 3 speed categories. Interface numbering
follows USB's consecutive zero-based numbering rule. Class/vendor descriptors retain opaque contents but
must have valid traversal lengths. Validated interface records retain
configuration value, interface number, alternate,
class, subclass, protocol and endpoint count. A malformed configuration rolls back
its interface records while retaining checked device identity. The current interface
arena holds 512 records per controller; exceeding it gives partial inventory. This
is a resource choice in `settings.h`, not a hardware or database requirement.

SuperSpeedPlus root devices use the same bounded inspection path as SuperSpeed.
Their link category comes from the host protocol metadata, never the device
descriptor version. Supported SuperSpeed and SuperSpeedPlus hubs expose boot descendants through
[hub traversal](usb-hubs.md); unsupported or unreadable branches remain partial.
The SSP isochronous companion is structurally traversed; its link-dependent
byte budget remains uninterpreted without non-control endpoint scheduling.

Supported hubs receive SET_CONFIGURATION and xHCI Slot hub metadata.
Completely inspected supported storage can select its first BOT configuration
and alternate, configure bulk endpoints and run the private media probe after
hub traversal. [Storage preparation](usb-storage.md#binding-and-preparation)
reserves its own bounded pool before AP startup. Checked boot keyboard/mouse
profiles bind on low/full/high-speed roots or paths through at most five supported
external USB 2 hubs, using the private [interrupt-IN transport](usb-interrupt-in.md).
Class setup and fixed report interpretation stay outside xHCI. Observation does
not grant disk, filesystem, input or raw transfer authority; binding outcomes
remain private and per device.

## Bounded runtime HID attachment

After boot publication, one BSP worker per prepared controller owns physical
topology admission. It monitors root connection generations and retained USB 2
and USB 3 hub notifications, admitting new/returning supported hubs and USB 2
low/full/high-speed HID children, including on another root or prepared
controller. Connection stability, reset and recovery precede addressing; root
speed is classified after reset, because USB 2 Port Speed is invalid before it.
[NCM recovery](usb-cdc-ncm.md#retained-routes-and-recovery) uses the same topology
owner's fresh same-route records for checked class rebind of its existing NIC;
moving a dock does not migrate that NIC's identity.

Each runtime attempt consumes a fresh prepared topology generation. Descriptor
inspection and interface accounting use private runtime records and scratch
storage rather than writing the published inventory. Enumeration/control and
class setup share a bounded deadline, with report collection progressing during
command waits. Failed or retired generations consume the attachment budget until
reboot. Device/subtree removal releases affected input and fences each host
slot child-first, leaving unrelated devices running. An unproven fence still
quarantines the controller. Supported hub configuration and HID binding remain
within the [retained topology bounds](usb-hubs.md#bounded-runtime-topology-recovery).
Runtime storage is descriptive only: no BOT probe, raw-block registration or
automatic pool reattachment. Runtime Bluetooth and USB 3 HID binding remain
excluded. No runtime allocation, mapping or DMA recycling is provided.

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
terminal completion can later retire it. An owned STALL can safely retire the
old sequence through Reset Endpoint, TT cleanup and Set TR Dequeue before reuse.
Owned request deadlines, known disappearance and owned transaction-error/babble
events close device/subtree admission and retain unresolved spans. Command
failure or unowned/corrupt completions still stop the controller. EP0, configured interrupt and synchronous/
asynchronous bulk endpoints are fenced before child-first Disable Slot; only
a failed fence quarantines the controller for that removal. Halt, interrupt
masking or disabling bus mastering alone cannot justify recycling backing; all old DMA and TD records
remain until reboot. Ordinary idle unbound leaf removal can also disable its
slot independently.

The runtime outer worker uses `usb_host_control_poll` before collecting an EP0
ticket. An active request stays posted; an owned STALL uses the existing bounded
Reset Endpoint/TT/dequeue retirement before collection. This does not authorize
class progress to wait or issue recovery commands during event-ring traversal.

`inventory` and each controller's private discovery/per-port/request records are
available for GDB inspection. Only the copied observation records cross the ABI.

See the [inventory bring-up record](../development/usb-inventory-bringup.md) and
[earlier control-transfer record](../development/usb-enumeration-bringup.md) for measured
behavior and limits. Context and transfer semantics follow the
[Intel xHCI 1.2b specification](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf),
particularly §§4.3, 4.6, 4.9, 4.11, 6.2, 6.4 and 7.2. Unbound class recognition
uses the [USB-IF class-code list](https://www.usb.org/defined-class-codes).
