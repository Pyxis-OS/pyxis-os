# Initial networking: loopback and manually configured IPv4

Status: agreed completion boundary and design direction, with a proposed PR
sequence below. This is planning, not authorization to implement the whole
milestone. Discuss the listed open decisions before the affected task.

## Completion boundary

A native userspace ping utility can exchange ICMP echo traffic through both
loopback and a modern virtio-net interface. The external interface uses manual
IPv4 configuration. Existing boot, presentation, host filesystem access and
userspace scheduling continue to work while networking waits for packets.

Loopback is part of this milestone, not a future accommodation. It works without
a virtio-net device. DHCP is wanted later; TCP and the first web server belong
to subsequent milestones. This slice is a deliberately limited IPv4 host, not
a claim of a complete Internet protocol implementation.

## Agreed design boundaries

An interface represents a network attachment; a driver owns hardware transport.
Virtio-net provides an Ethernet interface. Loopback provides local IP delivery
without PCI, DMA, Ethernet headers or ARP. Both enter the same IP receive path.
Use a small concrete interface boundary, not a general driver framework.

Provide a loopback interface named `lo`, initially configured as `127.0.0.1/8`.
Keep loopback traffic local: addresses in the loopback range must never escape
through the Ethernet device. Queue local delivery rather than recursively
entering receive processing from transmission. The local path must obey the
same packet ownership, resource bounds and completion rules as external traffic.
Loopback must not acquire a fake MAC address or depend on Ethernet initialization.

Manual configuration supplies an IPv4 address, prefix length and optional
default gateway. Keep configuration separate from device discovery and driver
initialization. Initial routing covers loopback, directly connected destinations
and an optional default route. No forwarding between interfaces is required.

Init holds configuration authority. Application communication is a separate
grant: permission to send traffic must not imply permission to change interface
addresses or routes. Interface names identify resources, not authority.

DHCP will use the same address/route configuration operations as manual setup.
The configuration contract needs a defined way to replace or remove settings;
do not make addresses immutable driver constants. A later DHCP client also needs
UDP, broadcast handling, lease deadlines and authority delegation. None of those
requires a DHCP-specific path through virtio-net now.

## Execution, memory and transport

Preserve the existing BSP allocation and shared-mapping rules. Device buffers
have distinct CPU virtual and DMA physical addresses; user pointers never become
device descriptors. Ownership must cover posted receive buffers, completed
packets, pending ARP resolution and in-flight transmissions.

Use a BSP worker for packet processing and a short interrupt handler for
recording activity and waking it. Keep parsing, allocation and protocol work
outside interrupt entry. Bound queued packets and work per scheduling turn so
receive traffic cannot consume all memory or prevent other tasks from running.
The chosen bounds and exhaustion/drop policy must be explicit at implementation.

Reuse existing PCI resource ownership, modern VirtIO setup, MSI-X and split-ring
knowledge where it fits. The current queue helper allows one request/reply chain
in flight and belongs to the virtio-fs use case. Networking needs posted receive
buffers and independently completed transmissions; do not simulate it as a FUSE
request/reply exchange or redesign the filesystem worker to accommodate it.
Extract shared mechanics only where both concrete consumers benefit.

Use VirtIO 1.4 as the reference. Feature selection, queue layout, buffer sizing,
interrupt routing and reset/failure behavior need their own review before the
driver task. Do not add offloads, multiple queue pairs or userspace DMA merely
because the device advertises them. A failed network device must leave loopback,
the filesystem and the rest of the OS usable.

Monotonic time supplies echo deadlines, ARP retries and expiry. No indefinite
wait for a peer or address resolution. Closing a communication handle must have
a defined relationship to pending work; a timeout alone never releases memory
still owned by the device.

## Decisions before implementation

The task 1 decisions are settled: one system-wide stack with shared interfaces,
routes and loopback; a small in-tree Ethernet/ARP/IPv4/ICMP implementation; and
one BSP worker with bounded queues. Spaces do not yet have network isolation.
Reconsider existing stack implementations before the later TCP milestone.

Packet lifetime, queue ownership and the initial budgets are implemented in
[network interfaces and loopback delivery](../networking.md). There is one
boot-lifetime loopback descriptor; no speculative interface registry or
namespace fields are needed yet.

Remaining decisions:

