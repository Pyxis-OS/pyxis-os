# USB BOT/SCSI read-only storage

With `CONFIG_XHCI=y`, Caelum independently probes supported boot-present storage
on each discovered xHCI controller, including traversed hub descendants. The
checked-in default remains `n`. Supported disks register with the kernel-only [block interface](block-storage.md)
and receive [GPT snapshots](gpt.md). Trusted init can open USB-backed npfs volumes
read-only using configured GUID authority, then delegate ordinary directory/file
grants. Installer raw-disk authority remains VirtIO-only; USB registration grants
no public raw-disk access. Writes, cache flushes, hotplug and native storage qualification
remain separate work.

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
The block backend additionally reserves two 64 KiB captured read buffers per
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
Optional stalled input collection copies bounded partial bytes only after
successful retirement, before buffer reuse. This supports successful short
Data-In responses terminated by STALL; the CSW still determines relevant bytes.
A failed recovery leaves the collection destination untouched.

BOT checks the exact 31-byte CBW and 13-byte CSW framing, signature, tag, status
and residue. Data-In STALL clears the halt before status collection. A status
STALL permits one clear-and-retry within the original exchange deadline. Valid
CSW command failure leads to REQUEST SENSE; invalid framing/status or a phase
error requires reset recovery. Reset ordering is class reset, Bulk-In halt clear,
then Bulk-Out halt clear, including the non-stalled pipe. Recovery never turns a
failed command into success or replays the read.

Each exchange has a five-second absolute deadline. During boot probing it is
also bounded by the controller's thirty-second enumeration/media deadline. Reset recovery has its own
five-second absolute deadline; retries do not restart the exchange deadline.
Unsafe host recovery, timeout, removal during active work or corrupt events
quarantine that controller and retain unresolved DMA. A class failure abandons
its device; other controllers continue independently.

## Block registration and queued reads

The shared block registry reserves metadata before AP startup and assigns a
nonzero boot ID to each terminal storage candidate. VirtIO candidates register
first; USB candidates append as controller workers finish their probes. IDs and
preparation results never change or get reused. Discovery is sealed when all
controllers reach terminal boot outcomes, including failed or unsupported ones.
A finished partial inventory remains explicitly incomplete while individually
READY devices can accept requests. No global disk winner or fallback exists.

Every READY USB device reports its checked geometry, a 64 KiB transfer limit,
two request slots, `writable=false` and `flush_supported=false`. WRITE and FLUSH
return `BLOCK_READ_ONLY`. Submission validates the whole logical-block range,
reserves a slot and wakes the owning xHCI worker. Queued, active and completed
uncollected reads all count against the slot limit. Reads run in admission order
per device; each worker invocation processes at most two per device.

The controller worker issues one BOT exchange at a time into a captured slot
buffer. `submitted` becomes true at the READ command's CBW TRB publication;
subsequent sense/recovery commands do not change that fact. Collection copies
the exact requested bytes only after successful whole-command completion.
Failure leaves the caller's destination untouched. There is no retained caller
read pointer and no allocation or new mapping in these operations.

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

## Read-only mount authority

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

Trusted init selects the GPT entry and volume and requests read-only directory
rights. The existing filesystem engine refuses writable USB opens and committed
journals that require replay; it performs no write or flush on this backend.
Returned roots carry the selected boot ID and remain independently retained
through delegation and executable capture. USB addresses, routes, GUIDs and
volume names do not give an application mount or raw-block authority. Installer
enumeration, explicit device opens and disk-volume inspection remain VirtIO-only
through the separate `block_installer_*` view.

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
64 bytes of each read, geometry, sense and command/read/recovery counters in
`usb_device_record.storage` for debugger inspection. Logs identify controller,
discovered path, unsupported/failure reason and successful geometry/read bytes.
Storage outcomes are separate from descriptor-inventory completeness.

The [manual bring-up record](../development/usb-storage-bringup.md) describes QEMU
coverage. The [owner-reported ThinkPad follow-up](../targets/t14-gen1-amd/usb-bringup.md#2026-10-04-read-only-storage-and-usb-3-hub-follow-up)
observed root and USB 3 hub-descendant reads with capacities matching Linux;
recovery and broader native qualification remain pending. Protocol behavior follows [USB BOT 1.0](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf)
§§3, 5 and 6, and [xHCI 1.2b](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf)
§§4.6.6, 4.6.8, 4.6.10 and 4.10.1. SCSI command fields follow the
[Seagate SCSI reference, Rev. M](https://knowledge.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068m.pdf)
§§3.8, 3.18, 3.20, 3.24–3.25, 3.40 and 3.57.
