# Multiple network interfaces

Accepted plan and four-task split, owner decision 2026-10-18. **Codex epsilon
owns this milestone; task 4 (qualification) is assigned 2026-10-19.**
The owner requires Ethernet and Wi-Fi together, with a tunnel class for later
Tailscale work. Acceptance settles the design gate for beta to start
[Wi-Fi stage 1](wifi.md#qualification-and-proposed-stages). Multi-interface
implementation must be delivered before Wi-Fi stage 3, so Ethernet remains an
independent management path while Wi-Fi carries traffic.

## Implemented public cut

Tasks 1–3 deliver retained native interfaces, scoped packet factories, separate
registry/policy/configuration authority, ordered boot profiles, independent DHCP,
selected route/DNS/management paths and read-only inventory. The sole BSP network
worker and aggregate packet/connection budgets remain unchanged. lwIP remains a
TCP projection; native IPv4/ARP/UDP and userspace DHCP stay outside it.

The [network reference](../devices/networking.md), [selection guide](../devices/net0-selection.md),
[DHCP](../devices/dhcp.md), [DNS](../userland/dns.md), [net](../userland/net.md)
and [TCP projection](../devices/lwip.md) describe the public interfaces.
[Task-3 qualification and controls](../development/experiments/network-interfaces-task3.md)
separate nested QEMU evidence from remaining task-4/native qualification. Full
native multi-interface qualification remains gated by task 4; no radio-traffic
qualification is claimed here.

## Identity, names and lifecycle

Accepted names: `lo`, `eth0`, `eth1`, ... for Ethernet (including VirtIO and
USB NCM), `wlan0`, ... for Wi-Fi, and `tun0`, ... for later IP-only tunnels.
Class and hardware driver are separate: a tunnel must not require a fabricated
MAC, Ethernet header or ARP. This reserves a convention, not a tunnel object,
provider interface or Tailscale implementation.

Trusted configuration creates named logical interfaces and binds each physical
interface uniquely to a retained controller. Names come from that configuration,
not PCI order or link speed; selectors are class-filtered, and ambiguous matches
or a controller selected twice are refused before binding. Existing link/MAC/
VirtIO selectors remain useful for Ethernet. Wi-Fi selection must identify the
radio before association, rather than require carrier as a condition to find it.
Unselected controllers remain visible in inventory and inactive.

A boot-local ID identifies the logical interface; retained handles bind that
object, not a mutable name lookup. Interface IDs are not recycled within a boot;
configuration/address and transport epochs distinguish stale work. A USB NCM
replacement may continue the same interface only under its existing checked
identity/recovery contract. Removal or failure cannot redirect an old grant to
a different controller. Keep each physical binding until reboot in this first
scope; administrative address/default changes do not reassign controllers.

## Datapath and addressing

Carry interface identity through driver RX/TX, native IPv4 routes/local delivery,
ARP caches and pending packets, echo state, UDP bindings/broadcasts and TCP PCB
context. One interface's ARP reply, link loss or DHCP packet must not affect
another's queues/configuration. Preserve existing bounded packet/connection
accounting; measure the additional interface state before choosing tuning limits.

Create one lwIP netif projection per configured L3 interface plus loopback. Both
native and lwIP paths use **one native route policy**; route/output hooks must
agree on the chosen interface/source. Pass ingress identity into TCP instead of
reconstructing it from the destination address. Administrative, carrier,
authenticated-Wi-Fi readiness and address state remain distinct. MTU belongs to
the interface; later tunnels need their real MTU/MSS, not a hardcoded Ethernet
assumption. IPv6, forwarding, bridging and NAT are outside this proposal.

Each interface owns one IPv4 address/prefix and optional gateway, or one DHCP
lease/client. Keep DHCP in userspace over native UDP, not lwIP DHCP. Bind
port 68, zero-source broadcast, replies, transaction identity, lease timers,
router/DNS contributions and address invalidation to that interface. A wrong-
interface offer cannot configure it. Clients wait independently for usable link;
a Wi-Fi client waits for association/key admission, not just firmware readiness.
An absent optional interface cannot hold up a working Ethernet client or local
shell. Existing DHCP limitations (including broadcast-only initial replies and
no independent kernel expiry backstop) remain explicit, scoped to each lease.

Allow overlapping connected prefixes: Ethernet and Wi-Fi commonly share a LAN.
Refuse duplicate local IPv4 addresses across interfaces in the initial profile.
Keep neighbor/broadcast namespaces per interface; the same UDP port, including
68, may be used independently on different interfaces. A UDP wildcard or
broadcast endpoint covers **one** selected interface, never all interfaces
implicitly. TCP listeners remain bound to an exact interface/address; no wildcard
TCP LISTEN is added. Validate ordinary unicast ingress against that interface's address;
loopback and local delivery retain their own interface/authority checks.

## Routes and DNS

Accepted bounded policy:

1. Apply the held factory's scope. An explicit source restricts candidates to
   its owning interface; an explicit interface restricts them likewise. Reject
   conflicting restrictions rather than choosing a different source/NIC.
2. Check loopback and host-owned destination addresses before connected routes.
   Local delivery carries the destination's owning interface and must satisfy
   both held scope and the restrictions above; it is not a cross-interface bypass.
3. Choose the longest connected prefix among eligible interfaces; configured
   interface order breaks equal-prefix ties. Otherwise a pinned operation,
   including a one-interface factory, uses its interface's gateway; an
   unrestricted routed operation uses the explicitly
   selected system default interface and gateway. There is no fallback to another
   NIC when the required interface loses link/lease.
4. Capture the interface and local-address epoch when opening a concrete UDP
   endpoint, TCP connection/listener or echo request. An interface-pinned DHCP
   broadcast endpoint is address-independent; each send captures its current
   source/epoch. Default changes affect new work;
   existing work cannot migrate to a different NIC. Address replacement/expiry
   invalidates only work bound to that address/interface. Gateway/neighbor
   changes revalidate pending work without replaying uncertain transmissions.

The administrator selects default route and default DNS independently. DHCP
supplies candidates per interface; receiving a lease does not seize either
system choice. DNS is either a numeric server explicitly paired with an
interface, or that interface's first DHCP DNS server. The packaged Ethernet
profile can retain the existing explicit fallback server; a missing configured
source must not silently select a resolver on a different interface.

A selected resolver address and the UDP grant permitted to reach it travel
through trusted launch as **declared resources**. A restricted interface's
client gets an appropriate scoped resolver path or an explicit unavailable
result, not a widened packet grant. `DNS_SERVER` text grants no authority. Keep
startup snapshots: existing processes/providers retain their resolver choice;
new launches receive updated choices. No split DNS, search-domain engine,
resolver service or Tailscale route policy is implemented by this milestone.

## Configuration, ABI and tools

`network.lua` uses an ordered interface array and separate route/DNS/management
policy. The legacy `net0` schema is rejected. A DHCP-selected resolver example:

```lua
return {
  interfaces = {
    { name = "eth0", class = "ethernet", select = "link",
      dhcp = true, optional = true },
  },
  routing = { default = "eth0" },
  dns = { interface = "eth0" },
  management = { remote = "eth0", log = "eth0", debug = "eth0" },
}
```

Additional Ethernet/Wi-Fi entries use unique controller selectors. A trusted
Wi-Fi profile supplies radio/credential authority separately; no passphrase
belongs in this Lua table. Validate the full plan before changing configuration;
thereafter independent link/DHCP failures affect their own interfaces. This is
not a promise of atomic hardware activation or rollback.

The [interface-selection reference](../devices/net0-selection.md) describes
controller selection. Task 3 replaces the net0-only ABI, SDK helpers, libconfig,
stock profile, session/Remote/DHCP consumers and launch/grant adapters together;
there is no parallel legacy net0 surface. Private profiles must migrate their
ordered interface and policy settings explicitly. The configuration editor uses
the same static validation; unmigrated `net0` profiles get a clear error, not an
implicit second configuration authority.

Accepted capability boundary (ABI layouts and status tables accompany code):

| Held authority | Scope and operations |
| --- | --- |
| Registry inventory | Enumerate retained controllers/interfaces and query status; lookup yields only an attenuated read facet, never configuration or packet rights |
| Interface configuration facet | Query/configure/clear that interface's L3 state and gateway; no other NIC, system defaults or radio/credential authority |
| System policy administration | Create/bind interfaces, delegate facets/factories, set route/DNS policy and trusted management selections |
| TCP/UDP/echo factories | Kernel-bound permitted interface set, further attenuable to one interface; existing operation rights still apply |
| Wi-Fi control | Separate scan/association/key authority as in the accepted Wi-Fi plan; not implied by L3 configuration |

`NET_REGISTRY`, `NET_POLICY` and `NET_INTERFACE` provide typed returned facets,
bounded interface snapshots and policy operations. Their layouts live in
[`net_config.h`](../../include/abi/net_config.h) and
[`net_interface.h`](../../include/abi/net_interface.h). No operation
may use a caller-supplied name or ID to escape its held scope. Permission to
configure an interface does not grant system-policy mutation. Copies/exported
holders preserve scope and revocation lineage. Closing a copied grant alone
still does not revoke other holders; retired/cut grants fail explicitly.

Stock sessions may receive routed factories covering their configured permitted
interfaces; restricted launches get pinned subsets through their declared grants.
Loopback is an explicit member of a permitted set, not a way to reach another
interface's local address outside that set. There is no per-space network
namespace: interfaces, addresses and policy remain shared system state. Per-
interface grants constrain operations, not all possible application relays.

`net` stays read-only. With its declared registry READ authority it lists
unbound controllers and one row per visible interface: name/ID/class/driver,
MAC or none, administrative/carrier/authentication state, address/prefix,
gateway, timestamped last-reported DHCP state and the route/DNS choices.
DHCP state is the userspace maintainer's assertion, not kernel-verified lease
health. Clock READ supplies report age. No query starts DMA, scan or DHCP.
Wi-Fi SSID/BSSID scan lists and credentials remain separately authorized.
The registry does not grant configuration or packet authority.

## Explicit management interface

Remote, normal UDP log and network GDB each select a configured interface;
packaged defaults choose `eth0`. Capture the held interface/generation during
trusted setup, not by looking up a mutable shell variable for each packet.
Missing/unusable selection leaves that service unavailable, without broadcasting
or listening on another NIC. Selecting management transport grants children
no configuration authority and does not define their ordinary egress scope.

Remote listeners bind only the chosen interface/current address. Reverse
beacons are received there and the follow-up TCP connect uses that same scoped
factory; ingress provenance cannot be lost between the operations. Address loss
invalidates affected streams/listeners. Reverse Remote may rediscover/reconnect
on the same retained interface after readiness returns, never silently move to
Wi-Fi. Inbound listener invalidation still ends service; automatic listener
recreation/supervision is not added here.

UDP log takes its MAC/source address and raw broadcast TX path from the chosen
interface. Retained replay waits for its address; fatal source-zero behavior
remains limited to that driver's existing safe takeover path. Network GDB
captures the selected supported driver's resources and address for its stopped
session. `debug.net` continues to name the target, not the NIC. No DHCP, routing,
DNS, allocation or interface switching occurs in the stopped loop.

Keep current emergency ownership/exclusion rules, including debugger versus
panic-log takeover; other live interfaces retain their DMA backing through the
stop and resume under their owners, without reset or buffer reuse. USB NCM still
lacks stopped-kernel/fatal transport. Unsupported management selection reports
unavailable; there is no fallback. Static wired management remains recommended
for native radio work. Early faults before that NIC is ready retain today's
local/retained-log limits; this is not independent early Ethernet initialization.

## Delivery and qualification

The owner accepted this split on 2026-10-18. Sizes describe review scope, not
elapsed-time promises. Task 1 is delivered in the contract and baseline below;
task 2 is merged in #973. Task 3 is merged in #976, userland #300 and ports
#130. Task 4 has the owner go; native windows remain explicitly scheduled.

- [x] **1. Contracts and baseline — small.** Specify native ABI layouts,
  authority, statuses, epochs/invalidation and resource accounting; capture
  existing single-interface TCP/UDP, idle and launch controls before runtime
  changes. After this task the owner can review an exact contract and comparable
  control costs. The owner accepted the three additional lifecycle/observation
  defaults on 2026-10-18; the baseline precedes runtime changes.
- [x] **2. Interface-owned core — medium.** Replace singleton internal state
  with interface contexts while still exposing one physical interface plus
  loopback through the current public ABI. Preserve BSP ownership, driver DMA
  and emergency handoff. After this task the owner can use existing networking
  on the interface-owned core, with [matched controls](../development/experiments/network-interfaces-core.md).
- [x] **3. Public multiple-interface delivery — large.** Deliver the scoped
  kernel ABI, SDK/userland helpers, route/DNS policy, ordered configuration,
  independent DHCP, declared grants and `net` as one linked dependency stack.
  Include explicit Remote/log/GDB binding and reverse-beacon provenance here;
  do not expose multiple interfaces while management still uses an implicit NIC.
  Replace the net0-only public surface and all in-tree consumers together.
  After this task the owner can configure independent interfaces and use scoped
  grants. Dependency PRs precede the parent pins.
- [ ] **4. Isolation and lifecycle qualification — medium.** Qualify dual
  networks, restricted grants, overlapping prefixes, independent DHCP port 68,
  pinned flows, link/address loss and management surviving data-interface
  failure; repeat matched costs and fix confirmed defects. Native windows are
  owner-scheduled. After this task the owner has the multiple-interface
  prerequisite for Wi-Fi stage 3; coordinate radio-specific checks with beta.

### Task-4 checkpoint

The native packet is prepared at
`/shared/present/network-interfaces/task4-native/`, from clean main `b52533d9`.
Its two profiles exchange data/default and DNS/management roles between RTL8111
and USB NCM on the ThinkPad's shared switch. Actual driver, DHCP, policy and
wire-source checks precede any performance claim. The run sheet includes exact
horse commands, bounded capture, a staging journal with byte-for-byte menu
restore, and an optional owner-present unplug observation.

The owner-scheduled `native-usb-02` and `native-pci-02` rounds passed 118/119
and 120/121 predicates respectively. Their sole gap is DNS wire visibility:
both default queries succeeded against the public resolver, but that switched
unicast gateway traffic cannot be observed at the capture host. Data sources,
reverse Remote, new-boot log headers/START, hashes, scope checks and controls
passed. The original rounds remain unqualified; their raw records are retained.

`DNS-README.md` in the packet provides a DNS-only supplement for each unchanged
profile: existing `dig @horse` still uses its selected `dns_udp` grant; a bounded
host forwarder relays real queries to the public resolver. Capture must show the
selected source and byte-identical query/reply pairs across all four legs.
This qualifies the grant's source on the connected LAN, not direct observation
of the default resolver's routed path. No image or benchmark change is needed.
The supplement and remaining lifecycle checks are pending; task 4 stays open.

Daybreak's task-3 source review found no material defect; it did not qualify
NCM unplug, retained flows or overlapping-prefix interleavings. The native
packet exercises both live interfaces on an overlapping LAN, scoped/off-link
routes, independent DHCP, DNS/Remote/log selection, counts/hashes and repeated
control costs. The optional observer records withdrawal/recovery while keeping
built-in management. It does not substitute for held-flow/address-loss tests
or native stopped-debugger qualification. Keep task 4 unchecked until its
scheduled evidence and remaining matrix are reviewed.

### Remaining task-4 qualification

QEMU qualification includes two independent VirtIO NICs/LANs and DHCP clients,
optional absence, selected DNS/management distinct from default egress, scoped
operations and carrier loss. Task 4 extends this to the full isolation/lifecycle
matrix: overlapping prefixes and competing defaults, held flows across policy
changes, address loss and retained broadcast endpoints, cancellation/fault paths,
management surviving data-interface failure, and matched costs.

The native task-4 rig is the owner's two-wired-NIC ThinkPad. Native windows need
explicit owner scheduling; no PXE or hardware action is implicit in task 3.
Wi-Fi/radio-specific qualification remains coordinated with beta. Tunnels,
IPv6, forwarding/NAT, automatic failover and controller rebinding need later
contracts; no placeholder implementation is included.

## Owner decisions (accepted 2026-10-18)

1. **Identity/lifetime:** configured `ethN`/`wlanN` names, `lo`, later IP-only
   `tunN`; unique controller binding and retained interface capabilities,
   no boot-order naming or runtime reassignment.
2. **Address/route/DNS policy:** native per-interface IPv4/DHCP and lwIP TCP
   projections; overlapping prefixes allowed, duplicate addresses refused;
   explicit default interface and independently selected DNS, no automatic
   failover; existing flows pinned.
3. **Authority/management:** per-interface configuration/packet scopes,
   separate system-policy authority, declared scoped DNS transport, explicit
   Remote/log/GDB interface binding with no fallback.

This acceptance unblocks beta's Wi-Fi stage 1. Epsilon owns the interface
milestone; beta retains radio/firmware/association implementation.

## Accepted task-1 refinements

Accepted 2026-10-18: address-independent DHCP broadcast endpoints survive address
changes; concrete endpoints retire with their address incarnation. Carrier/address
loss preserves the existing per-interface transport behavior without migration
or replay. `net` reports timestamped userspace DHCP state, not verified lease
health. The [baseline](../development/experiments/network-interfaces-baseline.md)
and [core controls](../development/experiments/network-interfaces-core.md) retain
the exact revisions and measured bounds.

## Limits

Per-interface configuration, DNS contributions and route policy are shared system
state, with no per-space namespace. Interfaces/projection indices are retained
until reboot and the dependency's eight-bit netif index permits 255 projections,
including loopback. Resource exhaustion is reported; packet/connection/control
queues remain shared rather than multiplied per interface. Driver binding never
moves to a different controller. DHCP has no kernel lease-expiry backstop. Inbound
Remote listener recreation and stopped-kernel USB NCM transport remain absent.
Task 4 owns deeper lifecycle and native qualification, not additional public API.
