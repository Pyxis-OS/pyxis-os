# Bluetooth task 2: interrupt-IN requirements

This is the 2026-10-08 assessment requested by
[investigation task 2](../../../devices/ax200-bluetooth.md#qualified-scope), after
[task 1](../bluetooth-task1/README.md) and its
[merged PR #511](https://git.internal/PyxisOS/pyxis-os/pulls/511).
It records source inspection and specification requirements, not measured
interrupt traffic or an implemented interface. The proposal below was unapproved
at assessment time; the later decisions are recorded in the addendum below. No
probe code, public ABI or driver changes were included in the assessment PR.

The inspected kernel revision is `41e772bc0bbad663f3c0cb8093867865d3c59237`.
Task 1's measured configuration 1, interface 0, alternate 0 has interrupt IN
`0x81`, maximum packet 64 and `bInterval=1`, at full speed. The controller's
bootloader/operational firmware state is still unknown.

## Existing code and gaps

The [descriptor validator](../../../../kernel/usb/core.c#L363) already checks
interrupt endpoints. Recognition does not configure them. The immutable
inventory retains interface summaries and endpoint counts; the full descriptors
live temporarily in reusable controller scratch. A binder must capture the
checked configuration/interface/alternate and endpoint values before scratch
reuse, following the placement of BOT selection in `core.c`.

| Existing path | Consequence for interrupt IN |
| --- | --- |
| [`set_bulk_endpoint`](../../../../kernel/usb/xhci.c#L1971) | Bulk type, no periodic interval or payload fields; cannot configure `0x81` as interrupt IN. |
| [`usb_host_configure_bulk`](../../../../kernel/usb/xhci.c#L2003) | Paired IN/OUT admission uses the storage pool; a persistent single receive endpoint needs independent admission and state. |
| [`consume_transfer`](../../../../kernel/usb/xhci.c#L961) | Every non-EP0 completion is sent to bulk state; interrupt slot/DCI/TD ownership needs its own dispatch. |
| [`usb_host_bulk_transfer`](../../../../kernel/usb/xhci.c#L2134) | Synchronous receive and hard timeout stop the controller; normal absence of an event cannot inherit that policy. |
| [`controller_worker`](../../../../kernel/usb/xhci.c#L2277) | Storage work can wait synchronously; receive progress cannot depend only on returning to the outer worker loop. |
| [`retire_root_devices`](../../../../kernel/usb/xhci.c#L1254), [`stop_controller`](../../../../kernel/usb/xhci.c#L1313) | Active/held interrupt work must join removal and failure accounting before persistent receives are admitted. |

The useful shared machinery is the transfer-ring initializer, stride-aware
input-context access, captured DMA buffers, Normal TRB publication, cycle/barrier
ordering, physical completion identity checks, event-ring draining and worker
notification. The [private host interfaces](../../../../kernel/usb/host.h)
already distinguish captured bytes from caller destinations. Sharing these
operations does not require combining class protocols or adding a callback
framework. The storage request/result state cannot also own an event stream.

## Endpoint context for the observed device

The following derives from
[xHCI 1.2b](https://cdrdv2-public.intel.com/625472/625472_xHCI_Rev1_2b.pdf),
sections 4.3.5, 4.8.2.4, 4.14.3 and 6.2.3, using task 1's descriptor:

| Field | Value |
| --- | --- |
| Device Context Index | 3 (`2 * endpoint_number + IN`) |
| Endpoint Type | 7, Interrupt IN |
| Max Packet Size | 64 |
| Interval | 3: `125 us * 2^3 = 1 ms`; USB `bInterval=1` is not copied directly |
| Max Burst / Mult / MaxPStreams | 0 / 0 / 0 |
| CErr | 3 |
| Max ESIT Payload | 64, including a zero high part |
| Dequeue / DCS | Owned ring physical address / initial cycle 1 |
| Average TRB Length | Nonzero; chosen for the receive TDs, not the packet limit |

Configure Endpoint must succeed before SET_CONFIGURATION and receive publication.
For configuration 1, alternate 0 needs no alternate switch. Preserve the highest
configured DCI in Slot Context Entries when later adding ACL endpoints; adding
DCI 3 must not shrink an existing DCI 5 configuration. The interrupt definitions
for type, interval and ESIT are missing from today's `registers.h`.

Interrupt receives use Normal TDs. A TD can exceed Max ESIT Payload and span
service intervals; a short IN packet terminates it. An ordinary NAK retries at
the next service interval rather than producing a completed empty read. These
hardware rules do not establish event latency or losslessness in Pyxis.

The current [BOT setup](../../../../kernel/usb/bot.c#L641) sends SET_CONFIGURATION
before configuring host endpoints. That is existing storage behavior, not a
sequence to transplant into the interrupt probe; its correction is outside
this investigation's scope.

## Ownership, progress and retirement

Keep the existing BSP controller worker as the sole owner of context/ring
mutation, receive state and completion accounting. Reserve ring pages, DMA
receive buffers, completion records and copied-data storage before AP startup,
independently of `USB_STORAGE_DEVICE_BUDGET`. No runtime allocator, mapping,
allocator lock or AP transfer worker is needed for this investigation.
The [SMP ownership contract](../../../kernel/smp.md) remains applicable.

Use a private nonblocking receive operation with completion collection. A
caller wait may expire without canceling the hardware TD, freeing its buffer
or consuming the completion identity. A receive can remain pending through a
long idle period; setup, command and recovery operations still need bounded
deadlines. The existing EP0 ticket contract is a useful client-lifetime model,
but its hard request deadline is unsuitable for an indefinitely idle stream.

| Receive state | Buffer/ring ownership |
| --- | --- |
| Prepared | Software may fill the unpublished descriptor and reserve a buffer. |
| Posted | Hardware owns the admitted span and receive buffer; no overwrite or reuse. |
| Completed | After the owned terminal event is consumed, copy actual successful bytes before making the buffer reusable. |
| Held | STALL or unresolved failure retains backing and ring identity until explicit retirement; failure does not copy a caller destination. |

Match controller, slot, DCI and physical TD identity, bound residue by requested
bytes, and reject unowned/duplicate completions. If several receives are posted,
each needs its own physical identity and retained buffer, rather than the bulk
path's single active field. Ticket generations protect client collection; they
do not make reuse of an unresolved physical ring span safe. Producer wrap must
respect both retirement and data collection.

`run_command`, `usb_host_control_wait` and `usb_host_bulk_transfer` all drain
events while waiting. Completion capture and bounded receive progress must
therefore work at those drain points too. Merely adding rearm after
`usb_storage_process` would leave the stream unarmed during long storage waits.
In the proposed probe, service after event traversal only captures/copies bytes
and rearms receives. Class commands and recovery wait until the current operation
releases command/EP0 ownership; they must not recurse from event consumption or
an inner wait. Queue consumption can still be delayed by synchronous storage,
so bounded receive progress does not remove the overflow question.
Notifications wake the worker; retained receive/queue state counts the data.
The USB service interval does not require replacing the worker's health poll
with a one-millisecond CPU polling loop.

STALL recovery cannot be a renamed bulk-clear call. That path reconstructs
bulk contexts, and its `clear_tt` helper encodes bulk for noncontrol traffic.
The qualified AX200 is root-connected and has no transaction translator;
behind-hub periodic recovery needs separate verification. The current code has
no Stop Endpoint operation or stopped-transfer accounting, so it cannot promise
recoverable cancellation of a posted receive.

Removal during active EP0/bulk work currently quarantines the whole controller
and retains backing until reboot. A persistent receive makes active removal
the usual case: retaining that policy would also stop unrelated storage on the
same controller. Accepting this limitation or designing safe endpoint/device
retirement was an owner decision at assessment time; the later acceptance is
recorded in the addendum below. Interrupt work must also be marked failed/held
by controller shutdown; halt or disabled bus mastering alone cannot authorize
buffer reuse under the current USB lifetime contract.

## Buffering and loss proposal

The [Bluetooth USB transport](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host-controller-interface/usb-transport-layer.html),
section 2.1.1, puts one HCI packet in one USB transfer, which can use several USB
transactions. The
[HCI event format](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/Core-54/out/en/host-controller-interface/host-controller-interface-functional-specification.html),
section 5.4.4, permits 255 parameter bytes plus a two-byte header. Thus a
64-byte packet limit is not a 64-byte event limit; a receive must accommodate
257 bytes. HCI framing and validation belong to the HCI class transport probe,
not the xHCI event parser. No UART packet-type byte is part of that USB event.

The assessment proposed two posted 257-byte receive buffers on one retained
ring and eight copied completion entries for the first event-only probe.
Average TRB Length would initially be 257 for these one-TRB receive requests.
These counts are provisional resource choices, not interface guarantees or
measured burst requirements. Completed bytes are copied into the bounded queue
by the same owning worker before rearm. The consumer receives actual length
and completion identity, not a borrowed live DMA pointer. A capacity rejection
must preserve the queued bytes for a later valid collection.

The assessment proposed latching an explicit stream discontinuity if the
completion queue fills, and ending the investigation command rather than
silently dropping an HCI response and continuing with apparently valid controller
state. Cease rearming;
already posted receives remain owned until their terminal events are accounted
for, or retained if retirement cannot be proved. Command responses and scan
reports share the endpoint; loss can destroy command-credit/state knowledge.
The queue cannot be relabeled as a lossless channel.

Leaving the endpoint unarmed provides backpressure only as far as the device's
own buffering permits. Its buffering, event bursts, CPU service delay and
coexistence with long storage operations are unmeasured. Two receive buffers
reduce rearm gaps but do not prove losslessness. Normal no-data NAKs are not
queue overflow; an empty successful transfer is not an HCI event.

## Sharing with USB HID and decisions before a probe

The planned [USB HID mouse](../../../wip/bluetooth-mouse.md#proposed-sharing-with-usb-hid) can share checked
interrupt-IN endpoint configuration, retained rings, completion dispatch and
receive-buffer ownership. Keep USB configuration/interface selection and class
framing with their consumers. HID boot/report interpretation and pointer delivery
are separate work; coalescing HID reports is not permission to discard HCI events.
High-speed/USB 3 companion fields and behind-hub periodic support need separate
profiles, not assumptions derived from this full-speed root device.

The assessment proposed an unmerged first probe: one boot-present,
root-connected full-speed Bluetooth function, event endpoint selected from its
checked descriptor, and existing EP0 for HCI commands. It would not configure
ACL or SCO, add application raw-USB authority, implement hotplug, or decide the
future userspace Bluetooth stack. Its descriptor match must not use a ThinkPad
port or Linux bus/address as driver policy.

At assessment time, the following choices required agreement:

- The narrow root/full-speed admission and how it reports unsupported profiles.
- Receive/queue budgets and progress during other controller waits; the proposal
  is two 257-byte receives, eight copied completions and explicit overflow failure.
- Idle wait/abandonment behavior, STALL handling, and whether controller-wide
  quarantine on active removal is an accepted investigation limit.

These were proposals and unresolved policy choices when task 2 completed their
assessment. The following addendum records the later decisions and task split.

## Decision addendum, 2026-10-08

After this assessment, owner comments on
[merged PR #517](https://git.internal/PyxisOS/pyxis-os/pulls/517) accepted
root-connected full-speed admission with explicit unsupported-profile results,
two posted 257-byte receives, eight copied completions and explicit overflow
failure/discontinuity. Controller-wide quarantine on active removal was accepted
as an investigation limit for the internal AX200, including its effect on other
storage on the same controller. The owner subsequently accepted STALL as a
terminal stream failure with DMA backing/ring identity retained until reboot and
no automatic recovery.

The [accepted investigation decisions](../../../devices/ax200-bluetooth.md#accepted-interrupt-in-decisions)
replace the unresolved status of those choices. Task 3a is now authorized to
implement shared kernel interrupt-IN support for merge through private interfaces,
without a public ABI; task 3b remains the unmerged HCI/controller-state probe,
followed by firmware and scan probes. This addendum records authorization and policy, not completed
implementation or new validation. The accepted limitations and revisit points
are in [technical debt](../../../technical-debt.md#xhci-hardware-profile-and-runtime-retention).

## Assessment validation and handoff

Reviewed the existing USB source, relevant ownership docs, task 1 descriptor
captures and primary xHCI/Bluetooth references. Checked local document links
and whitespace. No new build, boot, transfer, timing baseline or fault injection
was run: this PR changes documentation only. All actual AX200 measurements still
come from task 1, whose firmware-state limit remains unchanged.

The PR also folds in #511's requested account-name correction: the task 1 report
now describes device-node ownership as the invoking user with group `root`.
No dependency pins changed. Branch: `docs/bluetooth-task2`, based on `41e772b`;
userspace `48723de2a5f7f1df997034479c2451f07111d859`, ports
`1064c452a0236040c7d673ab51dbea3a5b81ff80`, filesystem
`b427df29f865bc361b8da92bcd74e114581e9a32`, lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`. No QEMU/debugger processes were started.
