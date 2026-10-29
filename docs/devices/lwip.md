# lwIP TCP integration

Caelum embeds the pinned IPv4/TCP subset from
[pyxis-lwip](https://git.internal/PyxisOS/pyxis-lwip) at `third_party/lwip`.
[UPSTREAM.md](https://git.internal/PyxisOS/pyxis-lwip/src/branch/main/UPSTREAM.md)
records lwIP 2.2.1's upstream revision, license and local adaptations.
Configuration, freestanding headers and allocator/clock hooks belong to Pyxis
under `kernel/net/lwip`. Ordinary `make kernel` and `make image` include the port;
there is no separate investigation target or alternative runtime mode.

```sh
git submodule update --init third_party/lwip
make image
```

The packet bridge and native CONNECT/LISTEN capabilities use immutable interface
scopes. Trusted boot setup mints factories over explicit retained IDs; normal
session and shell launches delegate CONNECT within their declared scope to
foreground/background children. Servers can instead receive a trusted-created listener with
INSPECT/ACCEPT rights. Streams expose INSPECT, READ, WRITE, SHUTDOWN_WRITE and ABORT.
The [tcp and ttcp utilities](tcp.md) consume these interfaces.
Traffic to closed ports still receives lwIP's normal reset response.

## Why lwIP

lwIP was chosen for the outbound TCP milestone over three alternatives. A native
TCP stack would fit the existing ownership model directly, but Pyxis would own
all protocol recovery, interoperability and maintenance. smoltcp is 0BSD and
needs no heap, but it is Rust, so this C kernel would need a Rust build and an
FFI boundary. picoTCP publishes GPLv2/GPLv3 terms, a licensing choice that was
not to be made incidentally. lwIP is BSD-licensed C with a single-context
callback mode (`NO_SYS=1`); its IP, packet-buffer, timer and resource policies
needed the deliberate adapter described below. The
[comparison](https://git.internal/PyxisOS/pyxis-os/src/commit/93851aebce74c71ceea93774c4d97e01bc2a60e7/docs/wip/tcp-stack-comparison.md)
records the source review.

## Worker and memory ownership

All lwIP entry points, callbacks, allocation and frees run in the existing BSP
network worker with IF=1. The context assertion checks that task's entry, BSP
identity and interrupt state. AP callers and interrupt handlers cannot enter the
raw API. Allocation briefly disables interrupts around the existing kernel heap,
then restores them. calloc checks multiplication overflow. A shared aligned
allocation header accounts requested bytes to the 64 MiB native TCP/UDP allowance,
including packets, segment/PCB/timer metadata and connection/call records.
`kernel/net/storage.c` owns actual, protocol and reserved-window accounting under
one short guard that never covers heap work. Ordinary storage plus reserved
windows plus 64 KiB control capacity must fit; protocol allocations consume those
preclaimed credits instead of being charged twice. New records/transmit data
cannot spend ingress credits. A timer-split hook keeps copied transmit payload
ordinary even inside timer processing. There is no parallel accounting authority. TLSF overhead, factory/readiness metadata,
and the separately bounded common software-packet/NIC/DMA storage are excluded.
There is no host libc, second heap or extra network worker.

The profile uses NO_SYS callbacks, IPv4 and TCP. UDP, ICMP, DNS, ARP, Ethernet,
IPv6, forwarding, IP fragmentation/reassembly, upstream loopback queues and
socket/netconn implementations are disabled in lwIP. Caelum retains its existing
UDP, ICMP, DNS, ARP and NIC paths. Both Caelum and lwIP reject IPv4 options in
this profile. Private headers and `-nostdinc` keep host headers out of the build.

Caelum first validates incoming IPv4 shape, checksum, addresses and ingress
interface. For TCP it copies the complete datagram into an owned RAM pbuf and
calls `ip4_input` on that interface's projection, which consumes it. Both driver
RX and deferred local delivery preserve the native interface identity. No DMA or
shared receive buffer is lent to lwIP. TCP checksums and segment validation
remain lwIP's responsibility.

The netif output callback borrows the pbuf chain only during the call. It copies
the whole IPv4 datagram to a Caelum packet, then submits it without rewriting its
headers. Submission validates shape/checksum and current source/route authority.
Success consumes the copy; failure leaves it for the callback to free. This is
an internal producer boundary, not a userspace raw-packet API.

## Routing and queued copies

Each retained logical interface has one netif projection, including `lo`.
Definition backing and projection storage are prepared on the BSP before the
worker publishes them. The projection's `state` points to its native interface,
which retains the projection. Neither owns a second route database, gateway
policy, ARP namespace or address epoch. The dependency's 255 nonzero 8-bit netif
indices bound simultaneous projections, including `lo`; public interface IDs
remain 64-bit and never recycle within a boot. Protocol/connection budgets remain
global rather than multiplied by interface count.

Public CONNECT (including native completion CONNECT) and LISTEN capture the held
factory scope and optional `net_path` restrictions. They apply the
[native route policy](networking.md#routing-and-configuration) before transport
effects. Connected-prefix ties follow definition order; an unrestricted gateway
route uses the selected system default. A scoped or pinned operation cannot fall
back to an unauthorized NIC, including through host-local delivery. Existing
connections retain their chosen interface when global policy changes.

The patched source-route hook is authoritative even when it rejects a route;
lwIP cannot fall back to a different interface. Input-triggered routing stays on
the ingress context. Active opens and exact listeners retain the native
interface and address epoch and bind their PCB to its projection; accepted
children inherit that identity. Output rechecks the selected projection, route
and connection epoch before submission. Local-address acceptance includes all
of Caelum's 127/8 range, not just 127.0.0.1.

Numerical NIC address replacement or clear retires the native address epoch.
The worker invalidates affected live streams and exact listeners before calling
lwIP's public address setter; a listener cannot follow a replacement address or
revive when an old address returns. Same-address renewal and prefix/gateway
changes keep the epoch and revalidate unsent work against current routing.
TIME_WAIT retains its tuple until normal expiry, but output from its retired
address epoch is refused, including ACKs after an A→B→A reassignment.

PCBs are bound to their projection with `tcp_bind_netif`, so lwIP can select
that netif without calling the route hook. A route/carrier failure between
worker passes is caught by `caelum_lwip_output`, after lwIP has updated its
retransmission/RTT bookkeeping. The next native service pass applies the
existing failure/abort rule. TCP WRITE still reports copied-byte acceptance,
not packet delivery; no automatic replay is added.

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
modulo 32 bits, following RFC 9293. A different key hashes the destination and
local address into a fixed offset. Following RFC 6056 algorithm 3, an advancing
global counter is added to that offset within 49152–65535, replacing independently
random starts that can immediately reuse a passive-closed tuple at its peer.
Each pending bind keeps its own contiguous cursor across worker turns, so
interleaved binds cannot make its scan repeat candidates. Occupied candidates
advance both that cursor and the global counter, preserving sequential progress
past collisions. Other endpoints and pending scans share counter progress;
this is not a timed reuse quarantine. The counter never wraps.
The secret offset separates endpoint sequences, but ports to an observed endpoint
advance predictably. Keys are never logged; debugger inspection should not
disclose them either. See [RFC 6056 section 3.3.3](https://www.rfc-editor.org/rfc/rfc6056.html#section-3.3.3).

`net_tcp_prepare` is worker-only and creates an owned pre-SYN record/PCB with
route and deadline. Binding resumes over at most sixteen unique candidates per
worker visit, across the full ephemeral namespace if necessary. Each turn checks
cancellation, source epoch, route and deadline; a SYN starts only after binding.
Reuse stays disabled, including TIME_WAIT. The provisional grant remains private
until the handshake completes; failure synchronously aborts it before reporting.
No small global record/listener count remains: actual storage shares the 64 MiB
allowance and exhaustion does not evict another connection.

The configured receive window and send payload budget are each
65,535 bytes. Transmit payload is allocated as needed. Before SYN/passive
acceptance, each stream preallocates its receive ring and reserves an ingress
window. Listener conversion releases unused stream backing; other reservations
survive TIME_WAIT and release after PCB/pbuf teardown has returned. These are
payload limits, not total heap limits; metadata is accounted separately.

`net_tcp_release` consumes the sole external ownership reference. Handle copies
share it through object references. Release normally aborts. If an explicit
write shutdown was committed and no received bytes remain unread, teardown
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

Native [tcp and ttcp utilities](tcp.md) exercise these operations. A POSIX sockets
layer remains outside this implementation.

## Native active open

`include/abi/tcp.h` defines `PROTOCOL_TCP_SERVICE` / CONNECT and `PROTOCOL_TCP` /
INSPECT, ABORT and SHUTDOWN_WRITE. Libpyxis exports matching `tcp_connect`,
`tcp_inspect`, `tcp_abort` and `tcp_shutdown_write` helpers in `<tcp.h>`, taking
explicit borrowed handles. The caller owns a successful CONNECT handle; normal copy/restrict/close operations apply.
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
storage before submitting work. Charged dynamic records cover CONNECT, LISTEN,
ACCEPT, INSPECT, ABORT and SHUTDOWN_WRITE, including completed replies until
callers resume. Records
contain copied arguments and stable wait metadata, never user or syscall-stack
buffers. A parked
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
allocating or consuming a call slot. Final release aborts unless write shutdown
was committed and no received bytes remain unread. Transport records remain
worker-owned until safe to reclaim.

INSPECT requires its own right and returns the tuple, CONNECTED / PEER_CLOSED /
CLOSED state and latched terminal status, including after reset or abort. An orderly
peer FIN has CALL_OK status; READ drains preceding bytes before returning EOF.
ABORT requires its separate right, affects all copies and is idempotent. A local
abort records ENDPOINT_CLOSED unless an earlier terminal error was already latched.
It frees queued transport data and makes a best-effort reset; local completion does
not wait for the peer. Shared-state operations serialize on the sole worker.

## Native passive open

LISTEN reserves a listener object/handle before binding an exact permitted local
source/interface and nonzero port; no wildcard TCP listener is added. Listener
records consume the shared byte allowance, without a separate global count.
The pinned lwIP listen conversion transfers extension arguments to the smaller PCB
and clears the old PCB's extensions before freeing it; the record then points to
the returned listener. Listener service never accesses full-stream PCB fields.

The backlog is enabled with four slots. lwIP charges SYN_RCVD connections at SYN
admission. Each new PCB gets its own generation and record from the allocation
hook. The passive-open extension restores the child's callback argument, which
lwIP overwrites with the listener's argument, then records its internal listener
owner and ten-second handshake deadline. SYN retransmissions do not restart it.
No published stream or caller table is needed during passive setup.

lwIP releases its backlog charge immediately before the successful accept
callback. That callback uses `tcp_backlog_delayed` to retain the slot, marks the
child established and appends it to the listener's ready queue. Native ACCEPT
reserves its own stream object and caller handle, rechecks the deadline, then
removes one ready child and calls `tcp_backlog_accepted`. Publication transfers
the child from listener ownership to independent stream ownership. LISTEN/ACCEPT
share charged control-record ownership with CONNECT and stream controls.

Child PCB destruction removes its listener accounting and ready-queue link while
canceling only that child's packet generation. Final listener release aborts
unaccepted children before calling `tcp_close` on the listener. Accepted streams
have no internal listener owner and survive that close. Exact-address listeners
are closed before lwIP's address setter could retarget them. Reclamation remains
worker-deferred; pending calls borrow no userspace data buffers.

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
Failures leave data and count unchanged. Dynamic read records hold up to
4 KiB each plus metadata, charged to the shared storage allowance.
One outstanding read per shared stream includes completed-but-uncollected replies;
other readers get BUSY. Allocation/allowance exhaustion returns NO_MEMORY.
Successful records own an extra
object reference until the worker has returned credit and reclaimed the slot.
A failed read has no receive credit; its caller clears the slot and releases the
extra reference before returning, including on WOULD_BLOCK.
The admitted call keeps the stream object alive while the caller parks, even if a
sibling closes the handle. A sibling's read on the same stream gets BUSY. The
data is copied out after wake, so a sibling that unmapped the output buffer meanwhile
makes the read return BAD_BUFFER; the consumed bytes are not restored.

lwIP validates sequence space and trims duplicates/overlaps. The receive callback
copies ordered data into its already admitted 65,535-byte ring and frees the
incoming pbuf. Ring storage remains until failure/final release or until peer FIN
and the last buffered byte have been consumed, independently of reservation
teardown. Reassembly uses lwIP's out-of-order queue, capped at 65,535 bytes and
sixteen pbufs; excess is dropped for retransmission. The current sixteen full
1,500-byte packet allocations plus segment/header/alignment overhead fit within
one reserved receive window; the current packet and control work have the
separate transient allowance. The receive window covers ordered unread bytes, successful
read replies not yet collected, and out-of-order sequence space together. READ
moves bytes from the ring to a slot without calling `tcp_recved`; only after the
caller resumes and collects the result does the worker return that credit.

The 65,535-byte window bounds retained payload, not total allocated memory. The
preallocated ring and ordinary read slots can coexist with reserved reassembly pbufs. Ring allocations,
pbuf storage/headers, segment records and allocator headers are all included in
the shared TCP/UDP allowance; common packet/DMA budgets remain separate. Packet
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
close without write shutdown still aborts; close must not be used as a flush or
delivery acknowledgment. Explicit write shutdown is described below.

Dynamic write records reserve the submitted payload and metadata under the
shared TCP/UDP allowance. They include staging, waiting and completed calls
until callers collect them; no user or private-stack pointer is borrowed. One
writer per shared stream is allowed (otherwise BUSY), independently of its
reader. Allocation/allowance exhaustion returns NO_MEMORY. A full send byte/pbuf budget parks the writer until the worker
processes ACKs, a terminal event or its original deadline, at most 30 seconds
ahead. A timeout leaves data from earlier successful calls queued.

The 65,535-byte per-connection send budget covers accepted unsent and
unacknowledged payload together. lwIP additionally caps queued pbufs with its
existing `TCP_SND_QUEUELEN` formula (180 for this profile); heap accounting includes segment
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

`TCP_MSS` is 1460 bytes for the 1500-byte interface MTU; lwIP advertises it and
reduces the send size to the peer's MSS at establishment. lwIP sets that send
MSS only when the connection is established, after `LWIP_HOOK_TCP_PCB_ALLOCATED`
has run, so `tcp_connection_established` clamps it in the connect and accept
callbacks instead. When the connection's `net_ipv4_route_on` reaches the peer through a gateway,
or fails, the connection sends at most `TCP_ROUTED_MSS`, 536 bytes. The initial
congestion window lwIP computed for 1460, 4380 bytes, is left as is; it is
within RFC 6928's initial window for 536. The MSS/window settings are unchanged
by the multiple-interface projection cut.
The same function disables Nagle on every connection.
This is not path-MTU discovery. No window scaling, timestamps, SACK,
fragmentation or new ICMP error/PMTU handling is added.

## Readiness and nonblocking attempts

The [readiness ABI](tcp.md#readiness-and-transfer-attempts) observes lwIP state
only in the network worker. The BSP service worker forwards each caller's typed
TCP/mixed request through an incoming queue; the network worker owns its active wait list,
retained objects and state recheck. Completion unlinks the request and drops all
retained objects before waking its caller. The existing per-process BSP request
allocation fits sixteen interests without a new allocation or global waiter cap.
Wait deadlines join the worker's existing earliest-deadline calculation.
Terminal-only waits use the independent BSP readiness worker; mixed waits observe
terminal queues under their lock and receive notifications after terminal mutations.

Ordinary readiness suppresses a direction already claimed by another operation;
terminal flags remain observable. Callers notify the worker when they release
completed operation slots, so a wait cannot miss a direction becoming usable.
The result still reserves nothing: a competing holder can consume readiness
before a transfer attempt. Deadline-free TRY_ACCEPT/READ/WRITE reuse the bounded
operation slots but return WOULD_BLOCK instead of waiting for an absent pending
connection, input or send capacity. That result leaves no operation slot,
provisional accept resources or wait registration behind. Successful reads keep
the existing receive-credit handoff after collection.

## Write shutdown and teardown

`TCP_SHUTDOWN_WRITE` is a header-only control operation with its own right and no
reply bytes. Libpyxis `tcp_shutdown_write` takes an explicit stream handle. The
worker commits shared write shutdown, schedules FIN after previously accepted
bytes and returns without waiting for allocation, transmission or acknowledgment.
Reads remain usable. A queued or blocked WRITE that has not accepted bytes fails
ENDPOINT_CLOSED; a completed WRITE retains its accepted count. Zero-length I/O
retains its existing no-op contract. Copies share the shutdown state.

A committed shutdown remains idempotent, even after a later transport failure;
repeating it does not clear the latched error. A first shutdown on an already
failed stream reports that failure. This uses the shared dynamic control
records, not a new queue or syscall. INSPECT independently reports WRITE_SHUTDOWN and
PEER_FIN flags. PEER_FIN can coexist with unread bytes, and CLOSED describes the
transport, including TIME_WAIT, rather than exhaustion of the receive buffer.

The adapter records write shutdown separately from lwIP's FIN-queued flag. Under
memory pressure lwIP can accept shutdown while deferring FIN allocation; new
writes must already be closed. Its existing timer retries that allocation. The
120-second no-progress deadline includes deferred FIN, as well as queued data/FIN;
repeated shutdown does not reset it. Owned connections whose FIN was acknowledged
can continue waiting for the peer while applications use bounded READ calls.

Normal LAST_ACK completion arrives through lwIP's error callback as ERR_CLSD.
For an orderly two-sided shutdown this is not a native error: buffered data stays
readable, followed by EOF, after the PCB is destroyed. Likewise, entering
TIME_WAIT does not discard unread bytes. The ring is released once those bytes
are consumed; no receive credit is sent through a TIME_WAIT or destroyed PCB.
Real resets/failures still discard unread bytes and remain visible as errors.

Final handle release without shutdown, or with unread ordered/reassembly data,
aborts. Otherwise the worker closes receive ownership and permits graceful
teardown for at most 60 seconds; lwIP's existing 20-second timeout for fully
closed FIN_WAIT_2 connections can reclaim one earlier. Data arriving after owner
release causes abort;
no process, task or user mapping is kept alive for transport teardown. Completed
READ slots hold the object until collected and credited, so final release cannot
race uncollected read data.

lwIP owns FIN ordering, acknowledgment, retransmission, simultaneous close and
TIME_WAIT. Its two-minute TIME_WAIT retains the tuple and charged transport
record even after final close, explicit ABORT or address/link invalidation. ABORT
can discard application data and latch ENDPOINT_CLOSED, but cannot free that tuple
early. Once the timer expires, PCB destruction detaches the transport and the
worker reclaims the record when no object owns it. A retained handle can continue
reporting closure/EOF afterward; repeated shutdown does not recreate transport.
