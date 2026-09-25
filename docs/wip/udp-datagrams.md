# Userspace UDP datagrams

Status: agreed initial scope and PR sequence. This milestone follows the
completed [IPv4/ICMP work](../networking.md). Settle the remaining task-specific
choices below before implementing the affected task; this document does not
freeze wire layouts or authorize broader networking work.

## Completion boundary

A native userspace UDP client and echo server exchange datagrams over loopback.
The client also exchanges traffic with a host UDP service through virtio-net,
using the existing static IPv4 configuration. Ordinary ping, host filesystem
access, presentation and scheduling continue to work while UDP calls wait.

Use the existing native capability model and tagged CALL requests. No POSIX
socket syscall layer, generic transport framework or asynchronous IPC redesign
is required. DHCP, DNS and TCP remain separate milestones. This is a bounded
IPv4 UDP subset, not a claim of full Internet host conformance.

## Objects and authority

A UDP service capability authorizes opening a bound datagram endpoint. Opening
supplies a local IPv4 address and port, and returns an owned endpoint handle
plus the actual bound address and port. Port zero requests an ephemeral port;
it does not create a receive binding on wire port zero.

The endpoint supports send, receive, inspection of its local address, and
shutdown. SEND, RECEIVE and SHUTDOWN are separate object-specific rights, so a
holder can delegate traffic access without granting shutdown authority. Copying
a handle shares the same endpoint and queues; it does not create another binding.

Creation authority, endpoint authority and network-configuration authority are
distinct. An address or port number identifies a binding, never permission.
Choose startup delegation of creation authority in task 1; do not implicitly
reuse the echo or configuration grants. The stack and port namespace remain
system-wide, with no new per-space isolation model.

## Binding and routing

Require an explicit local IPv4 address: an address on the existing loopback
route or the assigned NIC address. Reject duplicate address/port bindings.
The same numeric port on different explicit local addresses is a different
binding. Wildcard addresses, shared/reused bindings, rebinding and connected UDP
are outside this milestone.

Send supplies the destination IPv4 address and port. The bound address supplies
the source; routing must not silently select another source to make a send work.
Loopback stays local. Delivery to the assigned NIC address also remains local,
following the existing routing precedence. External traffic uses connected or
default routing and bounded ARP resolution.

Ordinary link down/up preserves bindings. Removing a configured local address
makes endpoints bound to it unavailable; they must be closed and reopened rather
than silently revived if the address returns. Loopback bindings remain independent.
Distinguish address removal from changing only the prefix or gateway; settle
pending-send handling for those route changes in task 2.

## Datagram operations

