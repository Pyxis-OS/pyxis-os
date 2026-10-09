# TCP listeners and streams

Caelum provides IPv4 TCP through native listener and stream capabilities. The pinned
[lwIP integration](lwip.md) runs in the existing BSP network worker; Caelum keeps
route/address authority, UDP, ICMP, ARP and NIC ownership. DNS and application
protocols remain in userspace. There is no POSIX socket layer.

Init delegates the `tcp` service through session and shell launch. Applications
use `include/abi/tcp.h` through the SDK and libpyxis's `tcp.h`; helpers borrow
explicit handles. The boot archive includes the `tcp` request/response utility
and the `ttcp` send and receive benchmark.

## Listening and admission

Trusted init receives separate CONNECT and LISTEN rights on its `tcp` service.
Normal session startup delegates only CONNECT to the interactive shell and its
children. An explicit init handoff can create a bound listener and delegate that
object, without giving the server authority to bind another endpoint.

LISTEN requires the exact configured NIC IPv4 address and a nonzero port, both
host-order. Wildcard, loopback and ephemeral listening addresses/ports, and
address reuse are unsupported. Invalid address/port requests return BAD_REQUEST;
an occupied bind or exhausted transport/listener budget returns QUEUE_FULL.
Missing TCP identity returns UNAVAILABLE; allocation can return NO_MEMORY. Handle
reservation precedes bind, and a failed call publishes no handle.

Listeners have independently restrictable INSPECT and ACCEPT rights. INSPECT
reports the bound endpoint, pending count, capacity and terminal status. ACCEPT
waits for a completed handshake and returns an ordinary TCP stream with the same
rights and behavior as CONNECT. Completed handshakes are accepted in arrival order.
One ACCEPT may be outstanding per shared listener, including an uncollected reply;
another returns BUSY. Its absolute monotonic deadline is at most 30 seconds ahead.
Timeout leaves the listener usable and does not remove a connection from its queue.
The caller's stream object and handle slot are reserved before waiting.

There are at most four listeners, each with four pending connections combined
across SYN handshakes and established-but-unaccepted streams. Each listener and
each child also occupies one of the 32 global transport records. New SYNs receive
no answer when admission is full; peers can retry normally. A handshake expires
ten seconds after admission; retransmissions do not extend it. Ready connections
have no idle timeout, and their data shares the existing bounded receive window.
Accepting releases the pending slot but retains the global transport record.
Accepted, closing and TIME_WAIT connections can therefore exhaust admission even
when a listener has pending capacity. No transport record is evicted.

Copies share a listener. Final release stops admission and aborts all unaccepted
connections; already accepted streams remain independent. Calls retain their
object while pending. Losing the configured address permanently invalidates its
listeners and wakes ACCEPT with UNAVAILABLE; INSPECT remains available. Restoring
an address does not revive the listener. Link changes alone do not retarget it.
READ, WRITE and ACCEPT retain their blocking behavior. Their separate try forms
and multi-object readiness waits support a single execution loop.

## Readiness and transfer attempts

