# USB BOT/SCSI storage

With `CONFIG_XHCI=y`, Caelum independently probes supported boot-present storage
on each discovered xHCI controller, including traversed hub descendants. The
checked-in default remains `n`. Supported disks register with the kernel-only
[block interface](block-storage.md) and receive [GPT snapshots](gpt.md). Trusted init can open USB-backed npfs volumes
using configured GUID authority, then delegate ordinary directory/file grants.
Explicit writable mounts require known clear write protection and successful
cache-synchronization qualification; other readable disks remain read-only.
The trusted Install init can delegate bounded [installer raw-disk
authority](installer-authority.md) for retained USB candidates. USB registration
alone grants applications no raw-disk access. Hotplug and physical
write/durability qualification remain separate work.

## Binding and preparation

`core.c` passes completely validated configuration descriptors to `bot.c` before
reusing its descriptor scratch buffer. Each physical device retains the first
supported configuration and alternate in descriptor order: exactly one interface,
SCSI transparent command set/Bulk-Only (`08/06/50`), two bulk endpoints with opposite
directions, and no streams. BOT/UAS alternative configurations can select BOT.
Incomplete inspection prevents that device's binding. Unrelated classes remain
unbound; recognized unsupported storage retains an individual diagnostic.
Controller, port, configuration, interface and endpoint identities come from
hardware/descriptor observations, never vendor IDs or machine topology.

GET_MAX_LUN accepts only LUN 0; a legitimate EP0 STALL means a single LUN after
safe host recovery. Multiple LUNs remain explicitly unsupported. Media setup
failure does not erase the candidate or choose another physical disk instead.
Unsupported controllers and uninspected branches still make `lsusb` partial,
while individually inspected disks can be probed.

All resources are prepared on the BSP before AP startup. The initial choices in
`kernel/usb/settings.h` reserve four storage entries per controller. Each owns
two ring pages and a transfer buffer up to 64 KiB, with padding to select a
64 KiB-aligned physical interior. One controller-owned DMA arena supplies these
slices; with current choices it occupies 528 KiB and one mapping. The USB core
also reserves one 64 KiB non-DMA read scratch buffer per controller. Admission
exhaustion is explicit. Admitted entries remain associated with their original
device even after setup failure; resources and records remain until reboot.
The block backend additionally reserves two 64 KiB captured I/O buffers per
supported device (512 KiB per controller at the current four-device budget),
and candidate metadata covering the retained root/descendant inventory. Failed
buffer preparation yields a terminal SETUP_FAILED candidate without discarding
descriptor observations. These are resource choices independent of image sizes
and physical topology.

## Transfers and recovery

The owning BSP controller worker serializes each interface's command/data/status
exchange. Private host transfers capture outbound bytes, retain no caller read
pointer, and use one Normal TRB within a checked 64 KiB boundary. Cycle publication
precedes the DCI doorbell. Completions must identify the active controller/device,
endpoint and physical TRB; residue is bounded before deriving actual bytes.
Ordinary successful input collection copies only actual bytes. Errors leave the
destination untouched.

An owned STALL holds the failed span. Recovery starts after its event has been
consumed, outside event-ring traversal. EP0 uses Reset Endpoint, required
transaction-translator cleanup and Set TR Dequeue past the old data/status
stages, then permits another SETUP. Full/low-speed devices behind high-speed hubs
use their discovered TT owner and actual USB address; EP0 cleans both directions.

Bulk halt clearing resets a halted xHCI endpoint, cleans its TT when required,
sends CLEAR_FEATURE(ENDPOINT_HALT), and uses Configure Endpoint Drop+Add to reset
host toggle state and start at the retained producer frontier. Already idle
non-stalled endpoints also receive device/host toggle reset during BOT recovery.
Stalled input or output reports bounded transferred bytes only after successful
retirement, before buffer reuse. Optional input collection copies those bytes.
This supports successful short Data-In responses terminated by STALL; the CSW
still determines relevant bytes. A failed recovery leaves the collection
destination untouched.

BOT checks the exact 31-byte CBW and 13-byte CSW framing, signature, tag, status
and residue. Data-In or Data-Out STALL clears the halt before status collection.
The CSW cannot report processing more bytes than were transferred; successful
block reads and writes require the complete requested span. A status
STALL permits one clear-and-retry within the original exchange deadline. Valid
CSW command failure leads to REQUEST SENSE; invalid framing/status or a phase
error requires reset recovery. Reset ordering is class reset, Bulk-In halt clear,
then Bulk-Out halt clear, including the non-stalled pipe. Recovery never turns a
failed command into success or replays data or a mutation command.

