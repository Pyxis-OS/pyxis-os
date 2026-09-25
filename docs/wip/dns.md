# DNS queries and hostname ping

Status: agreed milestone, implementation pending. Discuss any newly discovered
interface or policy decisions before implementing the affected task.

## Result and boundary

With networking enabled, boot Pyxis and run `dig example.com` against the
configured public resolver, initially `1.1.1.1`. Then use the same userspace DNS
code to run `ping example.com`. Numeric-address ping continues to work without
DNS or randomness. An external host may answer DNS but decline ICMP; successful
resolution does not guarantee an echo reply.

This is a small IPv4 DNS client using the existing UDP, clock and
[random](../randomness.md) capabilities. DNS packets, resolver selection and
answer interpretation belong to userspace. The kernel only gains destination-
based source-address selection for opening a UDP endpoint. No DNS syscall,
kernel resolver, service daemon or libc resolver is part of this milestone.

## UDP prerequisite

Current UDP OPEN requires an explicit local address. Add a route-aware open
operation on the UDP service: the caller supplies a destination IPv4 address
and desired source port, the kernel selects a local address through its existing
routing policy, and success returns an owned endpoint with its concrete binding.
Selection and binding happen together; no userspace route snapshot is needed.
Use the existing OPEN authority and preserve explicit-address OPEN.

The destination selects the source address only. The resulting endpoint is not
connected to one peer and is not a wildcard listener. Existing address-removal,
shutdown and close semantics apply. No route or unusable source address produces
an explicit failure. Loopback and the configured NIC use the same routing rules
as other traffic; do not embed QEMU addresses in the client or kernel.

## Resolver configuration

Extend the existing `app://config/network.lua`, evaluated by session, with an
optional top-level setting alongside `net0`:

```lua
dns = { server = "1.1.1.1" },
```

The packaged configuration explicitly selects `1.1.1.1`. Missing configuration
or an omitted server setting selects the same default. Validate a numeric IPv4
unicast server address; empty or malformed values are configuration errors,
not a request for fallback. There is one server, no search domain and no list of
fallback resolvers. Validate both network and DNS settings before applying boot
configuration or launching the shell.

Session replaces any inherited `DNS_SERVER` entry with the selected address,
alongside its existing `TZ` handling. The shell forwards the environment to
children normally. The DNS setting does not go to the kernel, change network
authority, enable a missing NIC or perform a boot-time lookup.

Client selection order is an explicit server argument, then `DNS_SERVER`, then
`1.1.1.1` if the variable is absent. An empty or malformed environment value is
an error when used; an explicit override takes precedence. This also permits
direct-init applications without a session launcher to use the default.

## Query and reply contract

The initial command forms are:

```text
dig example.com
dig example.com A
dig @1.1.1.1 example.com
```

Accept ASCII hostnames with an optional trailing dot. Enforce DNS label and
encoded-name length limits; no search suffixes, Unicode conversion or general
escaped DNS-name syntax. Send one recursive `IN A` question over IPv4 UDP to
port 53. Do not iterate referrals or contact authoritative servers directly.

Use host-backed randomness for the transaction ID and an explicitly selected
source port from the ephemeral range, with bounded retries for binding
collisions. The generic UDP ephemeral allocator remains unchanged. Unavailable
randomness is an error; no timestamp, libc `rand()` or sequential fallback.

Allow two attempts, each with a three-second monotonic deadline covering setup,
send and receive. Use fresh random query identity and a fresh binding for a
retry; release the previous endpoint. Retry a timeout, not a valid negative DNS
answer or a local configuration/authority failure. No silent resolver switching.

Match server address and port, transaction ID, response/opcode fields and the
question's name, type and class before accepting a response. Name matching is
ASCII case-insensitive. Ignore unrelated or malformed datagrams without
extending the deadline. Keep parsing and per-packet work bounded, including
compression-pointer traversal, expanded name length and record counts.

Start with classic 512-byte DNS messages and no EDNS. Validate offsets, lengths,
record extents and compressed names before using or displaying them. Unknown
record types can be skipped by their checked extents. Receive enough data to
recognize an oversized datagram without leaving it stuck at the queue head.
A matching response marked truncated reports that TCP fallback is unavailable;
never present its partial records as a complete result.

Display the selected server, response status and answer records, including
names, TTLs, IPv4 addresses and CNAMEs. Escape untrusted names for terminal
output. A valid complete response, including NXDOMAIN or no matching A data,
is a successful `dig` exchange with its DNS status shown. Usage, transport,
timeout and unsupported-truncation failures return a nonzero process status.

Do not issue additional queries to follow CNAMEs in this slice. For hostname
resolution, only accept A records owned by the requested name or reachable
through a checked, loop-free CNAME chain in the answer section. Unrelated A
records are not candidates. A chain without an address in the same response
produces a clear no-address result.

## Focused implementation tasks

- [x] **1. Route-aware UDP open.** Add the native operation and libpyxis helper,
  retaining explicit binding and existing endpoint ownership. Build and boot;
  inspect selection/binding for loopback and the configured NIC through ordinary
  debugger use, including a no-route result. Update the UDP interface reference.
- [x] **2. Resolver configuration.** Extend session's network configuration,
  package `1.1.1.1`, and export `DNS_SERVER`. Confirm the ordinary shell sees the
  selected setting, missing settings default correctly, and invalid configuration
  is diagnosed. Document direct-init and override behavior.
- [x] **3. Native dig.** Add focused query, parser and command files in userland,
  package the utility, and implement the bounded exchange above. Exercise the
  default public resolver, an explicit numeric override, a negative answer and
  failure reporting in ordinary QEMU use. Keep DNS-specific code in the
  application until the next task supplies its second consumer.
- [ ] **4. Hostname ping.** Share the concrete DNS query/answer code between dig
  and ping without building a provider framework or libc resolver. Resolve a
  hostname once before the existing echo loop, select the first eligible IPv4
  answer, and print the name and chosen address. Use `DNS_SERVER` and the same
  default policy. Resolution failure stops ping with a diagnostic and nonzero
  status; do not cycle through addresses or resolve again on every echo timeout.
  Verify hostname use and unchanged numeric loopback/external-address use.

Each task should be a focused PR, with paired userland/Pyxis PRs where SDK or
submodule integration requires them. Preserve ABI version policy: no version
bump or compatibility shim merely for adding the operation. Validation uses
ordinary builds, QEMU boots, manual commands and debugger inspection; no new
tests, self-tests, fault injection or output automation.

After task 4, rewrite this document around the implemented interfaces, commands
and limits, move it to `docs/dns.md`, and update the milestone index. Completion
requires both `dig` and hostname ping, with the public resolver configured by
default; ICMP reachability of an arbitrary resolved host is not an acceptance
condition.

## Deferred work

TCP fallback, EDNS, IPv6/AAAA, other query types, further CNAME queries, caching,
DNSSEC validation, encrypted DNS, DHCP-provided resolvers, multiple servers,
search domains and general libc name resolution belong to later work. Classic
UDP DNS does not authenticate replies; random matching fields are not DNSSEC.
No change to the default opt-in NIC configuration is needed for this milestone.

References: [DNS message format and compression (RFC 1035)](https://www.rfc-editor.org/rfc/rfc1035.html),
[response matching and query entropy (RFC 5452)](https://www.rfc-editor.org/rfc/rfc5452.html),
[current networking](../networking.md), [session configuration](../session-configuration.md).
