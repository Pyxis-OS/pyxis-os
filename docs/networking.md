# Networking: local IPv4 and ICMP echo

The kernel has one system-wide networking worker and a boot-lifetime interface
named `lo`. Interfaces and future addresses/routes are shared across spaces;
capability-mediated access will not by itself provide network isolation.
The implementation is in `kernel/net`, with kernel interfaces in
`include/kernel/net`. The native echo capability supports userspace ping without raw-packet authority.

The worker validates IPv4 packets and handles ICMP echo requests/replies over
loopback. There is no external route or configurable address yet.
No packets are allocated or transmitted automatically at boot. The
[networking milestone](wip/initial-networking.md) records the remaining tasks.

## Ownership and bounds

`net_packet_allocate(length)` creates an exclusively owned heap buffer with an
immutable length and writable payload. The caller must initialize every payload
byte before publication. It returns NULL for zero or oversized length, the
32-live-packet budget, or heap exhaustion. The maximum payload is 1500 bytes,
also the initial loopback MTU; there is no fragmentation or jumbo-packet support.
These software buffers are not DMA allocations.

All live packets count against the budget, whether held by a caller, queued or
being processed. Payload storage is therefore bounded by 48,000 bytes, plus
packet and heap metadata. No unbounded allocation occurs while enqueueing.
The loopback receive queue holds at most 16 packet pointers.

`net_transmit(&net_loopback, packet)` transfers ownership only on `NET_OK`.
Success means queued, not accepted by an IP protocol or delivered to an
application. A full queue returns `NET_QUEUE_FULL`; an unavailable worker returns
`NET_UNAVAILABLE`; invalid arguments return `NET_INVALID`. Every failure leaves
the packet owned by the caller, which may retry or release it.
`net_packet_release` consumes an owned packet; NULL is harmless.

The caller must not read, modify, resend or release a successfully queued packet.
The worker takes ownership on dequeue, lends the packet to protocol processing
for that call, then releases it on every path. No reference counting, retained
payload pointers, packet registry or device buffer sharing is introduced. The
sole interface is a constant descriptor, not a dynamically registered or removable
device.

## Local IPv4 and echo

`lo` uses `127.0.0.1/8`. All `127/8` destinations take the local route, and only
loopback sources are accepted on this path. Other destinations return
`NET_NO_ROUTE`; no route table or external-interface configuration is needed yet.
Kernel address arguments are host-order integers (`0x7f000001` is `127.0.0.1`),
while wire fields use network byte order. Header layouts remain kernel-private.

`net_ipv4_transmit` takes an owned packet whose first 20 bytes are reserved for
the IPv4 header and whose remaining payload is initialized. It fills that header
and queues delivery, transferring ownership only on success. A failed call may
have filled the header but retains caller ownership. Source and destination are
explicit; the echo request helper selects `127.0.0.1` for new requests. Replies
use the request's destination as their source, including other `127/8` addresses.

Receive checks version, header and total lengths, reserved flags and the header
checksum before protocol dispatch. It ignores bytes beyond IPv4 total length,
so trailing link padding cannot become echo payload. Options, fragments and
protocols other than ICMP are counted and discarded. Only an ordinary 20-byte
IPv4 header is supported; DF is allowed, MF and nonzero fragment offsets are not.
Local delivery does not decrement TTL or reject a packet solely for a low TTL.
Outgoing packets use TTL 64 and DF, with identification zero for atomic datagrams.
There is no fragmentation, reassembly, forwarding or general ICMP error generation.
These are explicit subset limits, not full IPv4 host conformance.

`net_icmp_echo_send(destination, identifier, sequence, payload, length)` copies
caller bytes, builds an echo request, and queues it. It needs BSP/IF=0 context.
Zero-length payloads are valid; the maximum is 1472 bytes within the 1500-byte
IP MTU. Invalid arguments return `NET_INVALID`, packet allocation/budget failure
returns `NET_NO_MEMORY`, and route/queue/worker errors propagate. The helper
releases its allocation on failure and never retains caller storage.

ICMP receive validates length, checksum and the zero echo code. Each valid echo
request creates a separate bounded reply carrying the unchanged identifier,
sequence and payload. Replies enter the same deferred queue; a failed reply
allocation or submission is counted and discarded. The original receive packet
remains owned by the worker until processing returns. Echo replies are currently
validated and offered to the echo service for matching. Unmatched replies are
consumed without waking an application. Transport success never claims that a
peer has answered.