One send supplies one datagram; one successful receive returns one datagram and
its sender's IPv4 address and port. Zero-length datagrams are valid and do not
mean EOF. There is no promise of delivery, ordering or duplicate suppression.
These are UDP properties, not an emulated reliable request/reply protocol.
See [RFC 768](https://www.rfc-editor.org/rfc/rfc768.html).

Keep the existing 1500-byte IP limit and ordinary 20-byte IPv4 header. The
eight-byte UDP header leaves at most 1472 payload bytes. Oversized sends fail
explicitly; IP options, fragmentation and reassembly stay deferred. Validate UDP
lengths and checksum semantics against the IPv4 payload before dispatch. Use the
IPv4 pseudo-header when producing/checking UDP checksums; packet headers remain
kernel-private.

Send and receive take absolute deadlines in the existing monotonic epoch. Send
completion means acceptance by deferred local delivery or the NIC transport,
including any preceding ARP wait. It does not wait for a reply or promise remote
delivery. A timed-out software packet must not later be submitted after ARP
resolution; a deadline cannot retract bytes already submitted to the device.

Receive waits for a queued datagram. An undersized destination buffer gets an
explicit error and leaves that datagram queued, with no partial consumption or
silent truncation. The caller can retry with the documented maximum. Capture
and check user buffers before parking, and leave output untouched on failure.
Settle exact status encodings, deadline limits and competing-waiter behavior
before task 2; do not silently inherit stream EOF or socket semantics.

## Bounds, ownership and lifetime

Start with four queued receive datagrams per endpoint and sixteen globally,
counted within the existing software packet budget. Drop new arrivals when a
receive bound is reached, preserving queued datagrams. Keep diagnostic drop
counts without introducing a statistics framework. UDP receive queues must not
consume the entire packet budget and permanently exclude other protocols.
Endpoint count and pending-call limits still need concrete bounds in task 1/2.

Keep protocol state, allocation policy and binding changes with the existing
BSP network worker. AP callers hand over captured data in stable shared storage;
no user pointers, caller stack pointers or private mappings cross CPUs. Received
DMA bytes must be copied into owned storage before the driver reposts them.
Creation failure, including inability to install the returned capability, must
unwind the reserved binding and object. Reuse existing capability-table growth
and retirement machinery where applicable.

Closing one handle releases that reference without revoking other copies.
Explicit SHUTDOWN, requiring its own right, stops the shared endpoint and wakes
pending operations with a closed result. Final release cleans up the binding and
queued storage. Define exactly when shutdown releases the port and how it orders
against worker delivery before implementing it. Outstanding DMA retains its
existing driver-owned lifetime; neither shutdown nor final close can free it early.

A blocked call keeps its live grant and mappings, as in the current single-task
process model. Other copies can request shutdown, but closing a copied grant
alone does not cancel the call. Endpoint/operation identities must prevent a late
completion from touching a retired object or a reused binding. Do not claim that
UDP can distinguish an old remote datagram from a new use of the same tuple.

## Focused tasks

1. [ ] **Endpoint creation, binding, rights and lifetime.** Define the tagged
   service/endpoint ABI and libpyxis helpers for opening, inspecting and shutting
   down endpoints. Add explicit startup delegation, worker-owned binding changes,
   ephemeral allocation, capability installation/unwind and final cleanup.
   Before implementation, settle the endpoint limit, creation-authority policy,
   inspection rights, ephemeral selection and shutdown/port-release ordering.
   The dynamic range in [RFC 6335](https://www.rfc-editor.org/rfc/rfc6335.html#section-6)
   is a starting point for ephemeral selection, not an allocation algorithm.
   Validate ordinary creation/lifetime paths with builds, boots and debugger
   inspection. Do not add successful placeholder send/receive operations.

2. [ ] **Wire processing, bounded delivery and deadlines.** Add UDP parsing and
   checksums, binding lookup, bounded receive queues, tagged send/receive calls,
   wait/completion handling and integration with IPv4/ARP. Preserve echo behavior.
   Settle pending-call bounds, simultaneous calls on shared endpoints, deadline
   policy, undersized-buffer status and route-change ordering first. Audit the
   current echo-specific transmit completion path and adapt it for the two
   concrete consumers without a generic callback framework. Decide and document
   treatment of unbound destination ports and ICMP errors rather than implying
   full UDP host conformance. Validate with ordinary traffic and debugger
   inspection; no packet-injection or self-test harness.

3. [ ] **Userspace tools and ordinary host use.** Add a small client and echo
   server, with bounded waits and a usable exit policy. Define their CLI and a
   practical way to launch a local pair with the current foreground-only shell;
   general job control is not part of this task. Document a real host service
   and any opt-in QEMU forwarding needed, keeping default boot free of daemons
   and network probes. Demonstrate loopback and external exchanges alongside
   ping and virtio-fs. Record the implemented limits and consolidate this plan
   into `docs/networking.md`; preserve deferred work in the relevant WIP notes.

Each task is a review boundary for focused commits/PRs, not an invitation to land
unused abstraction layers. Discuss remaining decisions before the affected task.
No tests, test infrastructure, fault injection, CI changes or boot/output
automation. Validation uses ordinary builds, QEMU boots and debugger inspection.

## Deferred work

Wildcard/connected UDP, multicast and broadcast, fragmentation, DHCP, DNS, TCP,
IPv6, asynchronous send and waiting on multiple objects are outside this slice.
DHCP will need unconfigured-address/broadcast handling and delegated configuration
authority; explicit-address unicast UDP alone does not provide that milestone.

Retawq and userspace HTTP/HTTPS scheme providers remain future application work.
Keep the [users/authority checkpoint](users-and-authority.md) ahead of broader
remotely accessible services and the [network-domain direction](later-os-directions.md#device-ownership-and-network-domains)
separate from these shared-stack bindings. Do not run an echo service by default.

Protocol references: [UDP](https://www.rfc-editor.org/rfc/rfc768.html),
[IPv4 host UDP requirements](https://www.rfc-editor.org/rfc/rfc1122.html#section-4.1),
[service and dynamic port ranges](https://www.rfc-editor.org/rfc/rfc6335.html#section-6).
