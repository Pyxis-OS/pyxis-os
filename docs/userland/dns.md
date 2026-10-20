# DNS queries and hostname ping

`dig` and `ping` share the concrete DNS message and exchange helpers in
`userspace/common/dns.h`, `dns_message.c` and `dns_query.c`. DNS is userspace
policy; the kernel provides UDP endpoints, monotonic deadlines, randomness and
ICMP echo. There is no DNS syscall, service daemon or libc resolver.

## Resolver configuration

Boot init reads the ordered interfaces and DNS selection from
`boot://config/network.lua`:

```lua
dns = { server = "1.1.1.1" },
```

With no `interface`, `server` selects a numeric resolver through the caller's
held routed UDP scope. Each fresh query can follow current route preference.
Adding `interface = "eth0"` pins the resolver path; omitting `server` then selects
the DHCP DNS contribution from that named interface. Omitting `dns` leaves resolution
unavailable. A different interface's lease never replaces the selection, and
there is no automatic public-resolver fallback. Empty or malformed selected
values are errors. See [network configuration](../devices/networking.md#boot-configuration-and-use).

Local and remote launchers read registry policy and supply a `dns_udp` factory:
a reduced-rights copy of their held UDP scope for routed numeric DNS, or a derived
single-interface factory for pinned DNS. The resulting transport
and numeric `DNS_SERVER` text travel together to resolver-capable children.
If the selected interface is outside that scope or no server is published,
launch supplies no resolver path; it never widens the grant. A later link or
address failure is reported by the actual query. Existing programs retain their startup
snapshot. Startup performs no DNS query.

Both clients use `DNS_SERVER`; absence, empty text or malformed values fail
without choosing another server. Only `dig` offers an explicit server override,
and that override still uses the held `dns_udp` scope. There is one selected
server, no fallback list and no search domain. QEMU networking remains opt-in
through `VIRTIO_NET=1`.

## Queries with dig

The boot archive includes `dig [@SERVER_IP] NAME [A]`, a native userspace DNS
diagnostic using the scoped `dns_udp`, clock and random capabilities. With
`make run VIRTIO_NET=1`:

```text
dig example.com
dig @1.1.1.1 www.github.com A
dig does-not-exist.invalid
```

Server selection is an explicit `@` argument, then `DNS_SERVER`. Missing both
is an error. An explicit override bypasses the environment value;
an empty or invalid selected value is an error. Only numeric unicast IPv4 server
addresses are accepted, including loopback. The command never switches servers
silently. Names use ASCII letters, digits and interior hyphens in nonempty
labels, with an optional trailing dot. Labels are limited to 63 bytes and the
encoded name to 255 bytes. There is no suffix search or Unicode conversion.

One recursive `IN A` question is sent to UDP port 53. Each of two attempts has
a three-second monotonic deadline covering source setup, send and receive.
The shared query helper also accepts an optional absolute overall deadline.
It caps each attempt and prevents retries after expiry; existing DNS tools pass
zero and retain their current behavior. [HTTP fetching](http-fetch.md) uses this
to include DNS in its whole-fetch budget.
Host-backed randomness supplies a fresh 16-bit query ID and source port in
49152–65535; at most sixteen random port choices are tried per attempt if binds
collide. The endpoint binds through route-aware OPEN, then is shut down and
closed after the attempt. Only timeouts trigger a second attempt. Missing random
hardware fails explicitly rather than substituting predictable values.

Replies must match the selected server address/port, query ID, response/opcode
fields and the case-insensitive question name, type and class. Unrelated or
malformed packets are discarded without extending the deadline. The parser
bounds label expansion, compression traversal and record counts, validates all
three record sections before exposing answers, and checks A/CNAME payloads it
interprets. Other record data is skipped by its checked extent. Printed names
escape control characters, whitespace, literal dots and backslashes in labels.

Output includes server, question, DNS response status, answer count, TTLs, A
addresses and CNAME targets. Unknown answer types/classes retain numeric labels.
An empty answer section or valid negative response such as NXDOMAIN is a
successful diagnostic exchange, not a transport error. Usage, native-call,
timeout, output and supported-size failures return a nonzero process status.

This slice uses classic 512-byte DNS messages without EDNS. A matching truncated
response reports that TCP fallback is unavailable; oversized replies are also
reported as unsupported. Neither exposes partial answers. Full supported UDP
datagrams are consumed so an oversized DNS response cannot block the receive
queue. Random matching fields do not authenticate DNS or provide DNSSEC.

References: [DNS wire format (RFC 1035)](https://www.rfc-editor.org/rfc/rfc1035.html)
and [query matching (RFC 5452)](https://www.rfc-editor.org/rfc/rfc5452.html).

## Hostname ping

```text
ping -c 2 example.com
ping -c 1 www.github.com
ping -c 1 127.0.0.1
```

A numeric dotted-decimal target follows the existing echo path without reading
`DNS_SERVER` or needing UDP/random capabilities. Other targets must pass the
same ASCII hostname validation as `dig`. Hostname ping resolves once before
starting its ICMP requests, prints the requested name and selected address, and
uses that address for every exchange. It does not cycle through addresses or
resolve again after an echo timeout.

Resolution requires a complete NOERROR reply. The client selects the first
answer-section `IN A` record for the requested name or the terminal name of its
CNAME chain. Names compare case-insensitively; answer ordering does not affect
chain traversal. Only answer-section `IN` records participate: unrelated A
records, authority data and additional data cannot supply the address.
Conflicting CNAME targets, CNAME/A coexistence for a traversed owner, and loops
are errors. Traversal is bounded by the answer count; no extra query follows a
CNAME whose address is missing from the same reply.

A negative DNS status, absent address, invalid chain, timeout or other resolution
failure produces a diagnostic and nonzero exit status before any echo request.
In contrast, `dig` reports valid negative answers as successful diagnostics.
An external host can resolve successfully and still decline ICMP echo.
[Echo pacing, statistics and exit status](../devices/networking.md#userspace-ping) otherwise
remain unchanged.

## Limits and deferred work

There is no cache, TCP fallback, EDNS, IPv6/AAAA, other query type, DNSSEC
validation, encrypted DNS, additional CNAME queries, multiple-server selection,
search domains or general libc name resolution.
Classic UDP DNS is unauthenticated; randomized matching fields are not DNSSEC.
DNS message parsing and selection use bounded storage and traversal, with no
allocation or provider framework.
