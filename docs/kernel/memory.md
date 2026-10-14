# Private userspace memory

The native [memory protocol](../../include/abi/memory.h) supplies backing for
userspace allocation without a userspace heap implementation. The boot launcher
explicitly grants a `memory` resource to each program. Discover it through
`startup_resource("memory")`; there is no ambient allocation syscall.

## Authority and lifetime

The service's MANAGE right permits allocation and release in the calling process.
The object holds no process pointer, address space or allocation list. A copied
or transferred grant lets its receiver manage that receiver's own private
regions; it never authorizes changes to the sender's mappings. Unknown rights
are rejected by the ordinary capability machinery.

Regions belong to the process. Closing a service handle, even the last handle
to the object, does not unmap anything. Releasing a region requires another
MANAGE grant. Final drained process teardown reclaims all remaining private
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

ALLOCATE_GUARDED takes nonzero usable backing bytes, rounds them up to pages and
adds one reserved unmapped lower guard page. It returns a `memory_reservation`:
an opaque identity and the base/size of the full range, including the guard.
The remaining suffix has eager zeroed RW/NX backing. Overflow, including the
added guard, is BAD_REQUEST. Failure publishes nothing. A guard does not prevent
a large stack jump from skipping its page.

RELEASE_RESERVATION takes only the identity and returns status without reply
bytes. Identity lookup applies to the calling process; a copied value grants no
authority over another process. Identities are never reused, including after
address reuse; stale or released identities are BAD_REQUEST. Identity exhaustion
is NO_MEMORY. Ordinary RELEASE cannot release guarded reservations, and guarded
identities cannot release ordinary allocations. Both live in the same
process-owned private-allocation registry. This registry and identity authority
are the foundation for the accepted future reservation family; general
reserve/commit/decommit operations remain unimplemented.

Native thread stack and FS contexts use guarded identities. A context pin
exclusively retains the whole usable suffix until native retirement; a pinned
reservation cannot be released and returns BUSY. Stack and FS identities must
be distinct. These pins hold no short mapping/activity lease and do not block
unrelated VM mutation. BSP acquires pins under the drained mutation token and
releases them after the task has left its root/stack and its continuations have
been reclaimed, before publishing native completion. Final process teardown
requires every context pin to have been released.

There is no resize, fixed-address request, permission change, reserve-only mode,
shared memory or per-region capability object. The later userspace allocator can
subdivide acquired regions without entering the kernel for each small allocation.

## Address-wait mapping identity

VM-owned private backing carries one non-reused incarnation per page; eligible
words are aligned 32/64-bit values in ordinary user RW RAM, including image data,
heap and stacks. Read-only and caller-owned/device mappings cannot enrol. This
identity is independent of a guarded reservation's authority or context pin.
Writable-to-writable protection preserves it; removing write access withdraws
waiters, and a later eligibility upgrade gets a fresh identity. Releasing backing
withdraws affected waiters before frames or virtual addresses are reused.

Token storage/counter exhaustion is preflighted before effects; failed provisional
backing burns assigned identities rather than reusing them. Current metadata costs
one 64-bit entry per privately backed page, including zero entries on read-only
pages (8 bytes/4096 bytes, about 0.2%, plus the allocation header). A derived
registration count skips bucket scans when no waits exist; it does not pin mappings.
BSP mutation drains active roots and short borrowers, so registration holds neither
through sleep. A successful unmap returns BAD_BUFFER to the detached waiters;
a failed mutation leaves registrations intact. See [address parking](../interfaces/threads.md#address-parking).

## Execution

All four operations capture their request, then park outside the private root
and kernel entry stack. BSP executes them under the process VM mutation
transaction shared with DISPLAY mapping changes. Dispatch reserves root activity
before CR3 activation; departure acknowledges it after loading the kernel root.
New admission closes during mutation, while previously admitted continuations
can finish bounded copies. Only active roots and mapping borrowers must drain;
a blocked staged I/O call holds no mapping lease.

An already quiescent process is serviced in the BSP executor. Otherwise a
separate VM worker scans pending processes without blocking that executor.
An ALLOCATE larger than one step (256 pages, tuning) always goes to the VM
worker, so the executor keeps forwarding other requests, including PCM
writes, while it runs.
The current one-second quiescence budget is an implementation choice, not an ABI
operation deadline. Failure returns BUSY before effects and preserves mappings
and backing. Competing mutation also returns BUSY. Final retirement requires
closed admission and zero root/borrow/request activity before reclamation.

BSP revalidates allocation output before effects, then admits only the mutator's
root until its final reply copy and cleanup finish. Allocation adds a disjoint
region; release touches no user memory after unmapping its captured request.
Stop/group termination still takes effect after the synchronous call unwinds,
including a stepped allocation, which first completes or unwinds.
Context pins protect registered backing independently of root/copy drainage.

The heap, physical allocator and per-CPU scratch mappings retain their
[SMP contracts](smp.md#memory-and-output-boundaries). BSP zeroes frames using its
scratch slots; failed allocation unwinds partial backing before NO_MEMORY.
The VM owns one full guarded reservation and one eager backed suffix. Whole
release and destruction free only that suffix; failure unmaps/frees only the
provisional backed prefix, leaving the full reservation bare for unwind.
Unmapping clears a run of leaf entries per page-table visit, and mapping
installs a run per visit; checking that a reservation is bare skips absent
tables and reads each present leaf table once.

ALLOCATE larger than 256 pages backs its region in steps of at most 256 pages
with interrupts off: allocate, zero and map. Between steps the VM worker
enables interrupts and yields to runnable work. A smaller ALLOCATE, and its
unwind after a failure, completes in one interrupts-off step and never yields,
so the executor can run it inline. The process VM transaction stays
exclusive throughout: admission is closed, the mutator is parked and sibling
threads are held, so no RELEASE, DISPLAY change or teardown of the same process
can interleave. The result stays all-or-nothing. On failure, the mapped prefix
is unmapped and freed from its end in the same bounded steps before NO_MEMORY
is returned, leaving the reservation bare for release. RELEASE still frees its
whole region in one step.

Final destruction of a retired space releases backing, range records and page
tables in bounded steps on the [BSP reaper](smp.md#scheduling-and-ownership).
Its memory therefore returns to the PMM some milliseconds after the last task
exits, not at once. A process that waits for the exit sees the memory freed,
but a concurrent large allocation elsewhere can briefly find it still in use.

## Native use

[Libpyxis](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/memory.h) exposes `memory_allocate()` and
`memory_release()`, returning native statuses and validating allocation replies.
Allocation clears its output on failure. Release takes the region by value and
writes no output, so its descriptor need not outlive the call.

Hello uses temporary acquired regions for file reads, releasing each after use.
Its path workspace has process lifetime and is reclaimed on exit, after hello
has closed its memory handle. Libc suballocates these private regions; this service provides backing rather
than a userspace allocator.
