# DHCP and link-based net0 selection

Status: **accepted, 2026-10-04.** After the RTL8111 milestone, add DHCP, then
let `net0` bind whichever port has link and get its address over it. The owner
accepted the defaults for all three [decisions](#owner-decisions) and the DHCP
tasks below. The [link-selection milestone](#next-milestone-bind-whichever-port-has-link)
is a sketch whose decisions are settled when it starts. Any decision can be
revised later by the owner.

## Goal

A Pyxis image that works on a network it wasn't built for. Today `net0` needs a
static address, prefix and gateway in `config/network.lua`, so an image only works
on the LAN it was configured for.

**Completion:** with no address in its profile, Pyxis gets an IPv4 lease, DNS and a
default route by DHCP, and the remote terminal is reachable at the leased address:
- in QEMU (the user network's built-in DHCP server hands out `10.0.2.15`);
- natively on the ThinkPad, where the router's existing reservation for the
  built-in port should hand out the same `.50`.

A second milestone then adds an opt-in "bind whichever port has link" mode.

## What's missing today

From [networking](../devices/networking.md#further-networking-work):

- **No broadcast.** IPv4 input accepts only unicast to the configured address, so
  limited (`255.255.255.255`) and subnet broadcasts are discarded.
- **No unconfigured sender.** A UDP endpoint binds a concrete local address. Nothing
  can send from `0.0.0.0` before an address exists, or send to the broadcast MAC
  without ARP.
- **No lease logic.** Configuration is a one-shot `NET_CONFIG` REPLACE from the
  session; nothing renews or expires it.

The [reverse remote terminal idea](thinkpad-next-steps.md#3-later-idea-reverse-remote-terminal-with-broadcast-discovery)
needs the same broadcast reception, so it can follow this milestone cheaply.

## Proposed shape

**The kernel gets one narrow primitive. The policy lives in userspace.**

- **A broadcast-capable UDP endpoint**, created only with a new right that trusted
  init gives to the session's network setup and to nothing else. Such an endpoint:
  - binds a port on the wildcard local address;
  - receives datagrams for that port sent to the interface address,
    `255.255.255.255`, or the subnet's broadcast address;
  - can send to `255.255.255.255` (Ethernet `ff:ff:ff:ff:ff:ff`, no ARP);
  - can send from `0.0.0.0`, but only while `net0` has no address.

  Ordinary UDP endpoints keep today's unicast-only rules.
- **A userspace DHCP client** in the session's network setup, which already owns
  `net0` configuration. It runs DISCOVER, OFFER, REQUEST, ACK and applies the lease
  with the existing REPLACE. It sets the DHCP broadcast flag so replies arrive
  before an address exists. It reads options 1 (netmask), 3 (router),
  6 (DNS), 51/58/59 (lease, T1, T2) and 54 (server ID).
- **Configuration syntax.** `dhcp = true` replaces `address`/`prefix`/`gateway` in a
  `net0` table, and the two forms are mutually exclusive:
  ```lua
  net0 = { driver = "virtio", dhcp = true }
  ```
  The existing selectors (`driver`, `mac`) and their bind-once rule are unchanged.

## Owner decisions

Accepted 2026-10-04, with the defaults below.

1. **Where the client runs.**
   - Userspace, in the session's network setup, as above. The kernel
     only gains the right-gated broadcast endpoint.
   - The alternative is a kernel DHCP client. lwIP has one, but in Pyxis lwIP only
     carries TCP, and lease policy is userspace work.
2. **Lease lifecycle.**
   - Follow RFC 2131 timers. Renew by unicast at T1 and rebind by
     broadcast at T2. At expiry, clear the IPv4 settings (the binding stays) and
     restart discovery.
   - No lease is saved across reboots. Pyxis may have no writable storage, and
     discovery takes a second.
   - No ARP conflict probe in v1; record it as a limitation.
3. **DNS server precedence.**
   - An explicit `dns.server` in the profile wins. Otherwise use the
     first server from the lease (option 6), and fall back to `1.1.1.1`.
   - Programs see DNS through `DNS_SERVER`, which is set when the session launches
     them. So the session waits a bounded time (about 10 s) for the first lease
     before starting the shell.
   - A DNS server that changes at renewal reaches only newly launched programs.
     Record that as a limitation.

## Tasks

- [ ] **1. Broadcast reception and the broadcast endpoint** (kernel, ABI).
  - The new right and the endpoint rules above, with unconfigured-source sends
    limited to the unassigned state.
  - Ordinary endpoints are unchanged.
  - **Finish when** a broadcast datagram from a host on the LAN reaches a
    right-holding endpoint in QEMU and natively, while ordinary endpoints still
    ignore it.
  - Capture the existing ping/UDP/TCP baseline before and after.
- [ ] **2. The DHCP client and configuration syntax** (userspace).
  - Discovery and request, applying the lease, and DNS precedence.
  - The session waits for the first lease. Remote startup already waits for the
    configuration owner to assign an address.
  - **Finish when** QEMU gets `10.0.2.15` from the user network, and the
    ThinkPad gets `.50` natively from the router, with the remote terminal, DNS
    and HTTPS working.
- [ ] **3. Lease lifecycle.**
  - Renew, rebind and expiry with deadlines through the existing worker and
    session paths.
  - **Finish when** a short-lease DHCP server on the development host shows
    renewal, rebind after the server stops answering, and clean expiry. The
    server could be the host's existing `dnsmasq` on a QEMU tap network, run
    manually with no new automation.

## Next milestone: bind whichever port has link

This is planned after DHCP, and its decisions are settled when it starts. It is
an opt-in selector mode beside `driver` and `mac`, for example
`net0 = { select = "link", dhcp = true }`.

- **Link for unbound controllers.** Today only the bound controller reports link.
  Lookup needs a read-only link state for prepared, unbound controllers. Their PHY
  is already up and negotiating.
- **Waiting at bind:** a bounded wait for any candidate's link (autonegotiation
  takes seconds).
- **Several ports with link:** an ordered preference list in the profile;
  otherwise the first one to come up.
- **Bind once until reboot,** as now. Moving the cable later doesn't rebind;
  runtime rebinding is a separate, larger step.

On the ThinkPad this pays off only when a second port is supported. The onboard
XID `502` behind the dock jack is a DASH controller and would need its own
identification, preparation and management-firmware coordination (see the
[RTL8111 driver](../devices/rtl8111.md) limits). Suggested order: DHCP, then
optionally the dock controller, then link selection. Link selection can go first
if it's wanted for QEMU setups with several NICs.

## Out of scope

DHCPv6 and IPv6, multiple active interfaces and routing between them, persistent
leases, ARP conflict detection, multicast membership, and authentication of DHCP
servers. DHCP trusts the LAN, the same assumption as the remote terminal.
