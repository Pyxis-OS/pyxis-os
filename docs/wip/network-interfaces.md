# Multiple network interfaces

Accepted plan, owner decision 2026-10-18. **No implementation is assigned here;
the owner will select its implementer.**
The owner requires Ethernet and Wi-Fi together, with a tunnel class for later
Tailscale work. Acceptance settles the design gate for beta to start
[Wi-Fi stage 1](wifi.md#qualification-and-proposed-stages). Multi-interface
implementation must be delivered before Wi-Fi stage 3, so Ethernet remains an
independent management path while Wi-Fi carries traffic.

## Inspected starting point

- Several NICs are inventoried, but [driver.c](../../kernel/net/driver.c) holds
  one `bound` controller. A different second bind is refused. MAC, transmit,
  service/deadline and panic/debug dispatch all follow that singleton.
- [IPv4](../../kernel/net/ipv4.c) has one address/prefix/gateway; its route result
  lacks interface identity. [ARP](../../kernel/net/arp.c) keys neighbors by IP
  alone; [UDP](../../kernel/net/udp.c) keys bindings by address/port, so two
  independent DHCP port-68 endpoints conflict. Address replacement invalidates
  state globally rather than by interface.
- lwIP is a **TCP-only projection**, not the existing Ethernet/ARP/UDP/DHCP
  stack. [bridge.c](../../kernel/net/lwip/bridge.c) creates loopback and one
  assigned netif; [lwipopts.h](../../kernel/net/lwip/include/lwipopts.h) disables
  lwIP UDP, ARP, DHCP and DNS. Native IPv4 supplies route/output hooks.
- [NET_CONFIG](../../include/abi/net_config.h) configures global net0 and one
  chosen DNS server. Its operation masks do not attenuate by NIC. TCP/UDP
  factory objects likewise lack interface scope.
- [Session setup](../userland/session-configuration.md),
  [DHCP](../devices/dhcp.md), Remote discovery/listeners, UDP log and network GDB
  all depend on that single binding. Reverse Remote receives only a beacon
  peer address, then uses a separate global TCP factory: there is no ingress
  interface carried through the handoff today.

These are source-inspected facts, not multi-interface runtime results. Keep the
single BSP network worker and its ownership rules. Multiple interfaces do not
require a second TCP stack, allocator, namespace, or per-interface TCP secret.

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
4. Capture the interface and local-address epoch when opening a UDP endpoint,
   TCP connection/listener or echo request. Default changes affect new work;
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
gateway, DHCP state and the route/DNS choices. No query starts DMA, scan or DHCP.
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

Planned implementation order, each requiring assignment:

1. Settle exact interface/factory ABI, epoch/invalidation and status contracts;
   capture repeated existing native/nested TCP/UDP/idle/launch controls before
   performance-affecting code. No new benchmark infrastructure.
2. Replace singleton native/driver/lwIP state with interface-owned contexts, then
   deliver the ABI/SDK/configuration/DHCP/`net` cut as coherent linked PRs updating
   all in-tree users. Do not land a broken intermediate public ABI or successful
   fake multi-interface operation.
3. Bind Remote/log/GDB explicitly, qualify lifetime/link/address changes, then
   qualify wired management surviving independent data-interface traffic/failure.
   This is the prerequisite delivered before Wi-Fi stage 3.

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

This acceptance unblocks beta's Wi-Fi stage 1. Multi-interface implementation
ownership is the owner's next assignment; it is not implicitly assigned to beta.
