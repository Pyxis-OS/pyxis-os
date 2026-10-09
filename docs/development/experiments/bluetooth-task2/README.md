# Bluetooth task 2: interrupt-IN requirements

This is the 2026-10-08 assessment requested by
[investigation task 2](../../../devices/ax200-bluetooth.md#qualified-scope), after
[task 1](../bluetooth-task1/README.md) and its
[merged PR #511](https://git.internal/PyxisOS/pyxis-os/pulls/511). It records source
inspection and specification requirements, not measured interrupt traffic; its proposals were
decided afterwards (see the decisions below). It is documentation only: no build, boot,
transfer or timing was run, and the AX200 measurements come from task 1, whose
firmware-state limit is unchanged. Inspected kernel revision:
`41e772bc0bbad663f3c0cb8093867865d3c59237`. Task 1's measured configuration 1, interface 0,
alternate 0 has interrupt IN `0x81`, maximum packet 64 and `bInterval=1`, at full speed.

## Existing code and gaps

The [descriptor validator](../../../../kernel/usb/core.c#L363) already checks interrupt
endpoints, but recognition does not configure them. The immutable inventory keeps only
interface summaries and endpoint counts, since full descriptors live temporarily in reusable
controller scratch, so a binder must capture the checked configuration, interface, alternate
and endpoint values before scratch reuse (as BOT selection does in `core.c`).

| Existing path | Consequence for interrupt IN |
| --- | --- |
| [`set_bulk_endpoint`](../../../../kernel/usb/xhci.c#L1971) | Bulk type, no periodic interval or payload fields; cannot configure `0x81`. |
| [`usb_host_configure_bulk`](../../../../kernel/usb/xhci.c#L2003) | Paired IN/OUT admission uses the storage pool; a persistent single receive endpoint needs independent admission and state. |
| [`consume_transfer`](../../../../kernel/usb/xhci.c#L961) | Every non-EP0 completion goes to bulk state; interrupt slot/DCI/TD ownership needs its own dispatch. |
| [`usb_host_bulk_transfer`](../../../../kernel/usb/xhci.c#L2134) | Synchronous receive with a hard timeout that stops the controller; normal absence of an event cannot inherit that policy. |
| [`controller_worker`](../../../../kernel/usb/xhci.c#L2277) | Storage work can wait synchronously; receive progress cannot depend on returning to the outer loop. |
| [`retire_root_devices`](../../../../kernel/usb/xhci.c#L1254), [`stop_controller`](../../../../kernel/usb/xhci.c#L1313) | Active or held interrupt work must join removal and failure accounting before persistent receives are admitted. |

Reusable machinery: the transfer-ring initializer, stride-aware input-context access,
captured DMA buffers, Normal TRB publication, cycle/barrier ordering, physical completion
identity checks, event-ring draining and worker notification, with the
[private host interfaces](../../../../kernel/usb/host.h) already distinguishing captured bytes
from caller destinations. Sharing them needs no combined class protocol or callback framework,
but the storage request/result state cannot also own an event stream.

## Endpoint context for the observed device

From [xHCI 1.2b](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf), sections 4.3.5,
4.8.2.4, 4.14.3 and 6.2.3, with task 1's descriptor:

| Field | Value |
| --- | --- |
| Device Context Index | 3 (`2 * endpoint_number + IN`) |
| Endpoint Type | 7, Interrupt IN |
| Max Packet Size | 64 |
| Interval | 3: `125 us * 2^3 = 1 ms`; USB `bInterval=1` is not copied directly |
| Max Burst / Mult / MaxPStreams | 0 / 0 / 0 |
| CErr | 3 |
| Max ESIT Payload | 64, including a zero high part |
| Dequeue / DCS | owned ring physical address / initial cycle 1 |
| Average TRB Length | nonzero, chosen for the receive TDs, not the packet limit |

Configure Endpoint must succeed before SET_CONFIGURATION and receive publication; configuration 1,
alternate 0 needs no alternate switch. Slot Context Entries must keep the highest configured DCI
when ACL endpoints are added later (adding DCI 3 must not shrink an existing DCI 5). `registers.h`
lacked the interrupt type, interval and ESIT definitions. Receives use Normal TDs: a TD can exceed
Max ESIT Payload and span service intervals, a short IN packet terminates it, and an ordinary NAK
retries at the next interval rather than completing an empty read. These hardware rules establish
no event latency or losslessness in Pyxis. The existing BOT setup sends SET_CONFIGURATION before
configuring host endpoints; that is storage behavior not to be transplanted, and its correction is
out of scope.

## Ownership, progress and retirement

The BSP controller worker stays the sole owner of context and ring mutation, receive state and
completion accounting. Ring pages, DMA receive buffers, completion records and copied-data storage
are reserved before AP startup, independently of `USB_STORAGE_DEVICE_BUDGET`, with no runtime
allocator, mapping, allocator lock or AP transfer worker (see the
[SMP ownership contract](../../../kernel/smp.md)). A private nonblocking receive operation with
completion collection is used: a caller wait may expire without canceling the hardware TD, freeing
its buffer or consuming the completion identity, and a receive can stay pending through a long
idle period, while setup, command and recovery operations keep bounded deadlines.

| Receive state | Buffer/ring ownership |
| --- | --- |
| Prepared | Software may fill the unpublished descriptor and reserve a buffer. |
| Posted | Hardware owns the admitted span and buffer; no overwrite or reuse. |
| Completed | After the owned terminal event is consumed, copy actual successful bytes before the buffer is reusable. |
| Held | STALL or unresolved failure retains backing and ring identity until explicit retirement; no copy to a caller destination. |

Completions are matched by controller, slot, DCI and physical TD identity, residue is bounded by
the requested bytes, and unowned or duplicate completions are rejected. Several posted receives
each need their own physical identity and retained buffer. Ticket generations protect client
collection but do not make reuse of an unresolved ring span safe, and producer wrap must respect both
retirement and collection.

`run_command`, `usb_host_control_wait` and `usb_host_bulk_transfer` all drain events while waiting,
so completion capture and bounded receive progress must work at those drain points too; rearming only
after `usb_storage_process` would leave the stream unarmed during long storage waits. Service after
event traversal only captures, copies and rearms; class commands and recovery wait until the current
operation releases command/EP0 ownership and never recurse from event consumption. Queue consumption
can still be delayed by synchronous storage, so bounded receive progress does not remove the overflow
question. The one-millisecond service interval does not require a one-millisecond CPU polling loop.

STALL recovery cannot be a renamed bulk-clear call (that reconstructs bulk contexts and its `clear_tt`
helper encodes bulk for noncontrol traffic). The AX200 is root-connected with no transaction
translator; behind-hub periodic recovery needs separate verification. There is no Stop Endpoint
operation or stopped-transfer accounting, so recoverable cancellation of a posted receive cannot be
promised. Removal during active EP0/bulk work already quarantines the whole controller and retains
backing until reboot; with a persistent receive, active removal becomes the usual case and the policy
would also stop unrelated storage on that controller. Controller shutdown must mark interrupt work
failed/held, because halt or disabled bus mastering alone cannot authorize buffer reuse under the USB
lifetime contract.

## Buffering and loss

The [Bluetooth USB transport](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host-controller-interface/usb-transport-layer.html),
section 2.1.1, puts one HCI packet in one USB transfer (several transactions), and the
[HCI event format](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host-controller-interface/host-controller-interface-functional-specification.html),
section 5.4.4, permits 255 parameter bytes plus a two-byte header. A 64-byte packet limit is therefore
not a 64-byte event limit, and a receive must hold 257 bytes (no UART packet-type byte). HCI framing and
validation belong to the HCI class transport, not the xHCI event parser.

The proposal for the first event-only probe was two posted 257-byte receives on one retained ring (Average
TRB Length 257) and eight copied completion entries: provisional resource choices, not guarantees or
measured burst requirements. Completed bytes are copied into the bounded queue by the owning worker
before rearm, and the consumer gets actual length and completion identity, not a borrowed DMA pointer;
a capacity rejection preserves queued bytes for a later collection. If the queue fills, the stream
latches an explicit discontinuity and the probe command ends rather than silently dropping an HCI
response, since loss can destroy command-credit and state knowledge. Rearming stops, and posted receives
stay owned until their terminal events are accounted for or retained if retirement cannot be proved.
Leaving the endpoint unarmed gives backpressure only as far as the device's own buffering (unmeasured)
permits, two receive buffers reduce rearm gaps without proving losslessness, normal no-data NAKs are not
overflow, and an empty successful transfer is not an HCI event.

## Sharing with USB HID

The planned [USB HID mouse](../../../wip/bluetooth-mouse.md#proposed-sharing-with-usb-hid) can share
the checked interrupt-IN configuration, retained rings, completion dispatch and receive-buffer
ownership, while USB configuration/interface selection and class framing stay with their consumers.
HID report interpretation and pointer delivery are separate work, and coalescing HID reports is not
permission to discard HCI events. High-speed/USB 3 companion fields and behind-hub periodic support need
separate profiles, not assumptions from this full-speed root device. The first probe admitted one
boot-present, root-connected full-speed Bluetooth function, selected its event endpoint from the checked
descriptor, used EP0 for HCI commands, and configured no ACL or SCO, raw-USB authority, hotplug or
userspace stack; descriptor matching must not use a ThinkPad port or Linux bus/address as policy.

## Decisions, 2026-10-08

Owner comments on [merged PR #517](https://git.internal/PyxisOS/pyxis-os/pulls/517) accepted: root-connected
full-speed admission with explicit unsupported-profile results; two posted 257-byte receives, eight copied
completions and explicit overflow failure/discontinuity; and controller-wide quarantine on active removal
for the internal AX200, including its effect on other storage on that controller. The owner then accepted
STALL as a terminal stream failure with DMA backing and ring identity retained until reboot and no
automatic recovery. The [accepted decisions](../../../devices/ax200-bluetooth.md#accepted-interrupt-in-decisions)
supersede the proposals above and the limits are in
[technical debt](../../../technical-debt.md#xhci-hardware-profile-and-runtime-retention). Task 3a was
authorized to implement shared kernel interrupt-IN support through private interfaces without a public
ABI; task 3b stayed the unmerged HCI/controller-state probe, followed by firmware and scan probes.
