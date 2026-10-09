# Multiple threads in one process

Status: the owner accepted all three defaults on 2026-10-09. No thread
implementation is provided by the investigation in
[#567](https://git.internal/PyxisOS/pyxis-os/pulls/567), now merged. Task 1's
process-lifetime split is implemented and delivered for review, with
[matched qualification](../development/experiments/threads-task1/README.md).
Exactly one user task per process remains; later tasks are unassigned.
Detailed thread interfaces below remain proposals to refine within those
accepted boundaries. This is separate from moving kernel services off the BSP in
[SMP follow-ups](scheduling-and-threads.md).

## Evidence and scope

Inspected Pyxis `26770a0c`, with pinned userland
`fe6f3efb0847cc500a68cb782304eab781bdff58` and ports
`a642f07382e14bd233ac1be2b6a814e95c32d835`. The prior runtime SMP closure
[#430](https://git.internal/PyxisOS/pyxis-os/pulls/430) is merged; its contracts
are now in [SMP](../kernel/smp.md) and [private memory](../kernel/memory.md).
Investigation used source inspection and one compile-only TLS rejection probe,
described below. No boot, timing or native hardware measurement was performed.
Performance costs below are expectations to measure, not measured regressions or
speedups. Source paths describe these revisions.

Consumers are hosted Clang, SDL2 worker/timer threads and later audio callbacks,
C++ `std::thread`, and libvncserver-style workers. Their shared pointers require
one address space; separate processes are not a thread substitute. This
investigation does not establish a complete hosted Clang or libvncserver port.
See [toolchains](toolchains-and-runtimes.md), [SDL2 limits](../technical-debt.md#sdl2-port-limits)
and [C++ limits](../technical-debt.md#c-runtime-subset).

## What already exists, and what changes

The scheduler already has distinct task stacks, register/FP state, CPU placement,
wait records, deadlines and reusable BSP request storage. Those are useful thread
building blocks. The missing boundary is shared process ownership, not another
ready queue.

The table records assumptions at the investigation revision above. Task 1 replaces
task-owned process lifetime and task-targeted process/group observation; the VM,
table and buffer safety work remains for task 2. Current task-1 behavior is in
[SMP ownership](../kernel/smp.md#scheduling-and-ownership).

| Inspected code and present invariant | Required change |
| --- | --- |
| [process](../../include/kernel/process.h), [task preparation and reaping](../../kernel/task.c): one task solely owns its process; every completed user task destroys it | A process owns a live/retiring thread set; task retirement and process destruction become separate operations |
| [private allocations](../../kernel/mm/private.c), [memory calls](../../kernel/object/memory.c): IF=0 protects a caller's sole-use VM and allocation list | Serialize VM metadata and coordinate every CPU and kernel borrower of that process |
| [paging](../../arch/x86_64/paging.c): unmap/protect invalidate locally; active-space checks inspect this CPU only | Track shared-root activity and prevent reclamation or permission completion before all relevant translations are invalidated |
| [user copies](../../kernel/user_memory.c): check page tables, then plain `memcpy` | Stable mapping leases across each check/copy; a second check alone cannot close the race |
| [capabilities](../../kernel/object/capability.c), [syscalls](../../kernel/syscall.c): resolved objects are borrowed from stable table slots | Synchronized table access and owned object references across blocking calls, close and table growth |
| [BSP requests](../../kernel/service/request.c): parking the sole caller during selected requests lends its table or VM exclusively | Task-local request ownership remains; process/table/VM loans need explicit shared-state exclusion |
| [process control](../../include/kernel/object/process.h): one borrowed task link, completion after its reclamation | Process stop targets every live thread; process completion follows final process/thread reclamation |
| [startup](../../kernel/user/startup.c), [user entry](../../kernel/task.c): each task enters with process startup in RDI | Only the initial thread runs process startup; siblings enter a runtime trampoline with a thread argument |

### Scheduling, spaces and VM

Each thread should inherit its process's space and obey that space's ceiling and
effective CPU set. Reuse least-load placement, user-mode migration and the rule
that a blocked syscall resumes on its assigned CPU. Threads add runnable tasks,
so they add round-robin shares; CPU sets are placement constraints, not CPU-time
budgets or thread-count limits. This does not introduce per-process fairness,
priorities or per-thread affinity. Creating a sibling must close affinity setup:
`task_space_set_affinity()` currently assumes the space's sole init task is the
caller while setup remains open.

The PMM, heap and per-CPU scratch slots already support allocation on any CPU.
General kernel VM mutation, task-stack allocation/reclamation, workers and the
request executor still have BSP ownership. Shared userspace VM needs its own
protocol; adding an allocator lock does not establish it.

Three VM approaches have different scope:

| Option | Benefit | Cost and limitation |
| --- | --- | --- |
| One executing thread per process, even on an SMP machine | Avoids simultaneous user-root activity | Gives concurrency only through blocking; still needs protection against parked kernel continuations, BSP loans and retained buffers. It does not make today's table loans safe |
| Process-wide quiescence for mapping mutation | Keeps BSP mutation of an inactive shared root and simple page-table ownership | Pauses siblings for mutation; needs dispatch/admission exclusion and safe draining of kernel borrowers |
| Concurrent mapping mutation plus per-range leases/pins and targeted shootdown | Can preserve unrelated sibling execution | Larger initial protocol: active-CPU membership, activation races, partial mutation, pins, reclamation and permission changes |

Accepted starting point: parallel user execution, with **process-wide quiescence
for VM mutation**. Preserve the BSP as the mutation owner while bringing up the
protocol; local shared-VM mutation can be reconsidered with evidence later.
This revisits the existing local private-memory fast path deliberately.

A mutation first closes dispatch and new VM-borrow admission for that process.
Executing siblings reach safe scheduler boundaries; kernel copies and loans
drain or finish with their mappings retained. Each participating CPU leaves the
private root and acknowledges after its CR3 reload. Only then may the BSP mutate
page tables and free backing, before reopening dispatch. A parked thread with
an outstanding operation is not automatically quiescent: its service may still
use the VM or it may later copy into a retained reply buffer. Capture/staging or
an explicit mapping lease must cover that interval.

Do not hold a spinlock while waiting for siblings or service completion. A caller
requesting mutation must itself park and relinquish its root. Drain must allow
admitted continuations to finish; a gate which blocks their completion can
deadlock. Waiting on join, a mutex or an I/O condition must not hold a global
process execution lock that prevents the sibling which can satisfy it running.

For fully quiesced user roots, PCID/global pages being disabled and CR3 reloads
provide translation invalidation; a new IPI shootdown is not inherently required
for this first approach. The acknowledgments and dispatch gate are still required
for every CPU, including the BSP. Any later mutation while siblings remain active
must invalidate affected translations on all active CPUs and order activation
against the mutation generation. Unmap must delay frame/page-table reuse until
acknowledgment; protect must not report success while a CPU can use stale
permissions. Failure to quiesce cannot be treated as success or authorize freeing
backing. Retain ownership and keep unsafe execution excluded until recovery.

The existing `arch_kernel_flush_remote()` in
[smp.c](../../arch/x86_64/smp.c) is narrower: BSP kernel task, IF=1, kernel root,
all online CPUs, and already quiesced shared kernel ranges. It flushes CR3 but
does not change page tables or stop refills. It is evidence of an interrupt/ACK
mechanism, not an existing general userspace shootdown API. Synchronous IPI waits
from IF=0 syscall paths would block delivery on peers also inside syscalls.

### Syscalls, shared tables and retained observations

Two separate races need fixing. A sibling can change input bytes while they are
copied, even with stable mappings: capture fixed requests once and treat
userspace data as untrusted. A sibling can also release backing between a buffer
check and copy, or during `memcpy`; this can currently become a kernel fault.
Replacing `KASSERT(copy_to_user(...))` with an error branch addresses neither
plain-copy fault recovery nor rollback of already committed side effects.

Proposed rule: short user access obtains a VM read lease; mutation waits for it.
Blocking operations capture inputs and hold object references. Their output
lifetime must be chosen explicitly: retain a mapping lease, or release it and
revalidate output under a fresh lease, with defined lost-reply/committed-operation
behavior. Retaining arbitrary buffers across unbounded waits can prevent RELEASE
indefinitely. Prefer owned staging and short final-copy leases where the protocol
permits; audit committing operations individually. A mapping lease protects
backing, not contents from sibling writes.

The capability table remains process-wide. Lookup must atomically capture rights
and a reference; CLOSE removes a slot but cannot destroy an object still used by
an admitted call. Table growth and bulk installation need exclusion, with no
borrowed entry pointer retained across reallocations. Allocate replacement storage
outside the lock, then lock, recheck capacity/state, copy current entries and
publish; discard/retry stale preparations. Alternatively use a sleepable exclusive
table lease for the existing BSP loan. No spinlock may remain held across a
resource wait. Handle
reuse and rights snapshots must preserve the existing generational authority
contract. A sibling closing one handle must not accidentally release another
sibling's admitted operation.

Storage references and grant references are different: `object_grant_retain()`
also accounts for console/terminal authority and execution-group controllers.
Proposed default: an admitted call retains an immutable rights snapshot and object
storage, without automatically retaining grant accounting. Protocol close may
cancel or end logical ownership while retained storage permits safe unwinding;
endpoint receipt/receiver close already changes logical ownership synchronously.
Atomically detach the table slot, then perform close effects using retained
storage. Grant retention must be deliberate where a protocol requires it;
otherwise final CLOSE must still apply hangup/controller release without
destroying an active continuation. Table borrowers include launch/group creation, HOST/native CREATE,
TCP open/listen/accept and capture FILE installation, as well as growth.

Wait records and request areas are already **per task**, not per process.
`readiness_request.caller` in [wait.c](../../kernel/user/wait.c) is a borrowed
process identity for ownership observations; its wait identifies the calling
task. Preserve one outstanding request and one wait per thread, and keep process
identity alive until registrations and workers detach. Workers must still make
no request access after publishing completion/wake.

### Device ownership audit

Process ownership can stay unchanged without treating sibling operations as
exclusive. Existing boundaries need the following corrections before use:

| Code | Inspected single-thread dependency | Required invariant |
| --- | --- | --- |
| [keyboard](../../kernel/object/keyboard.c), [pointer](../../kernel/object/pointer.c) | A second blocked READ asserts that the reader slot is empty; RELEASE and resumed reads assume no competing sibling | One blocked reader with explicit BUSY admission is the small option; a waiter queue is larger. RELEASE must refuse safely or cancel/detach readers, and resumed reads must tolerate changed sessions |
| [Bluetooth HCI](../../kernel/bluetooth/hci.c) | One `adapter.reader`; RECEIVE and process-exit cleanup assert sole-reader ownership | Protect reader admission and session lifetime; tear down only after readers detach |
| [display](../../kernel/object/display.c) | Pixel snapshots retain frame backing, but deferred requests lend an inactive process VM; REPLACE excludes its own reply range only | Keep retained-frame lifetime, coordinate all sibling mappings/buffers, and serialize session mutation without a raw process/table loan |

Readiness snapshots keyed by process still answer whether the **process** owns
a session. They are observations, not reservations for a particular thread.
Delayed operations must capture a session generation and recheck admission; a
second sibling can release and reacquire the same process-owned session.

Audio sessions were in open
[#557](https://git.internal/PyxisOS/pyxis-os/pulls/557) during source inspection at
`4281d372d54be8877fee6fee7886435df8555f75`; they are absent from this main baseline.
Before handoff, #557 merged as `c2407b6019a91df0dc231f18bf47e25e3d5ecf4a`.
The diff between those revisions for `kernel/object/audio.c`,
`include/kernel/object/audio.h`, `include/abi/audio.h` and
`kernel/user/readiness.c` is empty, so the following audio findings also describe
the merged code. The rest of this investigation
retains its original baseline and dependency pins.
Its `kernel/object/audio.c` uses locked process-owner/free-capacity snapshots,
request generations, copied PCM and process-exit invalidation. Preserve those
contracts. `audio_request.audio` still relies on the caller's capability
for storage lifetime; siblings require operation references and safe copies.
Run `audio_process_exit()` at final process teardown, not each thread's exit.
Nothing here qualifies that PR's playback behavior or changes its device policy.

Startup metadata and named grants belong to the process and are prepared once.
Each thread shares the table and namespace; it receives no automatic new device
authority. Runtime caches and startup-resource handle ownership must be audited
alongside the table. Copying startup for each thread would duplicate initialization
and create competing owners for shared resources.

## Proposed native lifecycle

Names here describe operations, not assigned syscall numbers or a final wire ABI.

- A native thread-management grant acts on the **calling process**, as MEMORY
  does; transferring it must not authorize editing the sender's process. CREATE
  supplies executable entry/trampoline, argument, disjoint writable stack and
  initial FS base. Admission validates mappings under VM protection and reserves
  kernel metadata before publication. Failed creation publishes no task or
  observer, releases allocations/references, and preserves caller-owned memory.
- Every task holds process lifetime until its kernel continuation, registrations
  and loans have returned. The process owns shared VM, capability table, startup,
  endpoints, device sessions and execution-group association. Publication must
  serialize with process/group stopping; CREATE after stopping begins fails.
  Ordinary group SEAL closes process-launch admission but still lets an existing
  process create internal threads under process-level membership. A process
  cannot gain a surviving sibling after its stop set is sampled.
- Thread EXIT records a machine-word result and retires only that thread.
  Returning from the trampoline runs runtime/TLS destructors before thread EXIT.
  Existing process EXIT, including returning from `main`, stops the whole process.
  Fatal user faults stop all siblings; isolation within a corrupted shared heap
  is not offered. The last ordinary thread exit completes the process with status
  zero if no process-wide reason was recorded. The first accepted process-wide
  terminal reason/status is retained under lifetime synchronization.
- A waitable thread observer retains result storage, not the address space or a
  raw task pointer after retirement. Completion means the task has left its root
  and stack and its kernel loans/registrations/storage have been reclaimed. Native
  observation may have multiple waiters; libc implements the single successful
  join claim, rejects self-join, and distinguishes join from detach. Result
  publication has release/acquire ordering so join observes stores preceding
  exit. Stop/error unwinds the waiter without losing the target's completion or
  join ownership.
- Closing the observer does not kill the thread. Detached execution still keeps
  the process alive. Runtime-owned user stack/TLS cannot be freed by the exiting
  thread on its own stack: join releases them after completion. Detach needs an
  internal observer retained by libc and collection after completion (for example
  at subsequent thread operations), or retirement-owned regions in a later
  native design. Do not promise immediate detached-stack reclamation with merely
  a CLOSE operation; final process destruction reclaims remaining backing.
- Ctrl+C remains foreground application termination, not an asynchronous signal
  injected into an arbitrary sibling. The inspected shell's `wait_or_interrupt()`
  in [launch.c](../../userspace/shell/launch.c) calls PROCESS_TERMINATE for every
  foreground pipeline stage. Process-control TERMINATE and
  group stop mark all threads, wake interruptible waits, and prevent user return
  only after each continuation unwinds safely. Published BSP/HOST loans remain
  uninterruptible until completion. Process observation and group completion
  differ: process observation completes after all process/thread execution
  resources are reclaimed; group completion additionally waits for attributed
  deferred object/device cleanup, including retained display pixels.

Execution groups currently embed a member in each task and store the group
reference in its process. Default direction: retain process-level application
membership and fan stop out through the process thread set. Tracking each thread
as an independent member is possible, but needs independent member references
and creation/sealing admission; reusing the process's one reference for multiple
task completions would undercount lifetime. No change to foreground shell
authority or group completion meaning is proposed.

Per-thread cancellation, signals, alternate signal stacks, fork, arbitrary
suspension and scheduling policy are outside this contract. C++ uncaught exceptions
retain process-wide termination; ordinary exception handling remains per thread.

## Synchronization proposal

| Native primitive | Tradeoff |
| --- | --- |
| Process-private wait on an aligned atomic word, plus wake | Cheap uncontended userspace locks; one small kernel park protocol can support mutexes, conditions, once and runtime guards. Requires atomic check/enqueue and VM/key lifetime coordination |
| Waitable synchronization object handles | Explicit object lifetime and authority; avoids an address-key registry. More handles, allocation and calls for many runtime locks |

Accepted direction: process-private address wait/wake, not a general Linux futex
ABI. Key it by process identity and virtual address plus mapping lifetime; a reused
address must not inherit waiters. WAIT compares an aligned word against the
expected value under the same wait-bucket synchronization as registration, with
a VM lease protecting the read. Mismatch returns without sleeping. Wakes and
deadlines detach registrations safely before notification; recheck the word on
return. No lost wake is permitted between comparison and park. Spurious wakeups
are permitted. A wake is notification, not ownership transfer or memory ordering:
libc uses release/acquire atomics on the word. Process termination removes waits;
unmap must invalidate/detach registrations before address reuse, without retaining
a VM lease over indefinite sleep. Shared mappings, cross-process wakes, requeue,
priority inheritance and robust owner-death mutexes are deferred.

Build libc mutexes/conditions/once on this primitive. C11 `thrd_create/exit/join`
and pthread creation/join map to native lifecycle; return values, detach state and
thread-specific destructors live in libc. Recursive/error-checking mutex semantics
can also live there. Do not advertise cancellation or other pthread features
until implemented. SDL and libc++ must consume that libc/runtime interface rather
than duplicating capability calls in each port.

## TLS, libc and C++ runtime

### Inspected configuration and compile evidence

[Architecture user state](../../arch/x86_64/user.c) already saves/restores FS
base per task, initializes it to zero, and disables user FSGSBASE instructions.
Creation needs a canonical user FS base, and the **initial thread** also needs a
way to establish it before TLS-using C code. FS remains thread state; kernel GS
remains CPU state. Preserve per-thread x87/MXCSR state and specify new-thread
floating-environment inheritance in the lifecycle ABI.

[Userland linker script](../../userspace/linker.ld) has no explicit TLS template
or bounds. [P1F](../../include/pxe/p1f.h) represents entry/load segments, with no
TLS descriptor; the [toolchain](../../toolchain/README.md) rejects language TLS.
The installed builder produced this compile diagnostic on 2026-10-09:

```sh
podman run --rm --network=none --entrypoint /bin/sh \
  git.internal/pyxisos/pyxis-builder:pyxis-llvm23.1.3-49e2c1a \
  -c 'printf "_Thread_local int thread_value;\n" | /opt/cross/bin/x86_64-unknown-pyxis-clang -x c -std=gnu23 -S -o /dev/null -; cat /opt/cross/share/pyxis-toolchain/llvm-revision'
```

Clang reports `thread-local storage is not supported for the current target`.
The revision file records fork `49e2c1a1518b3e4687b52ceb6001069c1b6d261e`.
The compiler exit status and image digest were not captured. This
measures compiler rejection only, not TLS generation, linking or runtime support.

Proposed first TLS scope: static, executable-owned TLS with one initialized
template, zero-filled tail and alignment; libc allocates/copies it per thread,
including a thread-control block and errno. Choose the x86_64 thread-pointer
layout and supported relocation/model in a focused compiler probe before editing
the target. Linker symbols and template bytes in ordinary load segments might
preserve P1F; a format extension is an alternative, not established necessity.
Dynamic loading, TLS module registration and DTV machinery are outside the first
scope. Destructors run before ordinary thread exit; process faults/forced stop
cannot promise user cleanup. A tiny explicit FS-based runtime record could
support the initial lifecycle slice, but is not language `thread_local` support.

### Shared libc state

| Pinned source | Work before enabling general threaded consumers |
| --- | --- |
| [errno](../../userspace/libc/errno.c), [header](../../userspace/libc/include/errno.h) | Replace process-global errno with per-thread storage available before allocation and runtime entry |
| [malloc](../../userspace/libc/malloc.c) | Serialize TLSF metadata and pool growth; support cross-thread free/realloc. Define allocator/VM/diagnostic lock ordering and do not use allocation to bootstrap its own lock |
| [descriptors](../../userspace/libc/descriptor.c) | Protect table growth and entry lifetime separately from cursor/read-ahead state. Entry pointers survive blocking backend calls today; a short table lock alone is insufficient |
| [stdio](../../userspace/libc/stdio.c), [stream state](../../userspace/libc/stream.h) | Per-stream operation serialization plus stream-list/open/close lifetime. Coordinate FILE locks with descriptor locks; avoid destruction while another operation uses a FILE |
| [exit](../../userspace/libc/exit.c), [libc entry](../../userspace/libc/startup.c) | One process initialization/shutdown owner. Protect handler registry, execute callbacks outside internal locks, and quiesce siblings before global finalizers/stdio teardown |
| [rand](../../userspace/libc/rand.c), [time](../../userspace/libc/time.c), [timezone](../../userspace/libc/timezone.c) | Audit mutable globals individually. `localtime_r` still uses shared timezone cache/designation lifetime; its name alone does not establish safety |
| [startup lookup](../../userspace/lib/startup.c) | Immutable startup values can remain shared after one-time publication; borrowed handles still require coordinated close/use ownership |

Serializing every syscall would not protect libc state changed in userspace, and
locking malloc alone would not make stdio/descriptor entry lifetime safe.
Orderly libc `exit()` also cannot just stop siblings and run callbacks: a stopped
sibling may own a runtime/user mutex those callbacks need. Define a cooperative
runtime shutdown/drain before finalizers, or require orderly callers to join
their workers first and document the remaining concurrent-exit limit. Kernel
process EXIT/fault/termination must reclaim safely without depending on user
destructors; they cannot guarantee callback execution or mutex recovery.

### C++ and SDL integration limits

[cxx-runtime.sh](../../scripts/cxx-runtime.sh) sets libunwind, libc++abi and
libc++ threading OFF and forces pthread probes off. Merely enabling `<thread>`
is insufficient: concurrent C++ exception state, exception fallback allocation,
local-static guards/once and unwinder registration require working synchronization
and TLS too. Thread-specific keys and exit destructors are needed by runtime
adapters where language TLS is not used.

Available LLVM runtime implementation source was inspected at older fork
`41ab6043`, **not the pinned compiler/runtime revision**. In that checkout,
`libcxxabi/src/cxa_exception_storage.cpp` uses one static exception state when
threads are disabled; `cxa_guard_impl.h` chooses `NoThreadsGuard`;
`fallback_malloc.cpp` and `libunwind/src/RWMutex.hpp` disable their locks.
libc++'s external thread adapter supports mutexes, conditions, once, thread IDs,
create/join/detach/sleep and TLS keys. These are useful audit targets and an
adapter option, not proof of identical implementation at `49e2c1a`; recheck that
pin before changing configuration. Pinned runtime sources were not available
locally during this investigation.

The exact [SDL config](../../ports/sdl2/SDL_config.h) disables threads; its
[recipe](../../ports/sdl2/Makefile) selects generic thread backends. Existing
[SDL docs](../development/sdl2.md) describe failed thread creation/timer callbacks
and successful no-op synchronization. Upstream generic function bodies were not
present locally and were not independently inspected. Threaded use needs real
native/libc thread and synchronization backends; retain generic helpers only
where their implementation is suitable. The inspected native
[timer backend](../../ports/sdl2/pyxis/SDL_systimer.c) also has unsynchronized
clock/start/ticks initialization. DevilutionX's
[patches](../../ports/devilutionx/patches/) select thread stubs and replace one
`thread_local` error variable with a global; reverse those assumptions only in a
separate consumer task. Audio, multiplayer and unrelated port features remain
outside thread runtime delivery.

Kernel/public ABI/SDK export work belongs in Pyxis; libc/startup/libpyxis work in
userland; SDL/consumer adapters in ports. Publish and review dependency PRs before
updating parent gitlinks. Runtime header/configuration changes need SDK rebuilds.
Enabling language TLS requires the LLVM target change, and possibly LLD changes;
that means a new fork pin and an owner-built/published compiler container. An
adapter added to the LLVM fork similarly changes the matched runtime/compiler
pin. Ordinary SDK or libc changes alone do not require rebuilding LLVM. No new
external source or mirror entry is introduced by this docs PR.

## First task and later gates

The smallest useful first task is **split process ownership from task retirement**,
while retaining exactly one submitted user task per process, including blocked
tasks. CREATE/EXIT/JOIN is
the first public thread slice only after the safety gates below; combining all
of them into a supposedly small create syscall would hide the actual work.

- [x] Task 1: move submitted-process ownership, thread membership, process-control
  stop targeting and terminal result into an explicit process lifetime. Keep
  current single-task execution and process-exit behavior. Reap task storage
  separately, and destroy the process once only after its final task and admitted
  shared activity drain. Update group membership/cleanup attribution together.
  Add no thread syscall, device behavior, TLS or dependency pin in this task.
  Implemented 2026-10-09; see [process lifetime and retirement](../kernel/smp.md#scheduling-and-ownership)
  and the [baseline/qualification record](../development/experiments/threads-task1/README.md).
- [ ] Task 2: establish process VM activity/quiescence, user-copy leases and
  synchronized capability/object lifetimes; replace exclusive BSP loans. Audit
  every blocking/committing syscall and retained reply/input. Unchanged devices
  must either be safe for siblings or explicitly refuse unsupported concurrent
  operations before any loan or side effect. One CPU is not an exemption.
- [ ] Task 3: native same-process CREATE, thread EXIT and waitable completion/join,
  with caller-provided stack/argument and per-task FS base. Keep process-wide
  exit/fault/group stop. Start with joinable threads; do not advertise detach or
  consumer runtime support. No new device semantics. Exercise genuine parallel
  shared-root execution within existing space CPU limits after task 2.
- [ ] Task 4: address wait/wake, static TLS/compiler support and thread-safe libc;
  then C11/pthread subset and C++ runtime integration. Detached collection and
  thread-specific destructors must have explicit lifetime bounds before enabling
  their APIs. Enable SDL worker/timer callbacks as a bounded consumer afterward;
  audio callbacks remain an independent device consumer task.

The owner assigned task 1 to Codex epsilon on 2026-10-09, after #567 merged.
Its exact-main baseline, matched single/four-CPU workloads and interactive
retirement/failure inspection are recorded in the qualification above. No
thread syscall, TLS, device behavior or dependency pin changed. Task 1 stops
for owner review; later tasks need separate assignment and their own focused PRs.
For later work, compare uncontended locks,
creation/join, VM mutation pauses and parallel work with repeated samples and
variation. Use existing programs/probes and interactive QEMU/debugger inspection,
not new test or boot automation. Inspect normal/fault/group retirement and
unpublished failure paths; later observe two distinct task stacks/FS bases sharing
CR3, safe retirement while another sibling runs, and buffer/table loans during
stop. Those later validation checks remain proposed; task 1 does not validate
multiple threads, shared-root execution or runtime TLS.

## Owner decisions

The owner accepted all three defaults on 2026-10-09. Alternatives below remain
unselected; acceptance establishes direction, not implemented behavior.

1. **Execution and VM scope:** accepted parallel siblings with process-wide
   quiescence and BSP-owned mapping mutation initially. Serial process execution
   is smaller for CPU activity but still requires the table/copy/loan audit;
   concurrent mutation with targeted shootdown is a larger alternative.
2. **Lifetime and interruption:** accepted thread-local ordinary exit, process-wide
   explicit exit/fatal fault, and existing foreground Ctrl+C termination; process
   completion after final reclamation. Per-thread fault isolation or arbitrary
   asynchronous cancellation would require a different recovery contract.
3. **Runtime parking:** accepted a process-private address wait/wake with deadlines,
   mapping-lifetime keys and ordinary stop unwinding. Handle-based synchronization
   is the alternative when explicit kernel object lifetime outweighs uncontended
   lock cost; full pthread/Linux futex semantics are outside this proposal.
