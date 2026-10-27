# LLVM JIT adapter

The kernel/manual-consumer foundation is implemented in #1519, userland #401,
ports #196 and integration #1520, following accepted #1516. This focused adapter
has not been implemented. It targets the current static development LLVM pin,
`a63ce582`, rather than silently changing the separate hosted-toolchain pin.

## Decision — explicit allocation intent

**Proposed default:** add an explicit JIT allocation selector to LLVM's Pyxis
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

The alternative is to use JIT opportunistically whenever the source is held.
It avoids a selector but has those quota/behavior consequences. A separate
consumer-specific mapper is also possible, but the native allocation adapter
would still need a way to distinguish the two ownership protocols. The explicit
selector is the recommended default; its spelling is an implementation detail.

## Bounded implementation after the decision

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
