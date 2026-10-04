# USB BOT/SCSI read-only probes

With `CONFIG_XHCI=y`, Caelum independently probes supported boot-present storage
on each discovered xHCI controller, including traversed hub descendants. The
checked-in default remains `n`. This is a kernel-internal boot consumer: USB disks
are not registered with the public block API or mounted. Writes, cache flushes,
hotplug and native storage qualification remain separate work.

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
These are resource choices independent of image sizes and physical topology.

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

Each exchange has a five-second absolute deadline bounded by the controller's
thirty-second boot enumeration/media deadline. Reset recovery has its own
five-second absolute deadline; retries do not restart the exchange deadline.
Unsafe host recovery, timeout, removal during active work or corrupt events
quarantine that controller and retain unresolved DMA. A class failure abandons
its device; other controllers continue independently.

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
coverage and remaining limits. Protocol behavior follows [USB BOT 1.0](https://www.usb.org/sites/default/files/usbmassbulk_10.pdf)
§§3, 5 and 6, and [xHCI 1.2b](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf)
§§4.6.6, 4.6.8, 4.6.10 and 4.10.1. SCSI command fields follow the
[Seagate SCSI reference, Rev. M](https://knowledge.seagate.com/files/staticfiles/support/docs/manual/Interface%20manuals/100293068m.pdf)
§§3.8, 3.18, 3.20, 3.24–3.25, 3.40 and 3.57.
