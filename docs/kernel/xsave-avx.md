# Userspace x87, SSE and AVX state

Caelum automatically enables x87/SSE/AVX, XCR0 `0x7`, when every online CPU has
a compatible profile (chronium, 2026-10-26). AVX-512 and all other components
remain disabled. Existing compiler baselines are unchanged; programs must gate
optional instructions with CPUID and XGETBV.

## Boot profile

Each CPU starts from the FXSR/SSE/SSE2 baseline and checks CPUID leaf `0xD`,
selected component geometry, newly enabled user XSAVE instruction support and
the relevant AVX-family instruction features. XCR0 cannot mask userspace CPUID.
All CPUs join a boot rendezvous after clock validation and before user dispatch.
The chosen profile is immutable for the boot; no feature affinity or CPU
offlining is introduced. Unsupported/incompatible profiles or unavailable state
warn once and retain FXSAVE system-wide with OSXSAVE clear. An unresponsive CPU
at the bounded startup rendezvous remains boot-fatal, like existing AP startup.

## Ownership and switching

Every user task owns a CPUID-sized, 64-byte-aligned kernel area, with its original
allocation pointer retained for release. The complete area/header is cleared;
initial process tasks get the existing x87/MXCSR defaults and zero registers.
Native siblings copy their creator's entire saved state, including exception
flags and YMM upper halves. Preparation failure or actual task retirement frees
the area; allocation failure uses existing NO_MEMORY results. Kernel workers
allocate no FP state.

Standard XSAVE64/XRSTOR64 save eagerly at existing blocking/preemption/relocation
boundaries and restore on dispatch. Nonswitching syscalls leave FP state alone.
Kernel code and built-in libraries retain `-mgeneral-regs-only`; there is no
kernel FP borrowing, lazy ownership, supervisor state or user signal frame.
[Userspace floating point](userspace.md#floating-point) defines the initial state.

## Availability and limits

No new native operation, P1F format, CPU-feature grant, libc feature API or
compiler-container rebuild is needed. Existing libraries can select their AVX
paths automatically. CPU faults retain the existing user-fault behavior.
Built-in GDB/fault reporting still lacks extended FP-register exposure.

[Qualification](../development/experiments/xsave-avx/README.md) includes QEMU
state/default/inheritance/migration/fallback checks, matched nested switching
workloads and the chronium-run vela/ThinkPad native passes. Heterogeneous profile
refusal is source-reviewed; native switching cost, Mesa/JIT and AVX-512-capable
hardware remain outside that evidence. The separate JIT grant is unchanged.

Architecture: [Intel SDM, chapter 13 and CPUID/XSAVE/XRSTOR/XSETBV](https://cdrdv2-public.intel.com/874240/325462-090-sdm-vol-1-2abcd-3abcd-4.pdf),
[AMD CPUID leaf 0xD](https://www.amd.com/content/dam/amd/en/documents/archived-tech-docs/design-guides/25481.pdf).
