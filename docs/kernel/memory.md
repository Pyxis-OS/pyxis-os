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

ALLOCATE and RELEASE capture their request, then park outside the private root
and kernel entry stack. BSP executes them under the process VM mutation
transaction shared with DISPLAY mapping changes. Dispatch reserves root activity
before CR3 activation; departure acknowledges it after loading the kernel root.
New admission closes during mutation, while previously admitted continuations
can finish bounded copies. Only active roots and mapping borrowers must drain;
a blocked staged I/O call holds no mapping lease.

An already quiescent process is serviced in the BSP executor. Otherwise a
separate VM worker scans pending processes without blocking that executor.
The current one-second quiescence budget is an implementation choice, not an ABI
operation deadline. Failure returns BUSY before effects and preserves mappings
and backing. Competing mutation also returns BUSY. Final retirement requires
closed admission and zero root/borrow/request activity before reclamation.

BSP revalidates ALLOCATE's output before effects, then admits only the mutator's
root until its final reply copy and cleanup finish. ALLOCATE adds a disjoint
region; RELEASE touches no user memory after unmapping its captured request.
Stop/group termination still takes effect after the synchronous call unwinds.
Exactly one user task remains; this transaction does not enable siblings.

The heap, physical allocator and per-CPU scratch mappings retain their
[SMP contracts](smp.md#memory-and-output-boundaries). BSP zeroes frames using its
scratch slots; failed allocation unwinds partial backing before NO_MEMORY.

## Native use

[Libpyxis](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/memory.h) exposes `memory_allocate()` and
`memory_release()`, returning native statuses and validating allocation replies.
Allocation clears its output on failure. Release takes the region by value and
writes no output, so its descriptor need not outlive the call.

Hello uses temporary acquired regions for file reads, releasing each after use.
Its path workspace has process lifetime and is reclaimed on exit, after hello
has closed its memory handle. Libc suballocates these private regions; this service provides backing rather
than a userspace allocator.
