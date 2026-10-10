# Network debugger task 3 qualification

QEMU and the final interleaved option-off comparison completed, 2026-10-10.
Native RTL8111 checkpoint, inspection, loss and terminal-panic checks were
completed by the owner. Local panic-display follow-up is being qualified in QEMU.

## Before-code baseline

Main `dc91a4c6`, existing LLVM23.1.3-49e2c1a builder; verified SDK/userland/ports
bundles, userland `8cbd9f87`, ports `19fb10b0`, lwIP `a1aadb91`, fs `b427df29`.
Q35/KVM nested VM, CPU max, four CPUs, 512 MiB, direct TAP LAN .1/.2 on
192.168.77.0/24, no gateway/DNS, standard VGA, inactive audio, VirtIO-net/RNG.
Captured before implementation; no profiling. Another owner's four-CPU VM was
active throughout; no whole-host-idle or native timing claim.

- HPET epoch-to-ready: 238.60033, 234.23313, 238.51072 ms (median 238.51072).
  This excludes firmware/earlier adapter work and uses a GDB hardware breakpoint.
- Plain remote echo: 0.04, 0.04, 0.04 s, host wall time at 0.01 s resolution.
- Fresh-boot Lua100 child launches: 0.54, 0.55, 0.56, 0.51, 0.54 s.
- Three existing 16 MiB `ttcp` sends: 4.336054, 4.385292, 4.352594 s;
  3.690, 3.649, 3.676 MiB/s. Host sink verified every byte total and pattern.

Exact commands/raw records remain task-local in
`pyxis-debug-net-baseline/build/baseline/notes.md`. Frozen launch archive SHA-256
`43536a6c6ad2e9b45cac854760d570daadcac255138a04f87ca48f6b33f73188`.

## Manual QEMU inspection

Ordinary image build passed; host bridge builds with native cc, no added source
or compiler dependency. Initial checkpoint ELF SHA-256
`da6250759c35a76978ab165d7efad0316b89e9abc95cfe10dc64347faa2da948`, code
`6ab6b5db` (staged ELF embeds its then-current build revision); later host-only
retry/discovery corrections do not change that kernel's input code.

Interactive GDB through the bridge enumerated four CPUs, exported integer and
segment/base/root registers, and matched owned snapshots inspected independently
through QEMU GDB. Guarded RAM and PCI reads worked. Null/unmapped virtual memory,
unknown MMIO, out-of-range physical RAM, unsupported PCI segment and widths were
refused. Register writes, software breakpoints, stepping, detach and kill were
refused. Continue restored PREPARED, matching per-CPU exit ACKs and an empty NIC
ownership gate; ordinary remote echo then exited successfully. Attached quiet
inspection remained COMPLETE past 30 s through bridge heartbeats.

A fresh unpaired checkpoint was COMPLETE after about 25 s, and subsequently
PREPARED with matching release/empty NIC gate and successful ordinary remote echo.
No peer traffic was sent in that run. Its clock was direct 64-bit HPET; it does
not qualify native 32-bit wrap/reentry. One separate retry froze inside firmware
before kernel entry with QEMU's default CD I/O path; it was excluded from kernel
results. A fresh CD drive with `aio=threads` booted normally.

Raw interactive records: `pyxis-debug-network/build/qualification/`.

One-CPU Q35/KVM passed admitted RNG MMIO (status 0x0f independently checked),
known physical RAM and PCI reads, and refusals of the selected transport's
BAR/DMA/PCI function, unknown MMIO, invalid configuration offsets and null virtual
RAM. A 46-second quiet attached interval stayed COMPLETE. Pausing the bridge for
16 seconds retained an unacknowledged reply and recovered one reported result
on restart; hardware-read executions were not counted. Bridge exit subsequently
restored PREPARED, NIC ownership zero, no outstanding debug TX and successful
ordinary remote echo. These are functional checks, not performance measurements.

Corrected code `e05a3e71` builds normally (ELF SHA-256
`b04f5ebfb63db91f2c8871b2d1db9c3c10a82b1e54b5248f3a2b79dddef8966c`);
four-CPU attachment, MMIO, addressed/signal-continue refusals and ordinary
continue/network recovery passed again. RSP memory capacity now matches its
advertised packet budget. Entry uses one atomic phase authority; secondary
terminal origins cannot claim the legacy fatal NIC during publication/rearming.

