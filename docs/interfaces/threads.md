# Native threads and address parking

Processes can add sibling threads in their own address space through the
`threads` MANAGE service. Siblings are parallel; the process, not the thread, is
the unit of exit, fault and stop. The kernel exposes only native primitives:
lifecycle, address parking and static TLS. Pthreads and C11 threads are libc
layers over them, described in [libc threads](../userland/threads.md). The owner's
condition, 2026-10-10, was "as long as we don't implement posix threads in the
kernel and still keep things pyxis native", so there are no POSIX-shaped kernel
calls, futex clone or signal-based cancellation.

The ABI is in [`abi/thread.h`](../../include/abi/thread.h). The kernel side lives
in [process](../../include/kernel/process.h) and [retirement](../../kernel/process.c),
[capabilities](../../kernel/object/capability.c) and the
[architecture entry](../../arch/x86_64/user.c). Related kernel behavior is in
[userspace](../kernel/userspace.md), [SMP](../kernel/smp.md) and
[BSP requests](../kernel/bsp-service-requests.md).

## Lifecycle

Explicit `session` delegation carries the optional `threads` service; ordinary
applications do not receive it. Bundle manifests admit `threads/manage` and
`pipe/create`, which the SDL consumers declare. A grant applies to the calling
process, so transferring it confers no control over the sender.

| Operation | Contract |
| --- | --- |
| MEMORY ALLOCATE_GUARDED / RELEASE_RESERVATION | Usable bytes become eager RW/NX backing under the calling process, plus one reserved unmapped lower guard. Returns a generational identity and the full reserved base and span. Whole-identity release only; BUSY while a registered stack or FS context is live or retiring. No sparse commit or decommit. |
| THREAD CREATE | Entry, argument, guarded stack identity and an optional distinct FS identity and base, in one complete zeroed message. The thread starts with RDI = argument and RSP = the 16-byte-aligned usable stack end, with no return address. Returns one completion handle. |
| THREAD SET_FS | Binds the current thread's context once; the base must lie within or one past its usable guarded context, so negative TLS offsets work. Rebinding is BUSY. Startup supplies this caller-only right without MANAGE or CREATE. |
| THREAD EXIT / completion WAIT | EXIT retires only the caller and does not return. WAIT returns EXITED with the word, or STOPPED with zero, repeatably; observers also support WAIT_COMPLETE readiness. Closing an observer never kills the thread. |

CREATE validates the mappings under the VM protocol, reserves task, observer and
reply storage, then publishes membership and runnable state atomically against
process and group stopping. Failure publishes nothing and returns every
provisional resource. Siblings never run process startup or constructors again.
Each task keeps its own floating-point state and inherits the creator's
environment, including exception flags. Completion publishes with release/acquire
ordering only after the task has left its stack and its continuations and storage
are reclaimed; the observer owns completion storage, not a VM. Guarded backing
is pinned until completion.

Process EXIT, return from `main`, foreground termination and fatal faults stop
every sibling; one atomic election keeps the first terminal reason. When the last
member leaves through THREAD_EXIT with no prior process result, the process
completes as PROCESS_EXITED with status 0, whatever that thread's word. Process
completion follows all thread and admitted-activity reclamation; deferred device
cleanup can outlast it and delays only group completion. Group membership stays
per process, and SEAL does not stop internal threads. There is no asynchronous
cancellation, per-thread fault isolation or signal.

## Address parking

Parking operations extend the `threads` service and compare naturally aligned
32- or 64-bit words in the calling process's ordinary VM-owned writable RAM.
Device, display and caller-owned mappings are rejected.

- **WAIT** carries the address, width, expected value and an absolute monotonic
  deadline. A mismatch returns WOULD_BLOCK without enrolling. An explicit wake
  returns OK, expiry TIMED_OUT and mapping withdrawal BAD_BUFFER. A zero deadline
  polls and the maximum machine word waits indefinitely; deadlines do not refresh.
- **WAKE** carries the address, width and a nonzero maximum count (the maximum
  machine word wakes all) and returns the number of waiters detached. Zero matches
  is success. A failed output copy does not undo committed wakes.
