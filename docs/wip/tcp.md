# Outbound TCP streams

Status: agreed next milestone; the contract and budgets below are the proposed
implementation plan. Review each task's remaining decisions before writing its
code. This document does not authorize parallel networking or storage tracks.

## Result and boundary

Open an outbound IPv4 TCP connection from Pyxis, send a request and read the
response through a native stream capability. A small `tcp` utility accepts a
numeric address or hostname and exercises the interface against an ordinary
host service. Applications gain a useful transport for later clients; hosting
the Pyxis website is not this milestone's acceptance criterion.

The project priority is a usable OS for simple everyday work. Editing, building,
running and persisting programs is an especially useful loop. Writable virtio-fs
is a candidate for the next milestone after TCP, with its ownership/authority
checkpoint first. It is not part of this implementation.

The [stack comparison](tcp-stack-comparison.md) led to adopting lwIP as the
IPv4/TCP engine behind Caelum's existing network worker. The
[implemented bridge](../lwip.md) preserves Caelum's route/address authority and
queued-packet ownership. UDP, ICMP, ARP and NIC ownership remain in Caelum.

DNS and application protocols
remain in userspace. No HTTP/TLS client, POSIX sockets layer, listener/accept API,
server daemon, browser port, new syscall or general transport framework is needed.
Use tagged synchronous CALLs and the existing capability lifecycle.

Loopback uses the same routing, packet and TCP paths as the NIC. Without a
listener there is no guest server to connect to yet: this slice checks refusal
on an unused loopback port, not a successful guest-to-guest exchange. Host
services reached through QEMU provide the first successful peers. Listening
and successful local client/server use form a later milestone.

## Proposed native contract

Init receives a `tcp` service grant with CONNECT authority, explicitly delegated
through session/shell launch as with UDP. A numeric destination address, nonzero
port and absolute monotonic deadline identify a CONNECT request. Route selection,
local ephemeral binding and connection state belong to the network worker.
The caller cannot choose TCP sequence numbers or impersonate a local address.

CONNECT returns an owned stream handle only after the handshake succeeds. Prepare
handle capacity and bounded connection storage before starting it; failure or
deadline expiry returns no handle and leaves no live unpublished connection.
The deadline covers preparation, entropy availability, ARP and handshake time.
A remote peer may still have observed packets from a failed attempt.

A stream has separately restrictable INSPECT, READ, WRITE, SHUTDOWN_WRITE and
ABORT rights. Copies refer to the same connection, byte queues and shutdown
state. INSPECT reports local/remote addresses and ports, connection state and
terminal failure. No method discovers startup resources implicitly; libpyxis
helpers take explicit borrowed handles.

| Operation | Completion and ownership |
| --- | --- |
| READ | Return available ordered bytes, up to the supplied capacity. A nonempty read returns zero only after the peer's FIN and all preceding data have been consumed. A read deadline is not EOF. |
| WRITE | Return the positive number of bytes copied into bounded kernel send storage. This is acceptance, not acknowledgment or remote application processing. The caller retries only the unaccepted suffix. |
| SHUTDOWN_WRITE | Stop accepting writes and queue FIN after all accepted bytes. Return once that local state change is committed; reads remain usable. Repeating it succeeds. |
| ABORT | Discard queued bytes, terminate the connection and wake pending operations. Copies are affected too. A best-effort reset does not delay local completion. |
| CLOSE | Release this handle reference using the existing syscall. Only final release retires the object; it is neither a flush nor a delivery acknowledgment. |

READ/WRITE may complete short. If no bytes can be transferred, park until data,
space, a terminal event or the original deadline. A failure result transfers no
bytes and leaves output storage unchanged; any positive transfer is reported as
success. Buffer validation and transfer-count reply validation precede queue
consumption, including when the caller resumes on another CPU. Zero-length calls
are explicit no-ops after handle/right/deadline validation, not a way to poll EOF.

A READ timeout consumes nothing and leaves the stream usable. A WRITE timeout
accepts nothing for that call; bytes accepted by earlier successful calls remain
queued and may still be transmitted. Caller deadlines do not retract previously
accepted data or replace the transport's own retransmission timers.

On a validated reset or permanent local failure, discard buffered data and latch
an error for subsequent I/O; do not turn it into clean EOF. Peer FIN preserves
ordered unread bytes and does not stop local writes. Distinguish refused
connection, reset, local shutdown, no route, unavailable interface, deadline
expiry and resource exhaustion. Add focused native result values where existing
statuses cannot express the difference; no version bump or fabricated success.

