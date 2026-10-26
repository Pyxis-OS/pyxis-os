# XSAVE and AVX userspace state

Assigned to Codex alpha (chronium, 2026-10-26). Kernel task only; BSP-removal
planning and the llvmpipe JIT grant follow separately.

## Accepted contract (chronium, 2026-10-26)

1. Enable x87/SSE/AVX only, XCR0 `0x7`, automatically when qualified.
   AVX-512 and every other component remain disabled.
2. Require one compatible profile on every online CPU before user scheduling.
   Compare the selected CPUID leaf `0xD` layout and the AVX-family features
   used by current Go/Mesa dispatchers; XCR0 cannot mask userspace CPUID.
   If support/layout/features differ or usable XSAVE state cannot be established,
   warn and retain FXSAVE system-wide, with OSXSAVE clear. No CPU offlining,
   feature affinity or new boot option. Existing FXSR/SSE/SSE2 minimums remain.
3. Keep eager save/restore at existing switching boundaries. Use standard-format
   XSAVE/XRSTOR with CPUID-sized, 64-byte-aligned owned user-task buffers.
   Initialize the complete area/header, preserve x87/MXCSR defaults and full
   sibling inheritance, and free only on actual retirement or failed preparation.
   Creation failures use existing NO_MEMORY results. Kernel code remains
   `-mgeneral-regs-only`; no lazy FP ownership, supervisor state, signal frames
   or debugger register expansion.

## Work

- [x] Record matched switching-workload baseline using existing ipcbench.
- [x] Per-CPU discovery, boot rendezvous, owned state and eager switching.
- [x] QEMU: 1 and 4+ CPUs, YMM preemption/blocking/migration/inheritance,
  fresh-task defaults, legacy fallback, Go feature selection and matched costs.
- [ ] Native vela and ThinkPad qualification through the orchestrator.

## Validation and limits

Keep exact revisions, kernel hashes, CPU models, devices and repeated samples.
Existing ipcbench elapsed time measures a switching workload, not isolated
XSAVE instruction cost. Label nested results; no new benchmark framework.
The baseline kernel preserved from the bulk-read check has identical kernel,
arch, library and header sources to main `1899009b` (a merge-only difference).
Private captures remain under `/shared/present/xsave-avx/qualification/`.

No new native operation, P1F change, CPU-feature grant, libc feature API or
compiler-container rebuild is needed. Existing CPUID.OSXSAVE/XGETBV dispatch
is sufficient. Keep compiler baselines unchanged. Faults remain ordinary fatal
user exceptions; built-in GDB still lacks usable extended FP registers.

Architecture: [Intel SDM, chapter 13 and CPUID/XSAVE/XRSTOR/XSETBV](https://cdrdv2-public.intel.com/874240/325462-090-sdm-vol-1-2abcd-3abcd-4.pdf),
[AMD CPUID leaf 0xD](https://www.amd.com/content/dam/amd/en/documents/archived-tech-docs/design-guides/25481.pdf).

## QEMU record (2026-10-26)

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
hardware run. Native vela/ThinkPad checks remain open.

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

The private native packet at `/shared/present/xsave-avx/native/` contains the
kernel, live RAM archive, C/Go bundles and the existing Remote collector adapted
for these checks. Its complete C/Go and optional bulk-read A/B/A/B procedure was
checked on nested QEMU before handoff. All archive/member/selected-set hashes
passed for four fresh 268 MiB/1242-file installs. No native staging or run was
done. The orchestrator schedules vela and ThinkPad; the optional bulk comparison
qualifies live RAM-npfs, not installed NVMe storage.
