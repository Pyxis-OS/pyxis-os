# Private userspace memory

The native [memory protocol](../../include/abi/memory.h) supplies backing for
userspace allocation without a userspace heap implementation. The boot launcher
explicitly grants a `memory` resource to each program. Discover it through
the named capability in the startup environment; there is no ambient allocation
syscall.

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
is NO_MEMORY. Ordinary RELEASE cannot release identity-owned reservations, and
identities cannot release ordinary allocations. Eager, guarded and sparse
allocations share the process-owned private-allocation registry.

RESERVE takes a nonzero page-rounded size and power-of-two alignment at least
one page, including 64 MiB. It returns an identity and an aligned disjoint user
range, without backing or a guard. Unused virtual space costs range/ownership
records only: no data frames, per-page token arrays or proportional page tables.
The loader's image-span limit does not cap these reservations.

COMMIT and DECOMMIT take a sparse identity and nonzero page-aligned contained
offset/size. They accept only identities created by RESERVE. A guarded identity
returns BUSY while pinned and BAD_REQUEST otherwise. COMMIT eagerly supplies
zeroed private RW/NX backing for holes while preserving existing bytes.
DECOMMIT discards backing, withdraws affected address waits and retains the
address range; recommit supplies zeros and new mapping incarnations. Both
capture their input before mutation and return status without a user reply copy,
so that input may itself be decommitted. Returning code/stack must remain mapped.

Sparse reservations own sorted committed extents of at most 256 pages, rather
than records for every unused page. Commit keeps new backing provisional until
it can publish without failure; exhaustion unwinds only the new pages. Decommit
prepares its possible interior split before any withdrawal/unmap. Failures
preserve old mappings, contents and identities. Whole RELEASE_RESERVATION walks
only committed extents, then frees the address range; stale identities refuse.

Native thread stack and FS contexts use guarded identities. A context pin
exclusively retains the whole usable suffix until native retirement; a pinned
reservation cannot be released and returns BUSY. Stack and FS identities must
be distinct. These pins hold no short mapping/activity lease and do not block
unrelated VM mutation. BSP thread-context clients acquire pins under the common drained mutation token
and release them after the task has left its root/stack and its continuations have
been reclaimed, before publishing native completion. Final process teardown
requires every context pin to have been released. Pin release is an atomic final
record access: after it, an AP mutation may free that record. Reservation and
address-wait identities use synchronized, non-reused global counters.

