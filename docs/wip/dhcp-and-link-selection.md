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

## Task 1 endpoint decisions

Accepted 2026-10-04:

- A wildcard binding may share its port with an ordinary net0 binding. Concrete
  unicast takes precedence; broadcast goes only to the wildcard. Each binding
  remains exclusive within its own address/port space; loopback stays independent.
- Wildcard endpoints survive IPv4 replacement and clearing, retaining queued
  datagrams and pending receives. Sends use the current address, including zero
  only for limited broadcast while unassigned. Configuration changes still cancel
  ARP-pending sends. Applications own interpretation of already received data.
- Add a manual trusted `session --udp-broadcast PORT` handoff to `udp-echo` for
  qualification before the DHCP client exists. Only trusted setup receives the
  creation right; the echo child receives an opened endpoint. Its replies use
  limited broadcast, so the same handoff works before address assignment. The
  owner also accepted `--udp-unassigned`: explicitly clear IPv4 after opening
  the endpoint, retaining its binding, to qualify zero-source sends.

## Task 2 decisions

Accepted 2026-10-04:

- Acquisition runs synchronously for at most about ten seconds. On timeout the
  local session continues offline with profile DNS or fallback. Successful leases
  remain configured until reboot; task 3 removes this temporary lifecycle limit.
- Userspace chooses DNS; NET_CONFIG stores the chosen server and reports it with
  READ authority. REPLACE publishes address/route/DNS together. SET_DNS changes
  only DNS, including before assignment; CLEAR preserves the chosen server.
  Non-owner provider scripts wait within the same initial acquisition budget
  before their first launch. Local and remote launch paths read the current value
  for each new program.
  Existing programs retain their startup environment.
- Remove the task-1 manual broadcast session/echo handoff once DHCP acquisition
  works. The broadcast endpoint and its authority separation remain.

## Tasks

- [x] **1. Broadcast reception and the broadcast endpoint** (kernel, ABI).
  - The new right and the endpoint rules above, with unconfigured-source sends
    limited to the unassigned state.
  - Ordinary endpoints are unchanged.
  - **Finish when** a broadcast datagram from a host on the LAN reaches a
    right-holding endpoint in QEMU and natively, while ordinary endpoints still
    ignore it.
  - Capture the existing ping/UDP/TCP baseline before and after.
- [x] **2. The DHCP client and configuration syntax** (userspace).
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

## Task 2 implementation and qualification