Libpyxis `wait_many` in `<wait.h>` accepts 1–16 `{handle, events}` interests and
returns one event mask per input entry, including zeros for entries that are not
ready. The complete input is captured and validated before registration; duplicate
handles or copies are separate observations. Invalid handles return BAD_HANDLE,
unsupported types/masks or zero masks return BAD_REQUEST, and insufficient rights
return DENIED. Failure leaves the output array unchanged. Native TCP streams,
listeners and [terminal attachments](../userland/terminal-sessions.md#readiness-and-ownership)
can share one wait; execution-group completion remains a later task.

| Interest | Required right | Automatically reported conditions |
| --- | --- | --- |
| READABLE, PEER_FIN | TCP READ | READABLE includes PEER_FIN; either includes terminal ERROR |
| WRITABLE, WRITE_CLOSED | TCP WRITE | WRITABLE includes WRITE_CLOSED; either includes terminal ERROR |
| ACCEPTABLE, CLOSED | Listener ACCEPT | ACCEPTABLE includes CLOSED; either includes terminal ERROR |

ERROR is output-only. INSPECT is not additionally required. FIN may coexist with
readable bytes; reads drain those bytes before a successful zero-byte result
reports EOF. FIN alone leaves writing open. Local shutdown closes writing, while
reset or transport failure reports ERROR. Closure/error events are reported even
while another call owns that direction. Ordinary data/capacity readiness excludes
an occupied direction, and releasing its call slot wakes the worker to recheck.

The deadline is absolute monotonic time, at most 30 seconds ahead. Zero requests
a poll. Current readiness is checked first even if a blocking deadline expired
while waiting for the worker. If nothing is ready, a poll succeeds with an empty
result, and an expired blocking wait returns TIMED_OUT. Results are level-triggered
observations, not reservations or an atomic snapshot across multiple calls. Every
return removes the registrations and temporary references. Closing a different
handle copy does not itself close the shared object.

`tcp_try_accept`, `tcp_try_read` and `tcp_try_write` have no deadline. Each performs
one worker attempt and returns progress, EOF, an error or WOULD_BLOCK. A stale
readiness result can legitimately lead to WOULD_BLOCK after another holder has
consumed the data or accepted connection. No operation remains queued on that
result, and try-accept does not reserve a stream/handle when its queue is empty.
BUSY, QUEUE_FULL, handle exhaustion and allocation failure remain distinct errors.
The existing short-transfer rules, 4 KiB transfer limits and zero-length no-op
behavior apply. Helpers preserve outputs on failure.

All these calls, including polls, may park for the BSP worker handoff. Try calls
and polls never wait for network readiness. lwIP remains exclusively worker-owned;
no userspace buffer, callback or submitted background I/O survives return.

## Capability contract

CONNECT takes a numeric host-order IPv4 address, nonzero port and absolute
monotonic deadline. The worker chooses the route, local address and randomized
ephemeral port. It prepares handle/connection capacity before starting a
handshake and returns an owned stream only on success. The deadline covers
entropy readiness, ARP and the handshake. Failure publishes no handle, although
the peer may have observed packets.

Copies share connection state and queues. Rights are separately restrictable:
INSPECT, READ, WRITE, SHUTDOWN_WRITE and ABORT. Helpers preserve caller outputs
on failure.

| Operation | Completion and ownership |
| --- | --- |
| INSPECT | Report local/remote addresses and ports, transport state, terminal status and independent local-write-shutdown/peer-FIN flags. CLOSED includes TIME_WAIT and can coexist with unread data. |
| READ | Return available ordered bytes, possibly short. A nonempty read returns zero only after peer FIN and all preceding data. Timeout consumes nothing and is not EOF. |
| WRITE | Return a positive count copied into send storage. This is local acceptance, not acknowledgment or remote application consumption. Retry only the unaccepted suffix. |
| SHUTDOWN_WRITE | Commit shared write closure and schedule FIN after accepted bytes. It does not wait for FIN allocation, transmission or acknowledgment. Reads remain usable. |
| ABORT | Discard queued bytes, latch failure and wake pending calls. All copies are affected; a best-effort reset does not delay completion. |
| Handle close | Release one reference. Final release retires external ownership; it is not a flush or delivery acknowledgment. |

One pending reader and one pending writer are allowed per shared stream; another
call in the same direction returns BUSY. Zero-length I/O validates authority,
buffers and deadline but does not poll EOF or transport state. A failed WRITE
accepts no bytes for that call; previously accepted bytes can still be sent.

Peer FIN preserves unread data and permits local writes until local shutdown.
A reset or permanent failure discards buffered data and latches an error rather
than reporting EOF. Repeating a committed write shutdown succeeds even after a
later failure, without clearing that error. A first shutdown on a failed stream
reports the failure. Unaccepted and future nonempty writes after shutdown fail
ENDPOINT_CLOSED.

Final release without write shutdown, or with unread ordered/reassembly data,
aborts. Otherwise bounded graceful teardown continues without task pointers or
user mappings. TIME_WAIT retains its tuple and admission slot even after abort,
close or address/link invalidation. A copied handle cannot shorten that lifetime.

## Ownership and limits

Only the BSP network worker touches lwIP state and timers. Callers stage bounded
copies and park; the worker retains no private user-buffer pointers. Completed
replies retain their call slots and object references until collected. Receive
window credit follows consumption, not merely copying into a pending reply.
Queued packet copies carry connection-generation ownership so PCB destruction
can cancel them without confusing a reused tuple or slot.

| Resource | Current limit |
| --- | --- |
| Transport records | 32 globally, including listeners, setup, closing and TIME_WAIT |
| Listeners / pending connections | Four listeners; four combined half-open/ready connections each |
| Payload storage | 65,535-byte receive window and send budget per connection; allocated as needed |
| Out-of-order receive | Shares the receive window; at most 16 pbufs |
| READ/WRITE extent | At most 4 KiB per call; helpers validate returned counts |
| Pending calls | Eight shared CONNECT/LISTEN/ACCEPT/control, sixteen READ and sixteen WRITE slots, including completed replies |
| Caller deadlines | At most 30 seconds ahead of monotonic time |
| Unacknowledged progress | 120 seconds without ACK progress, including deferred FIN |
| Orphan graceful teardown | At most 60 seconds; fully closed FIN_WAIT_2 can expire at lwIP's 20-second limit |
| TIME_WAIT | Two minutes; records are not evicted to admit new connections |

Idle owned streams do not expire merely for being idle. Caller deadlines and
transport timers are distinct. Repeated shutdown does not restart progress
timers. Admission can fail while TIME_WAIT consumes the small global budget.
Metadata, retained payloads, reassembly and pending replies are accounted; see
[lwIP ownership and allocation](lwip.md) for the implementation details.

A short-lived BSP task prepares independent entropy-backed SipHash keys for
sequence numbers and ephemeral ports. Missing entropy disables new TCP
connections explicitly while ordinary boot, UDP and numeric ping still work.
The transport owns this authority, independent of caller DNS/random grants.

lwIP handles congestion control, ACK validation, retransmission, RTT/RTO, zero
windows and FIN ordering. Nagle coalescing is disabled on every connection:
native writes already hand lwIP up to 4 KiB at once, and request and response
framing would otherwise wait for the peer's delayed ACK. Connections advertise an MSS of 1460
bytes for the 1500-byte interface MTU and send at most that to on-link peers,
further reduced by the peer's MSS. Without path-MTU discovery, a connection
whose peer is reached through a gateway sends at most 536 bytes per segment,
IPv4's minimum reassembly size minus headers. The clamp is applied once the
connection is established, before the application can send. Windows are
65,535 bytes, the largest without window scaling. Window scaling, SACK,
timestamps, ECN, IPv4 fragmentation and ICMP-error/PMTU handling are not
implemented; see [network throughput](../development/network-throughput.md).

## Request/response utility

`tcp HOST PORT [REQUEST_FILE]` resolves a hostname once or bypasses DNS for
numeric IPv4. It streams the optional file, shuts down writing and copies the
response to stdout until EOF. Without a file it sends no request bytes. Connect,
each write and each read use fresh ten-second deadlines. Errors go to stderr,
abort/close the stream and produce a nonzero exit status.

The request uses a 4 KiB buffer without a file-size cap. All sending precedes
reading; protocols needing simultaneous bidirectional progress can stall and
time out. There is no stdin pump or total-runtime limit. Response EOF is not an
application-level acknowledgment of the request. HTTP request files are raw
bytes to this utility; it contains no HTTP or TLS parser. See
[host request examples](networking.md#tcp-requestresponse-utility).

## Concurrent echo server

An opt-in trusted init can create a listener and hand it to the existing `tcp`
utility. Save this native init script as `/tmp/tcp-init.sh`:

```text
#!boot://shell.pxe
session boot://session.pxe --configure-network --tcp-server 10.0.2.15 5001 --tcp-count 3
```

Build/boot with `make run INIT=/tmp/tcp-init.sh CPUS=4 VIRTIO_NET=1`, using the
[usual firmware/emulator overrides](../development/qemu.md) where required. This
replaces the primary shell with the echo server; the default CPU 2 shell remains
available. On a one-CPU boot the server uses the BSP's space.

TCP forwarding is not yet a launcher option. Enter QEMU's monitor with Ctrl-a c
and add forwarding explicitly:

```text
hostfwd_add pyxis_net tcp:127.0.0.1:15001-10.0.2.15:5001
```

Return to serial with Ctrl-a c. From the host, use a TCP client that half-closes
its writing side after stdin EOF and continues reading, for example
`printf 'hello\n' | socat - TCP:127.0.0.1:15001`. Each connection echoes bytes
until peer EOF, drains pending output, then shuts down writing and closes. Up to
four accepted clients progress concurrently through `wait_many` and TCP try
operations. Each has a 4 KiB pending-output buffer; reads stop while it is full,
and writable readiness is requested only while output is queued. Each pass
allows at most four try operations and 8 KiB transferred per client, rotating
which client is served first. A stale readiness result followed by WOULD_BLOCK
returns that direction to waiting. Connection errors close only that client and
make the eventual server exit unsuccessful; other clients continue.

The loop retries ten-second readiness deadlines without imposing an idle-client
or output-drain deadline. Four stalled clients can occupy all active slots;
transport backlog and global record limits still bound further admission.

`--tcp-count 3` launches `tcp --serve 3`: stop admitting when the third stream is
accepted, close the listener, finish all active streams, then exit. Excess pending connections
are aborted when admission stops. Omit the count to keep serving. The child
receives only the bound listener, memory, a readable clock and stdout/stderr;
it receives no LISTEN service, launcher or filesystem roots. The ordinary
interactive session cannot invoke this handoff successfully because its TCP
authority is CONNECT-only. No remote command execution is introduced here.

## ttcp

```text
ttcp -t [-p PORT] [-n BUFFERS] [-l BYTES] HOST
ttcp -r [-p PORT] HOST
```

Defaults are port 5001, 2048 buffers and 8192 bytes per buffer: 16 MiB total.
Positive decimal option values can be attached (`-n128`) or separate (`-n 128`).
Count and length each fit 32 bits; multiplication uses 64 bits. One source buffer
is allocated and filled with repeating printable ASCII, restarted at each buffer
boundary. Short native writes retry only the remaining suffix. Numeric addresses
bypass DNS; hostnames use the same configured resolver as `tcp` and `ping`.

Start an ordinary classic ttcp receiver on the host, then boot with
`make run CPUS=4 VIRTIO_NET=1`:

```sh
# Host; classic ttcp, not iperf or a different benchmark protocol.
ttcp -r -p 5001
```

```text
# Pyxis shell
ttcp -t 10.0.2.2
ttcp -t -p 5001 -n 128 -l 8192 10.0.2.2
```

Restart a one-shot host receiver before each run. There is no listening or
UDP mode, stdin source, socket tuning or CPU-use accounting. This first-party
implementation records its classic public-domain command/pattern reference in
`userspace/ttcp/README.md`; it does not introduce a socket compatibility layer.

Timing excludes DNS, allocation and connection setup. It starts before sending
and ends after local write shutdown, peer EOF and confirmed error-free transport
closure. EOF alone can precede acknowledgment of the sender's data/FIN, so after
EOF the tool checks INSPECT for CLOSED with both shutdown flags and CALL_OK.
It sleeps up to 10 ms between checks; TIME_WAIT counts as closed without waiting
for its expiration. Incoming response bytes are discarded during completion.

Connect and each write have fresh ten-second deadlines. One additional ten-second
deadline covers shutdown, draining peer bytes, EOF and final closure together.
Failure aborts/closes the stream, exits unsuccessfully and reports only bytes
accepted locally, never a successful throughput figure. A zero elapsed interval
reports that the clock cannot resolve the rate rather than inventing a duration.

The printed MiB/s includes teardown and receiver close latency; it is not a pure
link measurement. Compare the configured byte count with the host receiver's
count. Orderly TCP closure proves transport acknowledgment, not application
consumption. Under QEMU user networking, the guest's TCP peer is also mediated
by the host backend, making the host receiver's count especially important.

`ttcp -r` connects to a peer that serves data, reads until EOF and discards it,
reporting bytes and MiB/s from the established connection to EOF. Classic
`ttcp -r` listens instead; ordinary sessions hold connect-only TCP authority, so
this one connects out. `-n` and `-l` are rejected with `-r`; each 4 KiB read has
a fresh ten-second deadline. Serve the data from the host, then receive:

```sh
# Host
socat -u OPEN:FILE TCP4-LISTEN:5002,reuseaddr
```

```text
# Pyxis shell
ttcp -r -p 5002 10.0.2.2
```

Content is not checked; use `tcp HOST PORT | sha256sum` when integrity matters.

## Remaining work

Readiness beyond TCP/terminal attachments, asynchronous calls, IPv6, DHCP, richer TCP
extensions and per-space network domains remain separate work. HTTP/TLS, Retawq and userspace scheme
providers are future consumers. Keep the [users/authority checkpoint](../wip/users-and-authority.md)
ahead of remotely accessible services. Writable virtio-fs is an independent
candidate for persisting the existing edit/build/run workflow.

See [networking](networking.md), [DNS](../userland/dns.md), [randomness](randomness.md),
[technical debt](../technical-debt.md) and [why lwIP](lwip.md#why-lwip).
