# Shared-address-space user threads

Owner-accepted defaults, 2026-10-10, subject to the native-kernel condition below.
Task 1, process lifetime, is merged in
[#612](https://git.internal/PyxisOS/pyxis-os/pulls/612); exactly one user task per
process remains. Task 2, admitted call references and CLOSE, is merged in
[#671](https://git.internal/PyxisOS/pyxis-os/pulls/671);
[qualification](../development/experiments/threads-task2/README.md) records its
build, debugger and matched-workload evidence. Task 3, shared-table delivery, is
merged in [#684](https://git.internal/PyxisOS/pyxis-os/pulls/684);
[qualification](../development/experiments/threads-task3/README.md) records the
matched comparison and 1/4-CPU inspection. Task 4, VM activity and buffer leases,
is merged in [#700](https://git.internal/PyxisOS/pyxis-os/pulls/700); its
[qualification](../development/experiments/threads-task4/README.md) separates
nested-QEMU evidence from the owner-run native A/B. The owner accepts the BSP
MEMORY latency follow-up; bounded string capture and its native rerun are
complete. The owner-assigned IPC copy-phase follow-up is implemented on this
branch: descriptor capture/payload prevalidation share one short lease, and
RECEIVE reuses its publication lease. Inspected CALL lease pairs fall from 15
to 11 and SEND admission from 4 to 3; no lease spans growth or waits. Its
[qualification](../development/experiments/ipc-copy-leases/README.md) records
1/4-CPU inspection and nested/native A/B: SEND64 saves 13 ns/message; CALL cost
remains recorded in technical debt. Task 5 stays queued for its proposal. The remaining proposal
originally inspected Pyxis `4236efc7`
and userland `51bcb56b`. Existing
[task 1 qualification](../development/experiments/threads-task1/README.md)
and [stack qualification](../kernel/program-loading.md) remain separate evidence.

The owner already accepted parallel siblings, process-wide quiescence with
BSP-owned VM mutation, thread-local ordinary exit but process-wide exit/fault,
and process-private address wait/wake. Those directions and the three defaults
below are settled; detailed interfaces remain proposals, not implemented APIs.
Moving kernel services [off the BSP](scheduling-and-threads.md#serial-services-off-the-bsp)
is independent.

## Current gaps

| Inspected source | Remaining gate |
| --- | --- |
| [Process](../../include/kernel/process.h), [retirement](../../kernel/process.c) | Process owns group membership and lifetime, but its task/retiring-storage fields and terminal-result writer are still singular. |
| [Capabilities](../../kernel/object/capability.c), [syscalls](../../kernel/syscall.c) | Owned storage snapshots, rechecked grant captures and reserved atomic delivery are implemented. Final teardown still requires drained activity; sibling membership/entry remains task 5. |
| [Endpoint replies](../../kernel/object/endpoint.c) | Exact slots are reserved before admission; successful replies move owned grants after precommit destination validation. Scoped RECEIVE/same-receipt REPLY claims are implemented; sibling races need qualification. |
| [User copies](../../kernel/user_memory.c), [private memory](../../kernel/object/memory.c) | Short mapping leases and BSP mutation transactions are implemented; sibling runtime admission remains task 5. |
| [BSP requests](../kernel/bsp-service-requests.md), [SMP ownership](../kernel/smp.md#scheduling-and-ownership) | Owned inputs/claims and root/borrow drainage replace exclusive table/VM loans. Actual sibling execution and process membership remain later gates. |
| [Architecture](../../arch/x86_64/user.c), [startup](../../userspace/libc/start.S) | FS is saved per task, but starts at zero; no public FS setup or sibling entry exists. |
| [Linker](../../userspace/linker.ld), [P1F](../../include/pxe/p1f.h), [toolchain](../../toolchain/README.md) | No static TLS template contract; the current compiler rejects language TLS. |

## Native lifecycle

A native thread-management grant applies to the calling process, like MEMORY;
transferring it confers no control over the sender's process. CREATE supplies an
executable trampoline, argument, disjoint writable stack/context ranges and
initial user FS base. Validate mappings under the VM protocol, reserve task and
observer storage and reply capacity, then publish membership and runnable state
atomically against process/group stopping. Failure publishes nothing and returns
all provisional ownership. Siblings run the trampoline, never process startup or
constructors again. Per-task FP state remains separate, with the creator's
floating-point environment inherited, including exception flags.

Thread EXIT records a machine-word result and retires only that task. Returning
from its runtime trampoline runs ordinary thread destructors, then EXIT. Native
completion is waitable and repeatable: it publishes the result with
release/acquire ordering only after the task has left its root/stack and kernel
continuations, registrations and storage have been reclaimed. The observer owns
completion storage, not a surviving VM or borrowed task pointer. Closing it does
not kill the thread. Libc adds a single join claim, self-join refusal and runtime
stack/TLS release after completion; it need not detect arbitrary join cycles.

Process EXIT, returning from main, foreground termination and fatal user faults
stop every sibling. Propose one fault-safe atomic election/publication of the
first process-wide terminal reason, without taking the lifetime lock in fault
entry. Later events preserve it. Last ordinary thread exit supplies process
status zero only if no process-wide result exists. Process completion follows
all thread/process reclamation and admitted process activity. Deferred device
cleanup may remain worker-owned afterward; it delays group completion through
attributed cleanup tokens, not process completion.
Group membership stays per process; SEAL prevents new processes but not internal
threads of an existing member. Stopping closes CREATE admission before sampling
siblings. No asynchronous cancellation, per-thread fault isolation or signals.

## Shared capability and VM ownership

**Capability admission — task 2 implemented.** Under short table exclusion,
`capability_acquire` resolves the exact generation into immutable rights and
transport snapshots plus an owned object-storage reference. Dispatch, secondary
handles and blocking cleanup retain no entry pointer. Storage references add no
grant authority: controller, terminal, console and endpoint logical close effects
remain separate. Pipe grants now count open directions independently of storage.
CLOSE detaches the slot and advances its generation under the guard, then uses
the detached grant's existing ownership for logical close and grant release
outside it. It needs no additional retain that could fail at saturation.
Table teardown uses the same close effects after detachment. No table guard spans
an object callback, allocation, user copy, wait or scheduling.

**Shared-table delivery — task 3 implemented.** BSP growth prepares backing
outside the guard, rechecks capacity and copies current entries/reservations
under it, then frees old backing after unlocking. Installation claims exact
slots/generations, hidden from lookup and competing installers. Operations own
capacity before commit or sleep, consume it once through atomic publication of
already-owned grants, and release unused/failure claims. BSP requests carry owned
inputs and destination claims instead of exclusive table loans; caller activity
keeps the table alive through completion. No entry or remote-stack pointer is
retained. Allocation and process owner-list mutation remain BSP-owned.

Storage and grant references drain before `task_syscall_leave`; existing
uninterruptible request and stop/unwind rules retain process/table lifetime.
Teardown requires no live claims. Task 4 supplies VM/activity and device-operation
admission; actual sibling membership, entry and race qualification remain later
work.

**Task 3 accepted details, 2026-10-10.** COPY, launch sources and endpoint
attachments retain prospective grant authority outside the table guard, then
recheck exact source generation, object and authority under it. Stale sources
fail and unwind; successful capture owns a grant surviving later source CLOSE.
Ordinary CALL/readiness keep storage-only references. Successful REPLY validates
destination policy before committing completion, then moves already-owned grants
into reserved slots without another retain. Rejection leaves the receipt for
correction; successful REPLY cannot fail collection for capacity. This never
implicitly rolls back provider mutations and preserves timeout/closure
uncertainty. [Task 3 qualification](../development/experiments/threads-task3/README.md)
records the baseline and later implementation evidence separately.

**Task 4 accepted contract, 2026-10-10.** MEMORY mutations move to BSP with
DISPLAY mapping mutations under one process transaction. Drain active roots and
actual mapping borrowers, acknowledge departure after CR3 reload, and keep
admission closed through the mutator's final reply copy. Pending quiescence never
blocks the BSP executor. Bounded failure returns BUSY before effects; retirement
waits for drainage. This is implemented with exactly one user task retained.

Use kernel staging and short validation/copy leases, with no mapping lease across
indefinite I/O. Revalidate output after waits; late failure is BAD_BUFFER without
rolling back consumed input or committed effects. Publish returned handles under
the output lease, or release only the operation's exact returned generations.
Refuse unsupported concurrency with BUSY before effects, per native object or
session: endpoint RECEIVE/same-receipt REPLY, keyboard/pointer readers and control
transactions, and clipboard ownership. Preserve existing network guards and
CLOSE/stop semantics; do not serialize all process syscalls.

**VM admission — task 4 implemented.** Dispatch reserves root activity before
CR3 activation and acknowledges departure only after kernel-root reload on the
permanent stack. New syscall continuation admission is atomic with gate closure;
old continuations may finish bounded copies during DRAINING. No borrower crosses
an I/O wait or root departure. Already quiescent mutation runs directly in the
BSP executor; otherwise the VM worker scans pending processes without blocking
that executor. MUTATING permits no process root; REPLY permits only its exact
mutator until final output and cleanup finish. The current one-second precommit
budget is tunable implementation policy; failure detaches pending references and
returns BUSY without effects. Unsubmitted loading and final drained retirement
retain separate ownership. PCID/global pages remain off; the kernel flush IPI is
not this process protocol.

Endpoint RECEIVE and same-receipt REPLY, keyboard/pointer readers and control
transactions, clipboard receiver ownership and HCI RECEIVE/RELEASE have scoped
refusals. Existing console/file/pipe queues, network-direction guards and audio
session generation checks remain. Inputs and worker buffers are kernel-owned;
late output failure never leaves undisclosed newly returned handle generations.
Device sessions stay process-owned; CLOSE/stop still revokes logical ownership
and lets admitted operations unwind. Exactly one user task remains. Source audit
and ordinary 1/4-CPU inspection do not claim tested sibling races; task 5 must
qualify them before exposing concurrent entry.

## TLS and runtime synchronization

Accepted TLS scope: static executable TLS with initialized template, zero tail and alignment,
with an x86-64 local-exec layout and FS-addressed libc thread record. Establish
the initial FS/errno record through allocation-free bootstrap before TLS-using C,
malloc or constructors; siblings receive prepared FS before user entry. Kernel
validates user addresses/lifetime; libc owns the TCB layout. Kernel GS remains
CPU-local; user FSGSBASE stays disabled. errno becomes per thread.

A focused compiler/LLD probe must settle template bounds, relocations and
bootstrap order before implementation. Prefer template bytes/bounds in ordinary
load segments if valid; do not assume a P1F change is necessary. Language TLS
requires an owner-published LLVM fork/container. Dynamic TLS/modules are later.
Runtime keys and bounded ordinary-exit destructor passes are separate from
language TLS. Forced exit/fault promises no user destructor execution.

Native WAIT compares an aligned word and enrolls atomically under wait-bucket
exclusion; mismatch never sleeps. Keys include process, address and mapping
incarnation. Sleeping releases mapping leases; unmap, timeout, stop and wake
remove registrations before notification/address reuse. Native deadlines are
monotonic, with overflow checks. Wake does not transfer ownership; callers
recheck predicates. No cross-process futex ABI, requeue or priority inheritance.

| Libc primitive | Required behavior |
| --- | --- |
| Mutex | Acquire/release fast path, real contended parking, explicit destruction/busy rules; no lock bootstrap allocation. |
| Condition | Capture the wake sequence while holding the mutex, unlock, then compare-and-park without a lost wake; predicate loop and mutex reacquisition on wake or timeout. Match advertised pthread clock semantics, never reinterpret wall time as monotonic. |
| Semaphore | Atomic bounded count, overflow refusal and real timed waiting. |
| Once | One initializer, run callbacks outside internal registry locks, release/acquire publication to all waiters. |

Pthreads and C11 threads are libc layers over lifecycle and parking, not new
POSIX-shaped kernel calls. Initially advertise only implemented create/exit/join,
self/equal, synchronization, keys/destructors and selected attributes. No successful
no-op locks, cancellation, robust owner-death recovery or scheduling promises.
Detached threads require a separate bounded collection contract before their
API or a consumer requiring it is enabled.

## Libc safety gate

| Shared state | Work required before ordinary threaded consumers |
| --- | --- |
| malloc/TLSF pools | Allocation-free lock bootstrap, metadata/pool growth exclusion and cross-thread free/realloc; no allocator lock held while quiescence needs its owner to run. |
| Descriptors | Stable admitted-operation references separate from table storage; close/unwind lifetime plus cursor/read-ahead serialization. No table lock across I/O; preserve uncertain-close behavior. |
| FILE and DIR | Per-stream serialization and open/list/close lifetime; define FILE-to-descriptor ordering. For DIR, document same-object caller serialization unless operation locking is supplied. |
| Exit handlers/startup | Initialize once; synchronize registrations and run callbacks outside locks. Require workers joined before orderly process finalizers; force-stopping a mutex owner can deadlock callbacks. Kernel forced teardown cannot depend on them. |
| Time/timezone/rand | Protect generator/cache publication and borrowed timezone strings; document static time-result lifetimes and provide safe reentrant paths. |
| C++ runtime | Replace disabled-thread exception state, guards/once, fallback allocator and unwinder assumptions before enabling libc++ threads. |

Immutable startup getenv values and the permanent C-only locale need no invented
mutable state or new locks. Synchronizing syscalls alone does not protect libc
userspace state, and synchronizing malloc alone does not protect FILE lifetime.

## Placement and stack limits

Siblings inherit the process space's effective CPU set and use existing least-load
placement/user migration. Kernel continuations keep their assigned CPU. First
sibling creation closes space affinity setup, which currently assumes a sole
init task. No per-thread affinity, priorities or CPU-time fairness change: more
runnable threads currently gain more round-robin shares.

Accepted libc default: **1 MiB eager RW/NX backing plus one reserved unmapped
lower guard**, independently allocated per sibling; no fixed arena or demand
fault growth. #617's 8 MiB eager initial-stack experiment increased memory and
launch costs, so that is not the default. Initially accept explicit libc sizes
using the existing page-aligned 1–8 MiB policy. Native caller-owned regions remain
available for other runtimes and are not restricted to libc's convenience range.

MEMORY currently cannot reserve an unmapped guard; guarded-region admission
must be supplied deliberately before these stacks are advertised. CREATE pins
registered stack/TLS context backing until native completion; overlapping RELEASE
returns BUSY. These lifetime reservations are distinct from short VM access
leases: unrelated mutation drains root/copy activity, not the thread's lifetime.
Join then releases libc-owned regions. Never free an exiting thread's own stack
from its trampoline. An unjoined record counts against capacity; final process
teardown reclaims remaining backing. Guards do not prevent a large stack jump
from skipping their page.

Accepted initial resource policy: **64 live/retiring native threads and 64
unreclaimed libc context records per process**, both including the initial
thread. Reserve before publication and release each count only at its respective
completion/reclamation boundary. Exhaustion returns a real limit/allocation
error. These tunable bounds allow roughly 64 MiB of default user stacks and
1 MiB of 16 KiB kernel stacks, plus TLS/metadata; they guarantee no physical
capacity. Large native contexts remain subject to VM/allocation admission.
The 1 MiB default and 64/64 bounds are tunable implementation policy, not ABI
or SDK contracts. Consumers must handle real limit/allocation errors rather
than assume these values; Go or libuv may need different tuning later.

## Delivery gates

Each gate needs its own owner assignment, baseline where costs change, ordinary
build and interactive QEMU/debugger qualification. No new test infrastructure.
The following split refines the former broad task 2; completed gates do not
authorize the next one.
Tasks 2–4 are ownership plumbing with no new runnable thread feature. The first
owner-runnable siblings arrive at task 5; ordinary threaded C arrives at task 8.
SDL audio callbacks and libuv workers require their later consumer tasks, not
merely completion of task 2.

1. [x] **Process lifetime (#612).** Independent process/group ownership and task retirement, still one user task.

   Owner can inspect process completion after task/process reclamation and group completion after deferred cleanup using existing programs.
2. [x] **Admitted call references and CLOSE.** Atomic handle resolution into owned storage/rights, detached-generation close and audited logical close effects; one user task and no public API change. See [qualification](../development/experiments/threads-task2/README.md).

   Owner can inspect call/close lifetime and run ordinary applications without borrowed-slot storage surviving a call.
3. [x] **Shared-table delivery.** Growth/atomic installations, reply slot reservations, readiness/COPY and BSP table-loan replacement; still one user task. See [qualification](../development/experiments/threads-task3/README.md).

   Owner can inspect preserved authority, delivery-capacity admission and failure unwind before siblings are enabled.
4. [x] **VM activity and user-buffer leases.** Dispatch/root departure, BSP quiescent mutation, blocking/committing operation audit and explicit concurrent-device refusals; still one user task. See [qualification](../development/experiments/threads-task4/README.md).

   Owner can inspect safe copy/mutation/retirement admission and matched ordinary-workload costs.
5. [ ] **Native CREATE/EXIT/completion.** Thread set/result election, guarded-context allocation/lifetime, initial/sibling FS setup and space placement; joinable only.

   Owner can run minimal native sibling probes with distinct stacks/FS and shared CR3, then join; ordinary threaded libc is not ready yet.
6. [ ] **Address WAIT/WAKE.** Mapping-incarnation keys, deadlines and wake/stop/unmap races.

   Owner can run real contended native parking and inspect clean registrations after timeout or process stop.
7. [ ] **Static TLS and errno.** Qualify compiler/LLD template/bootstrap, publish the matching compiler if needed, then integrate SDK/startup/runtime keys.

   Owner can inspect distinct language TLS/errno and ordinary thread destructors on native siblings.
8. [ ] **Thread-safe libc and C11/pthread profile.** Allocator, descriptors, stdio/caches/shutdown and real mutex/condition/semaphore/once; C++ adapter after its runtime audit.

   Owner can use the documented thread profile and shared allocation/I/O, without unsupported detach/cancellation promises.
9. [ ] **Separate consumer tasks.** SDL workers/timers then audio callbacks; libuv pool, Hax and Go each retain their own adapter/qualification scope.

   Owner can enable each qualified consumer independently; threads alone do not complete these ports.

[SDL audio](../development/sdl2.md) gains callbacks, not real-time guarantees. [libuv/Neovim](neovim-libuv.md)
needs workers and synchronization; Neovim's first slice continues to avoid them.
[Hax](claude-on-pyxis.md#track-2-hax-inventory-after-the-lua-harness) additionally
needs timed conditions, drainers and a qualified concurrent TLS transport.
[Go](go-runtime.md) needs native thread/TLS/parking plus its own stack/GC/VM
adaptation; GOMAXPROCS=1 does not eliminate OS threads, and cgo-disabled Go need
not go through pthreads. No consumer is delivered by this proposal.

Qualify later changes with matched/interleaved existing launch/IPI workloads and
one-/four-CPU QEMU inspection. At native sibling delivery inspect shared CR3,
distinct stack/FS, final reclamation, concurrent close/reply/unmap,
process fault/stop and allocation failure. Qualify distinct language TLS/errno
after task 7. Measure creation/join, uncontended/contended primitives, VM pause
and consumer workloads, distinguishing
nested-VM evidence from owner-native results. Kernel/ABI/SDK changes belong in
Pyxis, libc/libpyxis/startup in userland, adapters in ports; publish dependency
PRs before gitlinks. This docs PR changes no pin or compiler container.

## Accepted owner decisions (2026-10-10)

The earlier accepted execution, lifetime and parking directions remain unchanged.
The owner accepted all three defaults with this condition, recorded verbatim:

> as long as we don't implement posix threads in the kernel and still keep things pyxis native

The kernel exposes native thread, parking and TLS primitives only. Pthreads and
C11 threads are userland libc layers over them, like the rest of libc. No
POSIX-shaped kernel calls, futex clone or signals-based cancellation. Acceptance
does not assign later implementation; tasks 2 and 3 were separately assigned.
Task 4 and its IPC follow-up are separately owner-assigned. The owner queued task
5 for after the follow-up PR and native A/B: propose its concrete scope and open
decisions, then stop for owner review before implementation.

1. **Stack/admission policy — accepted:** eager guarded 1 MiB libc stacks, explicit
   1–8 MiB sizes, and both tunable 64-entry bounds above, outside the ABI/SDK
   contract. Native caller-owned stack sizes remain independent.
2. **TLS boundary — accepted:** static executable local-exec TLS plus runtime keys;
   settle template/FS bootstrap with a focused compiler probe before changing the
   fork, prefer ordinary load segments when possible. Dynamic/module TLS is later.
3. **First runtime profile — accepted:** joinable threads and only the C11/pthread
   subset implemented by its delivery task, with workers joined before orderly
   process finalizers.
   Detach/automatic collection and asynchronous cancellation stay unadvertised
   until their lifetime/recovery contracts are separately accepted and qualified.