References: [IPv4](https://www.rfc-editor.org/rfc/rfc791.html),
[ICMP echo](https://www.rfc-editor.org/rfc/rfc792.html),
[host loopback and TTL rules](https://www.rfc-editor.org/rfc/rfc1122.html),
[atomic datagram identification](https://www.rfc-editor.org/rfc/rfc6864.html#section-4.1).

## Execution and initialization

Call `net_init` once on the BSP with interrupts disabled, after `task_init` and
before scheduling begins. It creates the BSP worker; failure leaves sends
unavailable and logs a diagnostic while ordinary boot continues.

Allocation, release and transmission currently require BSP task or initialization
context with IF=0. They are not AP, user-pointer or interrupt-entry interfaces.
Future application calls will need the existing kind of explicit cross-CPU
ownership handoff; this slice adds no scheduler request state.

The worker runs with interrupts enabled. It disables them around allocation,
transmission, release and shared queue/wait operations. Receive parsing and
validation run with IF=1 outside interrupt entry. Each turn scans at most 16 echo
slots and delivers at most eight loopback packets. The optional NIC adds at most
16 RX and 16 TX completions per turn. The worker yields if it serviced echo
requests or exhausted either delivery budget. Local transmission never recursively
enters receive and has no Ethernet, ARP, PCI or DMA dependency.

Packet queue access remains BSP/IF=0. A separate small lock protects the worker's
notification flag and wait pointer because AP callers may now submit echo work.
Submission publishes a request before notifying, with the request lock released.
The remembered notification bridges the gap between inspecting deadlines and
publishing the wait. Wake detaches the pointer under the notification lock;
timeout resumption detaches under that same lock before reusing its wait record.
The worker sleeps until new work or the earliest echo deadline, indefinitely
when there is neither. No scheduler request queue or per-tick network polling
is added.

## Native echo capability

Init receives an `echo` resource. Session handoff and ordinary shell launches
copy its ECHO_RIGHT_SEND grant. It authorizes ICMP echo exchanges only: no raw
headers, user-selected source address, interface configuration or routing changes.
The stack and request budget remain shared between spaces. Configuration
capabilities are deferred until there is an external interface to configure.

[The echo protocol](../include/abi/echo.h) uses the existing CALL syscall with a
tagged ECHO_EXCHANGE request. Its input is a host-order IPv4 destination and an
absolute deadline in the CLOCK_NOW monotonic epoch, at most five seconds ahead.
A past deadline returns CALL_TIMED_OUT; an excessive future deadline or nonzero
reserved field is CALL_BAD_REQUEST. Capture/check all input and reply storage
before publishing any work. Errors return no bytes and leave user storage alone.
Success returns the peer address, wire identifier/sequence and round-trip
nanoseconds, measured from transport submission through reply processing.

The kernel owns the fixed 32-byte payload and wire identifiers. Every request
gets a nonzero 64-bit token that is never reused before reboot; exhaustion returns
CALL_LIMIT. Reply matching checks source/destination, identifier, sequence and
all payload bytes, including that token. This correlates late/duplicate replies;
it is not authentication against a peer that can observe or predict a token.

At most 16 calls may occupy the shared request table, including completed calls
whose callers have not resumed. Exhaustion returns CALL_QUEUE_FULL. Callers copy
scalar input into a slot, publish their task-metadata waiter and sleep. Only the
BSP worker allocates/sends packets, receives replies and expires requests. No
user pointers, caller stack pointers or private mappings cross this boundary.
No lock is held while allocating or transmitting. Slot state and completion are
protected across CPUs; completion detaches/wakes its waiter under the slot lock.
The original caller copies the result and releases the slot before returning.

A deadline covers time queued as well as time awaiting a reply. Replies processed
at or beyond the deadline time out. Completion removes the slot from matching;
late replies cannot wake its previous task or a new request. Packet ownership is
independent: timeout never frees a packet already transferred to the transport.
This is important for future device DMA as well as the current deferred queue.

Processes currently have one task and no external cancellation. The blocked
call keeps its capability and private mappings alive; closing a copied grant
elsewhere does not revoke it. Request state belongs to the call, not the echo
object, so closing another grant cannot destroy that state. Multithreading,
external termination or asynchronous cancellation must extend this contract.

A missing worker returns CALL_UNAVAILABLE, a missing route CALL_NO_ROUTE, and
allocation failure CALL_NO_MEMORY. Queue errors propagate. This synchronous echo
protocol does not constrain future socket/endpoint or asynchronous network APIs.

## Userspace ping

Libpyxis supplies `echo_exchange`, taking an echo handle, destination, deadline
and reply pointer. Its reply is cleared on failure and native status is preserved.
The initial utility needs the `echo` grant plus clock READ/SLEEP authority:

```text
ping 127.0.0.1
ping -c 10 127.0.0.2
```

The default is four requests, with one-second deadlines and starts paced at least
one second apart. `-c` accepts 1..65535. Addresses must have four decimal octets;
no DNS or shorthand is supported. Each success prints peer, sequence and RTT;
errors are explicit, followed by attempted/replied/unanswered counts and RTT
min/average/max when any replies arrived. Only timeouts continue to the next
request; other errors stop. Success requires every requested exchange to succeed.
There is no indefinite mode, signal/Ctrl+C cancellation or configurable payload
in this slice. Timeouts and pacing keep existing tasks runnable.

## Inspection

Build and boot normally; `net: lo` reports that the worker was created.
Debugger inspection can check `net_loopback`, the queue/counters in
`'kernel/net/interface.c'::loopback`, and
`'kernel/net/packet.c'::live_packets`. In an ordinary idle boot the queue and
live-packet count are zero and `worker_wait` is published. The `pending` array
in `kernel/net/echo.c` shows queued, sent and completed calls; after ping exits
all slots should be ECHO_FREE, with no retained waiter.

For manual calls, use the BSP/IF=0 pre-scheduling stop described in
[GDB](gdb.md), with TCG for inferior calls. `net_icmp_echo_send` exercises the
normal transmit path without hand-encoding headers. For example, a zero-payload
request needs no debugger-owned buffer:

```gdb
set scheduler-locking on
p net_icmp_echo_send(0x7f000001, 1, 1, 0, 0)
set scheduler-locking off
hbreak net_icmp_receive
continue
```

A breakpoint at `net_icmp_receive` observes the request and then the reply on
the worker stack. Inspect `source`, `destination`, `message` and `length`; the
ICMP type is 8 for a request and 0 for a reply. For payloads, allocate and fill
kernel storage before calling, then free it after submission: the helper copies
it. A GDB string literal otherwise tries to call an unavailable `malloc`.
After continuing past both deliveries, live-packet count should return to zero.

`'kernel/net/ipv4.c'::ipv4_stats` separates malformed, unsupported and nonlocal
input. `'kernel/net/icmp.c'::icmp_stats` records requests, replies and reply-send
failures. Queue and protocol counters are diagnostics, not a public statistics
ABI. Inspect a normal ping call at `net_echo_exchange`, and its BSP completion
at `net_echo_receive`, to follow cross-CPU handoff without injected requests.

## Virtio-net transport

`make run VIRTIO_NET=1` (also accepted by `make debug`) adds one modern
`virtio-net-pci` device backed by QEMU user networking. No TAP device, daemon or
privileged host setup is required. `VIRTIO_NET=0`, the default, explicitly
suppresses QEMU's implicit NIC; loopback still works. Values other than `0` or
`1` are rejected. The network option can be combined with
`VIRTIO_FS_SOCKET=...` using the [usual filesystem setup](virtio-fs.md).

Before AP startup, `virtio_net_prepare` claims the first modern network PCI
function (`1af4:1041`), confirms reset before BAR probing, maps its registers
uncached and negotiates `VIRTIO_F_VERSION_1` and `VIRTIO_NET_F_MAC`.
`VIRTIO_NET_F_STATUS` is accepted when offered; otherwise the link is assumed up.
The MAC must be nonzero and unicast. MAC and link status are sampled between
matching configuration generations, with a one-second deadline.

Queue zero (RX) and queue one (TX) each use 16 direct descriptors and 16 owned
2 KiB buffers. A device must support at least that queue size. Each queue has
one physically contiguous nine-page allocation: a ring page plus eight buffer
pages, totalling 72 KiB for both queues. CPU mappings and DMA physical addresses
are kept distinct. This storage is independent of the software packet budget.
No packed queues, indirect descriptors, offloads, merged RX buffers, control
queue or multiple queue pairs are negotiated.

Boot prepares the queues and posts all receive buffers, with PCI bus mastering
and `DRIVER_OK` clear and MSI-X delivery masked. At the start of the existing
BSP network worker, `virtio_net_start` enables DMA, sets `DRIVER_OK`, unmasks
MSI-X and notifies RX. Worker creation failure leaves the NIC inactive. Table
entry zero routes configuration and both queues to `APIC_VIRTIO_NET_VECTOR`
(35), independently of virtio-fs (34). The IRQ handler only records activity and
wakes the worker; arch acknowledges the APIC. It never touches ring ownership.

### Buffer ownership and bounded work

Each descriptor permanently names one buffer. Publishing an available index
lends it to the device. The worker snapshots a used index, validates the entire
batch (count, IDs, duplicate IDs, ownership and written lengths), then returns
those buffers to CPU ownership. Release/acquire DMA barriers order these
transfers; a full barrier precedes checking device notification suppression.
Available-ring interrupt suppression remains off, so completions can wake an
idle worker without polling.

The modern network header occupies twelve bytes even without merged buffers.
Incoming data must fit that header plus an ordinary 14..1514-byte Ethernet frame
and require neither segmentation nor checksum completion. Invalid packets are
counted and discarded. Valid frames are also counted and discarded for now:
Ethernet/ARP dispatch is the next task. After processing, the worker reposts each
RX buffer. It does not retain DMA pointers or feed external frames into the
loopback-only IP path.

`virtio_net_transmit(frame, length)` is a BSP network-worker interface, with
interrupts enabled. It copies a complete checksummed Ethernet frame, without
FCS, into a free TX buffer and supplies the VirtIO header. All return paths leave
caller storage owned by the caller. `NET_OK` means queued, `NET_QUEUE_FULL` means
all sixteen buffers remain lent, and `NET_UNAVAILABLE` covers an inactive NIC,
link-down or an unstable configuration. TX storage is reusable only after a
checked completion; modern TX used lengths must be zero. No userspace syscall
or ordinary Ethernet caller is introduced in this transport slice.

One turn processes at most sixteen completions from each queue. Reaching either
budget requests a yield after the worker also services echo and loopback work.
Pending notifications are remembered through the existing worker wait handoff.
Idle RX has no timeout, heartbeat or timer polling.

### Failure and link changes

Each outstanding TX buffer has a five-second completion deadline. The worker
sleeps until the earliest echo, TX or exceptional configuration/reset deadline.
Scheduling can make expiry handling late. A timeout never returns DMA ownership.

A broken completion, device status failure, activation failure, changed MAC or
configuration that fails to stabilize stops the NIC. It masks delivery, disables
MSI-X and bus mastering, requests reset and checks completion over at most one
second. Configuration retries and reset checks are spaced by one-millisecond
monotonic deadlines; actual wakes follow scheduler resolution. Each check returns
to the common worker loop, allowing loopback and echo processing to continue.
No lock is held across sleep. Resources stay allocated and mapped until reboot,
even after confirmed reset; there is no reconnect or runtime reclamation.

Ordinary link-status changes are sampled on worker wake. Link-down prevents new
transmissions, while already submitted buffers retain their completion deadlines.
Link-up permits new submissions without rebuilding queues. In the absence of a
pending operation or device interrupt, an idle device failure is not polled for.

Missing hardware is harmless. Failed boot preparation releases storage only
after confirmed reset and MSI-X disable; otherwise ownership is retained.
Neither absent nor failed hardware disables loopback or virtio-fs.

### Inspection and remaining scope

At `boot_start_cpus`, `network.prepared` should be true for supported hardware,
RX should have sixteen outstanding buffers, and TX none. Common status should
be `ACKNOWLEDGE | DRIVER | FEATURES_OK` (`0x0b`), with bus mastering clear and
MSI-X masked. At the worker's first `virtio_net_service`, activation should have
set status `0x0f`, enabled bus mastering and unmasked entry zero/function delivery.
Read MMIO only at individual register widths; do not read the ISR merely for
inspection because that acknowledges pending interrupts.

`network.rx` and `network.tx` expose physical/virtual storage, ownership, ring
indices and outstanding counts. `network.interrupts`, `received`, `dropped`,
`malformed`, `transmitted`, `completed` and `queue_full` are debugger diagnostics,
not a public statistics ABI. An idle worker sleeps with all RX buffers posted
and no TX in flight. Inspect real interrupts at `virtio_net_interrupt`.

For manual frame submission use TCG and the [GDB calling constraints](gdb.md):
allocate caller storage before AP startup, then stop in the network worker after
activation and call `virtio_net_transmit` with a valid frame. The call copies
bytes and does not wait. Continue normally to observe real device completions;
do not call from an arbitrary stopped task or IRQ. No boot-time packets or
validation hooks are built into the driver.

There is still no external IP route or address configuration. Ethernet, ARP and
manual IPv4 setup follow. QEMU's router at `10.0.2.2` is the intended first
external ping target; ordinary Internet ICMP has additional backend limitations.

References: [VirtIO 1.4 network device and PCI transport](https://docs.oasis-open.org/virtio/virtio/v1.4/cs01/virtio-v1.4-cs01.pdf),
[QEMU user networking](https://www.qemu.org/docs/master/system/devices/net.html).
