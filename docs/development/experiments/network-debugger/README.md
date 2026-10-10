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
