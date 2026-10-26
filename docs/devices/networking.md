# Networking: IPv4, ICMP echo and UDP

The kernel has one system-wide BSP network worker and retained logical interface
contexts. `lo` is predefined; trusted boot configuration defines ordered `ethN`
and `wlanN` names and binds supported Ethernet controllers through trusted boot
policy. Class and hardware driver are separate. A logical Wi-Fi definition does
not itself grant radio authority. The opt-in [AX200 backend](ax200-wifi.md)
supports one legacy 2.4 GHz OPEN or WPA2-Personal/CCMP link through the
[Wi-Fi interface](../interfaces/wifi.md), with independent addressing and DHCP.
Interfaces, addresses and routes remain shared across spaces. There is no tunnel API.

The implementation is in `kernel/net`, with kernel interfaces in
`include/kernel/net`. Native Ethernet, ARP, IPv4, ICMP and UDP share the worker;
TCP uses the [lwIP bridge](lwip.md). Deferred local delivery remains independent
of a physical controller. Immutable interface scopes on TCP, UDP and echo
factories bound packet authority separately from registry observation, system
policy and per-interface configuration.

Boot-init receives registry READ and policy MANAGE, prepares the declared
interfaces and selections, then mints explicit packet scopes and configuration
facets. Ordinary applications receive declared grants without policy or
configuration WRITE. Default boot without a NIC remains usable;
`make run VIRTIO_NET=1` enables QEMU user networking for the stock `eth0` profile.
There are no automatic ping probes. The
[multiple-interface reference](network-interfaces.md) summarizes identity and
authority; its [native qualification record](../development/experiments/network-interfaces-native.md)
separates exercised wired behavior from remaining lifecycle checks.

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
The shared deferred-local receive queue holds at most 16 packet pointers. ARP
holds at most 16 waiting packets, counted within that same 32-packet budget;
its global neighbor and pending pools retain the interface identity.

`net_transmit(interface, packet)` queues local delivery on `lo` or the assigned
interface address and transfers ownership only on `NET_OK`.
Success means queued, not accepted by an IP protocol or delivered to an
application. A full queue returns `NET_QUEUE_FULL`; an unavailable worker returns
`NET_UNAVAILABLE`; invalid arguments return `NET_INVALID`. Every failure leaves
the packet owned by the caller, which may retry or release it.
`net_packet_release` consumes an owned packet; NULL is harmless.

The caller must not read, modify, resend or release a successfully queued packet.
The worker takes ownership on dequeue, lends the packet to protocol processing
for that call, then releases it on every path. Each queued packet retains its
logical interface and address epoch; dequeue discards copies from a retired
address incarnation, even after an A→B→A reassignment. The private
`loopback.retired_address_drops` counter records these discards; local queue
acceptance does not promise delivery across address retirement. No reference
counting, retained payload pointers, packet
registry or device buffer sharing is introduced. Interface contexts and committed
controller bindings remain until reboot. The worker owns mutable protocol state;
each driver keeps its existing DMA storage and hardware ownership.

## Routing and configuration

Kernel address arguments are host-order integers (`0x7f000001` is `127.0.0.1`),
while wire fields use network byte order. Header layouts remain kernel-private.
The sole BSP network worker owns configuration and protocol state. With interrupts
enabled, `net_ipv4_configure(interface, address, prefix, gateway)` replaces that
context's settings; `net_ipv4_clear(interface)` removes them. Neither is an AP or
arbitrary BSP-task interface. Per-interface configuration facets serialize
userspace requests onto that worker; direct kernel calls are only for the worker.

Replacement validates all fields before changing state. Prefixes 1..32 are
accepted; the address must be unicast, outside `0/8`, `127/8`, multicast and
reserved high addresses. Network/broadcast addresses are rejected for prefixes
through /30; /31 has two host addresses and /32 one. A nonzero gateway must be a
distinct valid host on that connected subnet. Zero means no default route. Missing
hardware returns `NET_UNAVAILABLE`; invalid settings return `NET_INVALID` and
preserve the previous configuration.

