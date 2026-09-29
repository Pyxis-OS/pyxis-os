# Networking: IPv4, ICMP echo and UDP

The kernel has one system-wide networking worker and a boot-lifetime interface
named `lo`, plus `net0` when a supported VirtIO NIC is present. Interfaces,
addresses and routes are shared across spaces;
capability-mediated access will not by itself provide network isolation.
The implementation is in `kernel/net`, with kernel interfaces in
`include/kernel/net`. The native echo capability supports userspace ping without
raw-packet authority; UDP endpoints exchange bounded datagrams through the same stack.

The worker handles Ethernet, ARP, IPv4, ICMP and UDP. Deferred local delivery
remains independent of the NIC. Init receives separate configuration authority and
passes it to the session launcher. The launcher reads the packaged Lua network
settings before starting the shell. Default boot without a NIC remains usable;
`make run VIRTIO_NET=1` enables QEMU user networking and the configured `net0`.
There are no automatic ping probes.

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
The loopback receive queue holds at most 16 packet pointers. ARP holds at most
16 waiting packets, counted within that same 32-packet budget.

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
interface descriptors are constant metadata, not a dynamic device registry.

## Routing and configuration

Kernel address arguments are host-order integers (`0x7f000001` is `127.0.0.1`),
while wire fields use network byte order. Header layouts remain kernel-private.
The sole BSP network worker owns configuration and protocol state. With interrupts
enabled, `net_ipv4_configure(address, prefix, gateway)` replaces the `net0` settings;
`net_ipv4_clear()` removes them. Neither is an AP or arbitrary BSP-task interface.
The native configuration capability below serializes userspace requests onto
that worker; direct kernel calls are only for the worker itself.

Replacement validates all fields before changing state. Prefixes 1..32 are
accepted; the address must be unicast, outside `0/8`, `127/8`, multicast and
reserved high addresses. Network/broadcast addresses are rejected for prefixes
through /30; /31 has two host addresses and /32 one. A nonzero gateway must be a
distinct valid host on that connected subnet. Zero means no default route. Missing
hardware returns `NET_UNAVAILABLE`; invalid settings return `NET_INVALID` and
preserve the previous configuration.

Routing uses this precedence:

1. `127/8` stays on `lo`, selecting `127.0.0.1` for new requests.
2. The configured NIC address is also delivered locally, selecting that address.
3. Other unicast hosts on its subnet go through Ethernet, resolving the destination.
4. Other unicast destinations use the optional gateway, resolving the gateway.
5. Without a matching route, return `NET_NO_ROUTE`.

Both local paths work with the physical link down. External sends select the
configured address and return `NET_UNAVAILABLE` when the NIC/link is unavailable.
Loopback sources and destinations never escape onto Ethernet. A caller-supplied
source must match the selected route. Configuration replacement/clear invalidates
ARP and completes outstanding non-loopback echo calls as unavailable, including
calls to the previous local NIC address. In-flight DMA storage keeps its driver
lifetime; reconfiguration cannot retract a frame already submitted. Ordinary
link down/up preserves the assigned address and gateway.

## IPv4 and echo

`net_ipv4_transmit` takes an owned packet with 20 reserved header bytes followed
by initialized payload, source/destination, protocol, deadline and a completion
tag/token (NONE for generated replies). It fills the header and selects local delivery or
ARP/Ethernet. Success transfers ownership; failure may have filled the header
but retains caller ownership. Replies use the request destination as their source.

Receive checks version, header and total lengths, reserved flags and the header
checksum before protocol dispatch. It ignores bytes beyond IPv4 total length,
so trailing link padding cannot become echo payload. Options, fragments and
protocols other than ICMP and UDP are counted and discarded. Only an ordinary
20-byte IPv4 header is supported; DF is allowed, MF and nonzero fragment offsets are not.
Local delivery does not decrement TTL or reject a packet solely for a low TTL.
Outgoing packets use TTL 64 and DF, with identification zero for atomic datagrams.
There is no fragmentation, reassembly, forwarding or general ICMP error generation.
These are explicit subset limits, not full IPv4 host conformance.

