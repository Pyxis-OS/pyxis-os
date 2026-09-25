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

The packet bridge and internal connection preparation are live, but no native
connection capability, active-open caller or listener is exposed yet. Traffic to
closed TCP ports reaches lwIP and can receive its normal reset response. The
next [TCP tasks](wip/tcp.md) expose CONNECT and stream I/O.

## Worker and memory ownership

All lwIP entry points, callbacks, allocation and frees run in the existing BSP
network worker with IF=1. The context assertion checks that task's entry, BSP
identity and interrupt state. AP callers and interrupt handlers cannot enter the
raw API. Allocation briefly disables interrupts around the existing kernel heap,
then restores them. calloc checks multiplication overflow. A small aligned
allocation header accounts requested heap bytes, including pbufs, segment/PCB/
timer metadata and connection records. `lwip_memory` exposes current/peak bytes
and allocation count to GDB; this is accounting, not a separate heap quota.
TLSF overhead and the shared NIC/software-packet budgets remain separate. No host libc, extra
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

Each allocated TCP PCB gets a heap-owned connection record through lwIP's
existing extension arguments. Generation zero denotes stateless control output;
nonzero generations are never reused, including after PCB address reuse.
Generation exhaustion fails allocation. The same record also holds connection
ownership and deadlines; there is no second generation table. A narrow output
hook passes the generation through IPv4 construction to the copied packet.

The PCB destruction callback cancels every unsent copy for that generation in
ARP and the local queue, then detaches the PCB. It also runs when TIME_WAIT
finally expires. The worker frees the record only after callbacks unwind and its
external owner has released it. This matters because lwIP may invoke the error
callback after destroying the PCB. Application handles cannot free it directly.
No queued copy or completion retains a PCB pointer. Bytes already copied into
NIC DMA storage cannot be recalled. An abort's reset is best effort: if still
queued when the PCB is destroyed, it is canceled too.

ARP copies have a three-second lifetime and share the existing 32-packet budget.
Queue/NIC acceptance is not a peer ACK. Asynchronous drop/expiry needs no callback
into a possibly retired PCB; lwIP keeps its own retransmission state. Unsolicited
stateless resets carry no connection generation and remain bounded by the same
packet budget and ARP deadline. Control traffic is not a new unbounded queue.

## Connection preparation and identity

A short-lived BSP task obtains two independent 128-bit keys from VirtIO RNG with
a five-second deadline. It publishes immutable keys before marking identity
ready, clears its temporary copy and exits. The network worker never waits for
entropy. Missing entropy or preparation failure disables new TCP connections
for that boot; UDP, numeric ping and closed-port resets still work. Once seeded,
opening connections needs no further entropy-device requests.

Caelum owns sequence-number and source-port policy. The pinned CC0 SipHash-2-4
reference is in `third_party/siphash`, with its revision and freestanding changes
in `UPSTREAM.md`; it is not part of the lwIP protocol-engine fork. ISNs combine a
secret hash of the address/port tuple with the monotonic four-microsecond clock,
modulo 32 bits, following RFC 9293. A different key hashes the destination, local
address and a never-wrapping attempt counter to choose an ephemeral-port start
and odd stride. The stride visits the entire 49152–65535 range without repeats.
Keys are never logged; debugger inspection should not disclose them either.

`net_tcp_prepare` is worker-only and sends no SYN. It validates a nonzero peer
port and an absolute deadline at most 30 seconds ahead, selects the authoritative
route, allocates one owned record/PCB and explicitly binds an available port.
Failure leaves the output pointer untouched. Reuse is disabled: lwIP checks bound,
active and TIME_WAIT tuples. With at most 32 records, 33 distinct candidates are
sufficient. This internal API will be used by CONNECT; callers cannot choose an
ISN or source address through a public interface.

The 32-record admission limit includes prepared, closing and TIME_WAIT records,
and terminal records still retained by an owner. Exhaustion does not evict any
connection. The configured receive window and send payload budget are each
16 KiB. Payload is allocated as needed, not eagerly reserved at preparation.
These are payload limits, not total heap limits: metadata is accounted separately.
Receive callbacks currently refuse data so lwIP retains it without advancing
credit; native READ and its reassembly accounting are the next receive task.

`net_tcp_release` consumes the sole external ownership reference. Future handle
copies share it through object references. Release normally aborts. If an explicit
write shutdown already queued FIN and no received bytes remain unread, teardown
may continue without user pointers. Detached, unowned records are reclaimed on a
worker pass, never from a lwIP callback.

## Timers and remaining limits

The worker services lwIP and connection deadlines alongside its other protocol
work. `sys_now` wraps at 32-bit milliseconds; only relative lwIP delays cross into
Caelum's saturating 64-bit nanosecond deadline calculation. Never reinterpret an
absolute lwIP timestamp as a kernel deadline. Only TCP's cyclic timer is enabled;
it reschedules from the current time, so a delayed worker does not replay an
unbounded timer backlog. With no connections, lwIP adds no periodic idle wakeup.

Preparation retains the caller deadline until the handshake completes. Outstanding
send data has a separate 120-second progress timeout, refreshed by acknowledgment
progress; idle established streams do not expire. Ownerless graceful teardown
has a 60-second ceiling. TIME_WAIT instead retains lwIP's two-minute duration and
its admission slot, including across link/address changes. No other deadline
shortens it. Address removal, failed routing or an unavailable interface aborts
affected live connections. Queued packet copies are canceled when the PCB dies.

Native operation queues, capability publication and stream I/O remain in the TCP
milestone, including the 4 KiB per-call limit and pending-call budgets. Graceful
close/data interoperability still needs real exchanges as those operations become
usable. Listening and a POSIX sockets layer remain outside the milestone.