New operations apply this route policy within the held factory scope:

1. An explicit `net_path.interface_id` or source restricts the eligible interface.
   Conflicting restrictions return `CALL_BAD_REQUEST`; an out-of-scope interface
   returns `CALL_DENIED`.
2. Loopback and host-owned destinations select their owning interface first.
   That interface must satisfy the scope and explicit restrictions. Local
   delivery cannot bypass authority or cross to another pinned interface.
3. Among usable eligible interfaces, connected preference ranks Ethernet before
   Wi-Fi, then the lower observed TX/RX rate within the class (unknown last).
   Prefix length breaks ties only within the same rank; stable order breaks
   equal-prefix ties. Both automatic and pinned-default policies use this rule.
   Overlapping subnets are accepted; duplicate local addresses and Wi-Fi lease
   addresses equal to another interface's gateway/DNS are refused. See
   [lease admission](network-interfaces.md#wi-fi-lease-admission).
4. Otherwise a pinned operation (including a one-interface factory) uses only its
   interface's gateway. Automatic policy selects a usable default within the held
   scope using the same preference; explicit policy uses its selected default
   without fallback. See [selection](net0-selection.md#automatic-route-preference).

`net_factory_route` returns the interface, address epoch, source and next hop.
Internal TCP projection routing uses the same policy. Retained consumers use
`net_ipv4_route_on(interface, ...)`, which revalidates their captured interface
without repinning. Local delivery works with physical carrier down. Missing
eligible routes return `CALL_NO_ROUTE`; a selected unusable interface returns
`CALL_UNAVAILABLE`. Default-policy changes affect new operations, not existing
connections or endpoints.

Loopback sources and destinations never escape onto Ethernet. A caller-supplied
source must match the selected route. Configuration replacement/clear invalidates
ARP and completes outstanding non-loopback echo calls as unavailable, including
calls to the previous local NIC address. Numerical address replacement or clear
retires its address epoch; same-address renewal and prefix/gateway changes keep
that epoch while revising routing. Concrete UDP and TCP objects retain their
interface and address incarnation. In-flight DMA storage keeps its driver
lifetime; reconfiguration cannot retract a frame already submitted. Ordinary
link down/up preserves the assigned address and gateway.

## IPv4 and echo

`net_ipv4_transmit` takes the retained interface and an owned packet with 20
reserved header bytes followed by initialized payload, source/destination,
protocol, deadline and a completion tag/token (NONE for generated replies). It
fills the header and submits through that interface's local delivery or
ARP/Ethernet path. `net_ipv4_submit` accepts an already complete datagram without
rewriting headers and rechecks source/route authority on the supplied interface.
Success transfers ownership; failure retains caller ownership, although transmit
may have filled the header. Replies retain the ingress interface and use the
request destination as their source.

Driver service carries its interface through Ethernet into IPv4 receive. Deferred
local receive carries the same logical interface separately from physical
Ethernet ingress. IPv4 checks version, header and total lengths, reserved flags,
header checksum and that interface's source/destination acceptance rules before
protocol dispatch. It ignores bytes beyond IPv4 total length, so trailing link
padding cannot become echo payload. ICMP and UDP use the native paths; validated
TCP datagrams enter that interface's lwIP projection. Other protocols, options
and fragments are counted and discarded. Only an ordinary 20-byte IPv4 header is
supported; DF is allowed, MF and nonzero fragment offsets are not.
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

Trusted setup mints an `echo` factory over an explicit interface set. Session
handoff and ordinary shell launches copy its ECHO_RIGHT_SEND grant. It authorizes
ICMP echo exchanges within that scope. Requests may restrict interface/source
through `net_path`; they grant no raw headers, configuration or routing changes.
The stack and request budget remain shared between spaces. Configuration
uses a distinct authority that ordinary applications do not inherit.

[The echo protocol](../../include/abi/echo.h) uses the existing CALL syscall with a
tagged ECHO_EXCHANGE request. Its input is a path restriction, host-order IPv4 destination and an
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

Each exchange owns its pending slot, so sibling tasks and other processes
exchange independently until the slots run out (QUEUE_FULL). The blocked call
keeps the echo object alive through a CLOSE of its handle, whether by a sibling or
elsewhere; request state belongs to the call, not the object. No mapping is held across the
wait: the reply is copied after wake, and a sibling that unmapped it meanwhile
makes the call return BAD_BUFFER. A process stop wakes the wait, cancels the slot
and returns ENDPOINT_CLOSED; there is no other cancellation.

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

Before AP startup, `virtio_net_prepare` enumerates every modern network PCI
function (`1af4:1041`) into independent retained controller state, confirms reset
before BAR probing, maps its registers
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
and `DRIVER_OK` clear and MSI-X delivery masked. Configuration binding in the
BSP network worker activates only the selected controller: `virtio_net_start`
enables DMA, sets `DRIVER_OK`, unmasks MSI-X and notifies RX. Unbound controllers
and worker creation failure leave hardware inactive. Table
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

`virtio_net_transmit(controller, frame, length)` is a BSP network-worker interface, with
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

At `boot_start_cpus`, inspect the retained controllers from `virtio_net_first()`
and `virtio_net_next()`. Each supported controller's `prepared` should be true,
RX should have sixteen outstanding buffers, and TX none. Common status should
be `ACKNOWLEDGE | DRIVER | FEATURES_OK` (`0x0b`), with bus mastering clear and
MSI-X masked. After a committed binding activates that controller, status should
be `0x0f`, bus mastering enabled and entry zero/function delivery unmasked.
Unselected controllers remain inactive.
Read MMIO only at individual register widths; do not read the ISR merely for
inspection because that acknowledges pending interrupts.

Each controller's `rx` and `tx` expose physical/virtual storage, ownership, ring
indices and outstanding counts. Its `interrupts`, `received`,
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

Ethernet accepts its own unicast destination and broadcast ARP/IPv4. Ordinary
IPv4 traffic must be unicast to the configured IP. UDP limited broadcast and
the configured subnet broadcast are admitted only for wildcard endpoints below;
/31 and /32 have no subnet broadcast. Broadcast-MAC frames carrying a unicast
IP destination remain discarded. External loopback, nonlocal and multicast
IP traffic is discarded. Incoming IPv4 sources still require a valid unicast
host address, including before our address is assigned. Frames use Ethernet II with IPv4/ARP types, padding to
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

[The native control protocols](../../include/abi/net_config.h) separate three
objects and authorities:

| Object | Authority and operations |
| --- | --- |
| Registry | READ enumerates retained controllers/interfaces, reads selected policy and looks up an interface READ facet by boot-local ID. Lookup grants no packet or configuration WRITE authority. |
| Policy administrator | MANAGE defines ordered logical interfaces, binds batches, selects system route/DNS/management policy, mints interface facets and creates explicit immutable TCP/UDP/echo scopes. |
| Interface facet | READ queries one retained interface; WRITE replaces/clears its L3/DNS contribution and reports DHCP phase for an expected address epoch. `lo` never receives WRITE. |

Boot-local interface IDs never recycle. Names are canonical class-matching
`ethN`/`wlanN`; a held facet refers to identity rather than a later name lookup.
Controller inventory is read-only and includes unselected inactive hardware,
class, driver, preparation, reported carrier and known identity. Enumeration uses
ascending opaque IDs and zero to end. Interface snapshots distinguish definition,
binding, preparation, readiness, carrier, authentication, address, MAC, DNS and
DHCP-report validity. Unknown facts remain invalid rather than fabricated.

DEFINE and BIND copy and validate their entire batches before publication. BSP
prepares backing; only the network worker publishes definitions and bindings.
Matching is class-filtered and unique: ambiguous selection or a controller owned
by another interface returns `CALL_BUSY`. Failed validation changes no binding.
Activation follows committed associations; activation failure retains the binding
until reboot. Repeating the same association is idempotent. The exact-controller
selector admits prepared devices without known/up carrier. The linked-controller
selector additionally requires complete discovery and fresh prepared/up carrier
before first commitment. See [interface selection](net0-selection.md).

Interface REPLACE validates address/prefix/gateway and a per-interface DNS
contribution together. DNS zero means unknown. CLEAR retires address state and
clears contributions/reported DHCP state, preserving the binding. Numerical
address replacement retires concrete transports; same-address renewal preserves
the incarnation. REPORT_DHCP stamps monotonic time and rejects a stale address
epoch with `CALL_UNAVAILABLE`. It records the maintainer's last assertion, not
kernel-verified lease health. Libpyxis supplies the corresponding typed helpers.

Policy REPLACE validates all referenced IDs before selecting the default route,
DNS interface/source and separate Remote, log and debugger interfaces.
`NET_ROUTE_AUTOMATIC` requires zero default ID in requests; snapshots return the
current effective global default (zero when none is usable). Explicit
`NET_ROUTE_PINNED` keeps its requested default or none. DNS mode is none, numeric
with an optional interface pin, or a named interface's current DHCP contribution.
Numeric DNS with zero interface ID uses the caller's already-held factory scope.
Contributions never seize system policy, and missing DNS has no resolver fallback.
Selected management does not retarget already captured Remote factories, debugger stops or fatal-log transports.

Factory creation takes a nonempty copied set of retained IDs and operation
rights. Later definitions never enter that scope. Copies retain scope and native
factory derivation can create a one-interface subset with reduced rights;
forwarder lineage is preserved. These operations grant no IPC transport rights
or registry/configuration authority. Existing aggregate protocol budgets remain
shared, rather than multiplied per interface. lwIP's 255 nonzero projection
indices bound retained projections, including `lo`; allocation may fail earlier.

Eight shared control slots include completed calls until collection; exhaustion
returns `CALL_QUEUE_FULL`. The worker processes finite operations without waiting
for a device or borrowing user/private-stack memory. BSP allocates/reclaims
backing and installs returned handles. Failed copyout closes only the returned
handle generation. Settings survive capability close, and closing a copied grant
does not cancel an already admitted call. There is no new syscall or per-space
network namespace.

## Boot configuration and use

Trusted boot-init reads the complete ordered network profile before publishing
definitions, bindings and policy: `boot://config/network.lua` on live boots and
in the rescue space, the pool's
[`system://config/network.lua`](../userland/init.md#installed-configuration) on
installed boots. The stock profile is:

```lua
return {
  wired = { auto = true, exclude = {} },
  interfaces = {},
  routing = { auto = true },
  dns = { server = "1.1.1.1" },
  management = { remote = "eth0", log = "eth0", debug = "eth0" },
}
```

`wired.auto = true` waits for the complete boot inventory, then covers every
supported wired controller, including carrier-down and unknown-carrier devices.
Explicit entries reserve names and controllers first. In automatic mode an
explicit Ethernet linked role reserves its preferred prepared controller even
with down/unknown carrier; binding still waits for fresh known/up carrier.
Explicit-only linked selection is unchanged. Remaining controllers get
unused `ethN` names in ascending boot-local controller-ID order, with independent
optional DHCP clients. Use explicit MAC entries for names that must survive a
changed inventory. `wired.exclude = {"02:00:00:00:00:01", ...}` omits those MACs
from automatic definitions, DHCP and delegated packet scopes; explicitly selecting
an excluded wired NIC is an error. It does not disable mandatory driver draining.
Omitting `wired`, or using `auto = false`, retains explicit-only configuration.

Initial discovery has a ten-second setup deadline; failure publishes no automatic
plan. The resolved plan permits 254 non-loopback definitions. Bindings and packet
scopes remain fixed until reboot; brand-new post-boot adapters are outside this
slice. Same-controller recovery keeps its existing contract.

Each explicit entry chooses exactly one selector: `select = "link"`,
`driver = "virtio"` or a nonzero unicast `mac = "..."`. Link selection can rank
eligible candidates with `prefer = {"MAC", ...}`, then ascending controller ID.
A controller selected twice or an ambiguous explicit selector is refused.
Optional absent entries remain defined and unbound; they can be bound later,
without replacing an existing association. One absent interface does not hold up
a working peer's client or the local shell. A Wi-Fi definition remains unbound
until a configured, enabled radio backend binds it; ordinary spaces receive no
radio rights unless explicitly configured.

`dhcp = true` excludes static `address`, `prefix` and `gateway`. Static addresses
use four decimal octets, prefixes 1..32 and an optional gateway; validation checks
subnet/gateway relationships and duplicate addresses before effects. The legacy
`net0` profile is rejected. Convert private profiles to an ordered interface
entry and explicit routing/DNS/management names; names must refer to `lo` or a
declared interface. In automatic mode a canonical generated `ethN` reference
may be absent from the discovered set; that selection is unavailable, without
fallback, so an offline or all-excluded machine still boots locally. The stock
route and numeric DNS use [automatic preference](net0-selection.md#automatic-route-preference)
within held scopes; management remains pinned to `eth0`.
`routing.auto = true` excludes a named `routing.default`. Numeric DNS may omit
`interface` for routed selection; DHCP DNS requires an explicit interface.
An absent profile yields an empty plan rather than inferred
hardware policy. Unknown keys, malformed names, holes in arrays and wrong types
are errors. There is no live reload or controller reassignment.

Use `NETWORK_CONFIG=/private/path/network.lua` with the
[image profile override](../development/configuration.md#image-network-profile)
to package machine-specific settings without changing the userspace checkout.
The kernel and drivers contain no QEMU address defaults. Existing
[link-selection measurements](../development/link-selection-qualification.md)
and [RTL8111 hardware evidence](rtl8111-hardware.md#ethernet-io) cover their stated
historical revisions, not the multiple-interface cut.

Trusted session network ownership receives declared `config.NAME` facets and
one-interface `dhcp.NAME` broadcast factories. Private `network.NAME` controller-ID
text carries the resolved initial choice across the trusted SESSION handoff,
including temporarily unbound entries; text alone grants no authority and is not
exported to ordinary children. The owner adopts that plan rather than discovering
new automatic members after startup. Each [DHCP client](dhcp.md) keeps
its own endpoint, transaction and monotonic lease timers. Ordinary packet scopes
contain the explicitly configured boot-time ID set plus `lo`; later definitions
are excluded. Ordinary children receive no policy or interface WRITE authority.
Remote receives the selected interface READ facet and matching packet factories;
it waits for that interface's address and preserves ingress identity for reverse
connections.

Startup supplies canonical `DNS_SERVER` text together with a declared `dns_udp`
factory copied with reduced rights from the held UDP scope for routed numeric
DNS, or derived for an explicitly selected DNS interface. A named path outside
that scope is unavailable; setup never widens packet authority. Routed numeric
DNS is still delegated before any usable route exists, so later queries can
observe link/address recovery without relaunching the program. Existing processes
retain their startup choice. A numeric server is not proof of reachability. DNS packet
parsing remains in userspace; kernel policy stores only explicit selection and
interface contributions.

Session and network configuration use the restricted
[Lua evaluator](../userland/lua.md#embedding-and-session-configuration).
The default init mounts optional `host://` before session handoff.

```sh
make run VIRTIO_NET=1
```

Use `net` for read-only interface/policy inventory and last-reported DHCP phase
with validity and age. It does not grant configuration or packet authority.
Default no-NIC boot still supports `ping 127.0.0.1`. With the stock QEMU lease,
`ping 10.0.2.15` checks local delivery and `ping 10.0.2.2` checks the backend
gateway. Internet ICMP depends on host ping-socket permission and a reachable
peer; an external timeout alone does not identify a guest-stack failure.
The [multiple-interface reference](network-interfaces.md) records current
qualification and limits. IPv6, forwarding, bridging, NAT and richer route policy
remain outside this implementation.

## Native UDP endpoint lifetime

Trusted setup mints explicit UDP factories. Ordinary local/remote children receive
OPEN within their declared scope; DHCP receives separate one-interface BROADCAST
authority. An OPEN binds an exact local source on an allowed interface, including
`127/8` on `lo`. A wildcard/broadcast endpoint covers exactly one selected
interface, never every interface in a factory. The same port can be used on
different interfaces. A wildcard owns its port exclusively against concrete
bindings on its own interface (`CALL_BUSY`); duplicate concrete tuples return
`CALL_ALREADY_EXISTS`. There is no privileged-port distinction or per-space
port namespace, and the admission budgets remain global.

[The UDP protocols](../../include/abi/udp.h) use tagged CALL requests:

- `UDP_OPEN` on `PROTOCOL_UDP_SERVICE` takes a `net_path` with an exact local
  source and a port. An optional interface ID further restricts it. Zero port
  selects a free ephemeral port through a rotating scan of 49152–65535.
  Success returns an owned handle and bound address/port/state/interface/epoch.
  Ephemeral selection skips concrete and wildcard conflicts on that interface.
- `UDP_OPEN_ROUTE` takes independent destination and path-restriction fields.
  The worker selects a source within the factory scope and restrictions, then
  binds it in the same operation. Invalid/conflicting input returns
  `CALL_BAD_REQUEST`, missing routes `CALL_NO_ROUTE`, and a selected unusable
  interface `CALL_UNAVAILABLE`. Selection sends no packets, performs no ARP and
  proves no peer reachability.
- `UDP_OPEN_BROADCAST` requires BROADCAST, source zero, and either an explicit
  allowed interface ID or a one-interface factory. Ambiguous scope is
  `CALL_BAD_REQUEST`. The selected interface must be bound, but need not be
  assigned. It receives UDP to that interface's current address, limited
  broadcast or subnet broadcast. Loopback never selects a wildcard.
- `UDP_INSPECT` requires INSPECT and reports original address/port, state,
  interface ID and captured address epoch, including after invalidation. A
  broadcast endpoint reports address/epoch zero because its binding is address
  independent. BOUND describes the binding, not carrier.
- `UDP_SHUTDOWN` requires SHUTDOWN, returns no bytes and is idempotent. The worker
  releases the binding before completing the call; another endpoint may then
  acquire it while handles to the old object still exist.

New endpoints carry INSPECT, SEND, RECEIVE and SHUTDOWN rights. Existing handle
copying and launch grants can reduce those rights. UDP tools start only when
launched; trusted DHCP setup has its own declared broadcast authority.

Libpyxis provides `udp_open`, `udp_open_route`, `udp_inspect` and `udp_shutdown`,
with `_path` open variants for explicit interface/source restrictions. Factory
scope cursors and one-interface derivation use the held service protocol.
`udp_open_route(service, destination, port, &reply)` returns the selected local
address in `reply.local`. Both open helpers and INSPECT
require output pointers and clear their outputs on failure; both open helpers use
`HANDLE_INVALID`. At the raw CALL boundary, errors leave reply bytes untouched.
Payloads and output mappings are checked before any work is published.

The network worker owns bindings and endpoint state. Dynamic control records
include completed calls until their callers resume and consume shared transport
storage; allocation/allowance exhaustion is `CALL_NO_MEMORY`. OPEN lends the parked task's kernel-owned capability table to
the BSP worker, which allocates and installs the endpoint with interrupts disabled
before publishing its binding. Installation failure frees the unpublished object;
no port is reserved. Other endpoint calls borrow the caller's live grant until
completion. No user/private-stack pointers cross CPUs and no scheduler queue is
added.

Route selection and binding are serialized with configuration changes by that
same worker. OPEN/OPEN_ROUTE retain only their concrete local binding, not the
destination or a route snapshot: it is neither connected to a peer nor a
wildcard listener. Later sends use their own destinations and current routing,
subject to the bound source address. The existing invalidation and shutdown
rules below apply to those two operations.

Endpoint, control and pending-I/O records are dynamically owned under the shared
64 MiB TCP/UDP storage allowance, including stopped objects still retained by
handles. Each TCP stream preallocates its receive ring and reserves ingress
capacity before admission; native records and new transmit data cannot spend
those credits. A 64 KiB control allowance is separate. Exhaustion refuses with
NO_MEMORY; final logical close and retirement need no new operation allocation.

Removing or replacing the NIC's address marks its concrete bound endpoints UNAVAILABLE
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
handles. Another call in the same direction is `CALL_BUSY`. Dynamically allocated
control and I/O records retain direction ownership through synchronous collection
or native completion transfer, and remain charged until actual release.
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
have not resumed. Concrete endpoint address removal does the same with
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
receive timeout. Wildcard endpoints survive IPv4 replacement/clear, retaining
queued datagrams and pending receives. Their sends use the current assigned
source; while unassigned, only limited broadcast may use source zero. Limited
broadcast sends copy directly to Ethernet broadcast without ARP. Other sends
require ordinary routing on the pinned interface and an assigned source; loopback and
subnet-broadcast sends are unsupported for wildcard endpoints. ARP-pending
sends still fail on configuration changes. There is no fragmentation, reassembly, multicast,
retransmission, delivery/order guarantee or duplicate suppression. DHCP and the
DNS client own their bounded retries above this datagram service.


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

Boot with `make run CPUS=4 VIRTIO_NET=1`. With the stock QEMU DHCP lease:

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
capabilities. The stock profile explicitly selects `1.1.1.1` on `eth0`. Startup pairs
`DNS_SERVER` with the declared scoped `dns_udp` grant; missing authority means
unavailable resolution. Numeric ping bypasses DNS and randomness entirely.

See [DNS queries and hostname ping](../userland/dns.md) for command examples, configuration,
reply matching, CNAME selection and remaining limits. The kernel stores explicit DNS selection and contributions, but contains no
DNS packet parser or resolver.

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
`tmp://` with an editor also works; ensure its final header line is followed by
an empty line, and use line endings accepted by the chosen server.

The response includes raw HTTP headers; this utility does not interpret HTTP.
For a receive-only exchange, `tcp 10.0.2.2 18081` can receive a file from a host
listener such as `nc --send-only -l 18081 < message.txt` (Ncat syntax).

Each connect/read/write has a fresh ten-second deadline. Failures report to
stderr, abort/close the stream and exit unsuccessfully. The request uses a 4 KiB
transfer buffer with no fixed file-size limit, but all sending precedes reading.
A protocol requiring concurrent progress in both directions can stall and time
out. This request/response mode has no stdin pump or total-runtime limit. Normal completion
means response EOF, not proof that a remote application processed the request.
See [outbound TCP](tcp.md) for transport limits and the
[`ttcp` utility](tcp.md#ttcp).
Trusted init can separately delegate a bound listener to the
[concurrent `tcp --serve` consumer](tcp.md#concurrent-echo-server).

## Further networking work

Connected UDP, multicast, fragmentation, IPv6, asynchronous send and waiting
on multiple objects remain outside this implementation. [DHCP](dhcp.md) has the
narrow unconfigured-source/broadcast primitive and userspace lease maintenance.
[Interface selection](net0-selection.md) is implemented; runtime controller
rebinding remains separate work.
DNS queries, hostname ping and [native TCP listeners/streams](tcp.md) are implemented.
Remote-terminal application protocols have their own [reference](../userland/remote-terminal.md).
ICMP errors and generic UDP ephemeral-port selection are
tracked in [technical debt](../technical-debt.md#udp-icmp-errors-and-ephemeral-selection).
Keep the [users/authority checkpoint](../wip/users-and-authority.md) ahead of broader
remotely accessible services. [Network domains](../wip/later-os-directions.md#device-ownership-and-network-domains),
Retawq and userspace HTTP/HTTPS scheme providers remain future work.
