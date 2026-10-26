# Multiple network interfaces

Pyxis supports retained logical interfaces with independent IPv4/DHCP state,
scoped packet authority, and separate default-route, DNS and management choices.
The [network reference](networking.md) defines protocols and resource bounds;
[interface selection](net0-selection.md) covers controller binding,
[DHCP](dhcp.md) and [DNS](../userland/dns.md) cover resolution, and
[`net`](../userland/net.md) reports inventory and current policy.

## Identity and ownership

`lo` is predefined. Trusted boot configuration names Ethernet interfaces `ethN`
and Wi-Fi definitions `wlanN`. Explicit names identify configured roles.
The stock automatic wired policy reserves explicit entries first, then assigns
unused `ethN` names by ascending boot-local controller ID. Those generated names
are stable for the boot inventory, not across hardware changes. Class and hardware driver are separate. A logical Wi-Fi definition alone
grants no radio, association or authenticated packet path. `tunN` is reserved
as a naming direction for later IP-only tunnels; no tunnel API is implemented.

Each physical interface binds uniquely to a retained controller until reboot.
Selectors are class-filtered; ambiguous matches and selecting a controller
twice are refused. An absent optional interface remains defined and unbound;
it does not hold up a working peer or shell. Removal or failure cannot redirect
a retained grant to another controller. Automatic route preference selects new
operations among already-held interfaces; there is no runtime controller
reassignment.

