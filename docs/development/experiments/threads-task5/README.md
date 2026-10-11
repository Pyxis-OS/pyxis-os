# Native thread lifecycle qualification

Pre-code A: main `a617bcd74fcb875b3ddae8ad1ceaaeabae698f9e`, 2026-10-11,
including #704/#705. Exact-head successful CI #1836 supplied matching kernel,
SDK, userland and ports bundles; ordinary image assembly and host remote-client
build passed with LLVM 23.1.3 builder `49e2c1a`. No dependency pins changed.
Frozen ELF SHA-256 `036ee426bf60e2b8e53860c96b06e72e69a9da33bb6d1613a488d61cc1fde0c4`,
initrd `1db324cb59b374b9a4ce684a88500b3fd040520853ccfd67dde5b0a9f7ae2388`,
ISO `4d8326a9284b8cc5f0cc841476b3659a5de37be69c33ccb8dd7f3e5e56b19133`.

QEMU 10.2.2, Q35/nested KVM, CPU host, 1/4 cores/one thread per core, 2 GiB,
UTC RTC, raw OVMF/fresh VARS, standard VGA/display none, modern VirtIO SCSI
CD/net/RNG; info logging/profiling off. Remote reverse client through UDP NAT
2324 and localhost beacon, as in [IPC follow-up](../ipc-copy-leases/README.md).
No debugger/build during timing. Per boot: CALL/SEND 64/4096, 256 messages,
100 rounds; three `allocbench pages --size 65536 --live 8 --rounds 100` runs;
16-launch warmup then three 1024-child Lua sessions. All 800 timed IPC passes
verified, six page runs reported 800 allocations/releases with zero failures,
all launch counts matched, every invocation FINAL 0/complete. Raw captures and
frozen A remain in ignored `build/task5-baseline`.

After timing, interactive four-CPU GDB with target calls disabled observed the
existing BSP network worker completing UDP receive and sending the reschedule
IPI to CPU1; the remote echo/exit completed with FINAL 0/complete. Task-owned
baseline QEMU/GDB/client jobs stopped. Subsequent implementation/qualification
and matched comparison are pending; these nested values are not native costs.
