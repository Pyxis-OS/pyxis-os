# Hosted Clang task 4 pre-code baseline

2026-10-12, before implementation. Requested base `4366eb28`, userland
`21cf935c`, ports `fd1eb30`, fs `30f625`, lwIP `a1aadb91`; compiler fork
`2d234435`, published builder `pyxis-llvm23.1.3-2d23443`.
The reused qualified image was actually built at `2f963d05`, not `4366eb28`:
only three docs differ; kernel/ABI/SDK/boot inputs, dependency pins and active
configuration are identical. No compiler/container rebuild was needed.

Fresh one-/four-CPU QEMU 10.2.2 Q35, nested KVM/host CPU, 2 GiB, UTC,
raw OVMF, standard VGA, modern VirtIO SCSI/net/RNG. No task build, profiler or
debugger during timing; unrelated VMs remained, so host load was not isolated.
Existing commands:

```text
iobench write tmp://clang-baseline.bin --rounds 3
iobench read tmp://clang-baseline.bin --rounds 5
# Lua: 16 echo launches warmup, then three loops of 1024 echo -n children.
```

All 16 timed I/O samples and four warmups verified 1 MiB counts/content,
with no failures; launch counts matched and all 12 sessions ended FINAL 0/complete.
No exclusions. Median (minimum–maximum), milliseconds except launch wall:

| Interval | 1 CPU | 4 CPU |
| --- | --- | --- |
| RAM write | 12.289 (11.723–16.817) | 10.056 (9.997–10.101) |
| Read payload | 0.439 (0.399–0.888) | 0.419 (0.400–0.431) |
| Read open/read/EOF/close | 0.530 (0.480–0.985) | 0.496 (0.477–0.507) |
| 1024 launches, all wall samples s | 3.81 / 3.71 / 6.95 | 6.41 / 5.14 / 5.60 |

Clock calibration was 35,625–114,283 ns/read, not subtracted. Launch includes
discovery/session setup; RAM writes grow without sync. This is an OS baseline,
not compiler performance, physical-storage durability or native ThinkPad evidence.
No hosted Clang/LLD cross-build or execution has occurred for task 4.
Raw commands, provenance/hashes and captures remain local in
`build/hosted-clang-task4-baseline/`; owned VM/client jobs are stopped.
