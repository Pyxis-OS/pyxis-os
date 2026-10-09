# Network kernel debugger

Owner-requested proposal, 2026-10-09; code inspected at `b0a050b7`.
No implementation is assigned. Goal: GDB on the owner's host inspecting Caelum
on the ThinkPad during a PXE driver bring-up loop, including Renoir display work.

**Proposed route:** a kernel all-stop debugger with preallocated, polled NIC
transport, small reliable UDP framing, and a host TCP bridge for ordinary GDB
`target remote`. Default off. The first task builds resumable stop state, not an
entire stub. The first usable milestone includes modification and stepping;
read-only inspection is an earlier qualification point.

## Existing foundations and missing contracts

Read [remote debugging](../development/remote-debugging.md),
[kernel-log fatal ownership](../interfaces/kernel-log.md#fatal-network-ownership),
[SMP](../kernel/smp.md) and the selected
[VirtIO](../../kernel/virtio/net.c)/[RTL8111](../../kernel/net/rtl8111/io.c) drivers.

- Fatal UDP logging proves bounded, allocation-free TX outside lwIP/scheduling.
  It irrevocably takes the NIC until reboot; it is TX-only and cannot be used
  as a resumable debugger transport. Normal networking remains BSP/IF=1 work;
  lwIP receives allocate pbufs and native endpoints lock/wait.
- RX completion is not simply “available”: VirtIO advances completion accounting
  before lending stack-listed buffers to protocols; RTL lends a slot before
  reposting it. The hardware-mutation gate excludes these callbacks. An idle
  gate is not proof all buffers are free. New reversible handoff must account
  for device-owned, completed, borrowed and reposted slots, interrupted publication,
  pending TX, queue indices and interrupt masks with one authority.
- NMI currently follows fatal exception handling; only #DF has an IST stack.
  There is no resumable NMI stop, #BP/#DB interception or debug-register owner.
  Existing TLB-flush IPIs acknowledge translation retirement, not CPU quiescence.
- Ordinary VM query/scratch helpers are forbidden in interrupt/fault entry and
  can be busy when interrupted; malformed page tables can panic their walker.
  Safe debugger memory access needs separate preallocated infrastructure.

## Transport, availability and discovery

Keep existing net0 selection and address configuration authoritative. Enable the
stub only after the selected NIC is activated, assigned IPv4 and its debug
resources are prepared. Prefer the existing static-IP net0 profile for repeatable
PXE work. Preparation is not activation; `debug.wait` cannot stop before NIC
availability and still offer this network service. Earlier boot faults retain
existing log/serial behavior. Independent early NIC bring-up is later work.

Proposed options: `debug.net=NAME` enables the service, `debug.wait=1` waits once
at its readiness checkpoint; the latter without the former is invalid. Same
case-sensitive printable-name limits as `remote.beacon`, but a new debugger tag
and UDP port **2326**, distinct from remote discovery 2324 and logs 2325.
`remote.beacon=t14` and `log.udp=1` remain independent.

The bridge first discovers one named/MAC-filtered target using host-advertised
HELLO datagrams, like reverse-terminal discovery. The current beacon service is
userspace, so never call that service while stopped. The debugger's own bounded
HELLO exchange remains available in its polled stop loop, including when
debug.wait stops before any peer is armed; pin a peer before accepting commands.
Use limited/directed broadcast only for discovery, then pin one same-LAN peer's
MAC/IP/port and freeze the local NIC/address through a stop. Include target MAC,
fresh boot/session identities and image token. Existing log boot stamps are only
grouping hints and can collide; do not use them as replay protection. Session
identities reject stale boots/reconnections, not malicious peers. Duplicate
names require explicit target selection, never silent switching.

Stopped traffic uses prebuilt Ethernet/IPv4/UDP frames with checked lengths and
checksums, no IP fragments or routing. Learn peer MAC from validated discovery;
answer bounded ARP requests for the frozen local IPv4 while stopped, so the
host's neighbor-cache expiry does not strand the session. No DHCP, DNS, lwIP,
allocator, ordinary driver lock or sleeping service in the debug loop.

The bridge binds GDB TCP to `127.0.0.1:1235` by default. Forward RSP packets,
ack bytes and raw Ctrl+C through a versioned envelope: session, direction,
sequence/ack, kind, length, payload. Proposed encoded RSP limit **1024 bytes**,
fits one LAN datagram; advertise a conservative PacketSize accounting for
escaping/framing overhead and chunk memory/XML/thread replies. Stop-and-wait per direction,
retained retransmission data and duplicate suppression provide ordered delivery;
keep bounded recent replies/side-effect execution state. Raw Ctrl+C has its own
control record, able to interrupt an outstanding continue; a transport ACK
confirms receipt, not completion of the RSP operation. Duplicate continue,
write, breakpoint, reset or MMIO commands must not execute twice, including RSP
retries above UDP. Keep the executed control record across resume/re-entry;
unsupported/oversized packets return real errors, not OK. No automatic new
session on uncertain command completion. Rate/retry/idle budgets belong to the
implementation, with clock maintenance even when disconnected.

**Why not kernel TCP:** normal TCP needs lwIP state, allocation, timers and worker
execution that all-stop removes. A separate polling TCP stack could work, but adds
connection/retransmission/ARP machinery without avoiding NIC ownership or stop
problems. Leave TCP on the host; the kernel implements only this bounded LAN
transport. Fatal logging keeps its independent one-way fallback.

## Entry, all-stop and resume

The BSP is the debugger service CPU. Preallocate guarded per-CPU entry stacks,
frames and stop state, mapped in every relevant root. NMI entry must handle
SWAPGS windows and interrupted user RSP without assuming GS; use trusted CPU
identification. Accept debugger NMIs only under an armed stop generation; handle
simultaneous entry, late delivery and nesting. NMI hardware carries no generation
tag: do not claim perfect source attribution, and preserve other NMI fault handling.
An AP trap saves its context and
requests BSP service; NMI IPIs capture the other online CPUs, including IF-clear
peers. CPUs wait using dedicated atomics only, never scheduler/log/heap/VM locks.

One stop generation captures each CPU's registers and actual CR3. Advertise a
complete stop only after every online CPU acknowledges; missing acknowledgements
mean incomplete terminal state, not stable shared memory. The BSP's private
polling service is the sole execution exception. GDB thread IDs are logical
CPU index + 1, stable for this boot, labelled with APIC ID and stop reason;
these are CPU contexts, not scheduled tasks/processes. Release generations and
restore original execution state on continue. No non-stop debugging initially.

A stopped CPU may hold any kernel lock. Do not call ordinary services, publish
VM changes, inject kernel function calls, or enter normal panic logging for a
recoverable stop. Before enabling arbitrary traps, each NIC must either supply
an interruption-safe reversible handoff or declare that context unserviceable;
exclude debug/transport mutation code and unresolved borrowed-RX/publication
windows from breakpoint targets. Unknown ownership is not repaired by guessing.
Retain uncertain DMA storage and refuse network service/resume until proven safe;
an unavailable transport may require the owner's physical reset.

Running break-in: intercept debugger packets before normal protocol delivery;
add a bounded, non-consuming debug-frame peek on the **BSP timer path**
when enabled (currently 120 Hz preemption, also earlier deadline interrupts), so a starved network worker or masked NIC interrupt does not make
Ctrl+C depend entirely on scheduling. NIC IRQs still acknowledge/record activity;
they do not run the stub or lwIP. Scan a bounded set of DMA-returned, unborrowed completions, including behind
ordinary queued traffic, rather than trusting software counters alone. Unsafe
mutation defers entry to a coherent checkpoint; saturated RX can still hide
break-in. This is
new driver support, not a call to ordinary RX. Neither timer nor NIC IRQ can
break an IF-clear BSP hang; a dead NIC cannot carry break-in. Document this limit
rather than promise network-delivered NMI to a wedged service CPU.

Recognized #BP/#DB and the readiness checkpoint enter the recoverable path.
Panic/kernel-fault entry must be intercepted before irreversible log/NIC panic
handoff, capture what is safe and serve **read-only terminal inspection**.
Fault/panic continue, step and mutation are rejected; detach leaves it stopped.
Fatal recursion or unsafe transport retains uncertain storage and falls back
to best-effort reporting. Once debug mode owns the NIC, fatal text must use that
owner or serial only; never invoke the old fatal TX takeover against stale
ordinary queue state. There is one transport owner, not concurrent ring users.

## First usable debugger features

Implement the documented [GDB remote protocol](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Overview.html),
not an application launcher or POSIX signal interface. Negotiate only implemented
features using [qSupported and target XML](https://sourceware.org/gdb/current/onlinedocs/gdb.html/General-Query-Packets.html).

| Feature | Bounded contract |
| --- | --- |
| Registers/threads | `?`, stop replies, thread selection/enumeration, `g/p`, controlled `G/P`; publish an AMD64 description and captured integer/segment registers. Uncaptured FP/SIMD registers are unavailable, never invented. CR3/GS, privilege selectors and protected return state are inspection-only; reject unsafe edits. |
| Virtual RAM | `m/M` and optional `X`, through the selected CPU's captured CR3. Debug-only page-table/data windows, checked canonicality, overflow, reserved bits, every level/page and known RAM/cache class; no ordinary scratch allocator. Guarded-copy #PF/#GP fixups return errors rather than panic. Reject holes/MMIO and protected stub/DMA/page-table storage. Preflight whole writes; an unexpected partial write returns error, with no transactional claim. No guarantee against machine check/poisoned RAM. |
| Software breakpoints | bounded owned `Z0/z0` one-byte int3 table, original byte plus physical identity. Kernel text is read-only: use a dedicated RAM alias, never disable global write protection. Correct owned-trap RIP, restore/step/reinstall while peers remain parked, and serialize instruction visibility before release. Protect stub/clock/NIC entry code. |
| Step/continue | `vCont` and compatible `s/c`: TF/#DB ownership, DR6 interpretation and original flag restoration; suppress ordinary IRQ/scheduler entry while stepping, then restore original IF. Step one CPU with peers parked, continue all. Initial step scope is ordinary kernel code; syscall/iret/NMI/HLT boundaries and lock-dependent progress require explicit refusal or later qualification. The debugger's own NMI return must still release a selected CPU into an ordinary-code step. |
| Detach/kill | normal detach restores all owned breakpoint bytes and tracing state before acknowledged resume; restoration failure stays stopped. Transport loss does not detach or auto-resume. Bare-metal `k` resets into the boot/PXE loop through a stopped-safe architecture path, not lock-taking power services. No extended-remote inferior creation, register coercion or fake success. |

Packet specifics and memory/MMIO distinctions follow
[GDB's packet reference](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Packets.html).
DR0–DR3 hardware breakpoints/watchpoints, per-CPU DR7 ownership, broader stepping,
FP/SIMD edits and function injection remain later features. Unsupported packets
use the protocol's unsupported/error reply, never advertise watchpoint support.

## Driver helpers, symbols and paused devices

Proposed `monitor` vocabulary: `phys read/write ADDR WIDTH [COUNT|VALUE]`,
`mmio read/write ADDR WIDTH [COUNT|VALUE]`, and
`pci read/write SEG:BDF OFFSET WIDTH [VALUE]`; widths are explicit 8/16/32/64-bit
where that bus supports them. Physical addresses are typed physical values,
translated through debugger-only mappings, **never cast to C pointers**.
Validate alignment, overflow and pre-discovered RAM/BAR/ECAM bounds; unknown
ranges fail. PCI config stays within one function's configuration extent.
MMIO has read-to-clear/write-trigger side effects even while CPUs stop; use the
requested exact access width and ordering, no automatic write retries/rollback.
Phys addresses mean known RAM, mmio means validated device ranges; physical RAM
writes inherit all virtual-write exclusions, including page-table/live DMA frames.
Debugger mappings preserve compatible cache types: no WB device alias or UC
alias of a permanent WC framebuffer aperture. Unknown classification fails.
Exclude transport hardware/storage and stub infrastructure. These commands
bypass all userspace capabilities and give privileged machine control, not a
new app grant. Hardware absence, bus hangs or machine checks cannot be made safe
merely by a page walk. Mutation is only for a complete ordinary stop.

Use **the exact staged `caelum.elf`**, not a later build at the same path.
Kernel link base is fixed `0xffffffff80000000`; KASLR is not enabled. Build ID is
currently disabled, so do not claim an ELF build-ID handshake. Stage ELF and PXE
configuration together; a manifest records SHA-256/revision/config, and proposed
`debug.image=SHA256` in that boot entry supplies the image token to HELLO. The
bridge verifies its `--kernel` file against the token and refuses mismatch.
The digest is supplied outside ELF (no self-referential embedded full-file hash),
and is an operator staging claim, not a runtime measurement of loaded bytes or
authentication. The operator must publish the matching ELF/config pair. Proposed usage:

```sh
pyxis-gdb-bridge --target t14 --kernel /exact/staged/caelum.elf
# prints verified ELF path, MAC, boot/session identity and local TCP endpoint

gdb -q /exact/staged/caelum.elf
```
```gdb
set may-call-functions off
target remote 127.0.0.1:1235
info threads
```

All-stop stops CPUs, **not DMA or device clocks**. Timers/sleeps/deadlines age;
keep monotonic elapsed time, do not silently rebase deadlines. The polling loop
must maintain the software-extended 32-bit HPET below its wrap interval even with
no packets; losing wraps requires reboot. Audio's 5 ms watchdog/20 ms observation
horizon cannot survive a normal debugger pause: use an inactive-audio bring-up
profile, not a promise to pause playback. Long stops can expire outstanding
five-second network/block deadlines; drain confirmed completions on return,
then honor existing failure/retention rules for still-owned requests. Stopping
inside an interrupted mutation is not a coherent snapshot of device state.

## Proposed task split and qualification

1. **Small first task: resumable stop foundation.** Opt-in known BSP checkpoint,
   dedicated NMI/GS-safe stacks, per-CPU snapshot/generation/ack/release, bounded
   incomplete-stop handling, idle clock maintenance and unmodified continue.
   QEMU's existing debugger
   inspects the saved frames. No NIC/RSP, memory mutation or breakpoints yet.
2. **Guarded inspection.** Dedicated translation/copy windows and fault fixup;
   RAM classification, integer register export, invalid-address refusal.
3. **Polled VirtIO transport and host bridge.** Known coherent checkpoints,
   reversible RX/TX handoff, active net0/static IPv4, HELLO/image binding,
   retransmission/deduplication; read-only GDB attach/continue and reconnect.
4. **First usable driver milestone.** Full entry/break-in support in declared
   safe contexts; controlled registers/RAM, owned software breakpoints, step,
   detach/kill and typed physical/MMIO/PCI monitors. Qualify the RTL8111 adapter
   and PXE workflow. Hardware watchpoints remain a separate assignment.

Use ordinary builds and manual QEMU/GDB checks, first single-CPU then four-CPU
Q35/virtio-net on a TAP/bridge LAN; keep QEMU's existing GDB channel as independent
rescue. Inspect snapshots/resume, GS windows, borrowed RX and partial publication,
peer acknowledgement, guarded missing/cross-page memory, read-only text patches,
step-over, detach restoration, terminal panic, duplicate mutations and disconnect.
Manually drop/reorder traffic or interrupt the bridge; add no test framework.
Check disabled behavior and traffic/clock overhead with matched existing tools.

**Owner's native batch:** stage the exact manifest/ELF/PXE entry; start log capture
and bridge on the chosen LAN, boot `debug.net=t14 debug.wait=1 debug.image=…`
with normal static net0 and inactive audio. Confirm image identity and CPU threads,
read known RAM and safe documented Renoir registers, patch/step ordinary driver
code, continue/detach and confirm normal network/log operation. Test a deliberate
ordinary breakpoint before any GPU register write; then a planned terminal panic
and repeated PXE boots, stale-session refusal and cable/bridge loss. No native
result is claimed here. Early boot/IF-clear hangs and a failed NIC remain outside
this initial debugging coverage; keep physical reset available.

## Decisions for the owner

1. **Transport/availability:** default polled UDP plus localhost TCP bridge,
   one opt-in named/MAC-selected same-LAN peer after active net0/IPv4. Defer
   independent early NIC startup and in-kernel TCP.
2. **Stop policy:** default BSP-serviced all-stop with NMI peers, timer-assisted
   break-in only in qualified contexts; panic/fault is terminal read-only.
   Start with the small checkpoint foundation, not arbitrary-interruption claims.
3. **Control/exposure:** default no auto-resume on loss; explicit safe detach
   resumes, kill resets. Complete ordinary stops permit controlled RAM/register
   and typed device writes; no authentication/encryption on this opt-in home LAN.
   Any LAN peer that can reach the enabled endpoint can stop or take control of
   the kernel and devices, expose all memory and reset the laptop. Name/MAC/session
   filters are not security. Keep default off and host TCP bound to localhost.

## Licence and delivery

Prefer an original bounded AMD64 implementation against the protocol under
Pyxis's MPL-2.0, reusing native code only. The checked
[GDB i386 example at binutils-2_44](https://gnu.googlesource.com/binutils-gdb/+/refs/tags/binutils-2_44/gdb/stubs/i386-stub.c)
has HP's explicit public-domain offer/disclaimer, not a blanket GPL assumption;
it is 32-bit and does not provide these ownership/stop contracts. No example
code is copied here; protocol implementation does not import GDB itself. Any later import needs a pinned owner mirror/cache source,
per-file licence/provenance and preserved notices; audit other GDB files separately.

Only docs/wip changes in this PR. No stub, bridge, boot option, hardware access,
probe branch or qualification infrastructure added. Stop for owner review.
