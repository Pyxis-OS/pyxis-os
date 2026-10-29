# Network kernel debugger

An opt-in, default-off, read-only all-stop kernel debugger. Ordinary GDB on the
host inspects Caelum on the ThinkPad during a PXE bring-up loop: CPU contexts,
registers, guarded RAM and documented device registers, at a boot checkpoint or
after a panic. The route is a kernel stub with preallocated, polled NIC transport
and small reliable UDP framing, plus a host TCP bridge for `target remote`.
Writes, breakpoints, stepping, running Ctrl+C and detach/kill are not
implemented ([task 4](#decisions-and-unimplemented-task-4)). Qualification is in the
[task 3 record](../development/experiments/network-debugger/README.md),
with the earlier [checkpoint](../development/experiments/debug-checkpoint/README.md)
and [guarded inspection](../development/experiments/debug-inspection/README.md)
records. The related [remote debugging](../development/remote-debugging.md)
guide covers logs and the reverse terminal.

## Use

```sh
make -C tools gdb-bridge
make image DEBUG_NET=t14 DEBUG_WAIT=1
build/tools/pyxis-gdb-bridge --target t14 --kernel /exact/staged/caelum.elf
gdb -q /exact/staged/caelum.elf
```
```gdb
set may-call-functions off
target remote 127.0.0.1:1235
info threads
```

Boot options: `debug.net=NAME` enables the service (same case-sensitive printable
name limits as `remote.beacon`); `debug.wait=1` waits once at the readiness
checkpoint and is invalid without `debug.net`; `debug.image=SHA256` is mandatory
with `debug.net` and generated images derive it from the completed ELF.
Invalid or incomplete final `debug.net`/`debug.image`/`debug.wait` settings warn
and disable the debugger and initial wait together; they never stop normal boot.
Duplicates warn and the last occurrence wins.
`remote.beacon=t14` and `log.udp=1` stay independent. Prefer a static-IP profile on the
`management.debug` interface for repeatable PXE work.

The bridge runs on the host LAN of the target, with UDP 2326 reachable.
`--bind IPv4`, `--beacon-address IPv4` and optional `--source MAC` select the
LAN and target; TCP is fixed to `127.0.0.1:1235`. A real entropy read supplies
the boot nonce; failure leaves attach unavailable. QEMU needs a TAP/bridge LAN,
not user-mode NAT.

Use **the exact staged `caelum.elf`**. The kernel links at the fixed base
`0xffffffff80000000`, KASLR is not enabled and the build ID is disabled, so
there is no ELF build-ID handshake. The digest in `debug.image` comes from outside
the ELF; the bridge verifies its `--kernel` file against it and refuses a
mismatch. That is an operator staging claim, not a measurement of loaded bytes
or authentication. Stage the ELF and PXE entry together.
The digest names the uncompressed ELF: a PXE tree staging `caelum.elf.gz`
([compressed boot media](../development/boot-archive.md#compressed-boot-media))
passes the bridge the matching `build/caelum.elf`.

Monitor commands are read-only. Addresses, BDF and offset are hexadecimal;
width and count are decimal, widths are bits, at most eight values:

```gdb
monitor phys read 0x1000 32 1
monitor mmio read PHYSICAL_REGISTER_ADDRESS 32 1
monitor pci read 0:00:00.0 0 32
```

The AMD64 target XML names the flags register `eflags` (`info registers eflags`).
Unsupported widths, addresses, writes and control requests return errors, never
fake success. Normal `continue` takes no address or signal; GDB then waits for
the next terminal stop in the same boot and image.

**Exposure.** Name, nonce, digest and MAC are routing and stale-session checks,
not authentication. While the service runs, any peer on the LAN that can reach
UDP 2326 can inspect kernel RAM, registers and privileged device state, and can
stop the kernel. There is no encryption. This is accepted for the owner's home
LAN; keep the option off elsewhere and the host TCP bound to localhost.

## Transport and discovery

The service starts only after the `management.debug` interface's NIC is active with an IPv4
address and its debug resources are prepared; earlier boot faults keep existing
log/serial behavior. `debug.wait` cannot stop before NIC availability.
Independent early NIC bring-up and in-kernel TCP are not part of the route.

The bridge discovers one named, optionally MAC-filtered target with
host-advertised HELLO datagrams, then pins one same-LAN peer (MAC, IP, port) and
the kernel freezes its NIC and address through the stop. HELLO carries target
MAC, fresh boot and session identities and the image token; duplicate names need
explicit `--source` selection. Session identities reject stale boots and
reconnections, not malicious peers. Existing log boot stamps are not replay
protection.

Stopped traffic uses prebuilt Ethernet/IPv4/UDP frames with checked lengths and
checksums, no fragments or routing, and answers bounded ARP for the frozen
address. There is no DHCP, DNS, lwIP, allocator, driver lock or sleeping
service in the debug loop. The bridge forwards RSP packets, ack bytes and Ctrl+C
through a versioned envelope with session, direction, sequence/ack, kind and
payload (1024 bytes, `DEBUG_PAYLOAD_BYTES` in `include/debug/protocol.h`).
Delivery is stop-and-wait per direction with 250 ms retransmission, a separate
command identity and bounded reply cache, so retransmitted device reads are not
reissued. The stopped loop keeps the clock maintained even without packets.

**Transport-loss policy.** A complete recoverable stop resumes automatically
after 30 s without valid traffic from its pinned peer (`DEBUG_IDLE_MS`; a tuning
value, not a protocol invariant). The budget starts on entry, including an
unpaired `debug.wait`. Valid peer commands, acknowledgements and the bridge's
heartbeat (every 2 s while a GDB client is attached) refresh it; malformed or
stale packets, discovery broadcasts and unrelated traffic do not. Expiry uses
the ordinary continue path, closes the session and rejects delayed traffic. If
hardware restoration is uncertain, ownership is kept and CPUs stay parked until
physical reset. RELEASED gets a bounded 750 ms notice opportunity. **Panic and
fault stops never auto-resume.** A terminal stop retains its session until
reboot: losing the bridge or client requires a fresh PXE boot for another
attachment. The bridge reports loss and closes its GDB connection rather than
show stale state.

## Entry, all-stop and resume

`debug.net` prepares permanent 16 KiB RW/NX per-CPU IST stacks with unmapped
guards and opt-in NMI/#PF/#GP gates before AP startup. Default-off boots allocate
no debugger stacks, snapshots, windows or identity task, and the ordinary worker
does not poll the debugger. Unexpected NMIs keep fatal reporting.

The BSP is the service CPU. Entry is claimed by one atomic PREPARED-to-PENDING
transition before the generation is published. A GS-independent self-NMI captures
the BSP return frame and moves service onto its IST; the BSP sends NMIs to the
other online CPUs, including IF-clear peers, and a terminal origin on an AP keeps
its actual fault frame, root, GS bases and CR2 separately. Parked CPUs wait on
dedicated atomics only. COMPLETE requires every CPU's matching snapshot within a
one-second acquisition budget; otherwise the stop is INCOMPLETE and terminal,
including after late acknowledgements. Parked CPUs keep NMI blocking until IRET
([AMD64 Volume 2, section 8.1.4](https://docs.amd.com/v/u/en-US/24593_3.44_APM_Vol2)).
NMI hardware carries no generation tag, so source attribution is not perfect.

Only confirmed NIC TX and mask restoration permits ordinary resume. Per-CPU exit
acknowledgements precede register restoration and IRET and do not prove IRET
ran; the origin waits for all of them before rearming. Terminal stops never
return. Before the transport owns the NIC, unsafe or unavailable entry falls back
to the existing fatal reporting.

GDB threads are CPU contexts (logical index plus one, labelled with APIC ID and
stop reason), not scheduled tasks. All-stop only; no non-stop mode. Entry points
today are the readiness checkpoint at a coherent BSP network-worker point after
the active `management.debug` interface with IPv4, and panic or kernel fault. Readiness is an opportunity, not a
reservation: an AP can finish panic drawing and find the BSP already in ordinary
network work. That entry is refused before taking the NIC and falls back to the
fatal path, so AP panic attachment is not guaranteed.

A stopped CPU may hold any kernel lock, so the stub calls no ordinary service and
publishes no VM change. While stopped it consumes and reposts ordinary RX frames
without protocol delivery, serving only debugger UDP and ARP; ordinary packets
can be lost, with no replay queue or deadline rebasing. Both adapters (VirtIO,
RTL8111) provide a reversible RX/TX handoff at coherent checkpoints and for
terminal entry; retained uncertain DMA storage refuses network service and
resume rather than being guessed at.

### Panic and fault

With debugging enabled, `panic()` first attempts local-only direct framebuffer
text, without the log or fatal UDP paths; other terminal origins bounded-wait for
that message before stopping its writer. It needs no clock, heap, GS or ordinary
lock. A fault while rendering cancels the message and keeps fault inspection; a
failing display may show partial text, a stale frame or nothing. No panic-time
GPU reset or scanout programming occurs, and a selected VirtIO GPU has no direct
target. The stop is read-only terminal inspection; continue is refused. Once the
debugger owns the NIC, fatal text uses that owner or serial only, never the old
fatal TX takeover. Fatal recursion or unsafe transport retains uncertain storage
and falls back to best-effort reporting.

## Guarded inspection

While COMPLETE, the parked BSP serves captured integer, segment and base
registers, the return frame, CR3 and bounded virtual RAM through the selected
CPU's root. Uncaptured FP/SIMD registers are reported unavailable, never
invented; CR3, GS, privilege selectors and protected return state are
inspection-only. There is no function injection, allocation, ordinary VM scratch,
lock or logging. RAM reads return a real error on refusal and clear partial
output.

Two permanent reserved kernel pages provide dedicated translation and data
windows. Only the parked BSP remaps these read-only/NX aliases, invalidating its
local TLB and clearing both leaves before returning. Every page-table level and
destination frame must lie wholly in known boot RAM (usable, loader or kernel).
Canonicality, overflow, presence, physical width, reserved bits and cache
encodings are checked. Only native 4 KiB leaves and PAT index zero are accepted;
a boot-time PAT/CR0/MTRR snapshot must show effective WB (fixed and variable
ranges), and large pages or mixed cache classes are refused. Page tables, DMA and
debugger storage are readable but not frozen snapshots.

Only the opt-in IDT intercepts #PF/#GP. A GS-independent fixup recognizes the
exact guarded load, owned active probe, BSP, generation and saved call stack and
returns through that stack without IRET, preserving outer-NMI blocking. Other
faults enter the original handler with their original frame. This does not
protect against machine checks or poisoned RAM.

Monitor accesses preflight the whole range and use exact aligned widths. Physical
and device monitors take typed physical values, never C pointers, translated
through debugger-only mappings that preserve compatible cache types. `phys`
means known RAM; `mmio` means validated device ranges: already sized compatible
BAR extents plus audited Renoir register windows. The only unsized admission is
Renoir BAR5 offsets `[0x13000,0x15000)`; no BAR sizing, power change or device
write occurs. Debugger infrastructure and the selected transport's BAR and DMA
ranges are excluded; virtual RAM reads keep the broader policy above. PCI config
stays within one function. MMIO and PCI config reads can have side effects,
including read-to-clear registers, so read-only attach is not passive observation.
Duplicate commands never repeat a device read.

## Time and devices while stopped

All-stop halts CPUs, not DMA or device clocks. Timers, sleeps and deadlines age;
monotonic time is kept and deadlines are not rebased. The polling loop maintains
the software-extended 32-bit HPET below its wrap interval; a lost wrap requires
reboot. The clock code reloads the HPET sample after each failed extension CAS
and holds no interrupted-reader lock. That is source evidence, not native 32-bit
HPET/NMI reentry qualification, and entry at arbitrary interrupted clock contexts
is not enabled. Audio's 5 ms watchdog and 20 ms horizon cannot survive a pause;
use an inactive-audio profile. Long stops can expire five-second network and
block deadlines; confirmed completions drain on return, and still-owned requests
follow existing failure and retention rules.

## Limits

- Early boot, IF-clear hangs and a failed NIC are outside coverage; neither
  timer nor NIC interrupt can break into them. Keep physical reset available.
- Running Ctrl+C, writes, software breakpoints, stepping and detach/kill are
  refused or unadvertised ([task 4](#decisions-and-unimplemented-task-4)).
- Task 3 entry happens only at checkpoints and panic/fault, not arbitrary
  interrupted contexts.

## Decisions and unimplemented task 4

Accepted by the owner on 2026-10-09: polled UDP plus a localhost TCP bridge to one
opt-in, name- or MAC-selected same-LAN peer after the debug-selected interface is up; BSP-serviced
all-stop with NMI peers, where a panic or fault stop is terminal and read-only;
recoverable stops auto-resume on the defined idle loss, safe detach resumes
ordinary stops and kill resets. There is no authentication or encryption on this
opt-in home LAN: any peer that can reach the enabled endpoint can stop the kernel,
read all memory and reset the laptop, and name, MAC or session filters are not
security. Keep it default off with the host bridge on localhost.

Task 4, mutation and execution control, is unassigned. Its drafted contract
(commit `043f95f7`, `docs/wip/network-debugger.md`) covers running break-in from a
bounded debug-frame peek on the BSP timer path, controlled register, RAM and typed
physical, MMIO and PCI writes, owned software breakpoints through a RAM alias of
read-only text, step and detach/kill with restoration. Arbitrary traps need each
NIC to supply a reversible receive handoff first. Hardware watchpoints are a
separate assignment, and a task 4 import of GDB stub code needs the licence steps
below.

## Licence

The transport, RSP and bridge code is original MPL-2.0. Host SHA-256 reuses the
attributed public-domain source in `tools/remote/vendor`. No GDB stub example is
imported; a later import needs a pinned owner mirror/cache source, per-file
licence and provenance. HP's public-domain offer in the
[GDB i386 stub at binutils-2_44](https://gnu.googlesource.com/binutils-gdb/+/refs/tags/binutils-2_44/gdb/stubs/i386-stub.c)
was checked; that stub is 32-bit and lacks these ownership and stop contracts.