Boot-local IDs identify objects independently of names and are not recycled.
Address/configuration epochs distinguish stale work. AX200 open links also have a
nonreused connection incarnation: every Wi-Fi L3 mutation carries the sampled
incarnation, including CLEAR and DHCP phase reporting. Retirement clears L3 and
invalidates flows even if the old link had no address; stale DHCP cannot restore
it. Other interface classes use connection generation zero. See the
[Wi-Fi contract](../interfaces/wifi.md#stationdata-limits). All interfaces share the
single BSP network worker; ARP, ingress identity, UDP bindings and DHCP state
belong to their interfaces. Each configured L3 interface has an lwIP TCP
projection, with native IPv4/ARP/UDP outside lwIP and one shared route policy.
Administrative, carrier, authentication and address readiness remain distinct.

## Authority and packet paths

| Held object | Authority |
| --- | --- |
| Registry READ | Controller/interface inventory, policy snapshot, interface READ lookup; no configuration or packet rights |
| Interface facet | Query and, with WRITE, replace/clear that interface's L3 configuration; no system policy or other NIC |
| Policy MANAGE | Define/bind interfaces, select system policy and mint explicitly scoped grants |
| TCP/UDP/echo factory | Operations within its immutable permitted interface set; derivation can only attenuate scope and rights |

[`net_interface.h`](../../include/abi/net_interface.h) and
[`net_config.h`](../../include/abi/net_config.h) define layouts and operations.
SDK helpers and the [typed launch resolver](../userland/program-bundles.md)
carry declared grants. Copies preserve scope and revocation lineage; a caller-
supplied name, address or ID cannot widen held authority. Closing one holder
does not revoke others. Registry observation never implies configuration WRITE.
Interfaces and policies are system-wide, without per-space network namespaces.
Scope limits direct operations, not a program's ability to relay data it holds.

Route selection first applies held scope and explicit source/interface
restrictions, refusing conflicts. Loopback and host-owned destinations retain
the destination interface's authority checks. The connected preference rank
(usable Ethernet before Wi-Fi, then observed rate within the class) precedes
prefix length; the longest prefix only breaks equal-rank ties, then stable order
breaks equal-prefix ties. This applies in both automatic and pinned-default
policy. [Automatic preference](net0-selection.md#automatic-route-preference)
also selects the off-link default within the held scope; explicit policy retains
its named off-link default. An interface-pinned
operation always uses that interface, without fallback. Loopback must itself
belong to the allowed set.

Overlapping connected prefixes are allowed; duplicate local IPv4 addresses are
refused. UDP port namespaces are per interface; a wildcard/broadcast endpoint
covers one interface. Concrete UDP endpoints, TCP connections/listeners and
echo work retain their interface/address epoch and cannot migrate. Numerical
address replacement or clearing invalidates affected concrete objects; same-
address renewal and prefix/gateway changes preserve the address
incarnation. DHCP broadcast endpoints remain address-independent. Carrier/
neighbor changes retain the existing
transport behavior without replaying uncertain transmissions.

## Configuration, DNS and management

The `network.lua` profile expands boot-discovered wired controllers by default,
including carrier-down NICs; explicit MAC exclusions and `wired.auto = false`
remain available. The completed initial plan defines interfaces and independent
`routing`, `dns` and `management` selections. See the
[complete schema and stock example](networking.md#boot-configuration-and-use).
Legacy `net0` configuration is rejected. Private profiles can be packaged with
`NETWORK_CONFIG`; no machine MAC or credential belongs in the repository.
The full plan is validated before publication; driver/link/DHCP activation is
not an atomic hardware transaction. Configuration is not live-reloaded.

Each interface has one address/prefix and optional gateway, or its own DHCP
client and port-68 broadcast grant. A lease contributes a router/resolver but
cannot seize the system's selected defaults. Selected numeric DNS text travels
with a declared scoped `dns_udp` grant; unavailable authority does not widen the
scope. Existing processes retain their startup resolver choice. `net` reports
timestamped userspace DHCP assertions, not kernel-verified lease health.

Remote, normal UDP log and network GDB each use an explicitly selected interface;
packaged defaults choose `eth0`. Reverse Remote preserves beacon ingress through
the scoped follow-up connection. Address loss invalidates affected streams and
listeners; reverse discovery can resume on the same retained interface, never
another NIC. Selecting management does not grant children configuration rights.
Stopped debugging preserves the selected driver's existing ownership rules:
no DHCP, routing, allocation or interface switching occurs in the stopped loop.
USB NCM still has no stopped-kernel GDB or fatal-network transport.

## Qualification and limits

The [baseline](../development/experiments/network-interfaces-baseline.md),
[core controls](../development/experiments/network-interfaces-core.md) and
[public-cut QEMU record](../development/experiments/network-interfaces-task3.md)
retain exact revisions, costs and source-inspected versus exercised behavior.
The [native record](../development/experiments/network-interfaces-native.md)
qualifies both wired ThinkPad roles, overlapping prefixes, independent DHCP,
scopes and selected data/DNS/Remote/log sources, with transfer hashes and
launch/IPC/memory observations. A subsequent owner-present same-port NCM
unplug/replug preserved built-in-NIC Remote and recovered the same interface
and address epoch; no active-flow survival was tested. The DNS supplements make
their real forwarded query path observable; they do not claim capture of the default resolver's
routed path. Milestone closure includes these bounded checks, not all possible
lifecycle interleavings.

Interfaces/projection indices are retained until reboot; the dependency's
eight-bit netif index allows 255 projections including loopback. Packet,
connection and control budgets remain aggregate rather than multiplied per
interface. Exhaustion is reported. No independent kernel DHCP lease-expiry
backstop, automatic inbound-listener recreation, IPv6, forwarding, bridging,
NAT, split DNS or general route-table engine is added.

[Lifecycle qualification debt](https://git.internal/PyxisOS/pyxis-os/issues/1279)
retains held-flow and deeper failure-path limits. Slow post-warm-reboot NCM
readiness is [separately recorded](https://git.internal/PyxisOS/pyxis-os/issues/1280).
Radio delivery remains in the [Wi-Fi milestone](../wip/wifi.md); this core provides its independent Ethernet
management prerequisite without claiming radio traffic.

## Wi-Fi lease admission

Overlapping connected subnets are accepted, including Ethernet plus the same
home Wi-Fi. Connected preference precedes prefix length in both route modes,
so a lower-preference link cannot take a destination on-link on a higher-ranked
eligible link merely by supplying a narrower or broader lease. Held scopes and
explicit interface/source restrictions still apply first.

Before publishing Wi-Fi IPv4 state, the kernel refuses a lease whose own address
equals another interface's gateway or DNS server: otherwise local delivery would
capture traffic for that destination before route selection. The system's numeric
DNS server is also protected unless explicitly bound to this Wi-Fi interface.
Duplicate local addresses remain refused. Refusal is `CALL_ALREADY_EXISTS`; the
maintainer reports an address collision and publishes no address. There is no
minimum-prefix rule. DNS server addresses in 127/8 are refused for every interface.

## Network trust profile direction

Owner direction (2026-10-20), **not implemented**: a newly joined network starts
untrusted and works normally, including when it is the only link, without program
changes. Its connected routes never override another interface's routes, and
Pyxis services are never exposed on it. The owner marks a network trusted once;
it then behaves like Ethernet today. This is a network-level Public/Private-style
policy, not a requirement for applications to request Wi-Fi-specific grants.
The current preference-first connected routing and exact-address collision
refusal are immediate safeguards, not trust-profile enforcement.