- Keys combine the process, the exact address and width, and a non-reused
  per-page mapping incarnation. Tokens cover image data, BSS, heap and stacks.
  Their validation uses a short mapping lease, which is not held across sleep. The fixed
  WAIT/WAKE packet shares one explicit lease across separately validated header,
  body, word and reply accesses. WAIT releases it after enrollment and before
  sleeping; WAKE releases after the final checked reply copy. Syscall admission
  and capability/stop checks remain separate, and sibling-written contents are
  not frozen by mapping stability. Unmap or loss of eligibility invalidates the affected pages and detaches their waiters
  first; unrelated pages keep their identity. Failed mutation preflight changes
  neither mapping nor registration.
- Wake, timeout, stop and unmap each resolve one terminal reason under the
  parking bucket's exclusion and remove the registration before notifying or
  letting the address be reused. A committed wake or timeout is never replayed.
- A wake transfers no ownership; callers recheck their predicate. There is no
  cross-process key, requeue or priority inheritance.

## TLS and runtime keys

Executable-only local-exec TLS uses x86-64 variant II, with the libc TCB self
pointer at `FS:0`. The SDK linker exposes the initialized template, zero-tail size
and alignment; LLD validates the intermediate ELF PT_TLS and omits it from P1F, so
the template stays in ordinary load segments. Dynamic TLS and modules are not
supported.

Libc owns FS. A TLS-free bootstrap prepares guarded MEMORY backing and binds it
before stdio, malloc or constructors run; failure exits honestly. `errno` is a TCB
field. Kernel GS stays CPU-local and user FSGSBASE is disabled. Runtime keys are
process-local and generational, 128 slots by default. Thread exit runs language
destructors in reverse registration order, then at most four key-destructor passes
outside registry exclusion. FS survives `atexit`, `.fini_array` and stdio
shutdown. Forced stop or fault runs no user destructors.

## Libc synchronization

C11, pthread and C++ mutexes use one atomic ownership/waiter word with native
64-bit ADDRESS_WAIT/WAKE. Destruction succeeds only while unowned and without
registered contenders. Ownership identities retain their full generation;
recursive depth, try/timed errors and condition reacquisition remain checked.
There is no adaptive spinning or new scheduler fairness promise. As with other
process-private synchronization objects, callers exclude new operations before
destruction and retain storage until admitted calls return.

Condition stack-waiter queues and rwlock reader/writer accounting retain their
guards. Completed once reads use acquire publication; initialization, abort and
waiter bookkeeping still use the existing guard. See the
[qualification record](../development/experiments/soh-present.md).

## Shared capability and VM ownership

Sibling threads share one capability table and address space, which needed four
ownership rules, all in place:

- **Admission.** A call resolves its exact grant generation into immutable rights
  and transport snapshots plus an owned storage reference, under short table
  exclusion. No dispatch, secondary handle or blocking cleanup keeps an entry
  pointer. Storage references add no grant authority, and CLOSE detaches the slot
  and advances its generation before the logical close runs outside the guard.
- **Table growth and delivery.** Growth prepares backing outside the guard,
  rechecks and copies under it. Installation claims exact slots, hidden from
  lookup. Operations own capacity before they commit or sleep, and publish
  already-owned grants atomically. COPY, launch sources and endpoint attachments
  recheck the exact source generation under the guard. A successful REPLY moves
  owned grants into reserved slots and cannot fail for capacity.
- **VM activity.** MEMORY, JIT, DISPLAY and thread-context changes share one
  drained process transaction. MEMORY/JIT and thread preparation run in retained
  caller-CPU kernel-root continuations; DISPLAY retains its fixed worker. Dispatch reserves root activity before the CR3 switch and
  acknowledges departure after the kernel-root reload. Mutation drains active
  roots and actual mapping borrowers, and admission stays closed through the
  mutator's final reply copy. A one-second precommit budget is tunable policy;
  failure returns BUSY before any effect. PCID and global pages stay off.
- **User copies.** Kernel staging and short validation leases carry no mapping
  across indefinite I/O. Output is revalidated after waits and a late failure is
  BAD_BUFFER without rolling back committed effects. Unsupported concurrency is
  refused with BUSY before effects, per object or session: endpoint RECEIVE and
  same-receipt REPLY, keyboard and pointer readers and control transactions,
  clipboard receiver ownership and HCI RECEIVE/RELEASE.

