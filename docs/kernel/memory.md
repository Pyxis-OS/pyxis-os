# Private userspace memory

The native [memory protocol](../../include/abi/memory.h) supplies backing for
userspace allocation without a userspace heap implementation. The boot launcher
explicitly grants a `memory` resource to each program. Discover it through
`startup_resource("memory")`; there is no ambient allocation syscall.

## Authority and lifetime

The service's MANAGE right permits ALLOCATE and RELEASE in the calling process.
The object holds no process pointer, address space or allocation list. A copied
or transferred grant lets its receiver manage that receiver's own private
regions; it never authorizes changes to the sender's mappings. Unknown rights
are rejected by the ordinary capability machinery.

Regions belong to the process. Closing a service handle, even the last handle
to the object, does not unmap anything. Releasing a region requires another
MANAGE grant. Normal exit or a fatal user fault reclaims all remaining private
backing and allocation records through process destruction.

## Operations

ALLOCATE takes a nonzero byte count, rounds it up to the ABI's page size and
returns an address/size pair. The kernel chooses a disjoint, page-aligned virtual
range and eagerly supplies zeroed, user-readable/writable, non-executable pages.
The range is virtually contiguous; physical frames need not be contiguous.
Size overflow is BAD_REQUEST. Backing, metadata or virtual-range exhaustion is
NO_MEMORY. Failure publishes no region and preserves existing allocations.

RELEASE requires the exact address and rounded size returned by ALLOCATE. A
process-owned list identifies these allocations independently of the VM's other
ranges. An unknown address, incorrect size, partial release or repeated release
is BAD_REQUEST. Image segments, startup data and the initial stack cannot be
released through this service. Addresses can be reused: the pair identifies a
current allocation, not a generational handle, so callers must discard stale
pairs after release.

RELEASE returns status only and ignores reply storage. The kernel captures the
whole request before changing mappings, so the request may itself reside in
the region being released. It performs no user access afterward. The caller
must still keep its returning code and stack mapped.

There is no resize, fixed-address request, permission change, reserve-only mode,
shared memory or per-region object/handle. The later userspace allocator can
subdivide acquired regions without entering the kernel for each small allocation.

## Execution

ALLOCATE and RELEASE run in the caller's own syscall, on whichever CPU runs it,
with interrupts disabled. They change the caller's private address space while
it is active on that CPU, and nothing is lent to the BSP.

This relies on the single-task process model. The process's only task is the
only user of its address space, and it cannot move to another CPU during a
syscall. A CPU that stops running a task switches roots, and with PCID and
global pages off that flushes its TLB. No other CPU can therefore hold this
address space's translations, so unmapping needs only a local `invlpg`.
Display requests, which still lend the space to the BSP, cannot overlap a
memory call, because the task is inside one syscall at a time.

The heap, physical allocator and scratch mappings underneath are safe on any
CPU ([SMP](smp.md#memory-and-output-boundaries)). Range records come from the
kernel heap, frames are zeroed through the caller's CPU's own scratch slot, and
a failed allocation unwinds its partial backing before returning NO_MEMORY.

Allocation writes its already validated reply after changing the mappings; it
cannot invalidate that reply because it adds a disjoint range. Release makes no
user-memory access afterwards. A stop or group termination takes effect when
the syscall returns, so a process's address space and allocation records are
never torn down while its memory call runs.

## Native use

[Libpyxis](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/memory.h) exposes `memory_allocate()` and
`memory_release()`, returning native statuses and validating allocation replies.
Allocation clears its output on failure. Release takes the region by value and
writes no output, so its descriptor need not outlive the call.

Hello uses temporary acquired regions for file reads, releasing each after use.
Its path workspace has process lifetime and is reclaimed on exit, after hello
has closed its memory handle. The libc allocator choice remains the next task;
this service implements only private backing.
