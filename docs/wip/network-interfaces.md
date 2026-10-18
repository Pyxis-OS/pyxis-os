# Multiple network interfaces

Accepted plan and four-task split, owner decision 2026-10-18. **Codex epsilon
owns this milestone; task 3 (public delivery) is assigned 2026-10-18.**
The owner requires Ethernet and Wi-Fi together, with a tunnel class for later
Tailscale work. Acceptance settles the design gate for beta to start
[Wi-Fi stage 1](wifi.md#qualification-and-proposed-stages). Multi-interface
implementation must be delivered before Wi-Fi stage 3, so Ethernet remains an
independent management path while Wi-Fi carries traffic.

## Current implementation and remaining cut

Task 2 moves the existing `lo` and `net0` internal state into retained interface
contexts. Driver binding, IPv4 configuration, routes, ARP keys, echo calls,
UDP endpoints, TCP records/projections and deferred local delivery carry the
native interface and appropriate address incarnation. The sole BSP network
worker and aggregate packet/connection budgets remain unchanged. See the
[network reference](../devices/networking.md), [TCP projection](../devices/lwip.md)
and [matched controls](../development/experiments/network-interfaces-core.md).

The public ABI and configuration still expose exactly one physical `net0` plus
loopback. NET_CONFIG, factory authority, DHCP, route/DNS policy and management
selection remain the existing single-interface facade. Task 3 replaces that
surface and its consumers together; the accepted design below is not yet a
multiple-interface runtime contract. No dual-NIC isolation or native hardware
qualification is claimed by task 2.

lwIP remains a TCP-only projection: native IPv4/ARP/UDP and userspace DHCP stay
outside it. Multiple interfaces do not require a second TCP stack, allocator,
namespace or per-interface TCP secret.

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
   interface order breaks equal-prefix ties. Otherwise a pinned operation uses
   its interface's gateway; an unrestricted routed operation uses the explicitly
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

Replace `network.lua`'s single `net0` table with an ordered interface array and
separate route/DNS/management policy. Illustrative configuration shape, with
exact parser details to settle during implementation; this syntax is not provided
by Pyxis yet:

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

The [net0-selection reference](../devices/net0-selection.md) will become the
interface-selection reference when code lands. Replace the net0-only ABI,
SDK helpers, libconfig, stock/private-profile examples, session/Remote/DHCP
consumers and launch/grant adapters together; no parallel legacy net0 surface.
The configuration editor can offer the declared owned-profile migration;
unmigrated private `net0` profiles get a clear error, not an implicit second
configuration authority.

Accepted capability boundary (ABI layouts and status tables accompany code):

| Held authority | Scope and operations |
| --- | --- |
| Registry inventory | Enumerate retained controllers/interfaces and query status; lookup yields only an attenuated read facet, never configuration or packet rights |
| Interface configuration facet | Query/configure/clear that interface's L3 state and gateway; no other NIC, system defaults or radio/credential authority |
| System policy administration | Create/bind interfaces, delegate facets/factories, set route/DNS policy and trusted management selections |
| TCP/UDP/echo factories | Kernel-bound permitted interface set, further attenuable to one interface; existing operation rights still apply |
| Wi-Fi control | Separate scan/association/key authority as in the accepted Wi-Fi plan; not implied by L3 configuration |

Extend/replace NET_CONFIG's operations/replies for these scopes: typed returned
facets, bounded enumeration records with interface ID/name/class/controller,
state/address/prefix/gateway/DNS validity, and policy snapshots. No operation
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

`net` stays read-only. With its declared inventory/config READ authority it lists
unbound controllers and one row per visible interface: name/ID/class/driver,
MAC or none, administrative/carrier/authentication state, address/prefix,
gateway, timestamped last-reported DHCP state and the route/DNS choices.
DHCP state is the userspace maintainer's assertion, not kernel-verified lease
health. Clock READ supplies report age. No query starts DMA, scan or DHCP.
Wi-Fi SSID/BSSID scan lists and credentials remain separately authorized.
No new control command or placeholder ABI is introduced by this docs PR.

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
task 2 is merged in #973. Task 3 is assigned; task 4 needs its own go.

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
- [ ] **3. Public multiple-interface delivery — large.** Deliver the scoped
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

### Task-3 single-interface cut checklist

These are deliberately retained in task 2 and must change together before
publishing multiple interfaces:

- [ ] Replace `debug/network.c`'s `net_ethernet` references with the retained
  management selection; publish panic transport for that selected context.
- [ ] Check controller binding conflicts across all contexts and report BOUND
  for each selected controller, rather than only `net_ethernet`.
- [ ] Service all retained interfaces, including their driver/ARP deadlines,
  rather than only `net_ethernet`; admit local delivery by retained identity,
  replacing `net_transmit`'s `lo`/`net0` pointer check.
- [ ] Replace the loopback-or-net0 facade in `net_ipv4_route`, echo admission
  and the lwIP route fallback with the accepted scoped route policy. UDP OPEN
  and TCP listener preparation need an explicit address-to-interface lookup,
  rather than selecting ownership by routing an address to itself.
- [ ] Replace NET_CONFIG's fixed `net_ethernet` target and DHCP broadcast/log
  bindings with held interface scope. Deliver route/config revision tracking
  when its public snapshots and consumers land, rather than an unused counter.
- [ ] Replace the two static `cn` lwIP projections with projections for the
  retained interfaces and loopback. Keep PCB/netif index identity stable:
  removal/re-addition must never reuse an index under retained bound PCBs.
- [ ] Deliver the configuration, declared transport grants, independent DHCP,
  route/DNS choices and Remote reverse-beacon provenance in the same public cut.


QEMU first: two independent VirtIO NICs/LANs, two DHCP clients using port 68,
static plus DHCP, overlapping prefixes, competing defaults, separately selected
DNS, restricted grants, link/address loss, unchanged first-interface controls
and denial without side effects. Check that one NIC's packets/lease cannot
satisfy the other's state and that existing flows stay pinned after default
changes. No claim of radio emulation. Native ThinkPad then uses wired Ethernet
for Remote/log/GDB while the radio is scanned and later carries verified traffic;
repeat matched controls with exact revisions, flags, AC state and samples.
Tailscale/tunnel injection, IPv6, forwarding/NAT, automatic failover and dynamic
controller rebinding need later contracts; no tunnel implementation is assigned.

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

## Task 1: native contract draft

**Contract design, not an implemented ABI.** These details refine the accepted
architecture above; the owner accepted the three task-1 defaults on 2026-10-18.
No headers, kernel code, pins or configuration change in this task. The current
public ABI remains the single-interface ABI until task 3.

### Held objects and admission

| Held object | Proposed operations and authority |
| --- | --- |
| Registry, READ | Ascending controller/interface enumeration, policy snapshot, lookup yielding only an interface READ facet. No activation, binding or packet grant. |
| Interface facet, READ/WRITE | QUERY and that interface's REPLACE/CLEAR; WRITE publishes the reported DHCP status under decision 3. Requests cannot retarget the facet. `lo` is read-only. |
| Policy administrator | Define named logical interfaces, bind unique retained controllers, mint configuration facets and TCP/UDP/echo factories, replace default route/DNS/management selections. No radio/key authority is implied. |
| TCP/UDP/echo factory | Enumerate its immutable allowed interface IDs, derive a one-interface factory with equal/subset operation rights, and perform its existing transport operations within that set. |
| Endpoint/listener/stream | Retain interface identity and the appropriate address epoch; INSPECT reports these alongside existing endpoint/state fields. Existing directional rights remain separate. |

A factory holds kernel-owned interface references, not names or a caller mask.
Generic COPY attenuates rights, not interface scope; scope derivation therefore
returns a distinct native factory object. It requires a nonzero subset of the
held operation rights. Scope enumeration requires a held TCP/UDP/echo operation
right and reveals only allowed IDs, not controller/MAC inventory. Native
factories still carry zero IPC transport rights. Registry
READ is still needed for inventory and configuration observations.

Derived grants preserve existing forwarder lineage through
[`forwarder_derive_grant`](../../include/kernel/object/forwarder.h). COPY,
startup and IPC preserve wrappers. CUT rejects future/pre-start work as DENIED;
it does not recall a packet already accepted by a driver. Returned-handle
copyout failure closes only the newly returned exact generations, as existing
UDP/TCP publication does. It never closes a reused slot or revokes another copy.
Both synchronous and native completion CONNECT must capture the resolved
factory object/scope; the separate
[completion request](../../include/abi/completion.h) is part of the task-3 cut.

Define logical names/classes as a bounded batch before binding. Validate the
whole profile's names, classes and selectors before binding any controller;
a binding batch checks every selected interface/controller and duplicate before
committing its retained associations. Optional absent interfaces remain defined
and unbound; trusted setup may bind them later after discovery, without
reassigning an already bound interface. Activation is subsequent per-driver
work: a failed activation retains the committed binding and reports unavailable.
There is no promise of atomic hardware activation or rollback. Preparing a
batch fails with no published definitions/bindings if input validation or
required allocation fails. All lookup/selection queries remain read-only.

Packet/connection scope is checked before routing, including own-address/local
and loopback delivery. An out-of-scope ID is DENIED without an inventory lookup.
Source and interface restrictions that conflict are BAD_REQUEST. Broadcast
OPEN needs a pinned factory or an explicit allowed interface; an ambiguous
multi-interface broadcast OPEN fails before reserving a port. Ordinary UDP OPEN
keeps its concrete local-address requirement. Existing waits/deadlines, partial
I/O and terminal-status rules are not replaced by a second completion interface.

### Draft wire records

Integers use native ABI widths and host-order IPv4, with the existing 16-byte
message header. Reserved fields and string tails must be zero; lengths are
exact, replies need sufficient capacity, and errors return no reply bytes.
Operation numbers/protocol tags land with the task-3 headers, with no parallel
legacy version. These proposed layouts are not advertised by the current SDK.

| Record | Proposed shape / bytes |
| --- | --- |
| Interface cursor | Header plus `u64 after_id`: 24. Ascending opaque boot IDs; zero reply ID marks end, with completeness still reported. Controller enumeration retains its existing 32-bit controller IDs and 24-byte records. |
| Path restriction | `u64 interface_id`, `u32 source`, `u32 reserved`: 16. Zero ID means use routing within the held set; zero source means choose an eligible source where that operation permits it. |
| Factory restriction | Header, interface ID, requested rights: 32; returned typed native handle: 8. The requested set is a subset, never a widened grant. |
| TCP CONNECT / echo | Header, path restriction, destination/port-or-reserved fields and deadline: 48. Completion CONNECT has the same path restriction and destination/port fields: 24, with deadline in the existing completion envelope. |
| TCP LISTEN / UDP OPEN | Header, path restriction, port and zero reserved fields: 40. LISTEN still requires a concrete local address; UDP broadcast requires source zero and exactly one selected interface. |
| UDP OPEN_ROUTE | Header, path restriction, destination `u32`, port `u16`, reserved `u16`: 40. Preserve destination-based opening; destination and explicit source are independent fields. |
| Transport identity | Append interface ID and address epoch, 16 bytes, to native TCP connection/listener and UDP endpoint inspection. Concrete objects report their captured epoch; an address-independent broadcast endpoint reports no fixed address epoch. |
| Interface snapshot | 96: four `u64` fields (ID, address epoch, route revision, DHCP report time); ten `u32` fields (controller ID, class, driver, validity/state flags, MTU, address, prefix, gateway, DHCP DNS contribution, reported DHCP phase); name[16], MAC[6], reserved[2]. |

Names are NUL-terminated `lo` or class-matching `ethN`/`wlanN`, with a canonical
unsigned decimal suffix that fits the name field. Flags distinguish definition,
binding, preparation/readiness, known/up carrier, authentication validity,
address, MAC, DNS contribution and reported DHCP-state validity. IP-only future
interfaces must not fabricate MAC or Ethernet state; no tunnel API lands here.
There is no new administrative UP/DOWN operation: configured/bound state and
L3 CLEAR remain distinct from carrier and authenticated readiness.

Policy snapshots identify default route, selected DNS interface/server/validity,
management interface IDs and revision. The policy administrator validates every
referenced interface before replacing that snapshot. DNS from DHCP is a
contribution, not permission to seize the system choice; startup passes the
chosen numeric server and appropriate declared scoped UDP grant together.
Restricted launches with no permitted resolver path receive unavailable, not a
widened factory. Existing processes retain their startup choice.

Keep existing status meanings: BAD_REQUEST for malformed/conflicting input,
BAD_BUFFER for inaccessible input/output, DENIED for rights/scope, NOT_FOUND for
absent lookup, ALREADY_EXISTS for duplicate names/addresses, BUSY for ambiguous
selection or an already-owned controller, QUEUE_FULL for admission-slot
exhaustion, LIMIT for a representational bound, NO_MEMORY for backing exhaustion,
NO_ROUTE for no eligible route and UNAVAILABLE for a retained selected interface
that cannot currently carry the operation. Global default changes affect only
new operations; they cannot repin an existing object.

### Ownership, epochs and accounting

Keep five identities distinct: boot-local interface ID; address incarnation;
route/configuration revision; driver transport epoch; existing operation/TCP
generation. None is a replacement for the others. Logical interface metadata
and bindings survive until reboot, and IDs never recycle. Generic capability
objects/factories have ordinary reference lifetimes over those retained contexts.
BSP allocation/reclamation and the single network worker retain the existing
[memory](../kernel/memory.md) and [handoff](../kernel/smp.md) rules. Queued work
owns copied input, references and completion state, never borrowed caller memory.

Preserve aggregate budgets rather than multiplying them by interface count:

| Existing implementation accounting | Initial multiple-interface treatment |
| --- | --- |
| 32 software IP packets, 16 deferred-local entries | Global; pending ARP and queued UDP packets still consume packet backing. |
| 16 ARP neighbors and 16 pending packets | Global pools keyed by interface plus IP; one interface's invalidation touches only its entries. |
| 16 echo calls; 8 configuration calls | Global admission, including completed uncollected calls. |
| 16 UDP endpoints; 8 control calls, 8 send calls, 16 receive calls | Global. Closed/terminal objects retain admission until ordinary final release/worker retirement. |
| 4 queued datagrams per endpoint, 16 globally | Keep both bounds and packet accounting. |
| 32 TCP records; 4 listeners; 4 pending children per listener | Global; passive children, native owners and TIME_WAIT retain existing accounting. |
| 16 TCP reads and 16 writes; 8 control calls | Global; no per-interface multiplication. |
| Driver DMA and NCM frame queues | Existing per-controller owners/bounds; no depth tuning or allocator change. |

The [pinned lwIP netif representation](../../third_party/lwip/src/include/lwip/netif.h)
has 255 nonzero 8-bit indices: at most 255 simultaneous projections including
`lo`, hence 254 non-loopback projections. That dependency ceiling does not make
public IDs eight-bit or justify a new small fixed interface limit. Account for
actual context/factory/scope backing and reject allocation failure honestly.
Current 1500-byte packets/1472-byte UDP payloads and MSS/window settings stay
implementation limits, not universal properties of an interface. lwIP heap
bytes/peak are observations, not a hard byte quota; do not describe them as one.

### Task-1 decisions accepted 2026-10-18

1. **DHCP broadcast lifetime: interface-pinned, address-independent.**
   Preserve the [existing broadcast endpoint](../../include/abi/udp.h) across
   clear/replacement/expiry, retaining receive queues and pending receives.
   Each send captures/revalidates its current source and address epoch; pending
   ARP work from the old address fails. Ordinary concrete endpoints capture an
   address incarnation and become permanently unavailable when it is retired.
2. **Carrier and address changes: preserve current transport rules per
   interface.** Same-address renewal/prefix/gateway updates do not create
   a new address incarnation; they revise routing and revalidate unsent work.
   Carrier/usable-route loss fails active TCP work when observed, but exact
   listeners and concrete UDP bindings survive carrier-only loss while their
   address remains. Clear/expiry/numerical replacement retires concrete objects;
   listeners are not retargeted or recreated, and TIME_WAIT retains its tuple
   until normal expiry. No automatic flow migration or replay is added.
3. **DHCP observation: report the maintainer's last assertion.**
   Phase/lease state currently lives only in the userspace DHCP maintainer;
   [NET_CONFIG](../../include/abi/net_config.h) reports L3/DNS, not DHCP state.
   Add a bounded status publication through that interface's configuration
   WRITE facet: phase plus expected address epoch; the kernel stamps the report
   with monotonic nanoseconds and rejects a stale epoch. `net` labels it as the last reported state,
   with validity and age (declaring clock READ for that observation), never
   kernel-verified lease health. Address retirement
   clears report validity. Absent/stalled maintainers can leave stale reports;
   this adds no heartbeat, lease-expiry backstop or DHCP policy in the kernel.

### Baseline

The [task-1 record](../development/experiments/network-interfaces-baseline.md)
captures exact-main `d4dfc3b2` TCP/UDP, hash, idle/backing, Remote launch and IPC
controls in four-CPU nested KVM, with revisions, artifact/configuration hashes,
commands and repeated ranges. No runtime source changed. Repeat matched controls
after tasks 2/3 and interleave before/after qualification where appropriate;
re-establish controls if intervening main changes alter the workload. These are
VirtIO/user-network controls, not native NCM or multiple-interface results.
Native windows need owner scheduling. The separate NCM hands-free TX A/B remains
parked and is not authorized by this task.

## Task 3: integration checkpoint

Owner go 2026-10-18. This is the large public cut; intermediate draft branches
are not delivered multiple-interface support. Keep the task unchecked until the
accepted ABI/configuration and its in-tree consumers work together.

| Repository | Delivery and dependency |
| --- | --- |
| Pyxis | Registry/policy/interface objects, scoped transport factories and routing, synchronous plus completion ABI, driver servicing, management selection and SDK export. The integration PR stays draft until the linked userland cut is qualified. |
| Userland | Helpers against that SDK; ordered profile validation; trusted setup and per-interface DHCP; declared scoped transport/DNS grants; session/shell/provider/Remote handoff; read-only `net`; stock profile and private-profile migration documentation. Publish its dependency PR before the parent pins its head. |
| Ports, only if required | Change concrete direct ABI or launch/grant consumers found in the pinned source. Ordinary ports consume the rebuilt SDK/runtime. No unrelated dependency updates or compiler rebuild. |

The [fresh exact-main controls](../development/experiments/network-interfaces-task3.md)
precede source changes; the userland pin moved after task 2's measured source. The first kernel work retains interface identity and
ensures queued transport admission captures the resolved factory rather than
rights alone, including native completion CONNECT. Next deliver typed policy,
registry and configuration facets, immutable packet scopes, general route/worker
iteration and selected management; then link the SDK/userland migration as one
public cut. There is no intermediate merge promising multiple interfaces while
management selection, completion CONNECT or launch attenuation remains implicit.

The initial resource bootstrap currently creates global factories before
userspace defines interfaces. Replace it with policy authority and explicit
factory minting during trusted setup; do not turn a bootstrap factory into an
implicit grant for every interface defined later. Returned grants keep their
accepted immutable scope and forwarder lineage.

Checkpoint: the internal registry now drives service/deadline iteration and
controller ownership checks; immutable bootstrap factory scope is captured by
synchronous and completion admission, and checked before route/transport effects.
The bootstrap still has only `lo` plus `net0`; it is not the final registry/policy
ABI or trusted minting path. The [first-core functional checks](../development/experiments/network-interfaces-task3.md#first-internal-checkpoint)
cover the existing one-interface bootstrap, including native completion CONNECT.
No public configuration/grant migration or dependency pin has landed. Public delivery and its checklist remain unchecked.

Pinned ports inspection found Lua's native adapters copying declared TCP/UDP
capabilities, rather than constructing NET_CONFIG/TCP/UDP wire requests. Their
copies retain kernel scope. No ports change is required for this internal
checkpoint; recheck their launch/DNS adapters against the final userland cut.
