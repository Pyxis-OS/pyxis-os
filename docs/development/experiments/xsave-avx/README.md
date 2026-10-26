# XSAVE/AVX qualification (2026-10-26)

Implementation `74acf240`, baseline kernel code identical to main `1899009b`.
All reported final timings use fresh logs and those exact preserved ELF files.
Existing Clang 23 builder `pyxis-llvm23.1.3-3d69e83`; no toolchain rebuild.
Four-vCPU and one-vCPU nested KVM, q35, `host,+invtsc`, modern VirtIO SCSI CD
and rng. The FP probes passed fresh YMM defaults, all sixteen YMM registers
across computation and native blocking sleep, x87/MXCSR control inheritance,
four native siblings and their distinct states. Four-CPU affinity changes
forced the same live register patterns through every CPU before thread launch.
GDB readback found XSAVE enabled, an 832-byte payload, alignment modulo 64 of
zero, and a valid standard header with reserved fields zero. Kernel disassembly
contained no XMM/YMM/ZMM/x87 register operands. Go's unmodified internal/cpu
reported OSXSAVE/AVX/AVX2/SHA true, hashed `abc` correctly and completed four
cooperatively scheduled goroutines. Private probes/collectors are not merged.

With `host,+invtsc,-xsave,-avx,-avx2`, boot warned once, kept FXSAVE on four
CPUs and safely skipped AVX. Existing ipcbench and Go legacy checks cover this
path. A Skylake-Server TCG run passed, but this QEMU masks out requested
AVX-512 bits: it does **not** establish AVX-512-capable hardware coverage.
Mixed per-CPU feature/layout fallback has source review, not a heterogeneous
hardware run. Native qualification is recorded below.

Existing ipcbench CALL used sizes 0/64/4096 bytes, 256 messages and 20 verified
samples per size per boot. The launcher and receiver used the initial CPU0
space even with four configured CPUs. These are elapsed switching-workload
proxies, not isolated instruction costs. No debugger/profiling was present.
The table contains medians in milliseconds per 256 round trips; all payloads
and cleanup verified. Raw samples retain the ranges.

| CPUs | Bytes | A1 FXSAVE | B1 XSAVE | A2 FXSAVE | B2 XSAVE |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 0 | 1.178 | 1.193 | 1.174 | 1.200 |
| 1 | 64 | 1.187 | 1.221 | 1.202 | 1.215 |
| 1 | 4096 | 1.349 | 1.370 | 1.361 | 1.371 |
| 4 | 0 | 1.223 | 1.192 | 1.176 | 1.196 |
| 4 | 64 | 1.188 | 1.228 | 1.194 | 1.199 |
| 4 | 4096 | 1.353 | 1.351 | 1.339 | 1.374 |

Final per-pass ranges across the three sizes are A1 1.173–1.520 ms and
B1 1.185–1.512 ms on one CPU; A2 1.168–2.354 and B2 1.190–1.422 ms.
Four CPUs: A1 1.174–2.188, B1 1.182–1.378, A2 1.171–2.296 and B2
1.181–1.434 ms. Paired medians differ by about -2.5% to +3.4%; shared-host
variation and short batches prevent an isolated XSAVE-overhead or native-speed
claim. Preliminary captures remain private and are not mixed into this table.
The accepted eager engine enables correct AVX; native cost remains unqualified.


## Native qualification

Chronium-run packet from merged [#1246](https://git.internal/PyxisOS/pyxis-os/pulls/1246),
submitted revision `1605b216`, final kernel source `23be575c`. The sealed kernel
SHA-256 is `989962e7cabf3e37c4267f358c538ede3201a997d121753e2d8878794022f9a6`;
userland `71e7baad`, Go `f9346269`. The private live archive uses RAM storage.

| Machine | Online CPUs | Selected state | C state check | Go check |
| --- | ---: | --- | --- | --- |
| vela, IdeaPad Panther Lake | 16 | XCR0 `0x7`, 832 bytes | PASS | OSXSAVE/AVX/AVX2/SHA, known digest and goroutines PASS |
| ThinkPad T14 Gen 1 AMD, Renoir | 12 | XCR0 `0x7`, 832 bytes | PASS | OSXSAVE/AVX/AVX2/SHA, known digest and goroutines PASS |

Both C runs reported zero failures: fresh x87/MXCSR defaults, zero YMM upper
halves, four native siblings, inherited controls and distinct register patterns
through computation and blocking sleeps. These ordinary native bundles did not
force affinity; explicit migration coverage is the QEMU probe above. Vela's
log records Intel HWP applied to all 16 CPUs; the AMD machine reports HWP
unavailable, as expected. AC state was not captured in this record.

The orchestrator ran the existing packet collector over Remote. Evidence stays
private in `/shared/present/xsave-avx/native/results-20261026T102237/` (vela) and
`results-20261026T102322/` (ThinkPad): C/Go output, kernel log and typed host
completion events. No raw logs or hardware identifiers are committed. Vela's
[bulk-read check](../bulk-file-reads/README.md#native-vela-2026-10-26) shared this
window. This closes the functional native gate; no native before/after switching
cost, Mesa/JIT runtime result or AVX-512 hardware coverage is claimed.
