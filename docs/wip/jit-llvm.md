# LLVM JIT adapter

The kernel/manual-consumer foundation is implemented in #1519, userland #401,
ports #196 and integration #1520, following accepted #1516. The library adapter
is in implementation, rebased onto fork `8a4d001d`. It changes only the
separate development-library pin, not the compiler/container or hosted-toolchain
pins. Main already selects compiler `8a4d001d` for its C++ filesystem delivery.

The selector and borrowed-section adapter are implemented in pyxis-llvm #20.
The refreshed development prefix builds; ORCv2 with RuntimeDyld/SMM executes
generated code in QEMU. MCJIT qualification is being repeated with the accepted platform behaviour
below. No compiler/container pin moves.

## Decision — explicit allocation intent

**Accepted (chronium, 2026-10-27):** add an explicit JIT allocation selector to LLVM's Pyxis
mapped-memory path, and have SectionMemoryManager's default mapper select it
for its code, read-only and writable data storage. Such allocations require
held `jit/manage`; missing authority refuses. Ordinary mapped RW allocations
remain on MEMORY even when the program also holds JIT authority. No shipped
space or compiler bundle newly declares JIT.

`sys::Memory::allocateMappedMemory` receives initial permissions, not the eventual
use of RW pages. SectionMemoryManager initially requests RW for every section;
FileOutputBuffer also calls this API for ordinary output. Inferring intent from
the presence of a JIT grant would charge those unrelated buffers to JIT's
256 MiB/4096-region quota and make ordinary output depend on executable-memory
policy. Inferring it only from initial EXEC would miss writable code preparation.

## Implementation and qualification

Preserve the original native service, non-reused identity, base and size in
MemoryBlock-derived section descriptors. The original allocation owns release;
derived blocks borrow provenance, including a full-span view, and cannot release
it. PROTECT rounds a section to contained pages of that identity. No global
address-to-current-allocation registry or new code-entry lease is introduced.
The existing SectionMemoryManager owner outlives its pending/free descriptors.
NearBlock remains advisory, as LLVM documents; strict proximity is not promised.
Instruction-cache maintenance relies on completed native W→X publication and
the no-writable-alias contract, not SYSRET or an unsupported successful syscall.

Qualify a real MCJIT function using code and separately protected read-only data,
rewrite/republish, cleanup and clear missing-authority refusal. Rebuild the
affected native Support/ExecutionEngine objects and needed archives; this does
not call for a compiler-container rebuild. QEMU results remain distinct from
native hardware and llvmpipe qualification.

Do not enable the other ORC routes incidentally. JITLink's in-process manager
splits one slab into independently released lifetimes, while ORC mapper/executor
paths reconstruct descriptors from raw addresses. Whole-identity RELEASE does
not authorize those partial releases; they need a separate lifetime adaptation
before support is claimed. The first consumer remains SectionMemoryManager.

## Decided — process symbols and protection failures

**Accepted (chronium, 2026-10-27):** on Pyxis, skip only EngineBuilder's
unconditional `LoadLibraryPermanently(nullptr)` process scan. Callers register
symbols explicitly through existing LLVM symbol mappings/resolvers. Implicit
process/shared-library lookup waits for the dynamic linker work. DynamicLibrary
continues to report unsupported operations; no P1F symbol table or loader service
is added here.

**Accepted (chronium, 2026-10-27):** retain upstream MCJIT finalization behaviour;
add no engine fence, fatal-error policy or defensive finalization rewrite. The
Pyxis memory-protection adapter retries definite pre-effect `BUSY` refusals
(eight attempts, matching libc's current VM admission tuning). All other native
errors, including unknown outcomes, propagate without retries or a fallback.
Ordinary native W^X enforcement remains in force: executing unpublished NX code
faults and the process reports `FAULTED`. This does not make MCJIT's upstream
error reporting stronger or provide a code-entry lifetime lease.

The old exact-input QEMU run refused MCJIT creation because of its process scan;
the refreshed run and pin are recorded in the
[experiment](../development/experiments/llvm-jit.md).
