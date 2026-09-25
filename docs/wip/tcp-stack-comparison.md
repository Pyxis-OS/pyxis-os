# TCP implementation comparison

Status: source review for the [outbound TCP milestone](tcp.md). The selected
follow-up was to investigate lwIP; Rust integration and picoTCP's licensing choice
are deferred. The [build/link findings](lwip-port.md) now establish freestanding
compatibility and identify the decisions still required for runtime adoption.

## Candidates

| Candidate | Fit and cost for Caelum |
| --- | --- |
| Native TCP | Direct use of the existing packet, routing, worker and object ownership model. We own all protocol implementation, recovery behavior and maintenance. Small source size would not by itself mean lower complexity or better interoperability. |
| lwIP | C, BSD-licensed, callback API and a single-context mode; the strongest reuse candidate for the current kernel. Supplies TCP machinery, but its IP, packet-buffer, timer and resource policies need a deliberate adapter. |
| smoltcp | Event-driven, can operate without a heap, and is 0BSD-licensed. It is Rust, so this C kernel would additionally need a Rust build and an FFI/ownership boundary. A valid alternative, but not the smallest change for this milestone. |
| picoTCP | C and modular, but the upstream project publishes GPLv2/GPLv3 terms. That is a project licensing choice to discuss, not a dependency to introduce incidentally. It offers no established integration advantage here from this initial survey. |

Sources: [lwIP overview](https://github.com/lwip-tcpip/lwip),
[smoltcp overview](https://github.com/smoltcp-rs/smoltcp),
[picoTCP overview](https://github.com/tass-belgium/picotcp).
The detailed source review below concentrates on lwIP; this is not an exhaustive
security or maintenance audit of those projects.

## lwIP source reviewed

Review point: `STABLE-2_2_1_RELEASE`, commit
`77dcd25a72509eb83f72b033d219b1d40cd8eb95`. The investigation now pins this subset with its license and dependency record;
no runtime adoption or upstream patches follow from the build alone.

`NO_SYS=1` permits a single-context callback integration without sockets, netconn,
a tcpip thread or OS mailbox emulation. That maps naturally to Caelum's existing
network worker, although Caelum itself is an OS. Packet input, callbacks, timer
processing and TCP API calls would all stay in that worker. The native capability
facade would stage requests and wake parked callers; upstream callbacks would not
become Pyxis public interfaces. See the [mainloop port contract](https://github.com/lwip-tcpip/lwip/blob/77dcd25a72509eb83f72b033d219b1d40cd8eb95/doc/doxygen/main_page.h).

The API provides connect, write, receive callbacks, receive-window credit,
shutdown and abort. A copied write avoids lending user memory to retransmission.
Caelum must own/refcount received pbufs correctly and return receive credit only
as bytes leave its accounted receive budget. Successful close can transfer
control-block lifetime to lwIP; keeping the old pointer in a capability would
be unsafe. Error callbacks and completed calls must therefore be detached from
transport lifetime explicitly. See [TCP API and close ownership](https://github.com/lwip-tcpip/lwip/blob/77dcd25a72509eb83f72b033d219b1d40cd8eb95/src/core/tcp.c).

`sys_check_timeouts()` and `sys_timeouts_sleeptime()` can participate in the
worker's earliest-deadline wait. The port must handle the millisecond clock's
wraparound and avoid starvation while draining work. This needs no new timer
interrupt or busy polling. See [timeouts](https://github.com/lwip-tcpip/lwip/blob/77dcd25a72509eb83f72b033d219b1d40cd8eb95/src/core/timeouts.c).

## The difficult boundary is below TCP

lwIP is not a standalone TCP engine taking only a segment plus source/destination.
Input uses lwIP's current IP context; output routes through netif/IP functions.
Calling private `tcp_input` while manually fabricating that context would couple
Caelum to upstream internals. Extracting and renaming TCP files would create a
fork rather than remove the integration cost. See [TCP input](https://github.com/lwip-tcpip/lwip/blob/77dcd25a72509eb83f72b033d219b1d40cd8eb95/src/core/tcp_in.c)
and [TCP output](https://github.com/lwip-tcpip/lwip/blob/77dcd25a72509eb83f72b033d219b1d40cd8eb95/src/core/tcp_out.c).

The candidate to investigate is a TCP-only use of lwIP's public IPv4/netif path:
feed only TCP traffic into that path, disable its UDP/DNS/ARP/application services,
and bridge output back to Caelum's existing routing/ARP/packet submission. Keep
one source of configuration truth; any netif address/MTU state is a worker-owned
projection, not another independently configurable network stack.

This needs source and build investigation, especially local destinations. Caelum
accepts the full 127/8 loopback range, whereas lwIP's built-in loopback/input
policies are not automatically identical. Ordinary and loopback TCP must share
Caelum's deferred delivery rules. Also decide who constructs the IPv4 header;
never strip and rebuild it casually or queue the same packet in both stacks.
The candidate may need a small documented patch or a narrow complete-IP transmit
entry point. Neither is approved implicitly by choosing library reuse. See
[lwIP IPv4 routing/input/output](https://github.com/lwip-tcpip/lwip/blob/77dcd25a72509eb83f72b033d219b1d40cd8eb95/src/core/ipv4/ip4.c).

Replacing all existing IPv4/ARP/ICMP/UDP internals with lwIP would provide a cleaner
single-stack boundary, but it would reopen working subsystems and substantially
expand this milestone. Keep that as an explicit alternative, not an accidental
consequence of adding TCP.

## Defaults that cannot simply be accepted

- On control-block allocation failure, `tcp_alloc` tries reclaiming TIME_WAIT,
  closing and some active connections. This conflicts with the proposed policy
  of failing the new request without evicting other connections. Admission must
  prevent entry into that fallback even on heap failure, or a small recorded
  patch must change it. Counting open handles alone cannot cover closing state.
- `tcp_next_iss` has a predictable default unless `LWIP_HOOK_TCP_ISN` is supplied.
  Define a reviewed entropy-backed hook and initialization policy. Likewise,
  `LWIP_RAND` seeds the default ephemeral-port cursor; it does not independently
  randomize every new binding. Audit the actual desired port policy.

Both findings are in the pinned [TCP implementation](https://github.com/lwip-tcpip/lwip/blob/77dcd25a72509eb83f72b033d219b1d40cd8eb95/src/core/tcp.c).

Upstream offers custom allocator and pool configuration. Map those to bounded
kernel-owned storage without importing host libc or allocating on APs. Include
pbufs, segment metadata, reassembly and retired control blocks in the budget;
setting only a send-buffer size is insufficient. See [configuration options](https://github.com/lwip-tcpip/lwip/blob/77dcd25a72509eb83f72b033d219b1d40cd8eb95/src/include/lwip/opt.h).

## Recommendation and decision gate

The focused lwIP build/link investigation is complete. Reuse could save the
largest body of protocol work, and its callback model fits the worker. Runtime
adoption remains conditional on the documented packet and policy boundary. The
remaining cost is integration and policy work, not a POSIX ABI requirement or a
need to change the process/capability model.

The first investigation pins/builds the minimal subset and documents the
packet/timer/allocator boundary and required runtime adaptations. See its
[findings and decision gate](lwip-port.md).
No new public stream API is needed for that decision. Use ordinary builds,
boots and debugger inspection, not a new probe application or test framework.

Continue with lwIP only if the boundary remains small and readable. If it needs
private IP-context manipulation, scattered protocol patches or a second routing
policy, stop and discuss native TCP versus a separately scoped full-stack
replacement. Do not decide that tradeoff merely to make the port compile.