One pending READ and one pending WRITE per stream are allowed, including through
copies. Another operation in the same direction returns BUSY; the two directions
can proceed independently. SHUTDOWN_WRITE/ABORT serialize with data operations;
shutdown wakes a still-unaccepted WRITE as closed. Pending calls retain the
object and process mappings through the existing parked-call lifetime rules.

Final release without SHUTDOWN_WRITE aborts. After an explicit write shutdown,
final release may continue bounded graceful transport teardown without retaining
user mappings or task pointers; unread received data causes abort instead of
silently claiming delivery. FIN acknowledgment, peer close and TIME_WAIT can
outlive handles. Retired transport state must remain owned and counted until it
is safe to reclaim; never reuse its tuple just because an application exited.

## Proposed initial budgets

These are conservative development limits, not permanent ABI commitments. Confirm
them before the connection-state task; changing a limit later is not a reason to
redesign the object model.

| Resource | Initial proposal |
| --- | --- |
| Live transport records | 32 globally, including setup, closing and TIME_WAIT |
| Per-connection byte storage | 16 KiB receive and 16 KiB send, allocated on demand during setup; release payload storage when only TIME_WAIT metadata remains |
| Per-call READ/WRITE extent | At most 4 KiB; larger application transfers use short-call loops |
| Pending calls | Eight CONNECT/control slots, sixteen READ slots and sixteen WRITE slots; retirement and timers must not need a free user-call slot |
| Caller deadlines | At most 30 seconds ahead; expired deadlines fail before doing work |
| Unacknowledged-data progress | A separate 120-second transport timeout; idle established streams do not expire merely for being idle |
| Orphaned graceful teardown | At most 60 seconds waiting for progress/peer close, then abort; this does not shorten TIME_WAIT |

Account completed-but-unconsumed replies, queued bytes, retransmission storage,
receive reassembly and close state. Out-of-order data, if retained, shares the
receive budget; there is no second unbounded packet list. Exhaustion returns an
explicit result and must not stall UDP, echo, console or the network worker.
Document that TIME_WAIT can temporarily exhaust this small connection budget.
Do not evict a live TIME_WAIT record to make a new connection succeed.

## Transport and worker design

Keep first-party integration under `kernel/net`, with focused connection and
I/O files as needed. A reused protocol engine remains pinned vendor code; do not
reimplement its algorithms in the adapter. Object dispatch remains in `kernel/object`, ABI declarations in
`include/abi`, and helpers in libpyxis. The existing BSP network worker owns state
changes, packet processing and protocol timers. AP callers stage bounded data and
wait; workers never retain user buffers or private syscall-stack pointers.

Include TCP deadlines in the worker's existing earliest-deadline wait and apply
finite work budgets. Separate IPv4 queue/NIC acceptance from peer acknowledgment.
Late ARP/TX completion must identify the original connection/transmission,
not a recycled slot or freed pointer. Address removal or permanent device failure
invalidates affected connections without reviving them on reconfiguration.
Transient packet loss is handled by TCP's transport timers.

Initial sequence numbers must resist prediction and distinguish tuple reuse.
Use the RFC 9293 clock-plus-secret-derived construction and randomized ephemeral
binding, with entropy supplied by the existing VirtIO source. Kernel transport
identity must not depend on a caller-provided random grant. Preparation must not
block the sole network worker waiting for entropy; missing entropy disables new
TCP connections explicitly, while ordinary boot, UDP and numeric ping continue.
Before that task, select the small keyed primitive, its source/license if borrowed,
and the secret initialization/handoff. Do not invent a cryptographic function or
add a general crypto/provider framework.

Use RFC 9293 for wire validation, sequence-space comparisons, state transitions,
MSS negotiation, duplicate handling, FIN/RST processing and TIME_WAIT. Bound and
validate option parsing; do not negotiate extensions that are unimplemented.
Sequence arithmetic must work across wraparound. Receiving unsupported options
is not itself a reason to reject an otherwise valid segment.

Reliable sending includes congestion control, not just retransmission and the
peer's receive window. Use the RFC 5681 baseline with a conservative initial
window, and RFC 6298 RTT estimation, retransmission timeout and backoff. Retain
accepted bytes until acknowledged or a terminal error. Handle shrinking/zero
windows and reopening without busy polling. ACK progress, data deadlines and
caller deadlines are distinct. Timers remain monotonic, never wall-clock based.

The initial slice has no window scaling, SACK, timestamps, ECN or keepalive API.
Set a conservative effective send size within peer MSS and the current IP path's
limits; do not assume Ethernet's MTU proves the end-to-end path MTU. Discuss the
initial PMTU policy, out-of-order retention and exact TIME_WAIT duration before
the corresponding task. Record interoperability limits explicitly instead of
claiming complete Internet-host conformance. Existing IPv4 fragmentation and
ICMP-error limitations remain visible in [technical debt](../technical-debt.md).

