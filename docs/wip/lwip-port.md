# lwIP port investigation

Status: the selected lwIP 2.2.1 sources compile and link against Caelum. Runtime
adoption is still a decision gate. No packet bridge or public TCP API is enabled.
This is the first investigation from the [TCP milestone](tcp.md), following the
[stack comparison](tcp-stack-comparison.md).

## Reproducible build

```sh
git submodule update --init third_party/lwip
make lwip-port
```

Use the usual prebuilt Pyxis cross compiler. The target produces
`build/caelum-lwip.elf` and its link map, containing the normal kernel plus the
selected lwIP objects and freestanding port hooks. It links every selected
object, without relying on dead-section elimination to hide unresolved routines.
`make image` continues to use `build/caelum.elf`; there is no boot-time lwIP
initialization, alternative runtime mode or incoming TCP dispatch.

[The dependency record](https://git.internal/chronium/pyxis-lwip/src/branch/main/UPSTREAM.md) pins
`77dcd25a72509eb83f72b033d219b1d40cd8eb95` (`STABLE-2_2_1_RELEASE`). Sixteen C
sources and their 61 consumed upstream headers are imported without edits. No tests,
application sources, socket/netconn implementation or upstream build system are
imported. The sources live in the pinned `third_party/lwip` submodule from
pyxis-lwip; Caelum's build configuration and port hooks remain in pyxis-os.
Upstream adaptations belong in pyxis-lwip and must be recorded there, then
selected by an explicit parent pin update.

The profile builds in GNU C23 with the existing kernel flags, including
`-mgeneral-regs-only`, `-mno-red-zone` and the higher-half code model. Its additional
`-nostdinc` search path permits only compiler-provided freestanding headers and
private port headers. No host libc or userspace libc is used. The private headers
forward memory/string routines and supply local `strncmp` and decimal conversion
for upstream helpers; optional snprintf-based heap diagnostics are explicitly
unsupported rather than replaced with a dummy implementation.

The profile uses NO_SYS callback mode, IPv4 and TCP. UDP, DNS, ICMP, ARP, Ethernet,
IPv6, forwarding, IP fragmentation/reassembly, upstream loopback queues and
socket/netconn interfaces are disabled. This is a build selection, not a claim
that the remaining default options are the final transport policy.

The linked profile adds 47,413 text bytes, 8 initialized-data bytes and 272 BSS
bytes with the inspected compiler/configuration. Dynamic pbuf, connection and
segment storage is not included in those numbers. A successful build establishes
freestanding linkage, not TCP interoperability or bounded runtime memory use.

## Concrete port hooks

`kernel/net/lwip/port.c` provides real kernel-backed allocation/free and monotonic
milliseconds. Raw lwIP calls are restricted to a single BSP task with interrupts
enabled. Allocation hooks briefly disable interrupts around kmalloc/kfree and
restore the previous state. calloc checks multiplication overflow. The hooks do
not permit AP, interrupt or fault-context allocation, and there is no extra heap,
allocation registry or generic portability layer.

The eventual network worker remains the sole owner. The build's context assertion
checks BSP/IF=1; it does not by itself prove worker identity. Runtime wiring must
keep all calls and callbacks in that worker. No other kernel task should invoke
raw lwIP just because it is also on the BSP.

`sys_now` truncates monotonic milliseconds to lwIP's 32-bit clock. The future
worker should call `sys_check_timeouts`, then combine the *relative delay* from
`sys_timeouts_sleeptime` with its current 64-bit monotonic time. Handle the
infinite sentinel and overflow explicitly. Never interpret a wrapped lwIP
absolute timestamp as a Caelum nanosecond deadline. There is no timer polling
thread or additional APIC interrupt in this profile.

No lwIP routine is called on the normal boot path. In particular, these hooks do
not yet supply TCP identity generation or enforce the milestone's proposed
transport budgets. Existing upstream allocation-reclamation and ISN defaults
must not be enabled as production policy accidentally.

## Candidate packet boundary

The following is a concrete proposed interface boundary, not implemented code:

- Caelum's existing IPv4 input validates and classifies an incoming packet. Only
  accepted TCP packets would be copied into an owned RAM pbuf and passed through
  public `ip4_input`. No DMA-backed RX memory or Caelum receive buffer is lent to
  lwIP, and no private `ip_data` fields are fabricated.
- `ip4_input` consumes that pbuf. A receive callback either accepts ownership
  into the bounded stream queue or returns the documented backpressure result.
  Consumption by native READ releases pbuf storage and advances receive-window
  credit. Copying data to another queue does not remove it from the receive
  budget. Deferred delivery prevents recursive reentry into lwIP input.
- lwIP constructs the TCP and IPv4 headers. Its netif output callback borrows a
  pbuf chain only for the call, copies the complete datagram into a Caelum
  `net_packet`, then submits it through existing routing/ARP/local delivery.
  Successful submission transfers that copy; every failure leaves the callback
  responsible for releasing it. lwIP retains its own retransmission storage.
- Factor a narrow complete-IPv4-datagram submission operation from the existing
  transmitter. It must validate source/destination against current routing,
  accept only the supported header/length/fragment shape, and leave lwIP's
  TCP/IP headers intact. This is an internal producer boundary, not a userspace
  raw-packet capability. Do not strip/rebuild headers to reuse the old API.
- ARP/NIC acceptance is not a peer ACK. An asynchronously dropped copy can be
  recovered by lwIP retransmission; stale ARP copies must not be delivered after
  abort or tuple retirement. Define a live-generation token and cancellation/
  expiry handling for queued copies before connecting TCP to ARP. They must not
  retain a freed control-block pointer. A deadline alone is not immediate abort.

Two small netif descriptors could project the current NIC and loopback state,
owned and refreshed by the same worker. They are not independently configurable
interfaces or a second route database. Explicit local binding/netif selection at
CONNECT and revalidation on output keep source authority in Caelum. Address
removal must invalidate affected connections before a replacement is published.

## Runtime changes that need a decision

| Area | Source finding | Proposed resolution |
| --- | --- | --- |
| Routing | `LWIP_HOOK_IP4_ROUTE` is a fallback after built-in route matching; merely installing it does not make Caelum's policy authoritative. | Evaluate explicit source/netif binding and an authoritative route hook for control packets. Prefer one small change making the hook decisive over reproducing Caelum's route policy in mirrored lwIP tables. |
| Loopback | `ip4_input_accept` accepts a configured unicast address; the special loopback case covers 127.0.0.1, not Caelum's whole 127/8 range. | Add a narrow input-accept hook for Caelum's already validated local packets. Keep lwIP's own loopback queue disabled and use Caelum's existing deferred queue. |
| Resource pressure | `tcp_alloc` may reclaim TIME_WAIT, closing or active connections after allocation failure. Heap-backed pools do not remove that fallback. | A recorded local change should return allocation failure without eviction. Admission must count transport objects after their final application handle closes. |
| Identity | The default ISN generator is predictable. The random macro only seeds the default port cursor. | Use the existing ISN hook with a reviewed keyed primitive/boot secret and choose explicit random ephemeral binds. Prepare entropy outside the sole network worker; fail new connects if unavailable. |
| Queued output | Caelum ARP can retain a packet after the producer returns, while lwIP independently retransmits and can retire its connection. | Define the token lifetime and cancellation of queued copies at this boundary, before any SYN is submitted. |

The first three items may require local changes to upstream `ip4.c` and `tcp.c`;
none are applied in this PR. The identity hook is an existing extension point;
no new cryptographic algorithm is proposed. Allocation budgets, initialization,
retry/teardown policy and exact callback ownership remain the next design step.

A runtime adapter would therefore be more than a linkable library, but need not
replace the existing UDP, ICMP, DNS, ARP or device paths. If authoritative routing
and local delivery cannot be expressed with these narrow changes, stop and
compare native TCP with a separately scoped full-stack replacement. Do not
accumulate internal-context tricks or duplicate policy to force reuse.

## Result and next decision

The compiler/ABI/library dependency boundary is feasible. The optional artifact
links with no undefined symbols and no compiler warnings; imported source bytes
match the pinned upstream files. Normal image builds and ordinary boots validate
that this investigation has not enabled another network stack. They do not
validate lwIP packet exchange, recovery, timer integration or callback lifetime.

Before runtime implementation, decide whether to accept the narrow routing,
loopback and allocation-policy adaptations above. If accepted, turn them into
explicit reviewed patches and the packet bridge as the next focused task, before
building the connection-capability API. Remove the separate investigation target
when normal runtime integration supersedes it; it is not a second product to
maintain. If rejected, keep the findings and rescope rather than preserve dormant
vendor code for its own sake.