Each exchange has a five-second absolute deadline. During boot probing it is
also bounded by the controller's thirty-second enumeration/media deadline. Reset recovery has its own
five-second absolute deadline; retries do not restart the exchange deadline.
Unsafe host recovery, timeout, removal during active work or corrupt events
quarantine that controller and retain unresolved DMA. A class failure abandons
its device; other controllers continue independently.

## Write and cache qualification

After the required readable-media probe, MODE SENSE (6) requests current mode
pages with block descriptors disabled and a four-byte header allocation. Only
a clean rejection with current ILLEGAL REQUEST sense and invalid-opcode
(`20h/00h`) or invalid-field (`24h/00h`) detail permits fallback to MODE SENSE (10),
using an eight-byte header allocation. A validated header supplies the WP bit;
mode pages need not fit the deliberately short allocation.

Known write protection leaves both `writable` and `flush_supported` false.
Known WP-clear media must complete a real SYNCHRONIZE CACHE (10) before either
flag becomes true. IMMED is zero, so status waits for completion; zero LBA and
block count synchronize the whole medium, including READ CAPACITY (16) media.
Qualification changes no cache mode and performs no data write. A successful
command establishes the device-reported capability, rather than independently
proving physical persistence or power-loss durability.

A clean command rejection or unusable optional protection header preserves
read-only service while transport remains healthy, with retained sense and a
write-qualification diagnostic. Unknown protection never authorizes writes.
Malformed BOT framing, failed sense, timeout or failed recovery still retires the
candidate. Qualification uses the existing exchange and boot deadlines; there
is no retry that turns unsupported synchronization into successful flush.

## Block registration and queued I/O

The shared block registry reserves metadata before AP startup and assigns a
nonzero boot ID to each terminal storage candidate. VirtIO candidates register
first; USB candidates append as controller workers finish their probes. IDs and
preparation results never change or get reused. Discovery is sealed when all
controllers reach terminal boot outcomes, including failed or unsupported ones.
A finished partial inventory remains explicitly incomplete while individually
READY devices can accept requests. No global disk winner or fallback exists.

Every READY USB device reports its checked geometry, a 64 KiB transfer limit,
two request slots and its qualification flags. Unqualified disks return
`BLOCK_READ_ONLY` for WRITE and FLUSH. READ and WRITE submission validate the
whole nonzero logical-block range; WRITE captures caller bytes into the reserved
slot before returning. FLUSH takes zero start/count and no data pointer. Queued,
active and completed uncollected requests all count against the slot limit.
Requests run in admission order per device; each worker invocation processes at
most two per device. Earlier admitted I/O finishes before a flush is published,
and later I/O remains queued until that flush returns. Uncollected results do
not prevent progress. A completed write alone promises no persistence.

The controller worker issues one BOT exchange at a time into a captured slot
buffer. Reads and writes use READ/WRITE (10), or READ/WRITE (16) for wide media;
flush uses blocking whole-medium SYNCHRONIZE CACHE (10). `submitted` becomes
true at the original command's CBW TRB publication; subsequent sense/recovery
commands do not change that fact. Collection copies read bytes only after
successful whole-command completion. WRITE and FLUSH collect without a read
destination. Failure leaves the caller's destination untouched. There is no
retained caller read pointer and no allocation or new mapping in these operations.

Wait timeout does not consume or cancel a ticket. Abandonment cancels queued
work or releases a completed slot; active work retains its slot until the owning
exchange returns, even when its client abandons it. The owner-accepted read policy
distinguishes a valid READ rejection followed by
valid REQUEST SENSE from transport failure. A clean rejection completes only
that request with `BLOCK_IO_ERROR`, retains sense, and keeps READY admission for
later reads; it never replays the rejected command. Failed/malformed sense,
transport/protocol failure, failed recovery and timeout retire the device and
complete queued reads as unavailable. Probe-time READ rejection remains a terminal
setup failure: the initial probe must establish usable media before registration.

Any failed runtime WRITE or FLUSH latches `write_failed` until reboot. Abandoning
a mutation after CBW publication also latches failure, even if the exchange
later succeeds; an active abandoned request retains its slot until its worker
returns. If an abandoned active request subsequently publishes, its worker
latches failure on return. Later mutations, including already queued work,
complete with `BLOCK_WRITE_FAILED` without publication. A clean rejection with
valid sense preserves healthy read admission. Genuine transport/protocol
failure, timeout or failed recovery retires the device and queued I/O. No
mutation is replayed, and a later successful flush cannot clear the latch.
The block backend solely owns the mutable failure flag; probe qualification
facts remain unchanged.

Controller quarantine independently stops admission and queued
work on all its disks; the active exchange retires through its worker, and unsafe
host DMA stays retained until reboot. Other controllers and VirtIO continue.
Preparation facts and GPT snapshots remain immutable after later I/O failure.

