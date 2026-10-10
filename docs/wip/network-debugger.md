# Network kernel debugger

Owner-accepted plan, 2026-10-09; code inspected at `b0a050b7`.
Tasks 1–2 are implemented. Task 3 code is present; qualification is in progress
(2026-10-10), including owner-run RTL8111/PXE checks.
Task 4 remains unassigned. Goal: GDB on the
owner's host inspecting Caelum on the ThinkPad during a PXE driver bring-up loop,
including Renoir display work.

**Accepted route:** a kernel all-stop debugger with preallocated, polled NIC
transport, small reliable UDP framing, and a host TCP bridge for ordinary GDB
`target remote`. Default off. The first task builds resumable stop state, not an
entire stub. Task 3 provides native read-only inspection; task 4 adds modification
and stepping.

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
- Default-off NMI retains fatal exception handling and only #DF uses IST.
  Task 1 adds the opt-in checkpoint NMI/IST path below; #BP/#DB interception
  and debug-register ownership remain absent.
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
existing log/serial behavior. Independent early NIC bring-up and in-kernel TCP
are outside the accepted route.

Task 3 options: `debug.net=NAME` enables the service, `debug.wait=1` waits once
at its readiness checkpoint; the latter without the former is invalid. Same
case-sensitive printable-name limits as `remote.beacon`, but a new debugger tag
and UDP port **2326**, distinct from remote discovery 2324 and logs 2325.
`debug.image=SHA256` is mandatory with `debug.net`; generated images derive it
from the completed ELF. `remote.beacon=t14` and `log.udp=1` remain independent.

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
session on uncertain command completion. Rate/retry budgets belong to the
implementation; the stopped idle budget follows the accepted loss policy below.
Maintain the clock even when disconnected.

Normal TCP needs lwIP state, allocation, timers and worker execution that all-stop
removes. The accepted bridge keeps TCP on the host; the kernel provides only
bounded LAN transport. Fatal logging keeps its independent one-way fallback.

**Accepted transport-loss policy:** a complete recoverable stop automatically
resumes after a finite idle budget with no valid traffic from its pinned peer.
Proposed initial tuning default: **30 seconds**; the finite idle policy is
accepted, while the duration is an implementation value, not a protocol invariant. Start the budget on entry, including an unpaired
`debug.wait` checkpoint; valid peer/session commands, acknowledgements or explicit
heartbeats refresh it. Malformed packets, stale sessions, discovery broadcasts
and unrelated traffic do not. The bridge sends heartbeats comfortably within the
budget while its GDB client is attached, even when the user issues no commands;
without that client it stops heartbeats. Thus quiet inspection can remain stopped,
but bridge/client exit, cable loss or stalled delivery eventually releases it.

Expiry uses the same safe restoration/release path as ordinary continue, closes
the session and rejects its delayed traffic; it never replays uncertain writes.
Task 4 must restore owned breakpoint bytes and tracing state before release,
without undoing completed RAM/device writes. Restoration failure, incomplete CPU
stop or uncertain NIC ownership cannot safely resume: retain terminal state and
require physical reset. **Panic/fault stops never auto-resume**, paired or unpaired.
The bridge reports loss and possible timeout resumption, then closes its GDB
connection rather than presenting stale stopped state; it cannot confirm release
across a lost link. Explicit detach and kill remain separate
controls with their contracts below.

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
| Virtual RAM | `m/M` and optional `X`, through the selected CPU's captured CR3. Debug-only page-table/data windows, checked canonicality, overflow, reserved bits, every level/page and known RAM/cache class; no ordinary scratch allocator. Guarded-copy #PF/#GP fixups return errors rather than panic. Reject holes/MMIO and unknown/incompatible cache classes. Read-only known WB RAM may include debugger, DMA and page-table storage; mutation exclusions apply to writes. Preflight whole writes; an unexpected partial write returns error, with no transactional claim. No guarantee against machine check/poisoned RAM. |
| Software breakpoints | bounded owned `Z0/z0` one-byte int3 table, original byte plus physical identity. Kernel text is read-only: use a dedicated RAM alias, never disable global write protection. Correct owned-trap RIP, restore/step/reinstall while peers remain parked, and serialize instruction visibility before release. Protect stub/clock/NIC entry code. |
| Step/continue | `vCont` and compatible `s/c`: TF/#DB ownership, DR6 interpretation and original flag restoration; suppress ordinary IRQ/scheduler entry while stepping, then restore original IF. Step one CPU with peers parked, continue all. Initial step scope is ordinary kernel code; syscall/iret/NMI/HLT boundaries and lock-dependent progress require explicit refusal or later qualification. The debugger's own NMI return must still release a selected CPU into an ordinary-code step. |
| Detach/kill | normal detach restores all owned breakpoint bytes and tracing state before acknowledged resume; restoration failure stays stopped. Transport loss uses the accepted idle-timeout restoration/release path; panic/fault stops never resume. Bare-metal `k` resets into the boot/PXE loop through a stopped-safe architecture path, not lock-taking power services. No extended-remote inferior creation, register coercion or fake success. |

