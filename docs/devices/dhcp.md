# DHCP

Trusted session network ownership runs one IPv4 DHCP client per declared logical
interface. Each receives that interface's configuration WRITE facet and a
one-interface UDP BROADCAST factory. Lease policy stays in userspace; registry
READ and ordinary packet factories cannot configure an interface or change
system policy. Controller bindings survive until reboot, including across L3 clear.

An explicit entry can request DHCP:

```lua
interfaces = {
  { name = "eth0", class = "ethernet", select = "link", dhcp = true },
}
```

`dhcp = true` excludes static `address`, `prefix` and `gateway`. The stock
profile enables [automatic wired binding](net0-selection.md), giving every
nonexcluded boot-discovered wired NIC its own optional DHCP client. Explicit
entries reserve controllers/names first; `wired.auto = false` keeps explicit-only
profiles. MAC, VirtIO and linked selectors remain available. DNS is selected separately: `dns = { interface = "eth0" }`
uses that interface's DHCP contribution, while adding `server = "1.1.1.1"`
selects a numeric server. No missing-lease DNS fallback is inferred. See
[network configuration](networking.md#boot-configuration-and-use).

## Acquisition and maintenance

A client waits for its own bound/ready transport and usable carrier; when
an interface reports authentication validity, it must also report authenticated
readiness. Defining `wlanN` alone supplies none of those facts. Clients advance
in bounded slices with independent endpoints, transactions and monotonic timers,
so an absent optional interface does not hold up a working peer or local shell.
Remote waits specifically for its selected interface's assigned address.

The client reserves an interface-pinned, address-independent UDP port 68 before
clearing that interface's IPv4 state. DISCOVER and selecting REQUEST use source
`0.0.0.0`, limited broadcast and the BOOTP broadcast flag. The endpoint admits
replies only on its interface; DHCP additionally checks transaction, client MAC
and selected server. It reads netmask, first router, first DNS, lease duration,
T1, T2 and server identifier, including overloaded fields. Malformed options and
expired candidates are rejected. Discovery state survives bounded receive calls;
randomized exponential backoff advances only after successful local sends.

The trusted owner stays alive after successor handoff and retains the declared
facets/endpoints, clock, random, memory and diagnostic output needed for
maintenance. Ordinary children receive neither DHCP BROADCAST creation nor
interface WRITE. Explicit trusted policy authority, when retained for late
binding, remains separate from each client's configuration facet.

Finite leases use monotonic renewal, rebind and expiry deadlines anchored to
the earliest successful REQUEST in their transaction. At T1, REQUEST is unicast
to the server; at T2 it is broadcast. Both carry current `ciaddr`, omit the
requested-address/server-identifier options and share the transaction through
rebind. Retries use half the remaining phase time with a sixty-second minimum,
clipped to T2 or expiry. Renewal ACKs retain omitted mask/router/DNS values.
Invalid T1/T2 ordering uses half-lease and seven-eighths defaults, preserving
fractional precision for short leases. Infinite leases have no such deadlines.

Interface REPLACE publishes lease address, prefix, gateway and DNS contribution
together. An unchanged numerical address preserves its incarnation and concrete
transports; numerical replacement retires them. A contribution never changes the
selected system DNS interface or mode. Expiry or a matching NAK clears only that
interface's settings/contribution and restarts discovery with a fresh transaction.
Its binding and broadcast endpoint survive, including queued data and receives.
Concrete UDP/TCP objects follow normal address-retirement rules; services with
retired listeners need a new launch.

The owner publishes its last asserted phase through REPORT_DHCP with the expected
address epoch. The kernel rejects a stale epoch and stamps monotonic report time.
`net` displays report validity and age. This is observation of userspace state,
not kernel verification of lease health; address retirement clears validity.

## Limits

DHCP trusts the LAN. There is no persisted lease, ARP conflict probe, IPv6 DHCP,
server authentication or controller reassignment. While unassigned, the kernel
accepts broadcast replies rather than unicast to the offered address; a server
ignoring the BOOTP broadcast flag may fail acquisition.

A broadcast endpoint owns port 68 exclusively against concrete bindings on its
interface (`CALL_BUSY`). Other interfaces, including `lo`, have independent port
bindings. Endpoint/call/datagram budgets remain global.

Detected fatal errors and failed successor handoff attempt to clear owned settings
before closing endpoints. Unexpected maintainer failure or indefinite scheduling
stall has no independent kernel lease-expiry backstop, and its last report can
become stale. New programs receive the selected numeric DNS text together with a
permitted scoped `dns_udp` grant; existing programs keep their startup choice.
See [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1438).

The [DHCP qualification record](../development/dhcp-qualification.md) describes
measured historical coverage and native lifecycle limits at its listed revisions.
The [multiple-interface native record](../development/experiments/network-interfaces-native.md)
adds independent DHCP on the two wired ThinkPad interfaces; deeper lease and
withdrawal interleavings remain explicit qualification limits.