Unmerged BSP panic probe `probe/debug-network-panic-bsp` at `7ba11977`, same
code plus a one-line panic after the checkpoint resumes, ELF SHA-256
`2375ae4fc6f6272c92eb595272edf02672f36786f5f3f0d9d7f934a9b7cf65eb`:
checkpoint generation 1 continued; the same GDB connection rediscovered terminal
generation 2 and reported SIGABRT. Four CPU frames and the genuine panic call
stack were readable. A 1024-byte snapshot dump matched an independent QEMU GDB
dump byte-for-byte, spanning pages/chunks. Terminal continue was refused.
After bridge/client exit, independent QEMU inspection 119.967 seconds after the
last valid peer traffic still showed COMPLETE/terminal generation 2, resume
generation 1 and retained NIC gate 8/generation 2. All probe jobs were stopped;
this probe stays unmerged.

Unmerged AP probe `c5dd814f` (based on `db71d37a`), ELF SHA-256
`29a8bd91d34cd4558da71a3d665e412a7457c96a6e167e57ec9e53537d8bcdef`:
two AP scheduler callers panic after the checkpoint releases, without a barrier.
On four-CPU Q35/KVM, CPU 1 owned terminal generation 2; CPU 2's backtrace showed
`await_entry -> enter_stop -> panic`, proving a second terminal origin joined.
All four CPUs acknowledged generation 2 with no missing flags; continue returned
E01. At 44 seconds of host wall time after bridge exit, COMPLETE/terminal,
resume generation 1 and retained NIC gate 8/generation 2 persisted. The same probe repeated with `LOG_UDP=1` verified enabled logging and TX
reservation, all four ACKs, retained debugger ownership and legacy fatal owner
zero. This time CPU 2 owned entry and CPU 1 joined; admitted MMIO worked and
terminal continue was refused. Both probe jobs were stopped.

## Final option-off comparison