There is no resize, fixed-address request, permission change,
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
The exclusive mutation drains active roots and short borrowers, so registration holds neither
through sleep. A successful unmap returns BAD_BUFFER to the detached waiters;
a failed mutation leaves registrations intact. See [address parking](../interfaces/threads.md#address-parking).

## Execution

All MEMORY operations capture their request in retained per-task storage, then
switch to a kernel-root continuation in that same task on its calling CPU.
The permanent scheduler stack loads kernel CR3 and acknowledges departure before
running the continuation. Its kernel stack remains owned and mapped; no new
worker, per-call request allocation or BSP queue publication is involved.
Dispatch distinguishes this mode from an admitted syscall that still uses its
private root, and preserves FP/TLS state on every switch in either mode.

The common process VM transaction still serializes MEMORY with BSP DISPLAY and
thread-context changes. New root admission closes, previously admitted syscall
continuations may finish bounded copies, and roots/borrowers must drain before
effects. A blocked staged I/O call holds no mapping lease. The mutator parks on
its own joined waiter while drainage is pending; departures notify it directly.
Independent processes may mutate on different CPUs. Siblings still quiesce;
this slice adds no live-root mutation or targeted TLB shootdown.

Large eager backing and sparse commit/decommit/release yield between bounded
page/extent steps while retaining exclusive ownership. They execute on the
caller CPU, not the BSP VM worker. That worker remains for DISPLAY and native
thread-context clients. RESERVE stays independent of the unused virtual span.
The current one-second quiescence budget is an implementation choice, not an ABI
operation deadline. Failure returns BUSY before effects and preserves mappings
and backing. Competing mutation also returns BUSY. Final retirement requires
closed admission and zero root/borrow/request activity before reclamation.

The exclusive owner revalidates allocation/reservation output before effects, then admits only
the mutator's root until its final reply copy and cleanup finish. Allocation adds a disjoint
region; release touches no user memory after unmapping its captured request.
Stop/group termination still takes effect after the synchronous call unwinds,
including a stepped allocation, which first completes or unwinds.
Context pins protect registered backing independently of root/copy drainage.

The heap, physical allocator and per-CPU scratch mappings retain their
[SMP contracts](smp.md#memory-and-output-boundaries). The caller CPU zeroes frames
using its local scratch slots; failed allocation unwinds partial backing before NO_MEMORY.
VM backing claims and frees up to 64 arbitrary frames per PMM lock acquisition,
using transaction-owned stack vectors. Zeroing/mapping and pressure notification
run after unlock. A short claim is accounted immediately and follows the same
rollback; free validates the whole vector before clearing any bits. There is no
retained per-CPU free cache. The [matched record](../development/experiments/frame-batches/README.md)
covers lock cost, actual exhaustion and cross-CPU frees.
The VM owns one full guarded reservation and one eager backed suffix. Whole
release and destruction free only that suffix; failure unmaps/frees only the
provisional backed prefix, leaving the full reservation bare for unwind.
Unmapping clears a run of leaf entries per page-table visit, and mapping
installs a run per visit; checking that a reservation is bare skips absent
tables and reads each present leaf table once.

ALLOCATE larger than 256 pages backs its region in steps of at most 256 pages
with interrupts off: allocate, zero and map. Between steps its kernel-root
continuation services interrupts and yields to runnable work on that CPU. A
smaller ALLOCATE and its unwind complete in one interrupts-off step. The process
VM transaction stays exclusive throughout: admission is closed, the mutator
runs outside the private root and sibling threads are held, so no RELEASE, DISPLAY change or teardown of the same process
can interleave. The result stays all-or-nothing. On failure, the mapped prefix
is unmapped and freed from its end in the same bounded steps before NO_MEMORY
is returned, leaving the reservation bare for release. RELEASE still frees its
whole region in one interrupts-off step on the calling CPU; large eager and
guarded releases remain a latency tail. This task does not claim to bound them.

Final destruction of a retired space releases backing, range records and page
tables in bounded steps on the [BSP reaper](smp.md#scheduling-and-ownership).
Its memory therefore returns to the PMM some milliseconds after the last task
exits, not at once. A process that waits for the exit sees the memory freed,
but a concurrent large allocation elsewhere can briefly find it still in use.

Range records of the kernel space come from a static array of 4096 entries
(`VM_MAX_RANGES`), because early initialization cannot use the heap; each task
stack and separately allocated DMA buffer takes one, and exhaustion returns
`NO_MEMORY` (for example from task creation). Userspace spaces allocate their
records from the heap. See [technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1317).

Qualification and measured limits: [caller-CPU MEMORY](../development/experiments/memory-caller-cpu/README.md).

## Native use

Generated code uses the separate [JIT interface](../interfaces/jit.md), not an
execute flag on ordinary MEMORY. Its identity-owned allocations and contained
permission changes share the caller-CPU private transaction and final teardown.

[Libpyxis](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/memory.h) exposes `memory_allocate()` and
`memory_release()`, returning native statuses and validating allocation replies.
Allocation clears its output on failure. Release takes the region by value and
writes no output, so its descriptor need not outlive the call.

Hello uses temporary acquired regions for file reads, releasing each after use.
Its path workspace has process lifetime and is reclaimed on exit, after hello
has closed its memory handle. Libc suballocates these private regions; this service provides backing rather
than a userspace allocator.

## Sparse-memory limits

Extent steps bound backing, rollback and token work to 256 pages. Wait withdrawal
still scans the existing global buckets once per affected interval; that
interrupts-off tail is not bounded by the page-step budget. Every sparse address
WAIT/WAKE resolves its mapping token through a linear committed-extent lookup
(`owned_wait_token`); extents do not merge. A trimmed extent can retain up to
256 token entries. Reservation registry lookup is also linear, with no per-process
reservation-count limit; small empty reservations can exhaust kernel metadata.
Empty page tables remain until process destruction, including after failed
provisional commits. No user data backing remains from failed commits.

Internal VM statistics separate reserved/backed pages, extent records and retained
record/token bytes; they exclude heap headers, temporary operation bookkeeping
and page tables. Existing eager-memory profile totals exclude sparse
RESERVE/COMMIT/DECOMMIT; identity release retains its existing zero-byte accounting.
