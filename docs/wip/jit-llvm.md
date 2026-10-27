# LLVM JIT adapter

The kernel/manual-consumer foundation is implemented in #1519, userland #401,
ports #196 and integration #1520, following accepted #1516. The library adapter
is in implementation, rebased onto fork `8a4d001d`. It changes only the
separate development-library pin, not the compiler/container or hosted-toolchain
pins. Main already selects compiler `8a4d001d` for its C++ filesystem delivery.

The selector and borrowed-section adapter are implemented in pyxis-llvm #20.
The refreshed development prefix builds; ORCv2 with RuntimeDyld/SMM executes
generated code in QEMU. MCJIT remains blocked on the two decisions below; it
is not qualified or ready to merge. No compiler/container pin moves.

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

## Open decision 1 — MCJIT process-symbol bootstrap

**Proposed default:** on Pyxis, skip only EngineBuilder's unconditional
`LoadLibraryPermanently(nullptr)` process scan. Resolve imports through existing
explicit `LLVMAddSymbol`/global mappings or the caller's resolver. Named shared
libraries and implicit process-image symbol lookup remain unsupported. Do not
return fake success from DynamicLibrary, add a symbol table to P1F or create a
new loader service.

`EngineBuilder::create` currently attempts that unsupported operation even for
IR containing no imports. QEMU confirms it refuses with `dlopen() not supported
on this platform`. Explicit-symbol lookup already precedes library lookup; it
does not need process loading. The alternative is to retain refusal and leave
MCJIT unavailable; ORCv2's explicit RuntimeDyld/SMM layer works without that scan.

## Open decision 2 — permission-finalization failure

**Proposed default:** a failed native memory finalization ends the program
through LLVM's existing fatal-error path, with a clear permission-publication
diagnostic. Never execute code after a failed transition, retry publication or
invent a successful result. Kernel/process teardown retains its normal ownership
and reclamation guarantees. MCJIT already uses fatal errors for relocation and
other execution failures; this is the smallest correction that also covers its
legacy execution helpers without an error result.

MCJIT currently ignores `finalizeMemory()` failure, unlike ORC's RuntimeDyld
path. An error-message-only guard is insufficient: the C error getter clears
that message as part of reading it. The fence must survive diagnostic clearing.
This correction must accompany enabling MCJIT; otherwise a refused native
permission transition can be treated as finalized code. The recoverable
alternative is a persistent engine fence with its existing error API and
zero/null executable-address lookups, surviving diagnostic clearing until
destruction; legacy execution helpers still need a fatal error. That adds
engine state and checks across its execution entry points. Previously returned
raw pointers retain the caller's code-lifetime obligation in either design.

No code for these two decisions has been implemented. Source references are
`llvm/lib/ExecutionEngine/ExecutionEngine.cpp:510`,
`MCJIT/MCJIT.cpp:253` and `ExecutionEngineBindings.cpp:309–317` at development
base `a63ce582`. Qualification and the exact updated pin are recorded in the
[experiment](../development/experiments/llvm-jit.md).
