# Network debugger task 3 qualification

In progress, 2026-10-10. Native RTL8111/PXE and the final interleaved option-off
comparison are pending. No native result is claimed.

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
Transport-loss terminal retention is being inspected before stopping this probe.
