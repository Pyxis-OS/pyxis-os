# Network interfaces and loopback delivery

The kernel has one system-wide networking worker and a boot-lifetime interface
named `lo`. Interfaces and future addresses/routes are shared across spaces;
capability-mediated access will not by itself provide network isolation.
The implementation is in `kernel/net`, with kernel interfaces in
`include/kernel/net`. There is no userspace network ABI yet.

This first slice provides packet ownership and deferred local delivery only.
There is no IPv4 parser, address configuration, ICMP or ping. The worker counts
and discards received packets as unsupported until the next protocol task.
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
The worker takes ownership on dequeue and releases unsupported input. No
reference counting, borrowed payloads, packet registry or device buffer sharing
is introduced. The sole interface is a constant descriptor, not a dynamically
registered or removable device.

## Execution and initialization

Call `net_init` once on the BSP with interrupts disabled, after `task_init` and
before scheduling begins. It creates the BSP worker; failure leaves sends
unavailable and logs a diagnostic while ordinary boot continues.

Allocation, release and transmission currently require BSP task or initialization
context with IF=0. They are not AP, user-pointer or interrupt-entry interfaces.
Future application calls will need the existing kind of explicit cross-CPU
ownership handoff; this slice adds no scheduler request state.

The worker runs with interrupts enabled. It disables them briefly to check the
queue, publish its event wait, dequeue or release packet storage. A sender
detaches the wait pointer before waking it. Empty-queue checking and wait
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
[GDB](gdb.md), with TCG for inferior calls. Allocate a packet, initialize its
payload and inspect the transmit result. On success, leave it to the worker
after resuming; on failure, the debugger caller still owns it. A breakpoint at
`receive_packet` observes deferred delivery on the worker stack. After release
the live-packet count returns to zero. Counters distinguish submissions,
receives, unsupported input and queue-full attempts; they are diagnostics, not
a protocol success report or a public statistics ABI.
