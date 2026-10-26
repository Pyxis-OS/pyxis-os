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

- [ ] Record matched switching-workload baseline using existing ipcbench.
- [ ] Per-CPU discovery, boot rendezvous, owned state and eager switching.
- [ ] QEMU: 1 and 4+ CPUs, YMM preemption/blocking/migration/inheritance,
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
