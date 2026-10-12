# Shared-address-space user threads

Owner-accepted defaults, 2026-10-10, subject to the
[native-kernel condition](#accepted-owner-decisions-2026-10-10). The earlier accepted
directions (merged #567) stand: parallel siblings, process-wide quiescence with
BSP-owned VM mutation, thread-local ordinary exit but process-wide exit/fault, and
process-private address wait/wake. Tasks 1–5 are merged. Gate 6 is merged (#731)
and QEMU-qualified; owner-native A/B is recorded, with CALL64 fastest about 100 ns
higher.
Gates 7–9 remain later delivery work. Moving kernel services
[off the BSP](scheduling-and-threads.md#serial-services-off-the-bsp) is independent.

Merged tasks, with qualification records:

- Task 1, process lifetime: [#612](https://git.internal/PyxisOS/pyxis-os/pulls/612),
  [qualification](../development/experiments/threads-task1/README.md).
- Task 2, admitted call references and CLOSE: [#671](https://git.internal/PyxisOS/pyxis-os/pulls/671),
  [qualification](../development/experiments/threads-task2/README.md).
- Task 3, shared-table delivery: [#684](https://git.internal/PyxisOS/pyxis-os/pulls/684),
  [qualification](../development/experiments/threads-task3/README.md).
- Task 4, VM activity and buffer leases: [#700](https://git.internal/PyxisOS/pyxis-os/pulls/700),
  [qualification](../development/experiments/threads-task4/README.md); the BSP MEMORY
  latency and string-capture follow-ups are complete, and the IPC copy-phase
  follow-up is [#704](https://git.internal/PyxisOS/pyxis-os/pulls/704)
  ([qualification](../development/experiments/ipc-copy-leases/README.md); the remaining
  CALL cost is in [technical debt](../technical-debt.md), profiling parked until task 8).
- Task 5, native sibling lifecycle: [#709](https://git.internal/PyxisOS/pyxis-os/pulls/709),
  [qualification](../development/experiments/threads-task5/README.md). **Open:** the
  native SEND4096 completion regression (about +26%) is under follow-up and the
  owner's native rerun is pending; no improvement is claimed before it.

[Stack qualification](../kernel/program-loading.md) remains separate evidence.

## Current gaps

| Inspected source | Remaining gate |
| --- | --- |
| [Process](../../include/kernel/process.h), [retirement](../../kernel/process.c) | Native siblings are implemented; libc join accounting remains task 8. |
| [Capabilities](../../kernel/object/capability.c), [syscalls](../../kernel/syscall.c), [endpoint replies](../../kernel/object/endpoint.c) | Owned storage snapshots, reserved atomic delivery and scoped RECEIVE/same-receipt REPLY claims are implemented; sibling races need qualification. |
| [User copies](../../kernel/user_memory.c), [private memory](../../kernel/object/memory.c) | Short mapping leases, BSP mutation transactions and guarded-context pins are implemented; general sparse reservations remain a separate task. |
| [Architecture](../../arch/x86_64/user.c), [startup](../../userspace/libc/start.S) | Sibling FS/entry and libc initial-thread TLS bootstrap are implemented; general runtime safety remains task 8. |
| [Linker](../../userspace/linker.ld), [P1F](../../include/pxe/p1f.h), [toolchain](../../toolchain/README.md) | Static local-exec TLS template/P1F contract is qualified in gate 7; dynamic TLS remains unsupported. |

## Native lifecycle

**Task 5 decisions accepted and implemented ([#709](https://git.internal/PyxisOS/pyxis-os/pulls/709)), 2026-10-11.** Kernel-side behavior is also in [userspace](../kernel/userspace.md), [SMP](../kernel/smp.md) and [BSP requests](../kernel/bsp-service-requests.md).

1. Guarded backing uses MEMORY MANAGE on the calling process: eager RW/NX
   usable pages and one reserved unmapped lower guard. It shares private-memory
   ownership records with ordinary allocation, returning a process-owned opaque
   generational identity plus full reserved base/span. Whole identity release
   refuses BUSY while registered stack/FS backing is live or retiring, until
   native completion. Task 5 pins the whole usable extent; stack and FS records
   are distinct. The [accepted reservation family](go-runtime.md#accepted-private-vm-reservationcommit-contract)
   is not assigned: RESERVE followed by suffix COMMIT will later subsume this
   eager convenience operation under the same identity/record/authority model,
   without a competing reservation owner. Ordinary ALLOCATE/RELEASE stay intact;
   task 5 does not implement general COMMIT/DECOMMIT or sparse backing.
2. The current thread may bind its registered FS context once; CREATE supplies
   sibling FS before entry. Existing programs retain FS=0; rebinding returns
   BUSY. Libc TLS layout/startup remains task 7, coordinated separately.
3. Native completion distinguishes ordinary EXIT's machine-word value from
   forced process stop. It is waitable and repeatable; closing the observer never
   kills the thread. Publish only after retirement and context-pin release.

The [native ABI](../../include/abi/thread.h) exposes a `threads` MANAGE service
and WAIT-only completion observers. Explicit `session` delegation carries this
optional service; ordinary foreground/background applications do not receive it.
No pthread/C11 kernel API, address parking or ordinary thread-safe libc is part
of task 5. Bundle resource tables (`lib/bundle.c` in userland and `tools/pxa.py`)
do not yet admit `threads`; update both together when the first bundle consumer
needs it, such as libuv workers or SDL. This is deferred, not part of task 5.

| Operation | Implemented contract |
| --- | --- |
| MEMORY ALLOCATE_GUARDED / RELEASE_RESERVATION | Usable byte count becomes eager RW/NX backing plus one lower guard; full reserved base/span and identity returned. Whole identity release, no sparse commit/decommit. |
| THREAD CREATE | Complete zeroed message with entry, argument, guarded stack identity, optional distinct FS identity/base. RDI=argument, RSP=16-byte-aligned usable stack end, no return address. Returns one completion handle. FS=0 requires no context. |
| THREAD SET_FS | Bind current thread once; nonzero base within or one past its usable guarded context, allowing negative TLS offsets. Identity/base must be nonzero. Rebinding returns BUSY. |
| THREAD EXIT / completion WAIT | EXIT word retires only the caller and does not return on success. Repeated WAIT returns EXITED/value or STOPPED/zero; observers also support WAIT_COMPLETE readiness. |

A native thread-management grant applies to the calling process, like MEMORY;
transferring it confers no control over the sender's process. CREATE supplies an
executable trampoline, argument, disjoint writable stack/context ranges and
initial user FS base. Validate mappings under the VM protocol, reserve task and
observer storage and reply capacity, then publish membership and runnable state
atomically against process/group stopping. Failure publishes nothing and returns
all provisional ownership. Siblings run the trampoline, never process startup or
constructors again. Per-task FP state remains separate, with the creator's
floating-point environment inherited, including exception flags.

The native trampoline must call EXIT explicitly; runtime destructors belong to the
later libc/TLS tasks. Completion publishes the result with release/acquire
ordering only after the task has left its root/stack and its continuations,
registrations and storage have been reclaimed. The observer owns completion
storage, not a surviving VM or borrowed task pointer. Libc adds a single join
claim, self-join refusal and runtime stack/TLS release after completion; it need
not detect arbitrary join cycles.

Process EXIT, returning from main, foreground termination and fatal user faults
stop every sibling. One fault-safe atomic election/publication preserves the
first process-wide terminal reason, without taking the lifetime lock in fault
entry. Later events preserve it. **Owner-accepted rule, 2026-10-11:** when the
last member leaves via THREAD_EXIT without a prior process result, the process
completes as PROCESS_EXITED with status 0 (pthread-like), regardless of that
thread's return word. Process completion follows all thread/process reclamation
and admitted process activity. Deferred device cleanup may remain worker-owned
afterward; it delays group completion through
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
Teardown requires no live claims.

**Task 3 accepted details, 2026-10-10.** COPY, launch sources and endpoint
attachments retain prospective grant authority outside the table guard, then
recheck exact source generation, object and authority under it. Stale sources
fail and unwind; successful capture owns a grant surviving later source CLOSE.
Ordinary CALL/readiness keep storage-only references. Successful REPLY validates
destination policy before committing completion, then moves already-owned grants
into reserved slots without another retain. Rejection leaves the receipt for
correction; successful REPLY cannot fail collection for capacity. This never
implicitly rolls back provider mutations and preserves timeout/closure
uncertainty.

**Task 4 accepted contract, 2026-10-10.** MEMORY mutations move to BSP with
DISPLAY mapping mutations under one process transaction. Drain active roots and
actual mapping borrowers, acknowledge departure after CR3 reload, and keep
admission closed through the mutator's final reply copy. Pending quiescence never
blocks the BSP executor. Bounded failure returns BUSY before effects; retirement
waits for drainage. Task 4 first qualified this with one user task; task 5
enabled siblings under the same admission protocol.

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
and lets admitted operations unwind. Task 5 qualifies bounded sibling CALL/CLOSE
and live-context RELEASE interleavings on 1/4 CPUs; broader device/VM race
coverage remains with later delivery tasks.

## TLS and runtime synchronization

Gate 7 defaults accepted by the owner, 2026-10-12; implementation assigned:

- Executable-only local-exec TLS uses x86-64 variant II, with the libc TCB self
  pointer at FS:0. The SDK linker exposes initialized template bytes, zero-tail
  size and alignment. LLD validates the intermediate ELF PT_TLS and omits that
  header from P1F; the template remains in ordinary load segments. Dynamic TLS
  and modules remain out of scope. Qualify the compiler/linker contract before
  adopting the matching owner-published compiler container.
- Libc owns FS. A TLS-free bootstrap prepares guarded MEMORY backing and binds
  it before stdio, malloc or constructors. Startup supplies narrow caller-only
  SET_FS authority, without implicit MANAGE or CREATE authority. Binding is once;
  bootstrap failure exits honestly. errno is a TCB field exposed by an accessor.
  Kernel GS remains CPU-local and user FSGSBASE disabled; the kernel validates
  addresses and pins backing, while libc owns its layout. Sibling contexts live
  through native completion before their collector releases them.
- Runtime keys are process-local and generational, initially 128 slots (tunable
  policy, not ABI). Ordinary thread exit drains language destructors in reverse
  registration order, then runs at most four key-destructor passes, clearing a
  value before its callback and calling outside registry exclusion. FS stays
  alive through atexit, .fini_array and stdio shutdown. Forced stop/fault executes
  no user destructors. The [managed libc profile](../userland/threads.md) builds on this native bridge.

Native WAIT compares an aligned word and enrolls atomically under wait-bucket
exclusion; mismatch never sleeps. Keys include process, address and mapping
incarnation. Sleeping releases mapping leases; unmap, timeout, stop and wake
remove registrations before notification/address reuse. Native deadlines are
monotonic, with overflow checks. Wake does not transfer ownership; callers
recheck predicates. No cross-process futex ABI, requeue or priority inheritance.

### Gate 6 contract (owner accepted, 2026-10-11)

Owner assigned gate 6 and accepted all three binding/lifetime defaults on
2026-10-11; implemented in merged #731 and QEMU-qualified. Native A/B records
about 100 ns added to CALL64 fastest; see qualification.
Scope is native address parking only, over the
existing caller-scoped `threads` MANAGE service. No pthread/C11 kernel surface,
libc synchronization/TLS implementation, cross-process keys or new klog lines.

1. **Native binding and outcomes — accepted.** Add address WAIT/WAKE
   operations to the existing thread service, comparing naturally aligned 32- or
   64-bit words in the calling process's ordinary VM-owned writable user RAM.
   No display/device/caller-owned mappings. WAIT captures address, word width,
   expected value and an absolute monotonic deadline; unused bits/fields reject.
   Mismatch returns WOULD_BLOCK without enrolling; explicit wake returns OK;
   expiry returns TIMED_OUT and mapping withdrawal returns BAD_BUFFER. WAKE
   captures address/width and a nonzero maximum count, returning actual detached
   waiters (a maximum machine word requests all). It validates the current mapping
   incarnation; zero matches is ordinary success. Zero deadline polls and the
   maximum machine word means indefinite wait; finite deadlines do not refresh.
   Compare/enroll precedes expiry so mismatch never sleeps. Stop resumes native
   cleanup and never returns to userspace. Wake transfers no user lock ownership;
   clients use atomics and recheck predicates. Existing late-output rules apply
   to WAKE's returned count; a failed output does not undo already committed wakes.
2. **Mapping identity — accepted.** Key by process, exact word address/
   width and a non-reused VM-owned per-page incarnation. Assign tokens with user
   RAM backing, covering image data/BSS, heap and stacks, not only MEMORY service
   allocation records. Read/copy the token under a short mapping lease and retain
   no range pointer or lease during sleep. Invalidate affected pages before actual
   unmap/free or eligibility loss, detach their waiters, and assign fresh tokens
   on reback; unrelated pages keep their identities. Failed mutation preflight
   changes neither mapping nor registration. Preflight token storage/counter
   exhaustion before effects. This is compatible with later partial decommit;
   a physical page, reused range pointer or process-wide epoch is not the key.
3. **Terminal races — accepted.** Keep the accepted detach-before-notify/
   address-reuse rule literally for wake, timeout, stop and successful unmap.
   Resolve one terminal reason under parking-bucket exclusion, remove the link,
   then notify; retain task/process storage through cancellation and cleanup.
   Existing generic timed/stop sleeps notify before resource detachment, so they
   cannot be reused unchanged and described as meeting this rule. Add only the
   parking-specific cancellation integration needed, with no scheduler-queue to
   bucket lock inversion against enrollment. Committed wake/timeout/withdrawal
   is not replayed; permanent process stop still prevents any user return.

The [qualification record](../development/experiments/threads-task6/README.md)
contains the pre-code baseline at main `d124a053` (NVMe shutdown #710 included),
interleaved 1/4-CPU IPC/launch captures with one unchanged initrd, ordinary source
builds and interactive GDB evidence for contended siblings, mismatch/early wake,
timeout/stop/unmap/remap, process-key isolation and real allocation failure.
Linked registrations/pending expiry lists drain; no mapping lease survives sleep.
[Userland #221](https://git.internal/PyxisOS/pyxis-userland/pulls/221) supplies
native helpers and extends the existing manual threadprobe. Merge it before the
parent gitlink. Owner-native A/B is recorded; no native speedup is claimed.
The owner-approved scheduler-pointer follow-up reuses the permanent loop
context for expiry checks (#733, merged). Native A/B shows at most 25 ns recovered,
within noise; gate 6's CALL64 cost mostly remains, with no further optimization
assigned. See the
[follow-up record](../development/experiments/threads-task6/README.md#scheduler-pointer-follow-up).
Gates 7/8 remain separate.

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

### Gate 8 contract (owner accepted, 2026-10-12)

1. **Libc-owned creation and collection.** A process-local generational registry
   reserves one of the accepted 64 records before publication, including the
   initial record. Libc-created siblings own their guarded stack, TLS context
   and native completion observer until one joiner observes retirement and
   collects them. Initial/raw-native joins are refused; self/equal and initial
   thread exit remain available. Libc retains only supplied threads, memory and
   clock grants, never widening caller-only startup SET_FS authority. Ambiguous
   CREATE publication terminates the process without retry or premature cleanup.
2. **Explicit synchronization profile.** Process-private normal/recursive
   mutexes, conditions (including monotonic attributes), unnamed semaphores,
   once and keys use real native address parking. C11/default pthread absolute
   UTC deadlines are checked against the existing clock and translated to
   monotonic parking; missing time/authority and overflow fail honestly. Detach,
   cancellation, robust/process-shared objects, scheduling and C11 yield are
   refused. Enable C++ threads only after exception state, guards, fallback
   allocation and unwinder audit, including real rwlocks for the latter.
3. **Preserved close and pointer lifetime.** Admit descriptor operations into
   stable libc storage without retaining extra native grants or postponing final
   native CLOSE. Preserve uncertain-close rules. Serialize FILE operations;
   callers exclude new use during FILE/DIR destruction, with same-DIR operations
   caller-serialized. Internal cwd/environment users take owned snapshots;
   borrowed results require caller exclusion through their use. Registry/cache
   guards do not span blocking I/O, allocation growth or callbacks. Timezone
   snapshots retain escaped designations through process lifetime.

The gate 8 baseline at `ce8c53d1` was captured before gate 8 code on one/four
CPU nested QEMU using matching successful CI bundles; raw commands, hashes and
captures remain local. Native gate 7 launch cost is accepted and its static
initial-TLS optimization remains a much-later follow-up. Gate 8 does not deliver
consumer adapters or introduce POSIX-shaped kernel operations.

## Libc safety gate

| Shared state | Work required before ordinary threaded consumers |
| --- | --- |
| malloc/TLSF pools | Allocation-free lock bootstrap, metadata/pool growth exclusion and cross-thread free/realloc; no allocator lock held while quiescence needs its owner to run. |
| Descriptors | Stable admitted-operation references separate from table storage; close/unwind lifetime plus cursor/read-ahead serialization. No table lock across I/O; preserve uncertain-close behavior. |
| FILE and DIR | Per-stream serialization and open/list/close lifetime; define FILE-to-descriptor ordering. For DIR, document same-object caller serialization unless operation locking is supplied. |
| Exit handlers/startup | Initialize once; synchronize registrations and run callbacks outside locks. Require workers joined before orderly process finalizers; force-stopping a mutex owner can deadlock callbacks. Kernel forced teardown cannot depend on them. |
| Time/timezone/rand | Protect generator/cache publication and borrowed timezone strings; document static time-result lifetimes and provide safe reentrant paths. |
| C++ runtime | Replace disabled-thread exception state, guards/once, fallback allocator and unwinder assumptions before enabling libc++ threads. |

The permanent C-only locale needs no mutable-state locks. The existing
setenv/unsetenv and working-path overrides require synchronized publication and
owned internal snapshots; borrowed public results retain their documented
caller-exclusion rules. Synchronizing syscalls alone does not protect libc
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

MEMORY supplies the guarded eager subset described above. CREATE pins
registered stack/TLS context backing until native completion; overlapping RELEASE
returns BUSY. These lifetime reservations are distinct from short VM access
leases: unrelated mutation drains root/copy activity, not the thread's lifetime.
Join then releases libc-owned regions. Never free an exiting thread's own stack
from its trampoline. An unjoined libc record will count against its runtime capacity; final process
teardown reclaims remaining backing. Guards do not prevent a large stack jump
from skipping their page.

Accepted initial resource policy: **64 live/retiring native threads and 64
unreclaimed libc context records per process**, both including the initial
thread. Reserve before publication and release each count only at its respective
completion/reclamation boundary. Exhaustion returns a real limit/allocation
error. The native live/retiring storage bound is implemented; the separate libc record
budget belongs to task 8. Retaining a native completion observer consumes heap
and capability storage, not a live-task slot. These tunable bounds allow roughly 64 MiB of default user stacks and
1 MiB of 16 KiB kernel stacks, plus TLS/metadata; they guarantee no physical
capacity. Large native contexts remain subject to VM/allocation admission.
The 1 MiB default and 64/64 bounds are tunable implementation policy, not ABI
or SDK contracts. Consumers must handle real limit/allocation errors rather
than assume these values; Go or libuv may need different tuning later.

## Delivery gates

Each gate needs its own owner assignment, baseline where costs change, ordinary
build and interactive QEMU/debugger qualification. No new test infrastructure.
Completed gates do not authorize the next one. Tasks 2–4 are ownership plumbing with no new runnable thread feature. The first
owner-runnable siblings arrive at task 5; ordinary threaded C arrives at task 8.
SDL audio callbacks and libuv workers require their later consumer tasks, not
merely completion of task 2.

1. [x] **Process lifetime (#612).** Independent process/group ownership and task retirement; one user task.
2. [x] **Admitted call references and CLOSE (#671).** Owned storage/rights and detached-generation close; one user task.
3. [x] **Shared-table delivery (#684).** Growth/atomic installations, reply slot reservations and BSP table-loan replacement; one user task.
4. [x] **VM activity and user-buffer leases (#700).** Dispatch/root departure, BSP quiescent mutation and explicit concurrent-device refusals; one user task.
5. [x] **Native CREATE/EXIT/completion (#709), assigned 2026-10-11.** Thread set/result election, guarded-context lifetime, initial/sibling FS setup and space placement; joinable only. Owner can run minimal native sibling probes with distinct stacks/FS and shared CR3, then join; ordinary threaded libc is not ready yet.
6. [x] **Address WAIT/WAKE — assigned 2026-10-11.** Native word/deadline operations, mapping-incarnation keys and literal detach-before-notify on wake/timeout/stop/unmap are implemented and QEMU-qualified; owner-native A/B is recorded (CALL64 fastest about +100 ns). See [qualification](../development/experiments/threads-task6/README.md).

   Owner can run real contended native parking and inspect clean registrations after timeout or process stop.
7. [x] **Static TLS and errno (#748).** Compiler/LLD template/bootstrap, SDK/startup, per-thread errno and runtime keys/destructors are implemented and build/QEMU qualified. The matching compiler is published and CI passed before the latest dependency rebase. Native launch cost (+6–9%, about +0.11 ms/launch) is accepted by the owner, 2026-10-12; IPC/pages are unchanged. Initial-thread static TLS is a much-later launch-heavy-workload follow-up, not gate 8 work; see [qualification](../development/experiments/threads-task7/README.md).

   Owner can inspect distinct language TLS/errno and ordinary thread destructors on native siblings.
8. [ ] **Thread-safe libc and C11/pthread profile — assigned 2026-10-12; decisions accepted and baseline captured, implementation in progress.** Allocator, descriptors, stdio/caches/shutdown and real mutex/condition/semaphore/once; C++ adapter after its runtime audit.

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
PRs before gitlinks. Task 5 adds the native SDK ABI and libpyxis helpers; no
compiler-container rebuild or libc runtime/startup change is required.

## Accepted owner decisions (2026-10-10)

The earlier accepted execution, lifetime and parking directions remain unchanged.
The owner accepted all three defaults with this condition, recorded verbatim:

> as long as we don't implement posix threads in the kernel and still keep things pyxis native

The kernel exposes native thread, parking and TLS primitives only. Pthreads and
C11 threads are userland libc layers over them, like the rest of libc. No
POSIX-shaped kernel calls, futex clone or signals-based cancellation. Acceptance
does not assign later implementation: each gate is assigned separately (task 5 on
2026-10-11, then gate 6 with its accepted contract above); gates 7–9 remain separate.

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