GPT reserves snapshot capacity before AP startup and one shared additional
scratch buffer for USB. Its coordinator waits for sealed boot discovery, then
scans retained USB candidates sequentially through ordinary block tickets. READY
media can publish maps even when aggregate USB discovery is partial. Each actual
scan retains the existing thirty-second admission/read deadline. Existing VirtIO
scans start immediately and keep their separate scratch/worker lifetime.

## Configured mount authority

Configured `mount.disk` authority searches the full retained registry. A GUID
request waits for sealed boot discovery and terminal GPT snapshots, then selects
exactly one observed HEALTHY/DEGRADED match. Duplicate observed GUIDs fail.
Unrelated terminal GPT errors do not prevent a matching usable disk; failures
from the selected disk, partition or volume propagate without fallback.

The owner accepted observed uniqueness under partial discovery. Unsupported
EHCI and uninspected branches remain visible through `lsusb`; an unseen disk
could conceal another copy of the GUID. A matching observed disk may still
mount. No match returns NOT_FOUND for complete discovery and UNAVAILABLE for
partial discovery, avoiding a false claim of absence. Discovery and scanning
consume the existing mount operation's absolute deadline.

Init receives the configured mount scope when devices are known or discovery
cannot establish absence. Pending discovery never caches a permanent failure
in the mount object. Only a complete empty inventory at scope creation omits
authority; a retained pending scope may later fail to find a match. `--optional`
still skips only missing authority, rather than suppressing mount errors.

Trusted init selects the GPT entry and volume and explicitly requests read-only
or writable directory rights. Writable opening requires both qualified device
capabilities and no latched write failure, in addition to mount WRITE authority.
The existing filesystem engine can replay a valid committed journal through a
qualified writable backend; read-only opening still refuses replay. Unknown
read-only-compatible filesystem features also prevent writes and replay.
Returned roots carry the selected boot ID and remain independently retained
through delegation and executable capture. USB addresses, routes, GUIDs and
volume names do not give an application mount or raw-block authority. The
separate `block_installer_*` view exposes retained USB and VirtIO candidates only
through trusted Install init. It waits for sealed observed discovery, permitting
partial USB coverage while refusing lost registry records or incomplete VirtIO
bookkeeping. Unsupported, failed, protected and unqualified candidates remain
visible for eligibility diagnosis. An exclusive write claim requires qualified
write/flush support, no latched write failure and no mounted pool or existing
claim. Installer selection and typed consent remain unchanged; unseen disks
cannot be listed. See [installer authority](installer-authority.md).

## Media and retained results

The probe issues INQUIRY, TEST UNIT READY and REQUEST SENSE as needed. Only a
connected direct-access LUN is supported. Readiness permits three bounded
attempts for current UNIT ATTENTION; other failures retain sense and stop that
probe. READ CAPACITY (10) returns the last LBA; its saturated sentinel requires
READ CAPACITY (16). Such media use READ (16), including low-LBA reads.

The initial logical-block policy accepts 512 or 4096 bytes, matching native block
geometry; this is a Caelum support choice, not a SCSI restriction. Count, byte
capacity, LBA range and transfer multiplication are checked. READ CAPACITY (16) protection-enabled
geometry is deferred. The consumer reads up to the first 64 KiB and the final
logical block, requiring complete requested block data. It retains the first
64 bytes of each read, geometry, sense, protection/qualification details and
command/read/write/flush/recovery counters in `usb_device_record.storage` for
debugger inspection. Logs identify controller,
discovered path, unsupported/failure reason, successful geometry/read bytes and
write/flush qualification.
Storage outcomes are separate from descriptor-inventory completeness.

The [manual bring-up record](../development/usb-storage-bringup.md) describes QEMU
coverage. The [owner-reported ThinkPad follow-up](../targets/t14-gen1-amd/usb-bringup.md#2026-10-04-read-only-storage-and-usb-3-hub-follow-up)
observed root and USB 3 hub-descendant reads with capacities matching Linux;
physical writes, recovery and broader native qualification remain pending.
The C.1 error, abandonment and qualification-refusal paths have source review;
forced-error validation was not performed. Protocol behavior follows
[USB BOT 1.0](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf)
§§3, 5 and 6, and [xHCI 1.2b](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf)
§§4.6.6, 4.6.8, 4.6.10 and 4.10.1. SCSI command fields follow the
[Seagate SCSI reference, Rev. M](https://knowledge.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068m.pdf)
§§3.8, 3.13–3.14, 3.18, 3.20, 3.24–3.25, 3.40, 3.55 and the
WRITE command descriptions.
