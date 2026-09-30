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

## Address-space handoff

VM mutation and physical/heap allocation remain on the BSP with interrupts
disabled. A memory call cannot use the existing early-publication pattern of a
capability-table growth request: the requester must stop using its private root
before the BSP may modify it.

The memory subsystem captures its operation, process loan and region in a typed
BSP request in the caller's eagerly allocated reusable request area. Preparation
zeroes that typed record and reserves the area through result consumption;
submission allocates nothing. Persistent profiling uses a separate caller-only
allocation, and resource wait links remain in task metadata. The closed service
catalog requires deferred submission: after moving
to the permanent stack, activating the kernel root and clearing entry/current-task
state, the scheduler marks the task parked and publishes the request to the common
FIFO. Publication lends exclusive private-VM ownership to the BSP. Neither the
old CPU nor any other task accesses that address space during the loan.

The BSP executor changes the inactive space outside the request-queue lock with
interrupts disabled, stores results in the typed record and clears the process
loan. Completion precedes waking, which returns ownership; the executor makes
no further access to that request or caller. Normal resumption reloads CR3 before
touching the saved task stack. Only then does the allocation handler write its already
validated user reply. Allocation cannot invalidate that reply because it adds
a disjoint range. Release requires no user-memory access on resumption. The
caller merges any timing sample into its profile and explicitly releases the
request reservation after consuming the result; only then may another service
reuse the area.

The same path handles BSP userspace. Publication wakes the executor through the
ordinary ready queue; there is no memory-specific scheduler sweep. No remote
stack access, shared user mappings or TLB shootdown is introduced. This depends on the current
single-task process model and pinned tasks. Group termination preserves this
uninterruptible loan: the syscall continuation collects the result and releases
the reservation before its task can retire.

## Native use

[Libpyxis](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/memory.h) exposes `memory_allocate()` and
`memory_release()`, returning native statuses and validating allocation replies.
Allocation clears its output on failure. Release takes the region by value and
writes no output, so its descriptor need not outlive the call.

Hello uses temporary acquired regions for file reads, releasing each after use.
Its path workspace has process lifetime and is reclaimed on exit, after hello
has closed its memory handle. The libc allocator choice remains the next task;
this service implements only private backing.
