# lwIP TCP integration

Caelum embeds the pinned IPv4/TCP subset from
[pyxis-lwip](https://git.internal/chronium/pyxis-lwip) at `third_party/lwip`.
[UPSTREAM.md](https://git.internal/chronium/pyxis-lwip/src/branch/main/UPSTREAM.md)
records lwIP 2.2.1's upstream revision, license and local adaptations.
Configuration, freestanding headers and allocator/clock hooks belong to Pyxis
under `kernel/net/lwip`. Ordinary `make kernel` and `make image` include the port;
there is no separate investigation target or alternative runtime mode.

```sh
git submodule update --init third_party/lwip
make image
```

The packet bridge is live, but no connection capability, active open or listener
is exposed yet. Traffic to closed TCP ports reaches lwIP and can receive its
normal reset response. The next [TCP tasks](wip/tcp.md) add bounded connection
ownership and entropy-backed transport identity before exposing CONNECT. A
private ISN hook rejects accidental internal opens with a diagnostic rather than
silently using upstream's predictable default. This guard is not an application
error path; there are no normal callers that create/open connections yet.

## Worker and memory ownership

All lwIP entry points, callbacks, allocation and frees run in the existing BSP
network worker with IF=1. The context assertion checks that task's entry, BSP
identity and interrupt state. AP callers and interrupt handlers cannot enter the
raw API. Allocation briefly disables interrupts around the existing kernel heap,
then restores them. calloc checks multiplication overflow. No host libc, extra
heap, bounded allocation registry or second networking thread is introduced.

The profile uses NO_SYS callbacks, IPv4 and TCP. UDP, ICMP, DNS, ARP, Ethernet,
IPv6, forwarding, IP fragmentation/reassembly, upstream loopback queues and
socket/netconn implementations are disabled in lwIP. Caelum retains its existing
UDP, ICMP, DNS, ARP and NIC paths. Both Caelum and lwIP reject IPv4 options in
this profile. Private headers and `-nostdinc` keep host headers out of the build.

Caelum first validates incoming IPv4 shape, checksum, addresses and ingress
interface. For TCP it copies the complete datagram into an owned RAM pbuf and
calls `ip4_input`, which consumes it. No DMA or shared receive buffer is lent to
lwIP. TCP checksums and segment validation remain lwIP's responsibility.

The netif output callback borrows the pbuf chain only during the call. It copies
the whole IPv4 datagram to a Caelum packet, then submits it without rewriting its
headers. Submission validates shape/checksum and current source/route authority.
Success consumes the copy; failure leaves it for the callback to free. This is
an internal producer boundary, not a userspace raw-packet API.

## Routing and queued copies

Two netif descriptors project loopback and the assigned NIC address. They do
not own routes, gateway policy or ARP. The patched source-route hook is
authoritative even when it rejects a route; lwIP cannot fall back to a different
interface. Output rechecks that choice. Local-address acceptance includes all
of Caelum's 127/8 range, not just 127.0.0.1. NIC address changes use lwIP's public
address setter, which invalidates connections bound to the old address.

Output back to the local host always uses Caelum's deferred queue. It never
recursively invokes lwIP input from inside output. This also makes the scoped
output-generation context safe across callbacks and kernel task preemption.

Each allocated TCP PCB gets a heap-owned generation record through lwIP's
existing extension arguments. Generation zero denotes stateless control output;
nonzero generations are never reused, including after PCB address reuse.
Generation exhaustion fails allocation. No registry is needed. A narrow output
hook passes the generation through IPv4 construction to the copied packet.

The PCB destruction callback cancels every unsent copy for that generation in
ARP and the local queue before freeing its record. It also runs when TIME_WAIT
finally expires. Application handles must not free this record themselves.
No queued copy or completion retains a PCB pointer. Bytes already copied into
NIC DMA storage cannot be recalled. An abort's reset is best effort: if still
queued when the PCB is destroyed, it is canceled too.

ARP copies have a three-second lifetime and share the existing 32-packet budget.
Queue/NIC acceptance is not a peer ACK. Asynchronous drop/expiry needs no callback
into a possibly retired PCB; lwIP keeps its own retransmission state. Unsolicited
stateless resets carry no connection generation and remain bounded by the same
packet budget and ARP deadline. Control traffic is not a new unbounded queue.

## Timers and remaining limits

The worker services lwIP timers alongside its existing protocol work and includes
`sys_timeouts_sleeptime` in its earliest-deadline wait. `sys_now` wraps at 32-bit
milliseconds; only relative delays cross into Caelum's saturating 64-bit
nanosecond deadline calculation. Never reinterpret an absolute lwIP timestamp as
a kernel deadline. Only TCP's cyclic timer is enabled; it reschedules from the
current time, so a delayed worker does not replay an unbounded timer backlog.
With no active/closing connections, lwIP adds no periodic idle wakeup.

Allocation failure no longer evicts active/closing/TIME_WAIT PCBs. This does not
by itself establish connection or byte budgets. Admission, entropy-backed ISNs,
random ephemeral binding, link-failure invalidation of live connections,
transport memory accounting and native stream semantics remain in the TCP
milestone. Listening and a POSIX sockets layer remain outside it.

Build/boot and closed-port traffic establish the packet bridge, not handshake,
retransmission, stream delivery or teardown interoperability. Those need ordinary
peer exchanges as the remaining connection tasks become usable.