## Placement and limits

Siblings inherit the process space's effective CPU set and use the existing
least-load placement and migration. Kernel continuations keep their CPU. The first
sibling closes space affinity setup, which assumes a sole init task. There is no
per-thread affinity, priority or fairness change, so more runnable threads get
more round-robin share.

- **Stacks.** Libc allocates 1 MiB eager RW/NX backing plus a guard per sibling,
  with explicit page-aligned sizes of 1 to 8 MiB allowed. Native callers may pass
  any region. A guard does not stop a large stack jump from skipping its page.
  Join releases libc-owned regions; an exiting thread never frees its own stack.
- **Bounds.** At most 256 live or retiring native threads, and 64 unreclaimed libc
  context records, per process, both counting the initial thread. The native bound
  includes storage awaiting actual reclamation; libc retains its independent
  context bound. They are reserved
  before publication and released at completion and reclamation. These are tunable
  policy, not ABI; consumers must handle real limit and allocation errors. A
  retained completion observer uses heap and capability storage, not a task slot.
  Libc can use roughly 64 MiB of default stacks; 256 native threads require
  roughly 4 MiB of kernel stacks. These bounds guarantee no physical capacity.

## Delivery

| Gate | Delivered | Qualification |
| --- | --- | --- |
| 1 Process lifetime | #612 | [task 1](../development/experiments/threads-task1/README.md) |
| 2 Call references and CLOSE | #671 | |
| 3 Shared-table delivery | #684 | [task 3](../development/experiments/threads-task3/README.md) |
| 4 VM activity and user-buffer leases | #700 | |
| 5 CREATE, EXIT and completion | #709 | [task 5](../development/experiments/threads-task5/README.md) |
| 6 Address WAIT and WAKE | #731, #733 | [task 6](../development/experiments/threads-task6/README.md) |
| 7 Static TLS and errno | #748 | [task 7](../development/experiments/threads-task7/README.md) |
| 8 Thread-safe libc and the C11/pthread profile | #756, userland #236 | [task 8](../development/experiments/threads-task8/README.md) |
| 9 SDL workers, timers and audio callbacks; Chocolate Quake sound | #778, #784, #787 | [SDL threads and timers](../development/experiments/sdl-threads-timers/README.md), [SDL audio](../development/experiments/sdl-audio/README.md) |

Native measurements are in those records. Gate 6 added about 100 ns to the fastest
CALL64, and the scheduler-pointer follow-up recovered at most 25 ns, within noise.
Gate 7 adds 6 to 9% (about 0.11 ms) to native launch, which the owner accepted on
2026-10-12; static TLS for the initial thread is a much-later launch-heavy
follow-up. The SDL contracts are in [SDL2](../development/sdl2.md#threads-and-timers)
and [SDL2 audio](../development/sdl2.md#audio). Returning from `main` or calling
`exit` while threads are live skips libc finalizers
([technical debt](https://git.internal/PyxisOS/pyxis-os/issues/1302)).

Remaining limits and consumers:

- The native SEND4096 completion regression found in task 5 is still followed up;
  the owner's rerun is pending in its record. Stack qualification is separate
  evidence ([program loading](../kernel/program-loading.md)).
- Libc supports detached workers with [bounded deferred collection](../userland/threads.md#creation-and-collection).
  Cancellation, robust or process-shared objects and scheduling promises remain
  outside the profile.
- Consumer adapters need their own assignments: the libuv pool for
  [Neovim jobs](../wip/later-os-directions.md#neovim-follow-ups), the
  [Hax harness](../wip/claude-on-pyxis.md#track-2-hax-inventory-after-the-lua-harness) (timed conditions, drainers and a
  qualified concurrent TLS transport) and [Go](../wip/go-runtime.md) (its own
  stack, GC and VM adaptation; `GOMAXPROCS=1` still uses OS threads).
- Moving kernel services [off the BSP](../wip/scheduling-and-threads.md#serial-services-off-the-bsp)
  is independent of this interface.
- Native reservation of private VM (RESERVE then suffix COMMIT) would later
  subsume the eager guarded convenience under the same identity and authority
  model; see the [Go runtime contract](../wip/go-runtime.md#accepted-private-vm-reservationcommit-contract).