`net_icmp_echo_send` copies caller bytes, builds an echo request and queues it
with the selected source, application deadline and non-reused echo token. It
runs in the BSP network worker with interrupts enabled.
Zero-length payloads are valid; the maximum is 1472 bytes within the 1500-byte
IP MTU. Invalid arguments return `NET_INVALID`, packet allocation/budget failure
returns `NET_NO_MEMORY`, and route/queue/worker errors propagate. The helper
releases its allocation on failure and never retains caller storage.

ICMP receive validates length, checksum and the zero echo code. Each valid echo
request creates a separate bounded reply carrying the unchanged identifier,
sequence and payload. Replies use the same routing path with a three-second
deadline, including any ARP wait; failed allocation/submission is counted and
discarded. Receive bytes are borrowed only for processing, whether from a local
packet or a driver RX buffer. Echo replies are
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

Packet allocation/release and local queue publication require BSP/IF=0.
Protocol processing, configuration and Ethernet submission belong to the sole
network worker with interrupts enabled. They are not AP, user-pointer or
interrupt-entry interfaces. Application calls use explicit cross-CPU ownership
handoff without adding scheduler request queues.

Each turn budgets 16 RX/TX completions, eight configuration calls, eight UDP
control slots, 16 echo slots, the bounded ARP cache/pending lists and eight local
deliveries. The UDP I/O pass examines eight send and sixteen receive slots; each
datagram arrival also checks parked receivers. The worker yields after starting
or completing protocol/control work or exhausting a receive/completion budget.
It disables interrupts around allocation/release, capability installation and
shared queue/wait operations.
Local transmission never recursively enters receive and has no Ethernet, ARP,
PCI or DMA dependency.

Packet queue access remains BSP/IF=0. A separate small lock protects the worker's
notification flag and wait pointer because AP callers also submit network work.
Submission publishes a request before notifying, with the request lock released.
The remembered notification bridges the gap between inspecting deadlines and
publishing the wait. Wake detaches the pointer under the notification lock;
timeout resumption detaches under that same lock before reusing its wait record.
The worker sleeps until new work or the earliest echo, UDP, ARP or driver deadline,
indefinitely when there is neither. No scheduler request queue or per-tick network polling
is added.

## Native echo capability

Init receives an `echo` resource. Session handoff and ordinary shell launches
copy its ECHO_RIGHT_SEND grant. It authorizes ICMP echo exchanges only: no raw
headers, user-selected source address, interface configuration or routing changes.
The stack and request budget remain shared between spaces. Configuration
uses a distinct authority that ordinary applications do not inherit.

[The echo protocol](../../include/abi/echo.h) uses the existing CALL syscall with a
tagged ECHO_EXCHANGE request. Its input is a host-order IPv4 destination and an
absolute deadline in the CLOCK_NOW monotonic epoch, at most five seconds ahead.
A past deadline returns CALL_TIMED_OUT; an excessive future deadline or nonzero
reserved field is CALL_BAD_REQUEST. Capture/check all input and reply storage
before publishing any work. Errors return no bytes and leave user storage alone.
Success returns the peer address, wire identifier/sequence and round-trip
nanoseconds, measured from local enqueue or copy into NIC storage through reply
processing. ARP wait consumes the call deadline but is excluded from RTT.

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
independent: timeout never reclaims a DMA buffer. ARP discards expired software
packets before sending, even if a late resolution reply arrives. Its completion
uses the non-reused token rather than a pointer to a reusable echo slot.

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
The utility needs the `echo` grant plus clock READ/SLEEP authority:

```text
ping 127.0.0.1
ping -c 10 127.0.0.2
```

