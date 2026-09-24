# Networking: local IPv4 and ICMP echo

The kernel has one system-wide networking worker and a boot-lifetime interface
named `lo`. Interfaces and future addresses/routes are shared across spaces;
capability-mediated access will not by itself provide network isolation.
The implementation is in `kernel/net`, with kernel interfaces in
`include/kernel/net`. There is no userspace network ABI yet.

The worker validates IPv4 packets and handles ICMP echo requests/replies over
loopback. There is no userspace ping, external route or configurable address yet.
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
validated, counted and consumed; matching them to application requests, deadlines
and userspace completion belongs to the next task. Transmit success never claims
that a peer has answered.

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

The worker runs with interrupts enabled. It disables them briefly to check the
queue, publish its event wait, dequeue, allocate/queue an echo reply or release
packet storage. Receive parsing and validation run with IF=1, outside interrupt
entry. A sender detaches the wait pointer before waking it. Empty-queue checking and wait
publication share the same BSP/IF=0 exclusion, preserving wake-before-park.
Idle networking sleeps on an event instead of polling.

After eight deliveries the worker yields to the ready queue using a past
deadline. This bounds each batch without adding a timer sleep. Local transmission
never recursively enters receive processing. The current path has no Ethernet
header, MAC address, ARP, PCI or DMA dependency.

## Inspection

Build and boot normally; `net: lo` reports that the worker was created.
Debugger inspection can check `net_loopback`, the queue/counters in
`'kernel/net/interface.c'::loopback`, and
`'kernel/net/packet.c'::live_packets`. In an ordinary idle boot the queue and
live-packet count are zero and the worker has published its wait.

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
ABI or a substitute for future application reply matching.