- **IPv4 subset:** define source-address selection, local delivery to assigned
  external addresses, route precedence, MTU limits and behavior for unsupported
  options/fragments. Check lengths, byte order and checksums before access.
  Do not silently treat an incomplete implementation as full IPv4 support.
- **Native ABI:** choose the configuration and echo object requests, rights,
  reply matching, deadlines and close behavior before task 3. A focused echo
  facility may suffice for ping; unrestricted raw-packet authority is not an
  implicit requirement. Continue tagged requests and explicit capability grants.
  New syscalls are allowed if a concrete contract needs them. This milestone
  must not commit future networking to synchronous calls or add placeholder
  send/receive/wait APIs.
- **Host setup and init policy:** select the QEMU backend and a reachable peer,
  opt-in/default device behavior, configuration command syntax and missing-device
  policy before integration. Numeric addresses suffice; no DNS dependency.
  If Lua configuration gains a second consumer, revisit the
  [shared configuration helper](later-os-directions.md#lua-follow-ups).

## Proposed focused PRs

1. [x] **Interfaces, packet ownership and deferred loopback.** Resolve scope and
   implementation ownership first, then introduce the small interface/packet
   boundary and local delivery queue. No hardware dependency or boot-time probe.
   Implemented a 32-packet software allocation budget, 16-entry loopback queue,
   event-driven BSP worker and yield after eight deliveries. Send errors retain
   caller ownership. The worker explicitly discards unsupported input; IPv4
   processing and addressing remain task 2. See the [current contract](../networking.md).
   Validate with ordinary builds/boots and debugger inspection.
2. [ ] **Local IPv4 routing and ICMP echo.** Add the selected packet validation,
   local routing and echo processing on the common IP path, with explicit limits
   for unsupported packets. Keep wire layouts out of the application ABI.
3. [ ] **Native configuration and ping.** Define the agreed capability contract,
   libpyxis helpers and a small numeric-address ping utility. Complete ordinary
   userspace ping over loopback without a network device, including bounded waits.
4. [ ] **Virtio-net transport.** Prepare the selected PCI function and owned RX/TX
   queues, connect MSI-X and the BSP worker, and define failure/cleanup behavior.
   Keep virtio-fs working alongside it. Split resource preparation and active
   queues into separate PRs if needed for review.
5. [ ] **Ethernet, ARP and external routing.** Connect the device to IP through
   Ethernet framing and bounded ARP resolution. Add the directly connected and
   default-gateway paths, expiry/retry handling and explicit unavailable results.
6. [ ] **Init configuration and external ping.** Wire manual configuration into
   init with explicit delegation to the session. Document the actual host setup
   and demonstrate ping to a reachable peer alongside loopback, using normal
   boots and interactive commands. Record remaining protocol and resource limits.

These are review boundaries, not a requirement to land unused scaffolding.
Adjust or subdivide a task when its concrete caller or ownership contract makes
that clearer. Add no test framework, packet injection harness, boot probes or
output automation. On completion, consolidate the implemented contract and usage
into the existing `docs/networking.md` reference; Git retains this plan.

## Follow-up milestones

UDP and DHCP come after manual IPv4 works. TCP, DNS, server lifecycle and hosting
the Pyxis website need separate scopes. IPv6, richer routing, network isolation,
offloads and throughput work remain later decisions. Keep the
[user/authority checkpoint](users-and-authority.md) ahead of remotely accessible
services and cross-user communication policy. Loopback is not an authorization
boundary: a local peer still needs the intended access rules.

The next intended VirtIO storage driver remains virtio-blk. No disk format,
installer, compositor or general asynchronous IPC implementation is included.

## References

- [Existing PCI ownership and MSI-X](../pci.md).
- [Virtio-fs transport and worker lifetime](../virtio-fs.md).
- [BSP allocation constraints](../technical-debt.md#bsp-only-allocation-and-vm-mutation).
- [VirtIO 1.4, Committee Specification 01](https://docs.oasis-open.org/virtio/virtio/v1.4/cs01/virtio-v1.4-cs01.pdf), network device and split virtqueues.
- [RFC 1122](https://www.rfc-editor.org/rfc/rfc1122.html), IPv4 host requirements,
  local addresses and loopback restrictions; consult the applicable protocol
  specifications when defining the implemented subset.
- [QEMU network backends](https://www.qemu.org/docs/master/system/devices/net.html).