The default is four requests, with one-second deadlines and starts paced at least
one second apart. `-c` accepts 1..65535. Numeric addresses require four decimal
octets; shorthand is unsupported. [Hostnames](../userland/dns.md#hostname-ping) are resolved
once before the echo loop using additional UDP and random authority.
Each success prints peer, sequence and RTT; errors are explicit, followed by
attempted/replied/unanswered counts and RTT
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
in `kernel/net/echo.c` shows queued, ARP-waiting, sent and completed calls; after ping exits
all slots should be ECHO_FREE, with no retained waiter.

Packet allocation requires BSP/IF=0; protocol entry points and direct kernel
configuration require the sole network worker with interrupts enabled. Ordinary
boot now applies configuration through the userspace capability. See [GDB](../development/gdb.md)
for debugger calling restrictions; do not mutate worker state from another task.

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
counted and discarded. Valid frames are borrowed by Ethernet/ARP/IPv4 until
processing returns, then their RX buffers are reposted. Protocols never retain
DMA pointers or allocate a software copy just to parse an incoming frame.

`virtio_net_transmit(frame, length)` is a BSP network-worker interface, with
interrupts enabled. It copies a complete checksummed Ethernet frame, without
FCS, into a free TX buffer and supplies the VirtIO header. All return paths leave
caller storage owned by the caller. `NET_OK` means queued, `NET_QUEUE_FULL` means
all sixteen buffers remain lent, and `NET_UNAVAILABLE` covers an inactive NIC,
link-down or an unstable configuration. TX storage is reusable only after a
checked completion; modern TX used lengths must be zero. Ethernet supplies the
ordinary caller; applications have no raw-frame submission interface.

One turn processes at most sixteen completions from each queue. Reaching either
budget requests a yield after the worker also services echo and loopback work.
Pending notifications are remembered through the existing worker wait handoff.
Idle RX has no timeout, heartbeat or timer polling.

### Failure and link changes

Each outstanding TX buffer has a five-second completion deadline. The worker
sleeps until the earliest echo, ARP, TX or exceptional configuration/reset deadline.
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
transmissions and discards ARP state, completing external echo calls as unavailable.
Already submitted buffers retain their completion deadlines. Delivery to the
assigned local address and loopback continue. Link-up permits new submissions
without rebuilding queues or losing the IP configuration. In the absence of a
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
indices and outstanding counts. `network.interrupts`, `received`,
`malformed`, `transmitted`, `completed` and `queue_full` are debugger diagnostics,
not a public statistics ABI. An idle worker sleeps with all RX buffers posted
and no TX in flight. Inspect real interrupts at `virtio_net_interrupt`.

For manual frame submission use TCG and the [GDB calling constraints](../development/gdb.md):
allocate caller storage before AP startup, then stop in the network worker after
activation and call `virtio_net_transmit` with a valid frame. The call copies
bytes and does not wait. Continue normally to observe real device completions;
do not call from an arbitrary stopped task or IRQ. No boot-time packets or
validation hooks are built into the driver.


References: [VirtIO 1.4 network device and PCI transport](https://docs.oasis-open.org/virtio/virtio/v1.4/cs01/virtio-v1.4-cs01.pdf),
[QEMU user networking](https://www.qemu.org/docs/master/system/devices/net.html).

## Ethernet and ARP

Ethernet accepts its own unicast destination and broadcast ARP. IPv4 must be
unicast to the configured IP; external loopback, nonlocal, multicast and broadcast
IP traffic is discarded. Frames use Ethernet II with IPv4/ARP types, padding to
60 bytes before the hardware-supplied FCS. VLANs, multicast membership, forwarding,
address conflict detection and gratuitous ARP are outside this slice.

ARP validates Ethernet/IPv4 lengths, operation, matching Ethernet/ARP sender MAC
and target IP. Replies must also name our MAC. Requests to our address may teach
a neighbor; replies only update an existing entry. A sender of `0.0.0.0` may probe
our address and receive a reply, but is never cached. Other senders must be valid
on-link hosts. Unsolicited replies and gratuitous announcements are ignored.
ARP remains unauthenticated; these checks are not protection against an on-link
peer impersonating another host.

The cache holds 16 neighbors and a separate list holds up to 16 owned software
packets awaiting resolution. A resolved entry lasts 60 seconds. Resolution sends
at most three probes, spaced one second apart, then fails remaining packets as
`NET_TIMED_OUT` after the third response interval. TX queue pressure postpones a
probe without counting it as transmitted. The packet's own deadline may expire
first; ordinary `ping` uses one second. Resolved packets waiting for TX space
retry on worker wake or their deadline. Idle cache entries require no timer wake.

An entry with waiting packets cannot be evicted. Empty/expired entries are reused,
then idle least-recently-used entries. Recently probed unresolved entries retain
their cooldown after the last caller expires. Exhaustion returns `NET_QUEUE_FULL`;
no packet or neighbor list grows dynamically. Configuration changes and link loss
discard waiting packets and cache entries. DMA-submitted buffers are independent.

References: [ARP](https://www.rfc-editor.org/rfc/rfc826.html),
[ARP cache and retry requirements](https://www.rfc-editor.org/rfc/rfc1122.html#section-2.3.2),
[/31 host addresses](https://www.rfc-editor.org/rfc/rfc3021.html).

## Native configuration capability

Init receives `net_config` authority for the single `net0` interface, even if no
NIC is present. The object selects the interface; names in configuration do not
grant authority. [The tagged protocol](../../include/abi/net_config.h) uses CALL:

- `NET_CONFIG_QUERY` requires READ and returns a snapshot of presence, transport
  readiness, usable link, assigned address/prefix/gateway, MTU and MAC. Absence is
  a successful snapshot, distinct from a discovered but failed device.
- `NET_CONFIG_REPLACE` requires WRITE and supplies the complete address, prefix
  and optional gateway. Invalid fields return `CALL_BAD_REQUEST` without mutation;
  an unusable device returns `CALL_UNAVAILABLE`. A link-down prepared device can
  still be configured.
- `NET_CONFIG_CLEAR` requires WRITE and removes settings and ARP state. It works
  without a NIC and returns no data, as does replacement.

Libpyxis exposes `net_config_query`, `net_config_replace` and `net_config_clear`.
Query clears caller output on failure. Scalar requests are captured and reply
storage checked before parking the user task. Eight shared slots bound calls;
exhaustion returns `CALL_QUEUE_FULL`. The worker processes each finite operation
outside the slot lock; no device/peer response or application pointer is needed.
Completion detaches and wakes under the slot lock, with the slot retained until
its caller resumes. Settings survive closing authority; closing another copied
grant does not cancel a blocked call. No new syscall or scheduler queue is added.

The init shell passes this grant only through its explicit `session` handoff.
The session launcher consumes it for setup only with `--configure-network`
and does not pass it to the interactive shell. The development init requests
setup; other packaged init scripts leave shared network settings alone. Ordinary
commands inherit echo authority, never configuration authority.
This is delegation policy in those programs, not a restriction on a trusted holder
intentionally granting its capability elsewhere. Interfaces remain system-wide;
there is no per-space network isolation.

## Boot configuration and use

The session launcher reads `app://config/network.lua`, installed from
`userspace/config/network.lua`. The packaged QEMU user-network settings are:

```lua
return {
  dns = { server = "1.1.1.1" },
  net0 = {
    optional = true,
    address = "10.0.2.15",
    prefix = 24,
    gateway = "10.0.2.2",
  },
}
```

This is manual static configuration, not DHCP. The kernel and driver contain no
QEMU address defaults. An omitted gateway means no default route. Addresses use
four decimal octets; no DNS, shorthand or embedded NUL bytes. Unknown keys,
incorrect types and prefixes outside 1..32 are errors. The kernel validates subnet
and gateway relationships when applying replacement.

A missing file or missing `net0` leaves existing NIC settings alone (unconfigured on
fresh boot). `net0 = false` explicitly clears them. `optional` defaults to false;
true permits an absent NIC, but does not hide transport failure. A required absent
device or invalid configuration prevents shell launch. A present but unavailable
device or other runtime setup failure is diagnosed and the shell remains available
for recovery. A launcher without the configuration grant reports that it is keeping
current settings; this allows a later unprivileged session handoff.

The optional `dns` table accepts only `server`, a numeric IPv4 string. Session
exports it as `DNS_SERVER` in canonical dotted decimal, replacing any inherited
entry. Missing files, `dns` or `server` select `1.1.1.1`; empty strings, wrong
types, unknown keys, malformed addresses and embedded NUL bytes are errors.
Addresses in 0/8, multicast and reserved high ranges (224/4 and above) are
rejected; loopback is allowed for a future local resolver. This is address
validation, not a check that a server is reachable or speaks DNS.

DNS selection is independent of `net0`, NIC presence and network-configuration
authority. `net0 = false` still exports the selected resolver. Both settings
are decoded before network/terminal changes or shell launch; invalid DNS
configuration prevents those effects. The resolver address is userspace policy:
it is not sent to the kernel, and startup performs no DNS query or NIC enabling.
Shell launches inherit it through the existing environment forwarding.

Direct-init applications that bypass session do not receive a synthesized
`DNS_SERVER`. The [dig client](#dns-queries-with-dig) also defaults to `1.1.1.1`
when that variable is absent and accepts an explicit numeric server override.

Session and network configuration use the same restricted [Lua evaluator](../userland/lua.md#embedding-and-session-configuration).
Both files are read before applying settings. When requested, network setup
precedes terminal changes and shell launch; later failure does not roll back
an applied address or route. There is no live reload, supervision or automatic retry. The default init
still mounts optional `host://` before the session handoff.

```sh
make run VIRTIO_NET=1
# Optionally also supply VIRTIO_FS_SOCKET=/path/to/fs.sock.
```

In the shell, use `ping 127.0.0.1`, `ping 10.0.2.15`, `ping 10.0.2.2` and
`ping 1.1.1.1`. Default no-NIC boot still supports loopback. Internet ICMP through
QEMU user networking depends on host ping-socket permission and a reachable peer.
On Linux, the QEMU user's group must be permitted by `net.ipv4.ping_group_range`;
see [QEMU's host setup notes](https://www.qemu.org/docs/master/system/devices/net.html).
The backend gateway is the first diagnostic target; an external timeout alone
does not identify a guest-stack failure.

The initial networking milestone is complete. DHCP needs UDP, broadcast support,
lease deadlines and delegated configuration authority. [DNS](../userland/dns.md) is complete;
[outbound TCP](tcp.md) is available through native stream capabilities.
IPv6, richer routing, network
isolation and website hosting remain separate scopes in
[later directions](../wip/later-os-directions.md#networking-and-applications).
This stack deliberately implements a bounded IPv4/ICMP subset, not complete
Internet host conformance. See the limits above before adding another protocol.

## Native UDP endpoint lifetime

Init receives a separate `udp` service grant. Session and shell explicitly copy
its OPEN right into child startup resources, independently of echo and
configuration authority. The service authorizes binding any available port on an
explicit local IPv4 address: any address in `127/8`, or the configured NIC address.
There are no wildcard bindings, shared bindings or privileged-port distinctions.
The port namespace and resource bounds are system-wide, not isolated by space.

[The UDP protocols](../../include/abi/udp.h) use tagged CALL requests:

- `UDP_OPEN` on `PROTOCOL_UDP_SERVICE` takes a host-order address and port. Zero
  port selects a free ephemeral port using a rotating scan of 49152–65535.
  Success returns an owned endpoint handle and its bound address/port/state.
  A duplicate active address/port returns `CALL_ALREADY_EXISTS`; an address that
  is not currently local returns `CALL_UNAVAILABLE`. The same port can be bound
  on different local addresses.
- `UDP_OPEN_ROUTE` uses the same service, OPEN right, request layout and reply
  as OPEN, but the request address is a destination. The worker selects a local
  source through the existing IPv4 routing policy and binds it in the same
  operation. Loopback destinations select `127.0.0.1`; other destinations use
  the configured NIC address when a usable route exists. Invalid destinations
  return `CALL_BAD_REQUEST`, missing routes `CALL_NO_ROUTE`, and an unavailable
  interface `CALL_UNAVAILABLE`. Port zero and duplicate bindings follow OPEN's
  rules. Selection sends no packets and does not perform ARP or prove reachability.
- `UDP_INSPECT` on `PROTOCOL_UDP` requires INSPECT and returns the original bound
  address/port and current state: BOUND, SHUTDOWN or UNAVAILABLE. This succeeds
  on stopped endpoints too. BOUND describes the binding, not NIC carrier.
- `UDP_SHUTDOWN` requires SHUTDOWN, returns no bytes and is idempotent. The worker
  releases the binding before completing the call; another endpoint may then
  acquire it while handles to the old object still exist.

New endpoints carry INSPECT, SEND, RECEIVE and SHUTDOWN rights. Existing handle
copying and launch grants can reduce those rights. No echo server or automatic
network traffic runs at boot.

Libpyxis provides `udp_open`, `udp_open_route`, `udp_inspect` and `udp_shutdown`.
`udp_open_route(service, destination, port, &reply)` returns the selected local
address in `reply.local`. Both open helpers and INSPECT
require output pointers and clear their outputs on failure; both open helpers use
`HANDLE_INVALID`. At the raw CALL boundary, errors leave reply bytes untouched.
Payloads and output mappings are checked before any work is published.

The network worker owns bindings and endpoint state. Eight shared control slots
include completed calls until their callers resume; exhaustion is
`CALL_QUEUE_FULL`. OPEN lends the parked task's kernel-owned capability table to
the BSP worker, which allocates and installs the endpoint with interrupts disabled
before publishing its binding. Installation failure frees the unpublished object;
no port is reserved. Other endpoint calls borrow the caller's live grant until
completion. No user/private-stack pointers cross CPUs and no scheduler queue is
added.

Route selection and binding are serialized with configuration changes by that
same worker. The endpoint retains only its concrete local binding, not the
destination or a route snapshot: it is neither connected to a peer nor a
wildcard listener. Later sends use their own destinations and current routing,
subject to the bound source address. The existing invalidation and shutdown
rules below apply to both ways of opening an endpoint.

At most 16 endpoint objects may live, including stopped objects still held by
handles and those awaiting final cleanup. Exhaustion is `CALL_LIMIT`; allocation
failure is `CALL_NO_MEMORY`. Closing one handle only releases its reference.
Final close uses object retirement to hand the allocation to the network worker,
which removes its binding and frees it. That cleanup needs no control slot, but
may be deferred past CLOSE's return. Use SHUTDOWN when immediate binding release
is needed. A surviving copied handle continues to refer to the same object,
never a later endpoint that reuses its port.

Removing or replacing the NIC's address marks its bound endpoints UNAVAILABLE
and releases their bindings. Restoring the same address does not revive them.
Changing only prefix/gateway preserves bindings, as does link down/up. Loopback
bindings are independent of NIC configuration. Explicit shutdown also works on
an unavailable endpoint.


## UDP datagrams and deadlines

Libpyxis `udp_send(endpoint, address, port, data, length, deadline_ns)` sends one
complete datagram from the endpoint's bound address/port. Destination port zero
is invalid. Payloads may contain zero through 1472 bytes; larger sends return
`CALL_LIMIT`. SEND uses the endpoint's SEND right and returns no reply bytes.
Success means deferred local-queue or NIC acceptance, including any preceding
ARP wait. It does not promise remote delivery or wait for a reply.

`udp_receive(endpoint, data, capacity, deadline_ns, &reply)` requires RECEIVE.
Success copies one complete datagram and returns its sender IPv4 address, port
and payload length. An empty datagram is success with length zero, never EOF.
Source port zero may appear on received traffic, although SEND cannot reply to
port zero. The kernel validates only `min(capacity, UDP_MAX_PAYLOAD)` bytes and
requires data/reply ranges not to overlap. `CALL_BUFFER_TOO_SMALL` leaves the
head datagram queued; retry with the maximum buffer. No truncation or size-only
error reply is provided. Raw CALL failures leave both outputs untouched.
Libpyxis clears reply metadata on failure except for invalid overlapping outputs,
which it rejects before writing either range. It never clears the data buffer.

Both requests take absolute deadlines in CLOCK_NOW's monotonic epoch, including
time queued before the worker runs. SEND accepts up to five seconds ahead and
RECEIVE up to thirty; a larger interval is `CALL_BAD_REQUEST`. Past deadlines
return `CALL_TIMED_OUT`, even when a datagram is already queued. Empty receive
queues sleep without polling. Receive timeout does not consume queued data.
Applications may repeat bounded receives; no infinite-wait or poll sentinel is
provided. Queue exhaustion can fail immediately rather than waiting for space.

Each endpoint admits one outstanding call per direction, across all copied
handles. Another call in the same direction is `CALL_BUSY`. Separate global
limits of eight sends and sixteen receives leave the eight control slots
available for shutdown/inspection. Slots include completed calls until their
original callers resume and consume results. Their fixed payload staging uses
35,328 bytes plus metadata, separate from the existing software packet budget.
Caller input is captured before parking; only shared kernel copies and a live
endpoint reference cross CPUs. The worker never borrows a user buffer or private
syscall stack. Immediate transmit acceptance records completion before waking,
so the submitting stack cannot still be reading a slot that its caller reuses.

Receive queues hold four datagrams per endpoint and sixteen globally, counted
within the existing 32-packet software budget. They own copies of UDP header and
payload; DMA receive bytes are copied before reposting the descriptor. New
arrivals are dropped when either receive bound or packet allocation is exhausted.
Existing queued data is preserved. Diagnostic counters record delivery, unbound
ports, queue pressure, allocation failure, malformed lengths and checksum failure.

The wire path checks the eight-byte header and declared length against the IPv4
payload, and never delivers bytes beyond that length. Every outgoing datagram
has an IPv4 pseudo-header checksum; a computed zero is encoded as all ones.
Incoming nonzero checksums are verified; an omitted IPv4 UDP checksum is accepted.
Odd payload lengths use zero padding only for checksum calculation.
See [RFC 768](https://www.rfc-editor.org/rfc/rfc768.html).

Shutdown discards queued receive data, removes an ARP-waiting send and wakes
pending I/O with `CALL_ENDPOINT_CLOSED`. Worker processing serializes delivery
and shutdown; already completed calls keep their results even if their callers
have not resumed. Address removal does the same with
`CALL_UNAVAILABLE`. Final close discards remaining receive packets. Prefix/gateway
replacement preserves the endpoint and its receive queue, but clearing the old
ARP state fails sends awaiting transmission under that route as unavailable;
new work routes against current configuration. Link loss fails external pending
ARP sends while preserving bindings, received data and local delivery.

ARP identifies waiting consumers with an explicit echo/UDP tag and a token that
is never reused by that consumer. UDP cancellation removes its unsent ARP packet
before completing the call. Late completion cannot target a reused call slot.
Successful local submission has already transferred packet ownership, and
submitted DMA keeps its driver lifetime: timeout/shutdown cannot recall either
or revoke a completed send. Likewise, datagrams arriving after a port is rebound
cannot be distinguished from traffic intended for its new owner.

Unbound destination ports are silently dropped. ICMP error generation and
application delivery are deferred, so an absent listener usually appears as a
receive timeout. There is no fragmentation, reassembly, broadcast, multicast,
retransmission, delivery/order guarantee or duplicate suppression. DHCP and TCP
remain separate work; the DNS client below owns its bounded query retries.


## UDP tools

`udp-send LOCAL_IP DEST_IP PORT MESSAGE` binds an ephemeral local port and sends
one datagram. Addresses are numeric dotted-decimal IPv4; destination ports are
1..65535. The command accepts one string argument (quote spaces; `""` sends an
empty datagram), up to 1472 bytes. Shell input limits may impose a smaller limit.
It allows three seconds for send acceptance and then three seconds for a reply.
Only a reply from the specified address/port is displayed; other peers do not
extend the deadline. There is no retransmission or verification that the reply
matches the sent payload. Output includes sender, byte length and a quoted
payload with nonprintable bytes escaped as `\xNN`; received terminal escape
sequences are never executed. A timeout or native error returns failure.

`udp-echo LOCAL_IP PORT [--count N]` binds an explicit address/port and replies
with exactly the received bytes, including empty/binary datagrams. It exits
successfully after thirty seconds without an arrival or N successful echoes
(1..65535). Each send has a three-second deadline; an operation failure ends the
server unsuccessfully. Packets with source port zero cannot be replied to and
are skipped. A count limit still has the idle timeout. Neither tool requires
terminal input, and neither runs by default.

### Loopback pair

Start a background server using the shell's limited [trailing `&`](../userland/shell.md#background-commands):

```text
udp-echo 127.0.0.1 9000 --count 2 &
```

Wait for its listening message, then run:

```text
udp-send 127.0.0.1 127.0.0.1 9000 "hello"
udp-send 127.0.0.1 127.0.0.1 9000 ""
```

The server exits after those two replies. This works without a NIC and on a
single CPU. The shell remains usable while the server waits; its output may
share a line with the prompt. No job control or readiness protocol is implied.

### Guest client to host service

With `socat` installed on the host, run this ordinary UDP echo service in a
separate terminal:

```sh
socat -T 10 UDP4-RECVFROM:18080,bind=127.0.0.1,reuseaddr,fork EXEC:/bin/cat
```

Boot with `make run CPUS=4 VIRTIO_NET=1`. With the stock static configuration:

```text
udp-send 10.0.2.15 10.0.2.2 18080 "hello from Pyxis"
```

No forwarding option is needed for guest-initiated traffic. Stop the host
service with Ctrl+C when finished.

### Host client to guest server

Opt into one UDP forwarding rule while booting:

```sh
make run CPUS=4 VIRTIO_NET=1 UDP_FORWARD=19000:9000
```

`UDP_FORWARD=HOST_PORT:GUEST_PORT` accepts decimal ports 1..65535 without leading
zeros and requires `VIRTIO_NET=1`. It maps host `127.0.0.1:19000` to guest
`10.0.2.15:9000`, matching the stock network configuration. It does not expose a
host LAN listener or change guest routing. Omit it for the normal unforwarded
boot. See [QEMU user networking](https://www.qemu.org/docs/master/system/devices/net.html).

In Pyxis:

```text
udp-echo 10.0.2.15 9000 --count 1 &
```

After its listening message, run on the host:

```sh
printf 'hello from host\n' | socat -T 3 - UDP4:127.0.0.1:19000
```

The host receives its bytes back; the server exits after the echo. Regular ping
and `ls`/`cat host://...` remain available with the
[optional virtio-fs setup](virtio-fs.md). Echo listeners are development tools,
not supervised public services.

## DNS queries with dig

The boot archive includes `dig [@SERVER_IP] NAME [A]` and hostname support in
`ping`. They share a bounded userspace DNS client, using UDP, clock and random
capabilities. The default resolver is `1.1.1.1`, configured by session through
`DNS_SERVER`. Numeric ping bypasses DNS and randomness entirely.

See [DNS queries and hostname ping](../userland/dns.md) for command examples, configuration,
reply matching, CNAME selection and remaining limits. The kernel contains no
DNS policy or packet parser.

## TCP request/response utility

`tcp HOST PORT [REQUEST_FILE]` connects to an IPv4 address or resolves a hostname
using the same DNS configuration as `ping`. It streams the optional file, shuts
down its write half and copies response bytes to stdout until EOF. Without a
file it sends no bytes. Request paths use the usual working directory or Pyxis
URI. The current shell has no output redirection; stdout is the inherited
console.

For a host HTTP server, start `python3 -m http.server 18080` on the host and boot
with `VIRTIO_NET=1`. Create a raw request in the directory exported through the
[optional host filesystem](virtio-fs.md):

```sh
printf 'GET / HTTP/1.0\r\nHost: localhost\r\n\r\n' > "$export_dir/request"
```

Then run `tcp 10.0.2.2 18080 host://request` in the guest. A request saved in
`home://` with an editor also works; ensure its final header line is followed by
an empty line, and use line endings accepted by the chosen server.

The response includes raw HTTP headers; this utility does not interpret HTTP.
For a receive-only exchange, `tcp 10.0.2.2 18081` can receive a file from a host
listener such as `nc --send-only -l 18081 < message.txt` (Ncat syntax).

Each connect/read/write has a fresh ten-second deadline. Failures report to
stderr, abort/close the stream and exit unsuccessfully. The request uses a 4 KiB
transfer buffer with no fixed file-size limit, but all sending precedes reading.
A protocol requiring concurrent progress in both directions can stall and time
out. There is no stdin pump, listener or total-runtime limit. Normal completion
means response EOF, not proof that a remote application processed the request.
See [outbound TCP](tcp.md) for transport limits and the transmit-only
[`ttcp` utility](tcp.md#transmit-only-ttcp).

## Further networking work

Wildcard/connected UDP, broadcast/multicast, fragmentation, IPv6, asynchronous
send and waiting on multiple objects remain outside this implementation. DHCP
needs unconfigured-address and broadcast handling as well as configuration
authority and lease deadlines; explicit-address unicast UDP alone is insufficient.
DNS queries, hostname ping and [outbound TCP](tcp.md) are implemented.
Listening and application protocols remain future work.
ICMP errors and generic UDP ephemeral-port selection are
tracked in [technical debt](../technical-debt.md#udp-icmp-errors-and-ephemeral-selection).
Keep the [users/authority checkpoint](../wip/users-and-authority.md) ahead of broader
remotely accessible services. [Network domains](../wip/later-os-directions.md#device-ownership-and-network-domains),
Retawq and userspace HTTP/HTTPS scheme providers remain future work.