Five fresh pairs in order A1/B1 through A5/B5, A `dc91a4c6`, B `ab894fe9`.
Both use the frozen launch archive above, client, firmware, dependency pins,
kernel configuration and static TAP profile. Installed QEMU 10.2.2, nested KVM,
Q35/CPU max/four CPUs/512 MiB, inactive audio, no profiling. Each boot has fresh
OVMF variables. Both use VirtIO SCSI CD, following the
[existing launch configuration](../threads-task1/README.md#inputs-and-configuration),
with direct TAP instead of user networking. Earlier AHCI starts failed before
kernel entry and are retained separately, without performance samples; their
cause was not established. The final set contains ten successful SCSI boots.

Final inspection found unconditional debugger preparation; `ab894fe9` adds the
missing `debug.net` guard. B1 independently showed `arch_debug_enabled=false`,
null settings/snapshots, zero CPU count, DISABLED phase, NMI IST 0, no translation
windows/cache metadata, no entropy worker or driver handoff enablement. Ordinary
network-worker code remains unchanged; no normal klog line was added.

All valid samples are retained, including slower A1. Each cell gives A / B:

| Pair | HPET ready (ms) | Echo (s) | Lua100 (s) | TCP 16 MiB (s) |
| --- | --- | --- | --- | --- |
| 1 | 230.29527 / 226.65784 | 0.13 / 0.04 | 1.09 / 0.54 | 5.946057 / 4.377602 |
| 2 | 216.56810 / 229.27170 | 0.04 / 0.04 | 0.55 / 0.57 | 4.470799 / 4.450922 |
| 3 | 239.71438 / 234.54287 | 0.04 / 0.04 | 0.56 / 0.55 | 4.473284 / 4.478575 |
| 4 | 226.10012 / 228.17156 | 0.04 / 0.04 | 0.58 / 0.55 | — |
| 5 | 240.19259 / 212.87884 | 0.04 / 0.04 | 0.55 / 0.57 | — |

A/B medians: ready 230.29527/228.17156 ms; echo 0.04/0.04 s;
Lua100 0.56/0.55 s; TCP 4.473284/4.450922 s. Ready ranges are
216.56810–240.19259 / 212.87884–234.54287 ms; Lua 0.55–1.09 / 0.54–0.57 s;
TCP 4.470799–5.946057 / 4.377602–4.478575 s. Paired directions vary and ranges
overlap: no consistent option-off slowdown was measurable in this bounded set.
This establishes neither speedup nor idle-host/native performance. Another
owner's VM began during A2 and remained through B5; snapshots record its presence.
Capture window 08:42:25–08:47:16 UTC. GDB detached before client timing; HPET
ready samples retain hardware-breakpoint observation and exclude firmware.

Workloads use existing commands: remote `echo launch-baseline`,
`lua -e 'for i=1,100 do assert(pyxis.run{"boot://echo.pxe", "ipi-baseline"} == 0) end'`,
and `ttcp -t -n 2048 -l 8192 192.168.77.1`, each followed by shell `exit`.
Host `/usr/bin/time -f '%e'` measures echo/Lua sessions at 0.01 s resolution;
guest ttcp timing includes orderly closure. One-shot host socat receives on
192.168.77.1:5001. All ten echo/Lua outputs and command/FINAL statuses verified;
all six TCP captures contain exactly 16,777,216 bytes with the full source-defined
pattern. Content inspection followed timing. No new runner or tests were added.

B ELF SHA-256 `6ad0964b5291bb5d19d7d2dc46cdb2fb639e1ee10edc978a0ff1b387d331f949`;
both images contain the same archive hash recorded above. Exact build/QEMU/GDB
and client commands, firmware/client hashes, raw samples and process snapshots:
`pyxis-debug-net-baseline/build/comparison/notes.md`, `parsed-results.json` and
`final-identities.sha256`. Worktrees and raw records remain local and unmerged.

## Final enabled build and native gate

Clean `ab894fe9`, same ELF as the option-off comparison, ordinary image build
with `DEBUG_NET=qemu-delta DEBUG_WAIT=1` passed bundle verification without
warnings. Image SHA-256
`7501aab0b89398717be656a104a33c378dfb2e1199e188ea94a021802e108031`.
Four-CPU SCSI-CD/TAP inspection passed enabled COMPLETE generation 1, all CPU
RIP/RSP/CR3 exports, guarded RAM and admitted MMIO, and transport/unknown/null
refusals. Continue restored PREPARED/resume 1, NIC gate/debug generation/TX mask
zero, no failure and active networking. Remote echo exited 0 with complete drain.
Raw commands/results: `pyxis-debug-network/build/qualification/final-enabled-notes.md`.
All task-owned QEMU, bridge and GDB jobs were stopped.

## Native owner qualification

Owner-reported, 2026-10-10: Luna built `d5c1f608`, ELF digest prefix
`c7cfdcd9…` (full digest not supplied here), `debug.net=t14 debug.wait=1` and the
matching `debug.image`. Horse bridge/GDB 16 bound t14 at 192.168.0.50 over RTL8111,
12 CPUs, checkpoint reason 0/generation 1. Threads included APIC IDs; CPU 0
was in `enter_stop` from `network_debug_worker`, peers in `cpu_wait_interrupt`.
RIP/RSP/RBP, symbolized backtrace and CPU 4 stack RAM (`x/4gx $rsp`) were readable.

Typed PCI `0000:07:00.0`, offset 0/32 bits returned `0x16361002`.
Renoir MMIO 32-bit reads returned `0x00000f21` at `0xfd314030` twice and
`0x0230007c` at `0xfd314028`. These are successful native read admission/results,
not execution-count or read-side-effect proofs. `detach` returned E01 as scoped.
After GDB exited, the bridge reported idle loss and presentation/boot resumed
about 30 seconds later. Normal network/log recovery was not separately reported.

`info registers rflags` was refused by GDB's register naming. Our AMD64 core XML
uses the standard `eflags`; use `info registers eflags` or `p/x $eflags`.
[GDB's required feature](https://sourceware.org/gdb/current/onlinedocs/gdb.html/i386-Features.html)
specifies that name; the subsequent native panic check returned `0x46`.

The owner completed the remaining native checks on the ThinkPad, with horse
bridge/GDB and Luna builds. Ordinary source `d5c1f608`, rebuilt ELF SHA-256
`68a4ee0a2877927915fa896bcf346c62df2223ff2cd42c2dcb333e15909b431a`:
bridge termination after client exit resumed the kernel about 30 seconds later.
With GDB held attached and Ethernet unplugged for about 40 seconds, the bridge
closed its connection for lost target link; the kernel resumed and boot completed.
Detailed post-loss network/log traffic was not separately reported.

After link loss, the same bridge process discovered a PXE reboot as a fresh
generation-1 target and established a new image-bound session; GDB attached
normally, with no stale state presented. This qualifies fresh discovery after
loss/reboot, rather than an old still-attached connection or raw-datagram replay.

Unmerged panic source `ad607f4f`, native ELF SHA-256
`08ebbd278bee43fd28e91976890796a6216f25d6b210f532001998dbcf2c80b9`:
checkpoint continue reached SIGABRT, 12 terminal CPU threads, generation 2,
reason 1. Symbolized backtrace showed `enter_stop(DEBUG_STOP_PANIC) <- panic <-
debug_network_checkpoint`; stack RAM was readable and `p/x $eflags` was `0x46`.
Further continue was refused; detach returned E01. After GDB disconnected, the
owner observed no log lines for 60 seconds and reported no resume. QEMU's
independent post-loss stop-state inspection above remains separate evidence.

That native probe stopped before local panic output: only a frozen display was
visible. The owner also observed three power-LED flashes at debugger stops;
no EC diagnosis was performed. The local-output correction is qualified below,
not claimed as a new native screen result. Probe branches remain unmerged.
Native 32-bit HPET wrap/reentry remains unqualified; timer entry stays in task 4.
