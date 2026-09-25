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

The packet bridge and native outbound CONNECT capability are live. Init receives
`tcp` authority, explicitly delegated through session and shell to foreground and
background children. Streams expose INSPECT, READ, WRITE and ABORT; write
shutdown remains in the next [TCP tasks](wip/tcp.md). No listener is exposed.
Traffic to closed ports still receives lwIP's normal reset response.

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
sufficient. CONNECT uses this internal API; callers cannot choose an ISN or source address
through the public interface.

The 32-record admission limit includes prepared, closing and TIME_WAIT records,
and terminal records still retained by an owner. Exhaustion does not evict any
connection. The configured receive window and send payload budget are each
16 KiB. Payload is allocated as needed, not eagerly reserved at preparation.
These are payload limits, not total heap limits: metadata is accounted separately.
Receive storage and window accounting are described below.

`net_tcp_release` consumes the sole external ownership reference. Handle copies
share it through object references. Release normally aborts. If an explicit
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

Graceful shutdown remains in the TCP milestone. Its interoperability checks
follow when the operation becomes usable. Listening and a POSIX sockets layer remain outside the milestone.

## Native active open

`include/abi/tcp.h` defines `PROTOCOL_TCP_SERVICE` / CONNECT and `PROTOCOL_TCP` /
INSPECT and ABORT. Libpyxis exports matching `tcp_connect`, `tcp_inspect` and
`tcp_abort` helpers in `<tcp.h>`, taking explicit borrowed handles. The caller
owns a successful CONNECT handle; normal copy/restrict/close operations apply.
No new syscall, version field or socket layer is introduced.

CONNECT takes a numeric IPv4 destination, nonzero port and absolute monotonic
deadline at most 30 seconds ahead. It returns local/remote tuple metadata and a
stream handle only after the handshake. Route selection, ARP, lwIP's bounded SYN
retries and preparation share that deadline. Entropy not yet ready or unavailable
returns UNAVAILABLE immediately; the worker never waits for the boot entropy task.
Missing route, refusal, timeout and allocation/queue exhaustion remain distinct.
`CALL_CONNECTION_REFUSED` denotes a validated reset before establishment;
`CALL_CONNECTION_RESET` denotes one afterward. The pinned source adaptation
requires ACK on a SYN-SENT reset, not just a matching numeric ACK field.

The facade validates request size, rights, reserved fields and writable reply
storage before submitting work. Eight shared slots cover CONNECT, INSPECT and
ABORT, including completed replies until callers resume. Slots contain copied
arguments and stable wait metadata, never user or syscall-stack buffers. A parked
CONNECT caller lends its kernel-owned capability table exclusively to the worker.
The worker allocates the stream wrapper and installs a private table entry before
SYN; the caller cannot observe it while parked. Failure aborts transport and closes
that entry before completing the call. Only successful completion publishes its
handle. A connection can subsequently fail before the caller is scheduled again;
INSPECT reports its current state. Failed native calls and libpyxis helpers leave
reply storage untouched.

Stream wrappers own one external connection reference and are included in lwIP
allocation accounting. Copies share the wrapper; closing one does not close other
copies. Final object retirement hands the wrapper to the network worker without
allocating or consuming a call slot. Until explicit write shutdown is implemented,
final release aborts. Transport records remain worker-owned until safe to reclaim.

INSPECT requires its own right and returns the tuple, CONNECTED / PEER_CLOSED /
CLOSED state and latched terminal status, including after reset or abort. An orderly
peer FIN has CALL_OK status; READ drains preceding bytes before returning EOF.
ABORT requires its separate right, affects all copies and is idempotent. A local
abort records ENDPOINT_CLOSED unless an earlier terminal error was already latched.
It frees queued transport data and makes a best-effort reset; local completion does
not wait for the peer. Shared-state operations serialize on the sole worker.

## Native receive stream

`TCP_READ` requires `TCP_RIGHT_READ`. Libpyxis `tcp_read` takes an explicit stream,
output buffer/capacity, absolute monotonic deadline and transfer-count reply.
Capacity is clamped to 4 KiB; available ordered bytes return immediately, possibly
short. Otherwise the caller parks until data, peer FIN, terminal failure or its
original deadline (at most 30 seconds ahead). A nonempty read returns zero only
after peer FIN and preceding bytes. Zero capacity is a no-op after authority,
buffer and deadline validation, including on a terminal stream; it does not poll
EOF. Timeout consumes nothing and preserves the connection.

The facade checks both output ranges and rejects overlap before submitting work.
Failures leave data and count unchanged. Sixteen static read slots hold up to
4 KiB each, plus call metadata; this fixed storage is separate from `lwip_memory`.
One outstanding read per shared stream includes completed-but-uncollected replies;
other readers get BUSY. Slot exhaustion returns QUEUE_FULL. Slots own an extra
object reference until the worker has returned credit and reclaimed the slot.
The sole caller task's existing grant and mappings remain live while it parks.