Packet specifics and memory/MMIO distinctions follow
[GDB's packet reference](https://sourceware.org/gdb/current/onlinedocs/gdb.html/Packets.html).
DR0–DR3 hardware breakpoints/watchpoints, per-CPU DR7 ownership, broader stepping,
FP/SIMD edits and function injection remain later features. Unsupported packets
use the protocol's unsupported/error reply, never advertise watchpoint support.

## Driver helpers, symbols and paused devices

Accepted `monitor` scope: task 3 supplies reads only; task 4 adds writes. Vocabulary:
`phys read/write ADDR WIDTH [COUNT|VALUE]`,
`mmio read/write ADDR WIDTH [COUNT|VALUE]`, and
`pci read/write SEG:BDF OFFSET WIDTH [VALUE]`; widths are explicit 8/16/32/64-bit
where that bus supports them. Physical addresses are typed physical values,
translated through debugger-only mappings, **never cast to C pointers**.
Validate alignment, overflow and pre-discovered RAM/BAR/ECAM bounds; unknown
ranges fail. PCI config stays within one function's configuration extent.
MMIO and PCI configuration reads can have device side effects, including
read-to-clear MMIO registers; read-only attach is not a promise of passive hardware
observation. Select documented safe registers. Writes can trigger devices; use the
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
configuration together; a manifest records SHA-256/revision/config, and `debug.image=SHA256` in that boot entry supplies the image token to HELLO. The
bridge verifies its `--kernel` file against the token and refuses mismatch.
The digest is supplied outside ELF (no self-referential embedded full-file hash),
and is an operator staging claim, not a runtime measurement of loaded bytes or
authentication. The operator must publish the matching ELF/config pair. Task 3 usage:

```sh
pyxis-gdb-bridge --target t14 --kernel /exact/staged/caelum.elf
# verifies the supplied ELF digest before accepting commands

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
no packets; losing wraps requires reboot. Before tasks 3/4 enable arbitrary entry,
qualify clock-read/maintenance reentry or defer past an unsafe update; the task 1
known checkpoint does not qualify arbitrary interrupted clock contexts. Audio's 5 ms watchdog/20 ms observation
horizon cannot survive a normal debugger pause: use an inactive-audio bring-up
profile, not a promise to pause playback. Long stops can expire outstanding
five-second network/block deadlines; drain confirmed completions on return,
then honor existing failure/retention rules for still-owned requests. Stopping
inside an interrupted mutation is not a coherent snapshot of device state.

## Implemented stop foundation

`debug.net` prepares permanent 16 KiB RW/NX per-CPU IST stacks with unmapped
adjacent guards and opt-in NMI/#PF/#GP gates before AP startup. Default-off boots
allocate no debugger stacks, snapshots, windows or identity task; the ordinary
worker has no debugger iteration polling. New normal log lines are absent.
Unexpected NMIs retain fatal reporting.

A single atomic PREPARED-to-PENDING transition claims entry before publishing
its generation. Secondary terminal origins join the active stop; after a
recoverable release they retry their preserved fault instead of taking the
legacy NIC path. A GS-independent self-NMI captures the BSP return frame and moves service onto
its IST; no GS-base rewrite occurs. A terminal origin may forward from an AP,
retaining its actual fault frame, root, GS bases and CR2 separately from the NMI
return frame. The BSP sends NMIs to peers. COMPLETE requires every CPU's matching
snapshot/acknowledgement within the one-second acquisition budget. INCOMPLETE
freezes missing-CPU flags and stays terminal, including after late ACKs. The
parked path keeps NMI blocking until its eventual IRET
([AMD64 Volume 2, §8.1.4](https://docs.amd.com/v/u/en-US/24593_3.44_APM_Vol2)).

Only confirmed NIC TX/mask restoration permits ordinary resume. Per-CPU exit
ACKs precede register restoration/IRET; they are not proof IRET executed. The
origin waits for all exit ACKs before rearming a new generation. Terminal stops
never return. Before transport ownership, unsafe/unavailable entry falls back
to existing fatal reporting; retained uncertainty stays quiet and terminal.
`debug.checkpoint`, `release_generation` and the inspection mailbox were task 1–2
scaffolding and are removed, with no compatibility option. Their historical
qualification remains in [checkpoint](../development/experiments/debug-checkpoint/README.md)
and [guarded inspection](../development/experiments/debug-inspection/README.md).

## Implemented guarded inspection

While COMPLETE, the parked BSP exports captured integer registers/return frame,
CR3, FS and both GS bases, or bounded virtual RAM through the selected CPU's root. All other CPUs must have acknowledged
the generation. No function injection, target writes, allocation, ordinary VM
scratch, locks or logging. Valid pinned-peer traffic refreshes the recoverable stopped-idle budget.

Two permanent reserved kernel pages provide dedicated translation/data windows;
boot establishes their ancestors and retains their shared leaf pointers. Only
the parked BSP remaps these read-only/NX aliases, invalidating its local TLB and
clearing both leaves before returning. Every page-table level and destination
frame must be wholly covered by known boot RAM (usable, loader or kernel), with
no excluded overlap. Canonicality, overflow, presence, physical width, reserved
bits and cache encodings are checked. Native 4 KiB leaves and PAT index zero
only; large pages and other cache encodings are refused. A boot-time BSP
PAT/CR0/MTRR snapshot requires effective WB, including fixed and variable ranges;
unsupported or mixed cache classes are refused. Current code does not change
that cache setup. Page tables, DMA and debugger storage may be read; DMA and the
debugger's own working storage are not frozen snapshots. Reads return no
successful byte count on failure and clear partial output.

Only the opt-in IDT intercepts #PF/#GP. The GS-independent fixup recognizes the
exact guarded load, owned active probe, BSP/generation, source and saved call
stack. It preserves selectors/GPRs and returns through that call stack without
IRET, retaining outer-NMI blocking until checkpoint release. POPFQ clears RF;
the failed load is not retried. Other faults enter the original ISR with their
original hardware frame. This does not protect against machine checks/poisoned
RAM or qualify arbitrary interrupted contexts.

Task 3 uses GDB RSP instead of the temporary mailbox: unavailable FP state is
reported unavailable, and RAM reads return a real error on refusal. Monitor
accesses preflight the complete range and use exact aligned widths. Physical
RAM/device monitors exclude debugger infrastructure and the selected transport's
BAR/DMA ranges; virtual RAM inspection retains the accepted broader read policy.
The only unsized Renoir admission is BAR5 offsets `[0x13000,0x15000)`; no BAR
sizing, power change or device write occurs. Device reads can have side effects.

## Task 3 operation and qualification

```sh
make -C tools gdb-bridge
make image DEBUG_NET=t14 DEBUG_WAIT=1
build/tools/pyxis-gdb-bridge --target t14 --kernel /exact/staged/caelum.elf
```

The bridge runs on horse for native PXE, with UDP 2326 reachable on the ThinkPad
LAN. `--bind IPv4`, `--beacon-address IPv4` and optional `--source MAC` select that
LAN/target. TCP is fixed to localhost port 1235. A real entropy read supplies the
boot nonce; failure leaves attach unavailable. Name/nonce/digest/MAC are routing
and stale-session checks, not authentication: any peer on the LAN can inspect
kernel RAM/registers and privileged device state while this opt-in service runs.

GDB monitor grammar is deliberately small (addresses/BDF/offset hexadecimal,
width and count decimal; widths are bits; at most eight values):

```gdb
monitor phys read 0x1000 32 1
monitor mmio read PHYSICAL_REGISTER_ADDRESS 32 1
monitor pci read 0:00:00.0 0 32
```

Unsupported widths, addresses, writes and control requests return errors.
Normal `continue` has no address/signal argument; GDB waits for the next terminal
stop in the same boot/image. Running Ctrl+C is unavailable in task 3. GDB/bridge
exit stops heartbeats, allowing ordinary idle release; detach/kill remain refused.
A bound terminal stop retains its session until reboot: losing the bridge/client
requires a fresh PXE boot for another attachment, and never resumes the kernel.
Retries use 250 ms stop-and-wait transport and a separate command identity/cache,
so retransmitted device reads are not reissued. RELEASED gets a bounded 750 ms
notice opportunity; unreachable peers cannot prevent expiry. Hardware restoration
has its own bounded wait; uncertainty keeps ownership and CPUs parked.

[Task 3 qualification](../development/experiments/network-debugger/README.md)
records baseline, manual QEMU checks, option-off interleaving and native results.
Timer-assisted entry remains task 4. Current clock code reloads the HPET sample
after each failed extension CAS and holds no interrupted-reader lock; this is
source evidence, not native 32-bit HPET/NMI reentry qualification.

## Accepted task split and qualification

Tasks 1–2 are complete; task 3 qualification is pending, task 4 awaits assignment.
Historical accepted task 1 control:
`debug.checkpoint=1`, absent/default off, stops once after CPU/task initialization
and before BSP scheduling. A complete stop resumes on whichever comes first:
QEMU's GDB setting the matching `release_generation`, or a fixed 30-second
expiry. This is checkpoint expiry, not transport-loss detection. Incomplete stops
remain terminal. `debug.checkpoint` is task 1–2 scaffolding, replaced by
`debug.wait` when task 3 provides transport; keep no compatibility option.

1. [x] **Small first task: resumable stop foundation.**

   Owner can then inspect captured CPU frames and resume a known checkpoint using
   QEMU's existing debugger; native network GDB is not available yet.

   Opt-in known BSP checkpoint, dedicated NMI/GS-safe stacks, per-CPU
   snapshot/generation/ack/release, bounded incomplete-stop handling, idle clock
   maintenance and unmodified continue. No NIC/RSP, memory mutation or breakpoints.

2. [x] **Guarded inspection.**

   Accepted task 2 contracts (2026-10-09): opt-in #PF/#GP fixups recognize only
   the guarded probe and recover without IRET; other faults retain existing
   handling. A bounded owned request/result mailbox is serviced only while
   COMPLETE, through QEMU's GDB without injected calls. Keep fixed 30-second
   expiry. Read-only known WB RAM includes page tables, DMA and debugger storage;
   reject unknown/reserved/firmware/ACPI/framebuffer/MMIO/cache classes. DMA may
   change during reads; target-memory mutation exclusions remain for task 4.

   Owner can then inspect the guarded RAM/register machinery through QEMU's
   existing debugger, including refusal of invalid addresses.

   Dedicated translation/copy windows and fault fixup, RAM classification and
   integer register export; no network attach yet.

3. [ ] **VirtIO and RTL8111 transport, bridge and native read-only attach.**

   Owner can then use GDB on the ThinkPad at `debug.wait` or a terminal panic,
   inspecting CPU threads/registers, guarded RAM and documented safe Renoir registers.

   Both adapters provide reversible RX/TX handoff at coherent checkpoints and
   terminal panic entry; active net0/IPv4, HELLO/image binding,
   retransmission/deduplication, continue from ordinary checkpoints and the accepted
   idle-loss release policy. Read-only typed `monitor phys read`, `mmio read` and
   `pci read` include the read-side-effect caveat above. Qualify native RTL8111/PXE
   here. QEMU discovery requires **TAP/bridge LAN networking**, not user-mode NAT.
   Writes, software breakpoints, step and detach/kill are not advertised yet.

   Accepted task 3 bounds (2026-10-10): enter at a coherent BSP network-worker
   checkpoint after active net0/IPv4, or a prepared late terminal panic/fault.
   Timer-assisted running break-in stays in task 4; retain clock-reentry
   qualification. While stopped, consume/repost ordinary RX frames and discard
   their protocol delivery, serving only debugger UDP and bounded ARP. Pauses
   can lose ordinary packets; there is no replay queue or deadline rebasing.
   MMIO reads admit only already sized compatible BAR extents plus the audited
   Renoir register windows; refuse unknown ranges and the unsized remainder of
   BAR5. Device reads may have side effects and duplicate commands must not
   repeat them. QEMU uses a task-owned isolated TAP/bridge (host
   `192.168.77.1/24`, guest `.2`), removed after qualification. Native bridge
   runs on horse, on the ThinkPad LAN; Luna staging is coordinated by the owner.

4. **Mutation and execution control.**

   Owner can then patch and step ordinary driver code, write explicitly typed
   device registers, detach cleanly or reset into the PXE loop.

   Entry/timer-assisted break-in in declared safe contexts; controlled register,
   RAM and typed physical/MMIO/PCI writes, owned software breakpoints, step and
   detach/kill restoration. Timeout release must share safe restoration with
   continue/detach. Qualify both backends. Hardware watchpoints remain a separate
   assignment.

Use ordinary builds and manual QEMU/GDB checks, first single-CPU then four-CPU
Q35/virtio-net on a TAP/bridge LAN; keep QEMU's existing GDB channel as independent
rescue. Inspect snapshots/resume, GS windows, borrowed RX and partial publication,
peer acknowledgement, guarded missing/cross-page memory, read-only text patches,
step-over, detach restoration, terminal panic and duplicate mutations. Check idle
heartbeats preserve a quiet stop, bridge/cable loss releases ordinary stops after
the budget with restored state, stale packets cannot re-enter the expired session,
and terminal panic/fault stops remain stopped past the same budget.
Manually drop/reorder traffic or interrupt the bridge; add no test framework.
Check disabled behavior and traffic/clock overhead with matched existing tools.

**Owner's native batch:** stage the exact manifest/ELF/PXE entry; start log capture
and bridge on the chosen LAN, boot `debug.net=t14 debug.wait=1 debug.image=…`
with normal static net0 and inactive audio. In task 3, confirm image identity and
CPU threads, read known RAM and safe documented Renoir registers, continue and
confirm normal network/log operation; inspect a planned terminal panic. Check
quiet attached inspection, idle-loss release from ordinary stops and no release
from panic. Repeat PXE boots, stale-session refusal and cable/bridge loss. Task 4
adds patch/step, typed writes and detach/kill restoration; qualify a deliberate
ordinary breakpoint before any GPU register write. No native result is claimed
here. Early boot/IF-clear hangs and a failed NIC remain outside this initial
coverage; keep physical reset available.

## Accepted owner decisions (2026-10-09; task 4 not implemented)

1. **Transport/availability:** polled UDP plus localhost TCP bridge,
   one opt-in named/MAC-selected same-LAN peer after active net0/IPv4. Defer
   independent early NIC startup and in-kernel TCP.
2. **Stop policy:** BSP-serviced all-stop with NMI peers, timer-assisted
   break-in only in qualified contexts; panic/fault is terminal read-only.
   Start with the small checkpoint foundation, not arbitrary-interruption claims.
3. **Control/exposure:** recoverable stops auto-resume on the defined idle loss;
   terminal panic/fault stops never resume. Explicit safe detach resumes ordinary
   stops, kill resets. Complete ordinary stops permit controlled RAM/register
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

Task 3 adds original MPL-2.0 transport/RSP/bridge code. Host SHA-256 reuses the
existing attributed public-domain source in `tools/remote/vendor`; no GDB stub
example is imported. Task 4 remains a separate owner assignment. No test or
qualification framework is added.
