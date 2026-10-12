# Threads gate 8 qualification

2026-10-12: managed libc creation/collection, parking-backed synchronization,
shared allocator/descriptor/FILE/cache safety and audited external C++ threads.
No new kernel ABI or compiler source change. Consumer adapters remain gate 9.

## Inputs and baseline

Pre-code A: `ce8c53d17397027bdf4876787b72279027e8031e`, tree-identical to
qualified #748 `0c316b61`, now merged in main `7db4cb31`. Userland `2bc56bf4`,
ports `fd1eb30`, fs `28386cc7`, lwIP `a1aadb91`, published builder
`pyxis-llvm23.1.3-2d23443`. Successful exact-head CI REST2087/UI2010 bundles
passed independent verification and ordinary image assembly before capture.

Sequential fresh one/four-CPU QEMU10.2.2 Q35, nested KVM/host CPU, 2 GiB, UTC,
fresh raw OVMF, modern VirtIO SCSI/net/RNG, standard VGA. Existing remote client,
no debugger/build during timing; LOG_LEVEL=info and trace/profile off. Other
agents' VMs were present, so host load was not isolated. Commands: four ipcbench
CALL/SEND 64/4096 modes, 256 messages/100 rounds; allocbench pages size65536/live8/
rounds100; 16 echo launches warmup then three Lua loops of1024. Ordinary echo/exit
exercised the launch/IPI path without a separate IPI timing claim.

All800IPC passes, warmups, message/byte counts,800allocation/releases per boot,
launch counts and20FINAL0/complete records verified; no exclusions. Median
(minimum–maximum) ms per256-message pass:

| Workload | A1CPU | A4CPU |
| --- | --- | --- |
| CALL64 completion | 84.299 (83.614–90.425) | 119.099 (76.674–734.443) |
| CALL4096 completion | 84.136 (83.599–86.255) | 101.907 (67.237–574.976) |
| SEND64 completion | 13.037 (12.776–13.512) | 15.399 (11.465–22.135) |
| SEND64 admission | 1.197 (1.183–1.372) | 1.219 (1.155–1.811) |
| SEND4096 completion | 13.156 (12.892–14.970) | 15.688 (11.489–25.172) |
| SEND4096 admission | 1.273 (1.241–1.723) | 1.298 (1.233–1.957) |
| Pages ms | 185.511 | 262.209 |
| 1024launches wall s | 4.38 / 4.36 / 4.39 | 4.63 / 5.84 / 6.69 |

Clock calibration35,818–40,910ns/read was not subtracted. Launch wall includes
reverse discovery/session setup; pages has one sample/boot and four-CPU ranges
are broad. Matched B results and native owner A/B are not yet recorded.

## Implementation qualification in progress

Ordinary runtime, SDK (including threaded libc++/abi/unwind), ports, userland and
image builds pass with the existing published compiler. The first one-CPU manual
runtime/time/once/C++/I/O/63-sibling limit runs passed. GDB established real word
parking, distinct FS/stacks/errno, native blocked pipe admission with zero mapping
borrowers, and post-join task/storage count1, cleared backing identities/observers
and zero waiters. Final reviewed-input one/four-CPU checks, stream coverage and
real backing exhaustion are in progress; these are not yet final-head claims.

Manual commands use `session bin://threadprobe.pxe` with modes `runtime`,
`runtime-inspect`, `runtime-time`, `runtime-once`, `runtime-limit`, `runtime-cxx`,
`runtime-io` and `exhaust`. Existing TLS/stop/fault/parking modes remain available.
Use the matching ELF relink (`-Wl,--oformat=elf`) and CR3-qualified hardware
breakpoints; no injected calls or live user-data mutation. IO checkpoints require
actual `pipe_call` admission, not a ready flag or scheduling delay.

Raw input hashes, commands, captures and debugger records remain under the
isolated gate8 baseline and implementation worktrees' `build/gate8-*` directories.
No native performance or hardware result is claimed.

Four-CPU qualification exposed two failures before delivery. GDB caught a worker
returning from its first failed malloc with native MEMORY `CALL_BUSY` still in
its request record and errno ENOMEM. The reviewed allocator now rechecks published
pools and retries only bounded, definite pre-effect BUSY refusals outside guards;
managed backing/CREATE/cleanup uses the same bounded policy. Real NO_MEMORY and
unknown outcomes are not retried.

The 63-sibling stress also hit a kernel exception with a stack-address RIP and
corrupted interrupt return fields. An admission breakpoint independently caught
a migrated, not-yet-admitted syscall retaining CPU2's scheduler pointer/stack
while executing on CPU0. A repeated rejection could switch onto the wrong CPU's
permanent stack. The kernel correction refreshes the CPU-local scheduler after
that handoff, preserving pre-admission migration/stop behavior and adding no
ordinary successful-entry operation. Fixed-image repeats remain pending.