## Utility and ordinary validation

The proposed command is `tcp HOST PORT [REQUEST_FILE]`. Resolve a hostname once
using the existing DNS helper, or bypass DNS for a numeric address. Connect,
stream the optional file with short-write handling, shut down the write direction
and copy received bytes to stdout until EOF. With no file, send no request bytes.
Use finite per-call deadlines and report failures to stderr with a nonzero exit
status. This is a finite request/response tool, not an interactive terminal pump;
concurrent stdin/socket waiting belongs with later wait/multiplexing work.

Choose the tool's request-size/backpressure expectations before implementation:
sequential send-then-read suits small requests, but arbitrary duplex protocols
can require simultaneous reading and writing. State that limitation rather than
silently promising a general netcat replacement. A user-supplied HTTP request
file is bytes to this tool; HTTP parsing, TLS and URI providers remain userspace
follow-ups.

Use normal builds, QEMU boots, manual guest commands, ordinary host services and
debugger inspection. Inspect handshake/data/close behavior with packet capture
where useful. Exercise numeric and hostname connections, host data in both
directions, transfers spanning multiple buffers, EOF, refusal, read timeout and
cleanup while existing ping/dig continue to work. Loopback refusal should work
without a NIC, given working entropy. No test harnesses, boot automation or
fault injection. State honestly which recovery paths were inspected rather than
observed under naturally occurring traffic.

## Focused implementation tasks

- [x] **1. Stack integration and packet bridge.** The pinned lwIP source subset
  builds in the normal kernel and runs in the sole network worker. Reviewed
  upstream adaptations preserve authoritative routing, 127/8 acceptance and
  allocation failure without eviction. Owned packet copies carry a never-reused
  PCB generation and are canceled on destruction. Timers join the worker wait.
  No connection capability or open/listen caller is exposed yet; see
  [the implemented boundary](../lwip.md).
- [ ] **2. Connection ownership and transport identity.** Settle the proposed
  budgets, keyed primitive/secret preparation and tuple allocation. Add bounded
  worker-owned state, timer scheduling and retirement. Preserve the existing
  cross-CPU allocation model; do not turn task.c into a TCP implementation.
- [ ] **3. Active open and native connection capability.** Handshake, bounded
  retry, refusal/reset validation, deadline cleanup, INSPECT/ABORT and service
  delegation. Pair ABI/libpyxis changes with the kernel. No handle escapes on a
  failed open; do not expose unimplemented READ/WRITE operations yet.
- [ ] **4. Receive stream.** Ordered delivery, duplicates/overlap, chosen bounded
  reassembly policy, receive-window updates, peer FIN and native READ with
  short-result, timeout and EOF semantics.
- [ ] **5. Reliable send stream.** Native WRITE, retained bytes, segmentation,
  acknowledgment processing, congestion control, RTT/RTO and zero-window
  handling. With lwIP, expose and account its existing machinery rather than
  duplicating it. Settle the conservative PMTU policy. Do not expose public-network
  data sending without congestion control.
- [ ] **6. Graceful shutdown and lifecycle completion.** SHUTDOWN_WRITE, FIN
  retransmission, both closing orders, simultaneous close, TIME_WAIT and final
  handle release. Confirm time bounds, invalidation and pending-call unwinding.
- [ ] **7. Native tcp utility and milestone completion.** Add the finite tool,
  reuse DNS, run the ordinary interoperability checks above, and document the
  stream contract and limitations. Rewrite/move this file to `docs/tcp.md`,
  remove the completed checklist and update links.

Each task is its own focused PR, paired across Pyxis/userland when needed. Build
and boot each change; discuss newly discovered interface or policy decisions
before implementing them. Intermediate commits need not claim the full final
transport contract. Avoid temporary public interfaces that later tasks must
preserve merely for compatibility.

## Deferred work

Listening/accept, guest-to-guest services, asynchronous calls and multi-object
waits, POSIX socket compatibility, IPv6, advanced TCP extensions, DHCP and
per-space network domains remain separate milestones. HTTP, TLS, Retawq and
scheme providers are consumers, not reasons to pull application protocols into
the kernel. Writable virtio-fs and persistent development remain independent of
TCP completion.

References: [TCP specification (RFC 9293)](https://www.rfc-editor.org/rfc/rfc9293.html),
[congestion control (RFC 5681)](https://www.rfc-editor.org/rfc/rfc5681.html),
[retransmission timers (RFC 6298)](https://www.rfc-editor.org/rfc/rfc6298.html),
[current networking](../networking.md), [DNS](../dns.md),
[randomness](../randomness.md), [users/authority checkpoint](users-and-authority.md).
