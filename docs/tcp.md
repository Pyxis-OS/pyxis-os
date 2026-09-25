# Outbound TCP streams

Caelum provides outbound IPv4 TCP through native stream capabilities. The pinned
[lwIP integration](lwip.md) runs in the existing BSP network worker; Caelum keeps
route/address authority, UDP, ICMP, ARP and NIC ownership. DNS and application
protocols remain in userspace. There is no POSIX socket layer or listening API.

Init delegates the `tcp` service through session and shell launch. Applications
use `include/abi/tcp.h` through the SDK and libpyxis's `tcp.h`; helpers borrow
explicit handles. The boot archive includes the `tcp` request/response utility
and the transmit-only `ttcp` tool.

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
| Transport records | 32 globally, including setup, closing and TIME_WAIT |
| Payload storage | 16 KiB receive window and 16 KiB send budget per connection; allocated as needed |
| Out-of-order receive | Shares the receive window; at most 16 pbufs |
| READ/WRITE extent | At most 4 KiB per call; helpers validate returned counts |
| Pending calls | Eight CONNECT/control, sixteen READ and sixteen WRITE slots, including completed replies |
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

lwIP handles congestion control, ACK validation, retransmission, RTT/RTO, Nagle
coalescing, zero windows and FIN ordering. The initial MSS ceiling is 536 bytes,
further reduced by the peer or local MTU. This is not PMTU discovery. Window
scaling, SACK, timestamps, ECN, IPv4 fragmentation and new ICMP-error/PMTU handling
are not implemented. These limits constrain performance; throughput tuning is
separate work.

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

## Transmit-only ttcp

```text
ttcp -t [-p PORT] [-n BUFFERS] [-l BYTES] HOST
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

Restart a one-shot host receiver before each run. There is no guest receive or
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

## Remaining work

Listening/accept and successful guest-to-guest services, asynchronous calls and
multi-object waits, IPv6, DHCP, richer TCP extensions and per-space network
domains remain separate milestones. HTTP/TLS, Retawq and userspace scheme
providers are future consumers. Keep the [users/authority checkpoint](wip/users-and-authority.md)
ahead of remotely accessible services. Writable virtio-fs is an independent
candidate for persisting the existing edit/build/run workflow.

See [networking](networking.md), [DNS](dns.md), [randomness](randomness.md),
[technical debt](technical-debt.md) and the [stack comparison](wip/tcp-stack-comparison.md).
