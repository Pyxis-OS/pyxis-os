# Retained FILE accounting

## Before-code baseline — 2026-10-15

Parent `11e6ae8c`: main `b030c7b1` plus the unchanged handoff #874 source;
userland `7795dac6`, ports `0e760128`, fs `101e1f68`, lwIP `a1aadb91`.
Separate worktree/branch: `pyxis-retained-values`, `kernel/retained-values`.
The handoff review branches are untouched. Ordinary complete `make -j16 image`
passes using the existing `pyxis-llvm23.1.3-df87791` image.

Nested q35/KVM, QEMU 10.2.2, four CPUs, host CPU, 2 GiB, UTC RTC, fresh OVMF,
standard VGA, VirtIO net/RNG; loopback 24489 forwards guest 2323. Plain Remote
machine/no-echo is 80×24. The exact existing Lua OPEN/launch and 1 MiB iobench
read/write/pipe commands are in the
[handoff baseline](capability-output.md). Three warmed cohorts each contain ten
measured samples. All nine sessions exit zero with complete FINAL drain and
verified transfers. No owned build or debugger runs during timing; shared-host
load is uncontrolled. GDB afterward sees all CPUs idle, no retired objects and
zero retired heap bytes. No native cost or leak-freedom claim.

| Nested elapsed ms | Cohort medians | All-sample range |
| --- | --- | --- |
| Pipe | 0.74533 / 0.57960 / 0.74835 | 0.52043–5.30215 |
| Native OPEN | 0.03681 / 0.04047 / 0.03981 | 0.03653–0.43160 |
| Text OPEN | 1.60748 / 2.31484 / 1.78297 | 0.95586–4.60099 |
| Launch | 8.73048 / 7.37991 / 9.90250 | 2.94560–16.35479 |
| Boot read | 0.26838 / 0.25731 / 0.26599 | 0.25461–0.32602 |
| RAM read | 0.44273 / 0.44218 / 0.44866 | 0.43184–0.49143 |
| RAM write | 9.73688 / 9.76820 / 9.72725 | 9.46488–10.09453 |

Raw JSONL, input commands, image, summary and GDB inspection remain in ignored
`build/retention-baseline/`. All owned baseline jobs are stopped. Repeat matched
workloads after implementation with the preserved image interleaved as control.

Baseline ELF SHA256
`6817d22e43d3c80df3fe13676c959ab50b4b78b96318e11f7f0239385546a31b`;
ISO `765bdda6d3f37f8943809077805b41336168539026e32f5940b9069028901590`.
