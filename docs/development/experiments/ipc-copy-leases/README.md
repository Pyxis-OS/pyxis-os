# IPC copy-phase lease qualification

Pre-code A: clean main `2fca30fd1d5ee6a7eabc0b285bacf0a6b9b8f5c7`, including
merged #700/#702, 2026-10-11. Exact-head successful
[CI #1822](https://git.internal/PyxisOS/pyxis-os/actions/runs/1822) supplied
verified kernel/SDK/userland/ports bundles. Ordinary image assembly and host
remote-client build passed; LLVM 23.1.3 builder `49e2c1a`, cached owner-mirror
AX200 firmware, no upstream fetch or compiler rebuild. Userland `df210f1a`,
ports `df3210b0`, fs `b427df29`, lwIP `a1aadb91`; no pin changes.

Frozen A ELF SHA-256 `4131ab131dfb33a12fd1f0d9d02a4150d61f46e8b12d1420ea8d0589b1a77525`,
ISO `5800da8fe6b36ab1c95a89380033e6296e0615f9f9b628e4cda9de7bb6dc37ad`,
initrd `ade95a3487ebc89a40f32dff4bef4bfac3887e65ce5eb6b52bb016a9d947f020`.

QEMU 10.2.2, Q35/nested KVM, CPU host, 1/4 cores, one thread per core, 2 GiB,
UTC RTC, matching raw OVMF/fresh VARS, standard VGA/display none, modern VirtIO
SCSI CD/net/RNG. Info logging/profiling off. UDP NAT forward 2324; host reverse
client `--machine --no-shell-echo --listen t14 --beacon-address 127.0.0.1
0.0.0.0 2323`. No debugger/build during timings.

Each fresh baseline boot ran CALL/SEND 64/4096 bytes, 256 messages, 100 rounds
plus untimed warmup, using `session bin://ipcbench.pxe`. All 800 timed passes
verified with zero failures/rejects and FINAL 0/complete. Clock calibration
32,630–36,412 ns/read, not subtracted. Raw JSONL and decoded median/range values
remain in ignored `build/ipc-lease-baseline/{1,4}-A1`. These nested-VM results
are qualification evidence, not native performance or a speedup claim.