lwIP validates sequence space and trims duplicates/overlaps. The receive callback
copies ordered data into a 16 KiB ring, allocated on first payload receipt, and
frees the incoming pbuf. Allocation failure aborts with NO_MEMORY rather than
silently discarding acknowledged data. Ring storage remains until failure/final
release; an idle stream that has never received data allocates no ring. Reassembly
uses lwIP's out-of-order queue, capped at 16 KiB and sixteen pbufs; excess is dropped
for retransmission. The receive window covers ordered unread bytes, successful
read replies not yet collected, and out-of-order sequence space together. READ
moves bytes from the ring to a slot without calling `tcp_recved`; only after the
caller resumes and collects the result does the worker return that credit.

The 16 KiB window bounds retained payload, not total allocated memory. Reserved
ring space and read slots can coexist with reassembly pbufs. Ring allocations,
pbuf storage/headers, segment records and allocator headers are all included in
`lwip_memory`; static slots and shared packet/DMA budgets are separate. Packet
processing can transiently own the incoming packet before enforcing reassembly
limits. Ordered tiny segments do not accumulate per-packet metadata in the ring.

Peer FIN preserves unread bytes. Reset, ABORT and permanent local failure discard
unread ring/reassembly data and wake pending reads with the latched error. A read
already completed successfully before the failure retains its result. Failures
never fabricate clean EOF. The sole worker serializes completion, control calls,
packet processing and timers; it never keeps user pointers or syscall-stack
buffers. Completed reads cannot return new receive credit merely because their
caller has been woken but has not yet run.

## Native send stream

`TCP_WRITE` requires `TCP_RIGHT_WRITE`. Libpyxis `tcp_write` borrows an explicit
stream, input buffer/length and absolute monotonic deadline, and returns a count.
The kernel checks the count output and copies the effective input before queueing
work. Length is clamped to 4 KiB. A nonempty successful call always accepts a
positive count, possibly short; the caller retries only the remaining suffix.
Zero length validates authority, buffers and deadline but does not poll transport
state. Failed calls accept nothing and leave the count output unchanged.

Success means that lwIP copied the bytes into bounded send storage. It is neither
a peer acknowledgment nor proof that the peer application read them. After that
commit, output errors cannot turn the call into a failure that invites duplicate
sending: transport retries retain ownership, and a later terminal failure remains
visible through INSPECT and subsequent I/O. Peer FIN does not stop WRITE. Final
close still aborts; it must not be used to flush accepted data. Explicit graceful
write shutdown follows in the next task.

Sixteen static write slots each reserve 4 KiB plus metadata, separately from the
read slots and `lwip_memory`. They include staging, waiting and completed calls
until their callers collect them. The caller's grant keeps the stream alive;
slots contain no user or private-stack pointer. One writer per shared stream is
allowed (otherwise BUSY), independently of its reader. Global slot exhaustion
returns QUEUE_FULL. A full send byte/pbuf budget parks the writer until the worker
processes ACKs, a terminal event or its original deadline, at most 30 seconds
ahead. A timeout leaves data from earlier successful calls queued.

The 16 KiB per-connection send budget covers accepted unsent and unacknowledged
payload together. lwIP additionally caps queued pbufs with its existing
`TCP_SND_QUEUELEN` formula (123 for this profile); heap accounting includes segment
records, pbuf headers, spare tail capacity and allocator headers. Static staging
copies and shared ARP/local/NIC packet copies have separate bounded storage.
There is no second adapter retransmission queue. If a whole attempted copy does
not fit, bounded halving attempts permit a short result; lwIP rolls failed copies
back. With byte/pbuf room but not enough memory for even one byte, return NO_MEMORY
without aborting earlier accepted writes.

lwIP owns segmentation, ACK validation, congestion control, RTT/RTO estimation,
retransmission backoff, Nagle coalescing and zero-window probes/reopening. Its
existing timers and packet handling run in the sole worker before pending native
calls are serviced. Caelum's separate 120-second no-ACK-progress deadline bounds
retained send data; successful WRITE calls do not reset that deadline. Pending
WRITE deadlines join the worker's earliest-deadline sleep, without polling loops
or a second networking task.

`TCP_MSS` is explicitly 536 bytes, retaining the conservative existing default.
The peer's advertised MSS and local interface MTU can reduce the effective send
size further. This ceiling is not path-MTU discovery or a guarantee for every
route. No window scaling, timestamps, SACK, fragmentation or new ICMP error/PMTU
handling is added. Throughput tuning belongs after the initial interoperability
and graceful-lifecycle work.
