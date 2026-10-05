# DHCP

Trusted session network setup implements IPv4 DHCP for the single bound `net0`.
The kernel provides configuration and a right-gated wildcard UDP endpoint;
lease policy stays in userspace. Static profiles and the existing driver/MAC
selectors remain available. Binding lasts until reboot, including while IPv4
is cleared.

```lua
net0 = { select = "link", dhcp = true }
```

Use a MAC selector for a particular physical controller. `dhcp = true` excludes
`address`, `prefix` and `gateway`. The default profile requests DHCP after
[link selection](net0-selection.md).
Explicit driver/MAC selectors remain available.
An explicit `dns.server` wins over the first DHCP DNS server; without either,
use `1.1.1.1`. See [network configuration](networking.md#boot-configuration-and-use)
and [session configuration](../userland/session-configuration.md).

## Acquisition and maintenance

The configuration owner binds `net0` and reserves the wildcard UDP port 68 before
clearing IPv4. DISCOVER and selecting REQUEST use source `0.0.0.0`, limited
broadcast and the BOOTP broadcast flag. Replies must match the transaction,
client MAC and selected server. The client reads netmask, first router, first
DNS, lease duration, T1, T2 and server identifier, including overloaded fields.
It rejects malformed options and expired candidate leases.

Link selection and initial acquisition share at most about ten seconds. A timeout
starts the local
session offline with profile/fallback DNS; the same client continues discovery
in the background. Retry state survives bounded receive calls. Delays use
randomized exponential backoff, and only successful local sends advance the
retry stage. A remote launcher waits for an assigned address. An explicit
configuration-owner remote launch advances discovery itself while waiting.

The trusted setup session stays alive after handing off to the shell or service,
independently of successor exit. It retains only DHCP/configuration, clock,
random, memory and diagnostic-output authority, closing unrelated bootstrap
resources and input. Children receive ordinary UDP OPEN and NET_CONFIG READ,
without DHCP creation or configuration authority.

Finite leases have monotonic renewal, rebind and expiry deadlines anchored to
the earliest successful REQUEST in their transaction. At T1, REQUEST is unicast
to the server; at T2 it is broadcast. Both carry the current `ciaddr`, omit
requested-address/server-identifier options and share the transaction through
rebind. Retries use half the remaining phase time with a sixty-second minimum,
clipped to T2 or expiry. An unchanged IPv4 renewal preserves TCP connections;
only changed chosen DNS is published with SET_DNS. Address/subnet/route changes
use REPLACE. Renewal ACKs retain omitted mask/router/DNS values. Invalid T1/T2
ordering uses half-lease and seven-eighths defaults, with fractional precision
for short leases. Infinite leases have no renewal or expiry deadlines.

Expiry or a matching NAK clears IPv4, publishes profile/fallback DNS and restarts
discovery with a fresh transaction. The controller binding and wildcard endpoint
survive. Existing concrete UDP endpoints, TCP streams and TCP listeners follow
their normal address-change invalidation rules. Services holding an invalidated
listener need a new launch; DHCP does not supervise or recreate them.

## Limits

DHCP trusts the LAN. There is no persisted lease, ARP conflict probe, IPv6 DHCP,
server authentication or runtime controller rebinding. While unassigned, the
kernel accepts broadcast replies rather than unicast to the offered address;
a server that ignores the BOOTP broadcast flag may therefore fail acquisition.

The wildcard endpoint owns port 68 on net0 exclusively. Ordinary opens on its
port return `CALL_BUSY`; opening a wildcard on an ordinary net0 binding's port
also returns `CALL_BUSY`. This generic rule replaces the earlier shared-port
policy (owner decision, 2026-10-04). Loopback bindings remain independent.

Detected fatal errors and failed successor handoff attempt to clear settings
before closing the endpoint. Unexpected maintainer failure or an indefinite
scheduling stall requires reboot: the kernel has no independent lease-expiry
backstop. DNS changes reach newly launched programs; existing programs keep their
startup `DNS_SERVER`. See [technical debt](../technical-debt.md#dhcp-maintainer-and-client-limits).

[Qualification](../development/dhcp-qualification.md) records measured coverage
and remaining native lifecycle checks. [Link selection](net0-selection.md)
chooses the initial controller and can wait before this client opens its endpoint.
