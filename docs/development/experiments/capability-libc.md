# Capability libc baseline and qualification

2026-10-14. Before implementation: parent `a41f7fe4`, userland `8ac71538`,
ports `efe8d877`, fs `d84f8084`, lwIP `a1aadb91`; builder `df87791`.
Ordinary `make -j8 image`. Nested q35/KVM, four CPUs/one thread, 2 GiB,
std VGA, VirtIO net/RNG, stock 80×24 Remote. No task-owned build/debugger during
measurement; host load was not fully controlled (another QEMU was present).
These are nested measurements, not native throughput. Repeat unchanged/changed
boots interleaved after implementation before attributing a difference.

Three warmed cohorts: existing Lua times ten OPEN/close pairs for native and
text targets; `iobench read --bytes 1048576 --buffer 4088` uses ten native/RAM
rounds or three HTTP rounds. RAM is `cp boot://share/iobench.bin tmp://a4.bin`.
HTTP uses the existing local fixture at `http://10.0.2.2:18080/fixed`.
POST uses the existing Lua adapter: prepare `/echo` with `w+`, write sixteen
64 KiB blocks of `z`, explicit commit, read/verify `POST accepted`, close.
Each cohort warms once then measures three POSTs. Clock overhead is not subtracted.

| Workload | Cohort medians (ms) | All-sample range (ms) |
| --- | --- | --- |
| Lua native OPEN | 0.0390 / 0.0394 / 0.0399 | 0.0385–0.0456 |
| Lua text OPEN | 2.066 / 5.169 / 2.258 | 1.525–7.120 |
| Native 1 MiB complete | 0.313 / 0.276 / 0.292 | 0.269–0.410 |
| RAM 1 MiB complete | 0.485 / 0.464 / 0.463 | 0.436–0.566 |
| HTTP 1 MiB complete | 253.448 / 201.138 / 199.743 | 198.903–261.767 |
| POST staging, pooled nine | 188.482 | 175.189–220.129 |
| POST commit/read, pooled nine | 843.440 | 826.485–874.935 |
| POST stage/commit/read, pooled nine | 1031.922 | 1001.675–1092.761 |

Every command succeeded; FINAL reported exit zero and complete drain. All twelve
POSTs had 1,048,576 decoded bytes and matching server SHA-256
`3ac3338d67611f3edb444a8f730d5e3a6559d4640e7b1a2d5fa58bafbda3254a`.
Large variation, especially text IPC, limits inference from this first cohort.
Raw commands/output/server records remain local; no new benchmark infrastructure.
Baseline QEMU/server jobs were stopped before implementation.

**Pyxis must be able to hit good network targets overall in the future.**
The ThinkPad TCP reference remains 70.5 MiB/s send / 85 MiB/s receive (2026-10-09),
not a contract or a nested-QEMU target. Relevant native HTTP changes need matched
comparison to that path. This record has no new native A/B result.

## Implementation checkpoint

Ordinary SDK and image builds passed with the same builder. Four-CPU nested
QEMU/Remote success checks passed: native and text opens; a complete 1 MiB streamed
GET; plain Lua `io.open(..., "w+")` / write / flush / read / repeated flush /
close; and write-only explicit final close. All four programs exited zero, and
Remote FINAL reported zero and complete drain. The server observed exactly one
7-byte POST for the flush case and one 8-byte POST for the close case, with the
expected SHA-256 values. Jobs were stopped after the run.

This is functional evidence only. Matched after timings, ordinary C checks,
large/cap checks and Daybreak's adversarial/lifetime qualification remain. Lua
automatic cleanup awaits its owner decision; A4 stays unchecked/draft. Source
review corrected stale raw-close abandonment, FILE exclusion through raw close,
transaction target staging in dup2, and failed-COMMIT detail/body retention.