Implemented: kernel DNS code `2175849`, userland `901b343`
([dependency #114](https://git.internal/PyxisOS/pyxis-userland/pulls/114)), branch
`net/dhcp-client`. QEMU, RTL passthrough and owner native cold/PXE qualification
are complete. Review follow-ups are on
[parent #380](https://git.internal/PyxisOS/pyxis-os/pulls/380) and dependency
#114. The dependency is merged; the integration now pins userland main
`ad1d53a`, including the resolved pointer/network runtime helpers, and incorporates
parent main `0e422f4`. Task 3 has not started.
The default profile requests a VirtIO lease; private native profiles retain their
MAC selector and use `dhcp = true`. Ordinary image builds passed. No compiler
container rebuild is needed.

Host-KVM QEMU with four CPUs, 2 GiB RAM and VirtIO user networking completed
DISCOVER/OFFER/REQUEST/ACK: address `10.0.2.15/24`, gateway `10.0.2.2`, DNS
`10.0.2.3`, lease 86400 seconds. Packet inspection confirmed both client messages
use source zero, limited broadcast and the BOOTP broadcast flag. GDB confirmed
IPv4 and chosen DNS. Remote `dig duckduckgo.com` reported the lease server;
HTTPS `example.com` fetched successfully. Non-owner provider scripts now wait
for the initial chosen DNS before launching; static REPLACE publishes IPv4 and
DNS together without prepublishing DNS before validation.

Review follow-ups keep a rejected server-supplied lease from blocking local
startup: DHCP REPLACE BAD_REQUEST continues offline and publishes profile or
fallback DNS, while authored static configuration errors remain fatal. Invalid
T1/T2 ordering uses both half-lease and seven-eighths defaults; malformed option
encoding still rejects the packet. These error paths were checked in code, not
exercised with injected replies. Ordinary build and QEMU acquisition/DNS/HTTPS
checks passed with these fixes at userland `901b343`.

The owner's native results, relayed by Claude on #380, used the task-2 archive
on the ThinkPad T14 AMD built-in RTL8168h through PXE. Both cold boots acquired
a lease; the owner recalls the reserved `.50`, without a captured console line.
Gateway ping returned 4/4 replies (0.36–3.5 ms), `dig example.com` reported
`1.1.1.1`, NOERROR and two answers, and HTTPS `example.com` returned the page.
The router supplies `1.1.1.1` first and `0.0.0.1` second; only the first DNS
entry is used, and its equality with fallback means this native result does
not distinguish the DNS source. QEMU's `10.0.2.3` result does. Faster local-send
retries remain an optional future improvement after the two successful cold
boots; the accepted approximately ten-second acquisition bound is unchanged.

Matched unprofiled traffic used baseline main `8842a97`/userland `68c5f4b`
(static `.15/24`, gateway `.2`) and task-2 kernel code `2175849`/userland
`6912f11` (DHCP supplies the same address/route). Both boots used four CPUs,
2 GiB RAM, host KVM and VirtIO user networking, with no debugger or packet filter
attached. Commands:

```sh
QEMU_DISPLAY=none MEMORY=2G CPUS=4 ACCEL=kvm VIRTIO_NET=1 TCP_FORWARD=2323:2323 \
  OVMF_CODE=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  OVMF_VARS=/usr/share/edk2/ovmf/OVMF_VARS.fd scripts/run-qemu.sh run
# Host loopback sinks:
socat -T10 UDP4-RECVFROM:18080,bind=127.0.0.1,reuseaddr,fork EXEC:/bin/cat
socat -u TCP4-LISTEN:5001,bind=127.0.0.1,reuseaddr,fork OPEN:<capture>,creat,trunc
# Guest remote shell:
ping -c 5 10.0.2.2
ping -c 3 127.0.0.1
udp-send 10.0.2.15 10.0.2.2 18080 "DHCP acquisition baseline"
ttcp -t -n8192 -l8192 10.0.2.2  # three separate runs
```

Both boots returned 5/5 gateway and 3/3 loopback replies and the same 25-byte UDP
payload. TCP before: 29.544828, 29.816208, 29.969068 s; after: 30.231474,
30.114651, 29.982472 s. Means: 29.776701 versus 30.109532 s, **+1.12%**;
baseline spread 0.424240 s, after spread 0.249002 s. The mean increase is smaller
than the baseline run spread; these samples do not establish its cause. No
steady-state packet-path change is involved in the DNS store/launch work. The
final TCP sink contained 67,108,864 bytes; no content hash was checked. Final
remote dig and HTTPS also passed at userland `6912f11`.

The RTL passthrough attempt uses the same host/configuration as task 1 (dock
connected, private built-in-port selector, `VFIO_PCI=0000:05:00.0`, VirtIO net
disabled). Initial PHY negotiation exposed an early local send failure. Failed
local sends now retain the current retry stage; only successful transmission
advances the backoff. The first successful REQUEST supplies the lease origin.
Earlier attempts transmitted a checksum-valid DISCOVER but saw no OFFER. The
repeat at `6912f11` acquired `.50/24` in a debugger-assisted run; an unprofiled
repeat at `67548c4` also acquired `.50`, with remote 5/5 gateway replies, dig via
chosen DNS `1.1.1.1`, and HTTPS `example.com` all passing. The chosen DNS was not
separately distinguished between lease option 6 and fallback in the passthrough run.

No pre-assignment unicast receive exception has been added. Because the client
clears IPv4 before acquisition and only the limited broadcast is accepted while
unassigned, successful acquisition establishes that these replies used a
compatible broadcast IP destination; it does not qualify other servers' behavior.
The owner confirms DHCP is active with gateway `.1` and reservations `.50` for
the built-in port and `.52` for the other port. The existing receiver limitation
stays documented for servers ignoring
the broadcast flag.

Native handoff: `build/dhcp-task2-native.tar.gz`, matching kernel/initrd/ISO with
the private DHCP selector and README instructions. SHA-256:
`0bab25ce4467caff8263b84f440897c5abc27c8a94e5bed057e2c684d1380e23`.
This is the archive used for the owner's successful native qualification.
It contains code `2175849` and userland `67548c4`; later documentation/gitlink
integration and the `901b343` error-handling changes are separate from that
qualified snapshot. All agent QEMU, GDB,
remote-client and host listener processes are cleaned up before handoff.

## Task 1 qualification and handoff

Kernel implementation: `285167e`; userspace: `e8363bd`
([dependency PR #110](https://git.internal/PyxisOS/pyxis-userland/pulls/110)).
Baseline: main `7955f59`, userspace `06812bc`. Ordinary image builds passed.
The integration pin is now merged userland `68c5f4b`, containing both broadcast
setup #110 and installer follow-ups #111. Networking sources are unchanged from
the qualified `e8363bd`. No compiler-container rebuild is required. Task 1 is
complete; task 2 is in progress on `net/dhcp-client`. Link selection has not started.

Matched before/after runs used host KVM, four CPUs, 2 GiB RAM, the RTL8111 at
`0000:05:00.0` through VFIO, VirtIO networking disabled, and the same private
`.50/24`, gateway `.1` profile. The receiving host used Wi-Fi at `.51`; these
measurements are not wired throughput or native ThinkPad results. Builds used
`make -j16 image PYTHON=build/hpet-config-venv/bin/python3
CROSS_COMPILE=/home/chronium/opt/pyxis-cross/bin/x86_64-unknown-pyxis-
NETWORK_CONFIG=<private profile>`. Boots used `scripts/run-qemu.sh run` with
`QEMU_DISPLAY=none MEMORY=2G CPUS=4 ACCEL=kvm VFIO_PCI=0000:05:00.0
VIRTIO_NET=0` and the matching `/usr/share/edk2/ovmf/OVMF_{CODE,VARS}.fd` pair.

| Check | Baseline | Task 1 |
| --- | --- | --- |
| `ping -c 5 192.168.0.1` | 5/5; mean 0.783 ms | 5/5; mean 0.703 ms |
| `ping -c 3 127.0.0.1` | 3/3 | 3/3 |
| UDP port-18080 echo on `.51` | 23-byte payload returned | Same payload returned |
| Three 64 MiB TCP sends, seconds | 25.081756, 26.625992, 25.633783 | 25.333539, 25.629350, 25.476032 |

UDP used `udp-send 192.168.0.50 192.168.0.51 18080 "DHCP broadcast baseline"`
and host `socat -T10 UDP4-RECVFROM:18080,bind=192.168.0.51,reuseaddr,fork
EXEC:/bin/cat`. TCP used `ttcp -t -n 8192 -l 8192 192.168.0.51` and host
`socat -u TCP4-LISTEN:5001,bind=192.168.0.51,reuseaddr,fork
OPEN:<capture>,creat,trunc`. The final sink contained 67,108,864 bytes; no content
hash was checked. TCP mean changed by -1.17%, below the baseline's 1.54-second
sample range. These traffic runs were unprofiled and had no debugger attached.

With the explicit broadcast init handoff and GDB attached, LAN UDP to
`255.255.255.255:19000` and `192.168.0.255:19000` returned intact limited-broadcast
echoes from `.50`. A simultaneous ordinary `.50:19000` endpoint stayed available
for unicast, whose reply was unicast; after it closed, unicast fell through to
wildcard and produced a broadcast echo. Host `IP_PKTINFO` distinguished reply
destinations. Ordinary limited/subnet-broadcast sends returned BAD_REQUEST.
GDB observed wildcard address zero, BOUND state, and no bad UDP checksums.

With `--udp-unassigned`, GDB observed IPv4 address/prefix zero and the same
wildcard binding still BOUND. LAN normal, empty and odd-length datagrams returned
intact from `0.0.0.0:19000` to limited broadcast. Queue retention across a later
address change and source-zero receive rejection were inspected in code, not
separately exercised. Local captures use `build/dhcp-task1-*`. QEMU/debugger jobs
have been cleaned up.

Owner-run native cold/PXE qualification on 2026-10-04 used the
`dhcp-task1-native` archive, the built-in RTL8168h, `.50/24`, and desktop `.213`.
Nine LAN broadcasts returned intact from `.50:19000` to `255.255.255.255`
(desktop `IP_PKTINFO`). With an ordinary endpoint on the same port, unicast
returned to `.213`; after that endpoint closed, unicast returned by limited
broadcast through the wildcard. This confirms native reception, ordinary
broadcast exclusion, concrete-first delivery and wildcard fallback. Native
subnet-broadcast and zero-source runs were not reported; those remain covered
by the QEMU/VFIO checks. Results and the blocker-free review are on
[integration PR #374](https://git.internal/PyxisOS/pyxis-os/pulls/374).

## Review notes for task 2

- Set the accepted BOOTP broadcast flag first. While unassigned, input still
  drops unicast IP destinations even when Ethernet names our MAC. If a server
  or relay ignores the flag and qualification shows a missing OFFER/ACK, inspect
  its destination before proposing a narrow receive exception. Such an exception
  is not implemented or accepted here.
- Once the client exists, decide whether to retain the manual broadcast handoff
  as a diagnostic or remove it. Task 1 does not settle that later choice.

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
