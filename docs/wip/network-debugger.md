# Network kernel debugger

Owner-accepted plan, 2026-10-09. Tasks 1-3 are implemented and natively
qualified (task 3 merged as #652); see the
[implemented reference](../kernel/network-debugger.md). **Task 4 is unassigned.**
Goal: GDB on the owner's host inspecting and modifying Caelum on the ThinkPad
during a PXE driver bring-up loop, including Renoir display work.

**Accepted route:** a kernel all-stop debugger with preallocated, polled NIC
transport, small reliable UDP framing and a host TCP bridge for ordinary GDB
`target remote`. Default off.

## Accepted owner decisions (2026-10-09)

1. **Transport/availability:** polled UDP plus localhost TCP bridge, one opt-in
   named/MAC-selected same-LAN peer after active net0/IPv4. Independent early
   NIC startup and in-kernel TCP are deferred.
2. **Stop policy:** BSP-serviced all-stop with NMI peers, timer-assisted
   break-in only in qualified contexts; panic/fault is terminal read-only.
3. **Control/exposure:** recoverable stops auto-resume on the defined idle loss;
   terminal panic/fault stops never resume. Explicit safe detach resumes ordinary
   stops, kill resets. Complete ordinary stops permit controlled RAM/register and
   typed device writes; no authentication or encryption on this opt-in home LAN.
   Any LAN peer that can reach the enabled endpoint can stop or take control of
   the kernel and devices, expose all memory and reset the laptop. Name/MAC/
   session filters are not security. Keep default off and host TCP on localhost.

## Tasks

1. [x] **Resumable stop foundation** (checkpoint NMI/IST, per-CPU snapshots and
   generations). Reference: [entry, all-stop and resume](../kernel/network-debugger.md#entry-all-stop-and-resume).
2. [x] **Guarded inspection** (#PF/#GP fixups, translation windows, RAM
   classification). Reference: [guarded inspection](../kernel/network-debugger.md#guarded-inspection).
3. [x] **VirtIO and RTL8111 transport, bridge and native read-only attach**
   (#652). Reference: [use](../kernel/network-debugger.md#use) and
   [transport](../kernel/network-debugger.md#transport-and-discovery);
   [qualification](../development/experiments/network-debugger/README.md).
4. [ ] **Mutation and execution control.** Entry and timer-assisted break-in in
   declared safe contexts; controlled register, RAM and typed physical/MMIO/PCI
   writes; owned software breakpoints; step; detach/kill restoration. Timeout
   release must share safe restoration with continue/detach. Qualify both
   backends. Hardware watchpoints remain a separate assignment.
   **After this task the owner can** patch and step ordinary driver code, write
   explicitly typed device registers, detach cleanly or reset into the PXE loop.

## Task 4 contract

### Running break-in

Intercept debugger packets before normal protocol delivery. Add a bounded,
non-consuming debug-frame peek on the **BSP timer path** when enabled (currently
120 Hz preemption, also earlier deadline interrupts), so a starved network worker
or masked NIC interrupt does not make Ctrl+C depend on scheduling. NIC IRQs
acknowledge and record activity only; they do not run the stub or lwIP. Scan a
bounded set of DMA-returned, unborrowed completions, including behind queued
traffic, rather than trusting software counters. This is new driver support, not
a call to ordinary RX. Unsafe mutation defers entry to a coherent checkpoint;
saturated RX can still hide break-in. Neither timer nor NIC IRQ can break an
IF-clear BSP hang and a dead NIC cannot carry break-in: document this rather
than promise network-delivered NMI to a wedged service CPU.

Raw Ctrl+C has its own control record able to interrupt an outstanding continue;
a transport ACK confirms receipt, not completion of the RSP operation. Duplicate
continue, write, breakpoint, reset or MMIO commands must not execute twice,
including RSP retries above UDP: keep the executed control record across
resume/re-entry. Unsupported or oversized packets return real errors. No
automatic new session on uncertain command completion.

### Arbitrary traps

Before enabling arbitrary #BP/#DB traps, each NIC must supply an
interruption-safe reversible handoff or declare that context unserviceable.
RX completion is not simply available: VirtIO advances completion accounting
before lending buffers to protocols and RTL lends a slot before reposting it, and
an idle hardware-mutation gate does not prove all buffers are free. The handoff
must account for device-owned, completed, borrowed and reposted slots,
interrupted publication, pending TX, queue indices and interrupt masks under one
authority. Exclude debug/transport mutation code and unresolved borrowed-RX and
publication windows from breakpoint targets; unknown ownership is never repaired
by guessing. Before arbitrary entry, qualify clock-read and maintenance reentry
or defer past an unsafe update. The checkpoint does not qualify arbitrary
interrupted clock contexts. Debug-register ownership and #BP/#DB interception do
not exist yet.

### Features

| Feature | Bounded contract |
| --- | --- |
| Register writes | Controlled `G/P`. CR3/GS, privilege selectors and protected return state stay inspection-only; reject unsafe edits. |
| Virtual RAM writes | `M` and optional `X` through the selected CPU's captured CR3, using the existing guarded windows. Preflight whole writes; an unexpected partial write returns an error with no transactional claim. Reject holes, MMIO and unknown cache classes. Writes exclude debugger, transport DMA and page-table storage. |
| Software breakpoints | Bounded owned `Z0/z0` one-byte int3 table with original byte and physical identity. Kernel text is read-only: use a dedicated RAM alias, never disable global write protection. Correct owned-trap RIP, restore/step/reinstall while peers stay parked, serialize instruction visibility before release. Protect stub, clock and NIC entry code. |
| Step/continue | `vCont` and compatible `s/c`: TF/#DB ownership, DR6 interpretation, original flag restoration; suppress ordinary IRQ/scheduler entry while stepping, then restore IF. Step one CPU with peers parked, continue all. Initial scope is ordinary kernel code; syscall/iret/NMI/HLT boundaries and lock-dependent progress are refused or qualified later. The debugger's own NMI return must still release a selected CPU into a step. |
| Detach/kill | Detach restores all owned breakpoint bytes and tracing state before acknowledged resume; restoration failure stays stopped. Bare-metal `k` resets into the boot/PXE loop through a stopped-safe architecture path, not lock-taking power services. No extended-remote inferior creation, register coercion or fake success. |

Idle-timeout release must restore owned breakpoint bytes and tracing state before
release, without undoing completed RAM or device writes. Restoration failure,
incomplete CPU stop or uncertain NIC ownership cannot resume: retain terminal
state and require physical reset. Panic/fault stops still reject continue, step
and mutation, and detach leaves them stopped.

### Monitor writes

Vocabulary extends the read-only commands: `phys write ADDR WIDTH VALUE`,
`mmio write ADDR WIDTH VALUE` and `pci write SEG:BDF OFFSET WIDTH VALUE`, with
explicit 8/16/32/64-bit widths where the bus supports them. Writes can trigger
devices: use the exact access width and ordering, no automatic retries or
rollback. Physical RAM writes inherit all virtual-write exclusions, including
page-table and live DMA frames. Exclude transport hardware and storage and stub
infrastructure. These commands bypass userspace capabilities and give privileged
machine control, not an app grant. Hardware absence, bus hangs or machine checks
cannot be made safe by a page walk. Mutation is only for a complete ordinary stop.

DR0-DR3 hardware breakpoints/watchpoints, per-CPU DR7 ownership, broader
stepping, FP/SIMD edits and function injection remain later features;
unsupported packets keep the protocol's unsupported/error reply and watchpoint
support is never advertised.

### Qualification

Ordinary builds and manual QEMU/GDB checks, first single-CPU then four-CPU
Q35/virtio-net on a TAP/bridge LAN; keep QEMU's GDB channel as independent
rescue. Inspect snapshots/resume, GS windows, borrowed RX and partial
publication, peer acknowledgement, read-only text patches, step-over, detach
restoration, terminal panic and duplicate mutations. Check that heartbeats
preserve a quiet stop, bridge or cable loss releases ordinary stops after the
budget with restored state, stale packets cannot re-enter the expired session,
and terminal stops stay stopped. Manually drop/reorder traffic or interrupt the
bridge; add no test framework. Check disabled behavior and overhead with matched
existing tools.

**Owner's native batch:** stage the exact manifest/ELF/PXE entry, start log
capture and bridge, boot `debug.net=t14 debug.wait=1 debug.image=...` with
static net0 and inactive audio; repeat PXE boots, stale-session refusal and
cable/bridge loss. Task 4 adds patch/step, typed writes and detach/kill
restoration; qualify a deliberate ordinary breakpoint before any GPU register
write. Early boot/IF-clear hangs and a failed NIC stay outside coverage; keep
physical reset available.

Task 4 remains a separate owner assignment and adds no test or qualification
framework; any import of GDB stub code needs the licence steps in the
[reference](../kernel/network-debugger.md#licence).
