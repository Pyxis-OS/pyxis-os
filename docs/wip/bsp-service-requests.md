# Task state and BSP service requests

Status: in progress, with the BSP executor serving pipe creation, private memory,
display, capability growth, namespace/endpoint creation, RAMFS/FILE backing
operations, launch preparation and HOST forwarding, and consolidated user-request
and profiling storage after the completed
[read-only filesystem milestone](../filesystem-readonly.md). This document selects
the boundaries and scheduling policy; individual tasks still require their own
implementation PRs. It does not authorize concurrent allocation, task migration
or process threads.

## Problem and completion target

[`task.c`](../../kernel/task.c) originally combined execution state with capability growth,
directory allocation, file-buffer replacement, HOST requests, launch preparation,
memory/display operations, and pipe, endpoint and namespace creation. Request
payloads, queue links, service bodies and profiling state accumulated in `task`.
Both the scheduler loop and preemption decision enumerated subsystem queues.

Separate task execution, request submission/completion, and subsystem operations.
Adding a service should change its subsystem and the explicit BSP service catalog,
not add payload fields or queue checks to the scheduler. A file split, giant union
or opaque copy of today's task structure is not the completion target.

Keep existing public interfaces, authority, results and cleanup behavior. The
intentional scheduling changes are FIFO service admission, prompt worker
notification, and a scheduling opportunity between requests. No throughput or
latency improvement is assumed before measurement.

## Agreed ownership boundaries

| Component | Responsibility |
| --- | --- |
| Task/scheduler | Execution state, ready queues, parking/wakeup, deadlines, retirement and safe post-switch handoff |
| Internal BSP request mechanism | Request ownership transitions, FIFO publication, dispatch and completion notification |
| Subsystem | Typed request fields, capture/validation, actual operation, rollback and resource loans |
| BSP executor | Run nonblocking service operations under existing BSP allocation/VM rules |
| Existing HOST worker | Blocking filesystem transport and final completion of forwarded HOST requests |

Use a small closed service tag and explicit dispatch, not arbitrary callbacks,
registration tables or a general continuation framework. Subsystem code owns the
meaning of each request. The scheduler only needs to know whether a submission
must wait until execution has left the task stack and private address space.

Execution retirement and scheduler waits remain in the task layer. Object
destruction retains its existing BSP ownership and lifecycle path; redesigning
reclamation is not required to remove subsystem request sweeps.

## Requests are operations, not sleeping tasks

The ownership lifecycle is:

```text
prepare -> submit -> service -> complete -> consume result -> release/reuse
```

Completion is distinct from waiting. Today a synchronous wrapper prepares a wait,
submits, sleeps if necessary, consumes the result and releases its request storage.
The dispatcher must not require that the submitter is already parked. Completion
state/result publication and waking a waiter are distinct responsibilities; no
asynchronous completion API is introduced merely to express that distinction.

A future asynchronous caller would require independently owned operation storage,
completion observation and explicit resource retention. It could submit without
immediately waiting, then consume and release later. Completion queues, waitable
receipts, multiple outstanding requests, cancellation, quotas and process-exit
draining are deferred decisions, not placeholder APIs in this milestone.

Asynchronous submission does not make an exclusive loan safe while the process
continues running. Capability-table growth and private-VM mutation still depend
on today's single-task process ownership. Future concurrency must replace that
premise explicitly. Borrowed buffers likewise need stable contents and lifetime
or independent capture before the caller can continue using them.

## Storage and lifetime

Provision one reusable request area on the BSP when preparing each user task.
Its storage has stable shared kernel mappings and remains allocated through task
retirement. Subsystems define typed records; provisioning computes a sufficient
size and alignment for the supported records and checks each use. There is no
arbitrary one-page limit and no scheduler-owned union of all subsystem fields.

Only one synchronous service request may occupy the area at a time. Ownership
extends through result consumption, including HOST reply copying. A second
preparation must not overwrite a submitted or completed-but-unconsumed request.
Allocation at task creation may fail and must unwind before publication. Request
submission itself does not allocate its storage, so memory exhaustion cannot
prevent required discard/release operations from being submitted.

Kernel workers do not provision user-request areas or synchronously use the client
transport in this slice. BSP service implementations call local helpers directly;
the executor must never submit to itself and wait. Enabling kernel clients later
requires an explicit nesting and dependency contract.

Task-lifetime reservation is an accepted cost, especially for the HOST staging
buffer. Lazy provisioning would add allocation failures and another handoff, and
must preserve guaranteed cleanup capacity. Leave that as later resource-policy
work. Measure fresh task/request sizes before and after; old build-artifact sizes
are not a current baseline.

Persistent profiling aggregates are separate from transient request storage.
Preserve their current caller scope and control behavior; request timing samples
travel with the operation. Do not decide future per-process versus per-thread
profiling here. Resource wait links are also distinct from service requests.
Consolidate identical wait-link shapes only where their actual lifetime contracts
match, preserving timed-wait detachment before reuse.

Launch captures/groups and other resources that survive several calls retain
their independent lifetimes. Sequential waits are valid: acquire a file's logical
busy ownership, detach that wait, then request backing replacement. No spinlock
may be held across service execution or waiting.

## Publication and completion rules

Ordinary requests capture validated inputs in shared storage, prepare their wait,
then publish under the request-queue lock. The caller lends the specified table,
object or logical operation until completion. It retains only what the waiting
protocol permits while the request belongs to the service.

Complete by writing status/results, detaching queue/worker references, and
publishing completion before waking. Save any next pointer before completion.
The service must make no request or caller-state accesses afterward: the caller
may immediately consume, reuse or retire it. Early completion records notification;
it must never enqueue a task whose execution has not safely stopped.

Memory and all display operations, including PRESENT, use deferred publication.
The closed service catalog enforces this choice; callers cannot select an
early-publication mode for these operations. The scheduler first switches to
its permanent stack, activates the kernel root, clears entry/current-task state
and establishes the parked handoff, then publishes the request. The BSP receives
an inactive private address space. Dispatch reloads the process root before
returning to the task stack. Preserve these rules in single-CPU mode too.

Task stacks are shared kernel mappings, not inherently inaccessible remote
memory. However, mapping reuse, translation freshness and frame lifetime do not
provide a general remote-stack borrowing contract. This milestone retains shared
request metadata and does not introduce remote stack pointers. See
[VM ownership](../memory.md) and [SMP handoffs](../smp.md).

Current synchronous execution has no external task cancellation. Retirement must
still establish that no request/worker retains its storage before freeing it.
Future cancellation or concurrent sibling threads need additional protocols.

## Agreed queue and executor policy

- Use one FIFO submission queue. Concurrent submitters are ordered by publication
  under its lock. Deferred VM requests enter FIFO only after their safe handoff;
  time spent preparing is not a queue reservation. Forwarded work can complete
  out of order; FIFO is not a global completion-order promise.
- Notify promptly when the executor transitions from idle to pending work, using
  a synchronized prepare/check/sleep handshake. Combine redundant notifications
  while it is already runnable or servicing work. A queue-empty check alone is
  not proof that the worker is asleep. Notify after unlocking; no self-IPI on BSP.
- After each request, yield when another task is runnable; otherwise continue.
  Merely enabling interrupts is not an explicit yield. Use scheduler-owned
  runnable-state decisions rather than inspecting ready queues from subsystems.
- When no requests remain, park the executor without polling. Initialize it
  before publishing request-producing tasks; failure must not leave queued work
  with no executor. Single-CPU callers park and allow that same executor to run.
- Keep allocator/VM operations on BSP with interrupts disabled where required.
  Reach an interrupt-enabled scheduling boundary between requests. An individual
  long allocation/copy remains non-preemptible under the existing contract.
- Forward blocking HOST work to its existing worker without completing the
  operation prematurely. The HOST worker owns final completion; an outstanding
  HOST request does not stop the executor servicing another caller.

The FIFO policy replaces fixed subsystem sweep order. There are no priorities,
batch quotas or elapsed-time service budgets in this slice. A slow individual
operation still delays requests behind it. Measure that separately from wakeup
and scheduling delays before considering staged execution or batching.

## Focused tasks

Before each task, discuss any newly discovered behavior or lifetime decision;
this checklist does not turn missing decisions into implementation permission.
Temporary coexistence during migration must not double-submit or double-service
an operation. Remove obsolete paths as their consumers move.

- [x] **1. Establish request ownership and one ordinary consumer.** Implement the
  narrow operation lifecycle and synchronous wrapper using pipe creation first.
  Put pipe creation/rollback in its subsystem. Storage can temporarily use the
  existing task record, and dispatch can temporarily run from the scheduler, but
  isolate those adapters. Preserve atomic installation and early completion.
  Record fresh structure sizes and existing workload measurements as the baseline.
- [x] **2. Introduce the BSP executor and agreed scheduling policy.** Move migrated
  dispatch to a dedicated worker with FIFO, idle notification and conditional
  yield. Establish initialization/failure ordering, single-CPU progress, and the
  prohibition on self-submission. Keep pending-work visibility correct while
  older queues coexist; remove the migrated queue from scheduler/preemption logic.
- [x] **3. Migrate private-memory and display handoffs.** Replace subsystem-specific
  scheduler payload/pending knowledge with one explicit deferred submission path.
  Keep authority and reply validation in callers, operations in their subsystems,
  inactive-root ownership and CR3 reload ordering unchanged.
- [x] **4. Migrate capability and object creation services.** Move table growth,
  namespace and endpoint creation/export onto the mechanism with typed records.
  Preserve exclusive table loans, capability references and failure unwinding;
  remove their old task APIs and queues.
- [x] **5. Migrate directory and file backing services.** Move entry/name allocation,
  discard and buffer replacement to their owning subsystems. Preserve logical
  file-operation ownership, guaranteed cleanup submission and profiling results.
- [x] **6. Migrate launch preparation.** Preserve capture/group lifetimes and
  batch publication/rollback. Keep launch-local operations direct on BSP, with
  no executor self-waits.
- [x] **7. Migrate HOST forwarding.** Preserve HOST worker ownership, staged data,
  profiling and final completion. Forward from the common executor without
  blocking its progress on another caller's request.
- [x] **8. Consolidate storage and finish the task boundary.** Provision the reusable
  user-request area, separate persistent profiling, and remove superseded payloads,
  queue links, service sweeps and task API dependencies. Review wait-link sharing
  without changing wait semantics. Confirm kernel workers pay no user-request
  storage cost, and task creation/retirement handles all ownership exactly once.
- [ ] **9. Validate and close.** Exercise migrated operations together; compare fresh
  sizes and existing benchmarks with the baseline. Update implemented SMP/memory
  and profiling contracts and technical debt. Move this document into `docs/`,
  replacing completed worklists with implemented behavior and retained limits.

## Task 1 implementation and baseline

Task 1 is implemented by `cad461c` on `kernel/bsp-pipe-requests`. The request
mechanism lives in `kernel/service/request.c`; pipe records, capture, allocation,
installation and rollback belong to `kernel/object/pipe.c` and its header.
`task_create_pipe` and the pipe-specific scheduler queue/service body are removed.

The request header records FREE, PREPARED, QUEUED, SERVICING and COMPLETE states.
Preparation requires FREE storage and prepares the existing task wait. Submission
publishes the record under a separate FIFO queue lock, saving the wait pointer
before lending the record. The synchronous wrapper only inspects results after
wait notification. Completion clears queue/wait references and publishes COMPLETE
before waking; dispatch makes no further accesses to the completed record. Consumption ends with explicit
release to FREE. This state is not an asynchronous polling interface.

The pipe record carries only the capability-table loan, reply and status in
addition to the common header. The service installs both endpoints or closes a
partially installed reader and drops both temporary references, preserving the
previous failure results. It clears the table pointer before completion. The
caller retains existing authority and user-buffer validation and copies the
result locally before releasing the request. Task retirement and prepared-task
discard assert that the embedded request is FREE.

Task 1 used `task_bsp_request_storage` to provide the typed record embedded in
task metadata; task 3 replaces that adapter with a reservation across service
types. Both user and kernel tasks still include the records until task 8. Task 1
used scheduler-driven detached batches and an explicit pending check; task 2
removed both dispatch adapters in favor of the executor described below. Other
subsystem queues and resource waits are unchanged. No public ABI or dependency
revision changed.

### Fresh sizes

Measured with GDB from a forced ordinary kernel rebuild of baseline `3cdb965`
and the task-1 implementation, using GCC 16.2.0, GNU C23, `-O2 -g3` and the
repository's normal freestanding flags. Sizes are bytes, excluding heap overhead
and the separate 16 KiB task stack.

| Storage | Baseline | Task 1 |
| --- | ---: | ---: |
| `struct task` | 7,088 | 7,104 |
| Pipe service fields/typed record | 32 | 56 |
| Common `struct bsp_request` header, included in the typed record | — | 24 |
| `struct task_wait` | 32 | 32 |
| `struct pipe_wait` | 16 | 16 |
| `struct hostfs_request` | 4,904 | 4,904 |
| Persistent memory/file/HOST profile aggregates combined | 816 | 816 |
| `struct pipe_pair`, including its 64 KiB buffer | 65,680 | 65,680 |

The old pipe fields occupied 32 bytes through the next field's alignment; overall
task layout padding makes the task-size increase 16 bytes. This intermediate
migration is not a storage reduction. The 816-byte profile total comprises
232-byte memory, 176-byte file and 408-byte HOST aggregates.

### Interactive validation and measurements

Validation on 2026-09-29 used nested KVM, QEMU 10.2.2 with the documented
[AHCI fix](../qemu.md), CPU model `max`, 256 MiB RAM, matching Fedora OVMF
`/usr/share/edk2/ovmf/OVMF_{CODE,VARS}.fd`, virtio entropy enabled, and no network,
HOST export or block device. One-CPU runs used CPU 0; four-CPU runs used the
Development session on CPU 1. The display backend was `none`, with manual QEMU
monitor keyboard input and framebuffer inspection. SDK, userspace and ports
bundles passed the existing verifier and match the unchanged pinned inputs.

Both revisions ran `session app://iobench.pxe pipe --buffer 4096`: one verified
warmup and five verified 1 MiB samples with fresh pipes/workers per pass. Every
sample wrote and consumed 1,048,576 bytes with 256 write and 256 read calls,
zero short transfers and zero errors. All workers completed successfully.
Elapsed values below are median (minimum–maximum), in milliseconds.

| CPUs | Revision | Write acceptance | Consumer acknowledgment |
| --- | --- | ---: | ---: |
| 1 | Baseline | 2.921 (2.842–3.847) | 3.370 (3.342–4.298) |
| 1 | Task 1 | 3.095 (2.850–3.641) | 3.544 (3.300–4.091) |
| 4 | Baseline | 0.734 (0.720–1.730) | 1.043 (1.030–2.668) |
| 4 | Task 1 | 0.718 (0.707–0.726) | 1.031 (0.993–1.251) |

These are regression observations, not an improvement claim. Pipe creation and
worker launch are outside this benchmark's timed interval; it does not isolate
request queue, service or resumption costs. No timing instrumentation was added.
Owner-host measurements remain unavailable.

Four-CPU GDB inspection stopped the AP immediately after publication and ran
only the BSP until completion. The unparked caller received notification without
being enqueued, returned from its wait without parking, consumed COMPLETE results
and released the header to FREE. A subsequent pipeline reused the same request
address. Its ordinary parked completion cleared the wait and queued the task on
CPU 1. Service ran with IF=0, the kernel root active and no current BSP task.
Single-CPU inspection likewise found the CPU 0 caller parked before service under
the kernel root with IF=0. Debugger-controlled runs were separate from timing.

Checksum pipelines verified 32 KiB stream transfer and a three-stage 1 MiB input
with `head -c 32`; early reader closure produced the expected upstream EPIPE and
the final 32-byte checksum. Both single- and four-CPU shells resumed after child
cleanup. Allocation/install failure unwinding and retirement were also reviewed
by inspection; no allocation failure or task fault was injected.

Ordinary kernel/image builds and `git diff --check` passed. The existing HOST
profiling local named `started` still produces its pre-existing shadow warning;
this change adds no compiler warnings. No tests, boot/output automation or CI
configuration were added. These task-1 observations precede the executor change
below; VM/display handoff inspection remains part of task 3.

## Task 2 executor and validation

Task 2 is implemented by `22be47d` on `kernel/bsp-executor`. Boot calls
`bsp_requests_init()` immediately after `task_init()`, before publishing user
tasks. Failure to create the worker panics with the allocation error: it is
required infrastructure, with no degraded mode or queued work left without an
executor. APs acquire initialization through the existing scheduler-startup
publication. The worker starts runnable, so requests arriving before its first
execution need no special notification.

The executor removes one FIFO head under the request lock and services it with
IF=0 after unlocking. It completes the operation, enables interrupts, and calls
`kernel_task_yield_if_runnable()`. That scheduler-owned helper checks the BSP
ready queue with IF=0 under the scheduler lock and switches only when another
task is runnable, returning with IF=1. Only the BSP dequeues that ready queue,
so a positive check remains valid until the switch. Timer/preemption handling
still services the unmigrated subsystem queues and deadlines. Individual service
operations remain non-preemptible; no quota, priority or service budget was added.

An empty queue causes the worker to prepare and publish an untimed wait under
the request lock, then unlock and sleep. The first publisher takes and clears
that wait pointer under the same lock, then wakes it after unlocking. Subsequent
publishers see no waiter while the worker is runnable or servicing requests.
An early notification is remembered without enqueueing a still-running worker;
a parked worker is enqueued and a remote publisher sends the existing BSP
reschedule IPI. BSP callers send no self-IPI. The worker always finishes its
published wait even if a request arrives before sleep: a publisher may already
hold the detached pointer, so checking the queue again and reusing the wait
would be unsafe. There are no timed wakes or other notification sources for this
wait. Request submission allocates nothing and still validates current-user-task
storage ownership, excluding kernel clients and executor self-submission.

`bsp_requests_service` and `bsp_requests_pending` are removed. Scheduler dispatch
and preemption now see this service only through the executor's normal ready
state; old subsystem queues keep their existing visibility. At task 2, pipe
creation was the only migrated operation. `struct task` remains 7,104 bytes and the pipe
record remains 56 bytes. The executor adds one task allocation and a 16 KiB stack,
plus their existing allocator/VM bookkeeping. Kernel-worker request storage
remains an accepted intermediate cost until task 8.

Ordinary kernel/image builds passed using verified unchanged SDK, userspace and
ports bundles; only the pre-existing HOST-profile shadow warning was emitted.
An independent read-only review found no correctness issue. Interactive validation
used the same one/four-CPU nested-KVM configuration recorded for task 1, with
normal entropy and background kernel workers. GDB inspection established:

- The initialized executor is a BSP kernel task; service runs under the kernel
  root with IF=0 and the caller parked in both CPU configurations.
- On four CPUs, the worker was stopped after publishing its wait but before
  sleeping. An AP published the next pipe operation, detached the worker waiter,
  and notified it while it remained unparked. The worker resumed through its wait
  and serviced the request without a lost wake or duplicate enqueue.
- With the worker parked, AP publication detached its waiter under the request
  lock, then after unlocking placed the worker on the BSP ready queue and reached
  `arch_cpu_reschedule(0)`.
- The boundary after service entered the conditional-yield helper with IF=1.
  A four-CPU observation found no other runnable BSP task and selected no switch.
  A single-CPU completion made the caller runnable, and the helper reached
  `arch_context_switch` with both queue locks released.

Both configurations completed checksum pipelines over 32 KiB and three-stage
pipelines with `head -c 32`, including expected upstream EPIPE and child cleanup.
With GDB detached, each ran `session app://iobench.pxe pipe --buffer 4096`: one
verified warmup and five verified 1 MiB samples, all with 256 reads, 256 writes,
zero shorts/errors and successful worker teardown. Elapsed milliseconds were:

| CPUs | Write acceptance median (range) | Consumer acknowledgment median (range) |
| --- | ---: | ---: |
| 1 | 2.884 (2.872–3.889) | 3.341 (3.331–4.347) |
| 4 | 0.730 (0.708–1.703) | 1.074 (1.022–2.590) |

As in task 1, pipe creation is outside these measured intervals; these results
check regression behavior and make no executor-latency improvement claim. FIFO
order, notification coalescing across publishers, startup failure unwinding and
preservation of old pending queues were also reviewed by inspection. No failure
injection, new tests, boot/output automation or CI changes were introduced. All
validation QEMU/GDB processes were stopped. These observations precede the
private-memory/display migration below.

## Task 3 private-memory/display migration

Private-memory and display requests now use the executor. Their subsystems own
capture, typed payloads, operations, results and loans. The scheduler has one
pending deferred-request pointer; its memory/display payload fields, queues,
service sweeps, pending checks and public submission APIs are removed. Typed
records remain embedded temporarily, and one common reservation rejects any
second preparation until the previous result is consumed and released. Retirement
and prepared-task discard assert that neither this reservation nor a deferred
request remains.

Submission enters DEFERRED and saves the wait without queue publication. After
the ordinary stack/root switch and entry/current-task clearing, the scheduler
establishes parking under its lock, clears the deferred pointer, unlocks, then
publishes to the request FIFO. No caller/request access follows publication.
This applies to both memory operations and ACQUIRE/PRESENT/RELEASE, including on
CPU 0. The executor clears process/display loans before completing the request.
Existing authority, user-buffer validation, allocation rollback, display backing
references and process-exit cleanup remain in their owning subsystems.

Transient memory profiling samples travel in the typed request. Persistent
aggregates and controls remain task-local until task 8; only the resumed caller
accesses them. Profiling still reads five clocks per admitted request and none
when disabled. Publication is measured just before taking the request lock,
after the scheduler has established parking. Queue time therefore includes FIFO
and executor scheduling delay; service remains bounded by the private-memory
operation itself. Saturation and public profile results are unchanged.

GDB sizes with the same compiler/flags as the baseline are 7,184 bytes for
`struct task` (7,104 after task 2), 104 for the memory record and 112 for display.
The common header remains 24 bytes and persistent profiling remains 816 bytes.
The temporary 80-byte task increase also affects kernel workers until task 8.
No public ABI, dependency pins or compiler-container inputs changed.

### Validation and allocation observations

Ordinary kernel/image builds and `git diff --check` passed; only the existing
HOST-profile shadow warning remained. An independent read-only review found no
concrete defect in publication ordering, cross-service ownership, retirement,
profiling or display backing lifetime. Allocation failure unwinding and fatal
fault cleanup were reviewed by inspection, without failure/fault injection.

Interactive one/four-CPU checks used the same nested-KVM configuration recorded
above and verified unchanged SDK/userspace/ports bundles. GDB observed memory
publication under the kernel root, on the permanent stack, with cleared entry
stack/current task and a parked caller. Service ran in the BSP kernel executor
with IF=0; completed records had detached wait/queue links and cleared loans.
Both callers resumed under their original private root and task stack.
All three display operations were observed at deferred publication on four CPUs,
including PRESENT, followed by successful release and resumption. Mandelbrot
rendered and returned to the shell; a subsequent run reacquired the display.

Four-CPU allocation observations compare task-2 main `9d52266` with task 3,
with GDB detached during measurement. Each command ran once, using its defaults:
64 KiB pages, 64 page allocate/release pairs; growth held 128 blocks with 8 MiB
peak payload. All allocations succeeded. The page profile counted 64 requests
in each direction and 4,194,304 bytes each; growth counted 128 backing allocations
and 9,961,472 bytes, with no backing release because libc retains its pools.
Elapsed time, in milliseconds:

| Workload | Task 2 | Task 3 |
| --- | ---: | ---: |
| `allocbench pages` | 27.645 | 32.844 |
| `allocbench pages --profile` | 39.863 | 52.239 |
| `allocbench growth` | 44.919 | 46.375 |
| `allocbench growth --profile` | 55.043 | 64.112 |

Profile interval sums below are milliseconds; each cell is task 2 → task 3.

| Operation | Publication | Queue | Service | Resume | Total |
| --- | ---: | ---: | ---: | ---: | ---: |
| Page allocate | 2.034 → 2.337 | 5.473 → 7.780 | 10.638 → 10.908 | 3.196 → 5.203 | 21.341 → 26.228 |
| Page release | 2.033 → 2.422 | 5.529 → 9.742 | 3.689 → 3.711 | 3.016 → 5.411 | 14.267 → 21.285 |
| Growth allocate | 4.873 → 4.726 | 11.913 → 17.829 | 26.454 → 26.255 | 6.630 → 10.481 | 49.870 → 59.290 |

These samples show additional queue/resume delay while service time is similar.
They are single nested-VM observations with background workers, not statistically
established costs or owner-host performance. Clock-call means were 34–41 µs;
profiling overhead is material and was not subtracted. FIFO admission and a
scheduling opportunity between operations are intentional policy changes.

Single-CPU page and growth workloads also completed with zero failures, with the
same request/byte counts. Page elapsed time was 28.525 ms unprofiled and 53.071 ms
profiled; growth was 40.155 ms and 62.954 ms. In both CPU configurations,
Mandelbrot rendered, released its display/keyboard ownership on exit and
reacquired them on a second run. Checksum pipelines verified a 32 KiB transfer;
the four-CPU three-stage `head -c 32` pipeline also verified early-reader closure,
expected upstream EPIPE and child cleanup. All validation QEMU/GDB processes were
stopped. These observations precede the capability/object migration below.

## Task 4 capability and object creation

Capability growth, namespace creation and endpoint creation/export now use four
ordinary request tags on the common FIFO. Capability and namespace records lend
only the caller's table. Endpoint records lend the process, including its receiver
owner list for creation; export captures its descriptor by value. All handlers
clear loans before common completion. Callers consume results before releasing
the cross-service reservation. The scheduler's old payloads, queues, service
sweeps, pending checks and submission APIs for these operations are removed.

These requests do not mutate private mappings and may complete before parking.
The caller accesses only its saved wait until notification. Growth preserves
entries, generations and references; its consumers keep a retained object or
live source slot, never an entry pointer into replaced storage. Export retains
its existing service-authority/user-buffer checks in the caller and receiver
CONTROL/type/owner checks in the BSP helper. It still validates metadata and
receiver state before allocation; namespace creation still allocates before
installation. Local BSP helpers grow tables directly, without submitting a
nested request. Existing atomic endpoint installation, rollback, object retirement
and namespace capture semantics remain unchanged.

With the same compiler and flags as prior measurements, `struct task` grows from
7,184 bytes on main `3a7cb06` to 7,296 bytes. Typed records, including the unchanged
24-byte common header, are 40 bytes for growth, 48 for namespace creation, 56 for
endpoint creation and 104 for export. These replace the former raw fields but
add 112 bytes overall. Records remain embedded in user and kernel tasks until
task 8; this is another intermediate storage cost, not a size reduction. The
capability table remains 16 bytes and endpoint backing remains 136,784 bytes.

Ordinary kernel/image builds passed with verified unchanged SDK/userspace/ports
bundles; only the existing HOST-profile shadow warning remained. `git diff --check` and independent read-only review passed. No public ABI, dependency pins,
compiler-container inputs, tests or CI configuration changed.

Interactive validation used the same one/four-CPU nested-KVM, QEMU/OVMF, 256 MiB,
CPU `max` and entropy configuration recorded above, without network, HOST or
block devices. GDB established:

- Four-CPU namespace creation and endpoint export ran in the BSP kernel executor
  under the kernel root with IF=0 and parked AP callers. Completion detached
  request links/waits, cleared loans and preserved the reservation for consumption.
- A naturally occurring capability-growth request was stopped on CPU 1 after
  publication but before sleeping. With that AP held, the BSP grew its table
  from 16 to 32 slots and recorded completion/notification while the caller stayed
  unparked. The AP then consumed the result without parking or a lost wake.
- On one CPU, growth and endpoint creation ran with the caller parked. Growth
  returned its record to FREE before the same caller submitted endpoint creation;
  creation returned both handles with a cleared process loan.

Four-CPU provider checks exercised publication, lookup, update, denied namespace
removal and restricted ADD, replacement, provider exit/closed lookup, and removal
followed by NOT_FOUND. One-CPU publication, restricted lookup and removal also
passed. A one-CPU pipe regression produced checksum `1349564844` for 32,768 bytes.
Failure unwinding, retained-reference lifetime and unchanged error ordering were
reviewed by inspection; no allocation failure or task fault was injected.

With GDB detached, `ipcbench` used 64-byte messages, eight messages per pass, one
verified warmup and five verified samples with fresh receivers/endpoints. The
four-CPU CALL baseline on `3a7cb06` had completion median 2.177 ms (2.026–2.855);
task 4 measured 2.068 ms (2.042–3.222). Every pass verified eight round trips,
512 request and 512 reply bytes, with no failed calls/deliveries. The one-CPU
SEND run admitted and acknowledged all eight messages/512 bytes per pass with
zero rejections: completion median 0.504 ms (0.502–0.516), admission median
0.037 ms. Creation, launch and cleanup are outside these timed intervals, so
these are delivery regression observations, not measurements of request queue
latency or owner-host performance. All QEMU/GDB processes were stopped.

These observations precede the directory/file backing migration below.

## Task 5 directory and file backing

RAM entry allocation, rename-name allocation and detached-entry disposal now
belong to a typed request in `kernel/fs/ramfs.c`. Allocation returns an owned,
unpublished entry; the caller fills the name, performs existing capability and
directory rechecks, and either publishes or disposes of it. Disposal transfers
the detached entry, releases its child reference and clears the pointer before
completion. Submission never allocates, including cleanup after failure.

RAM file replacement now belongs to `kernel/object/file.c`. The caller keeps its
live file reference and logical `busy` ownership across the request, without
holding the file spinlock. Resource waiters detach before wakeup; a caller can
finish that wait and then prepare a service wait. The executor clears its file
loan before completion. Only the caller updates logical size and hands operation
ownership onward. Zero-capacity replacement frees backing without allocation;
geometric growth and exact-size fallback remain separate, fully consumed requests.

Both services use ordinary FIFO publication and prompt executor notification;
neither mutates private mappings. Old task payloads, service APIs, directory/file
queues, scheduler sweeps and pending checks are removed. Local allocation/disposal
and replacement helpers are private to their subsystems. Persistent FILE profile
aggregates and controls remain task-local until task 8. Transient samples travel
in the request: preparation starts before wait preparation, publication is stamped
immediately before the request lock, service brackets the unchanged local helper,
and caller resumption precedes aggregation. Missing allocation/copy/release phases
remain zero; inactive collection adds no clock reads.

GDB sizes from ordinary builds with the existing compiler/flags are 7,296 bytes
for `struct task` on baseline main `74a0bf3`, and 7,328 after task 5. The RAMFS
record is 56 bytes and the FILE record 144, including their 24-byte headers.
`file_wait` remains 16 bytes and persistent FILE profiling 176. The temporary
32-byte task increase also affects kernel workers until task 8. No public ABI,
dependency pins or compiler-container inputs changed.

### Validation and matched RAM controls

Kernel/image builds and `git diff --check` passed using verified unchanged
SDK/userspace/ports bundles. Only the existing HOST-profile shadow warning
remained. Independent read-only review found no concrete defect. Failure fallback,
allocation-failure cleanup and file waiter detachment were reviewed by inspection;
no failure injection or new tests/boot automation were added.

Interactive checks used the one/four-CPU nested-KVM configuration recorded above:
QEMU 10.2.2 with the documented AHCI fix, CPU `max`, 256 MiB, matching OVMF,
entropy enabled, and no network, HOST or block device. GDB observed an AP's RAMFS
allocation complete before sleeping: notification and an owned entry were ready
while the caller remained unparked, and consumption cleared the request's entry
pointer. FILE service ran in the BSP kernel worker under the kernel root with
IF=0, a parked caller, `busy` set and no file spinlock held, on both CPU counts.
Completion cleared the file loan. For a zero-capacity request, data/capacity were
cleared while logical size still held 32,768 and `busy` remained set; the caller
then finished resize and regrowth. No remote user-buffer or stack access was used.

Directory create, rename, replacement rename, file removal and empty-directory
removal passed. Truncate/regrow preserved the 32 KiB fixture checksum; replacement
rename preserved the 1 MiB checksum. A PXE copied into a RAM file after truncation
loaded and ran successfully. All QEMU/GDB processes were stopped.

Four-CPU baseline and migrated builds each ran eight matched `iobench` controls:
write/generated and copy/`app://share/iobench.bin`, grow-from-zero and `--prepared`,
profiling off and `--profile`. Each used a unique RAM output, default 4,080-byte
buffer, one warmup and five samples, no sync and GDB detached during measurement.
All 96 passes verified the 1 MiB contents, length and EOF, with 258 writes and no
short writes or failures; copy also used 258 reads with no shorts. Transfer times
below are median (minimum–maximum) milliseconds.

| Workload | Profile | Baseline | Task 5 |
| --- | --- | ---: | ---: |
| Write, grow | off | 59.041 (52.402–59.180) | 1.957 (1.900–2.721) |
| Write, grow | on | 52.634 (52.103–71.288) | 6.149 (5.657–7.676) |
| Write, prepared | off | 0.275 (0.265–0.293) | 0.284 (0.269–0.291) |
| Write, prepared | on | 0.299 (0.291–0.377) | 0.293 (0.292–0.317) |
| Copy, grow | off | 58.852 (58.218–59.218) | 2.540 (2.171–2.605) |
| Copy, grow | on | 57.780 (51.943–59.660) | 6.280 (6.189–6.705) |
| Copy, prepared | off | 0.481 (0.478–0.493) | 0.491 (0.479–0.536) |
| Copy, prepared | on | 0.491 (0.479–0.586) | 0.487 (0.479–0.492) |

Profiled growth retained ten successful replacements per pass, requested-capacity
sum 4,173,840 and copied-byte sum 2,084,880; prepared passes reported zero events.
For illustration, the third profiled write pass's queue sum fell from 46.504 to
1.283 ms while service was 2.826 versus 2.763 ms. The corresponding copy pass's
queue sum fell from 51.411 to 1.485 ms, with service 2.745 versus 2.692 ms.
This is consistent with removing the missing-notification delay; allocation and
copy policy did not change. Queue intervals still combine locking, worker
availability and scheduling. These sequential five-sample nested-VM groups do
not isolate a constant instrumentation cost or establish owner-host performance.
Prepared controls remain close to baseline. Profiling adds material overhead to
the now-shorter growing transfers and must not be subtracted as a constant.

Single-CPU controls also passed: growing writes had median 2.153 ms unprofiled
and 6.816 ms profiled, with unchanged replacement counts/bytes. Prepared profiled
copy had median 0.472 ms and zero replacement events. Each included one warmup
and five verified samples. The missing FILE publication notification is resolved;
individual non-preemptible services and shared FIFO scheduling remain the agreed
limits. The remaining migrations are launch preparation (task 6) and HOST forwarding
(task 7).

## Task 6 launch preparation

Launch capture allocation/disposal, single launch and batch group operations now
use a typed `launcher_request` on the ordinary common FIFO. The launcher owns
submission, service and result consumption. The caller captures its parent
process and assigned CPU for child preparation, lending the parent table and image
operation. The service clears capture/group/parent pointers before completion.
Capture and group allocation results transfer to the caller before request
release. Their independent heap allocations persist across calls; transient
request reuse does not overwrite captured startup data or prepared children.

START and GROUP_PREPARE consume capture and any owned HOST image bytes on success
or failure. Group preparation retains unpublished children and observers until a
later publish/discard request. Publication copies the ordered handles, preserves
zero unused slots, publishes every child and consumes the group. Rollback still
removes provisional observers and destroys prepared children without publishing
any. Submission/discard needs no allocation. Caller-side user capture, authority,
file operation waits, reply validation and CPU assignment remain unchanged.

BSP-local loading, capability installation and task preparation remain direct
calls. HOST image capture still completes through the existing HOST worker before
launch submission; the executor never submits to itself or blocks on transport.
The old launch task fields, APIs, queue, scheduler sweep and preemption check are
removed. HOST forwarding is the remaining subsystem queue migration (task 7).
Launch-local capture/helper declarations now live in a private header shared by
the launcher and spawn implementation.

Ordinary GCC 16.2.0 build/debug sizes are 7,328 bytes for `struct task` on baseline
main `b64151f` and 7,376 after task 6. The typed launch record is 160 bytes;
`launch_capture` remains 65,736 bytes and `launch_group` 208. The temporary 48-byte
task increase also affects kernel workers until storage consolidation in task 8.
No public ABI, submodule pin or compiler-container input changed.

Kernel and image builds passed with verified unchanged SDK/userspace/ports
bundles, as did `git diff --check` and independent read-only code review. Only the
existing HOST-profile shadow warning remained. Interactive validation used one
and four CPUs under nested KVM, QEMU 10.2.2 with the documented AHCI fix, CPU `max`,
256 MiB, matching OVMF, entropy and a private virtiofsd 1.14.0 export; network and
block devices were absent. All QEMU/GDB/daemon processes were stopped afterward.

On both CPU counts, boot/session and single launches, repeated-image pipelines,
invalid-image batch rollback, and RAM/HOST executable pipelines passed. Both RAM
and HOST copies of `cat.pxe` piped the 32 KiB fixture to `cksum`, producing
`1349564844 32768`. A batch with a valid first child writing a marker and an invalid
second image failed at stage 2, leaving the marker empty; a later valid pipeline
completed normally. Four-CPU early-reader closure produced the expected upstream
closed-end errors and returned to the prompt.

GDB observed launch service in the BSP kernel worker with kernel root active,
IF=0, a parked caller and matching captured parent/CPU. INITRD image operation
ownership was held without its spinlock. HOST launch used an owned 49,087-byte
image capture. Completed requests had detached wait/queue links and cleared
capture/group/parent loans; allocation-result ownership had been removed before
release. During four-CPU rollback, one child was prepared, its provisional
observer slot was cleared, its task was discarded, and the group count became
zero before disposal. On one CPU, a three-stage publication returned its three
ordered handles with all five unused slots zero.

Early completion and allocation-exhaustion unwinding were reviewed by code;
this task did not inject failures or force an early-completion interleaving.
No new tests, self-tests or boot/output automation were added. Individual launch
operations remain non-preemptible; no launch latency or owner-host performance
claim is made from these interactive checks.

## Task 7 HOST forwarding

HOST callers now reserve a typed `hostfs_request` through the common BSP FIFO.
The executor marks it FORWARDED and hands it to the existing HOST worker without
waiting for transport. The worker owns final completion, including unavailable
forwarding and initialization failure. It clears queue links, input loans and
the transport profile pointer before marking COMPLETE and waking the caller;
neither forwarding nor completion touches the record after returning ownership.
FIFO admission does not impose completion order across services.

Callers retain the reservation through scalar/reply consumption and staged user
copies. Returned objects and executable captures transfer to independent caller
ownership before release. Capability growth and launch preparation happen after
that release. All 13 call sites use explicit release on success and failure.
The old HOST task APIs, scheduler queue/sweep and preemption check are removed.
Profiling aggregation moves into HOST code; its persistent task-local snapshot
and existing phase/counter meanings remain unchanged. No public ABI or dependency
pin changes; no compiler-container rebuild is needed.

Ordinary GCC 16.2.0 sizes move from 7,376 to 7,392 bytes for `struct task`, and from
4,904 to 4,920 for `hostfs_request`. The transient profile remains 120 bytes and
the persistent HOST snapshot 408. The temporary per-task increase, including
kernel workers, remains for storage consolidation in task 8.

### Slowdown investigation and correction

The first migrated build regressed despite unchanged transfer counts. A fresh
four-CPU boot with no debugger connection reproduced a 145.284 ms unprofiled
read median (143.888–146.462 ms), excluding a retained GDB connection as the cause.
Code inspection found unconditional monotonic-clock reads on every BSP scheduler
pass, even with no timed waits or kernel sleepers. The added executor scheduling
path amplified this cost. The clock reads HPET high/low/high through uncached
MMIO; ordinary guest CLOCK-call calibration was roughly 32–38 microseconds per
call in this nested environment, including syscall overhead.

The correction skips the clock read when each scheduler deadline list is empty,
checking timed waits under the existing queue lock and kernel sleepers under
BSP/IF=0 ownership. HOST's untimed idle wait also skips its redundant deadline
check. Nonempty expiration, finite transport deadlines, wake ordering and the
executor's conditional yield remain unchanged. Independent review found no
locking or wakeup defects.

A direct-parking experiment alone measured read medians of 165.253 ms off and
190.976 ms on, so it was discarded. With the original yield policy restored,
the two empty-list guards alone measured 90.118 ms off and 146.220 ms on. The
final build below also omits the untimed HOST deadline check. These comparisons
support eliminating unnecessary clock work as the remedy; they do not isolate
an exact per-read cost or attribute all elapsed time to HPET.

Matched four-CPU controls used baseline main `515b4cb`, initial forwarding, and
the corrected implementation. Each cell is median milliseconds (minimum–maximum)
from five samples after one warmup, with 1 MiB, sync off, default read buffer
4,088 bytes and write/copy buffer 4,080 bytes. Destinations were prepared outside
the transfer interval. Read reports payload time; write/copy report transfer time.
Profiling was off then on for each workload, with GDB detached during all controls.

| Workload | Baseline | Initial forwarding | Corrected forwarding |
| --- | ---: | ---: | ---: |
| Read, profile off | 88.099 (86.814–88.677) | 148.633 (146.378–153.036) | 87.288 (84.926–89.499) |
| Read, profile on | 164.220 (157.614–168.897) | 224.834 (220.818–230.078) | 164.230 (159.882–187.305) |
| Prepared write, off | 87.180 (86.608–91.942) | 132.446 (130.606–137.435) | 86.481 (85.126–88.259) |
| Prepared write, on | 162.126 (158.666–182.229) | 218.808 (213.359–229.312) | 168.413 (158.678–187.943) |
| HOST → prepared RAM, off | 91.431 (85.795–98.178) | 145.698 (144.447–146.685) | 88.375 (87.424–91.203) |
| HOST → prepared RAM, on | 180.528 (161.571–199.594) | 216.831 (215.701–221.765) | 159.819 (156.815–169.097) |

All 108 passes across these three sets verified contents and length. Profiled
reads counted 257 native READs and 258 transport submissions/completions, including
lazy OPEN; prepared writes and copies each counted 258 native HOST operations
and 258 transport submissions/completions. Each transferred 1,048,576 bytes with
no native/transport failure, short transfer or in-window EOF. Read's verification
EOF occurs outside HOST collection. Final unprofiled times return to baseline;
profiled read/write ranges overlap baseline and still show substantial observer
overhead. These sequential groups neither establish owner-host performance nor
justify subtracting a constant profiling cost.

Final one-CPU read controls also verified all twelve passes: off median 96.956 ms
(96.461–97.796), on 172.803 ms (172.313–173.393). No matched one-CPU baseline was
collected for this task.

### Validation and limits

Kernel/image builds passed without warnings using verified unchanged SDK,
userspace and ports bundles. `git diff --check` and independent read-only code
review passed. Interactive validation used nested KVM, fixed QEMU 10.2.2,
CPU `max`, 256 MiB, matching OVMF, entropy and a private virtiofsd 1.14.0 export
with default cache policy. No cache eviction was attempted; network and block
devices were absent. The one- and four-CPU checks exercised HOST creation,
read/write, enumeration, rename, sync, removal, missing-file failure and HOST
executable loading. Read-only session creation was denied as expected. The final
one-CPU HOST executable pipeline returned `1349564844 32768` for the 32 KiB
fixture, followed by successful rename/sync/removal. A separate four-CPU boot
without virtio-fs reached the shell, listed RAM storage and returned not-found
for the absent HOST mount. All QEMU/GDB/daemon processes were stopped afterward.

GDB observed BSP kernel-root/IF=0 forwarding with parked callers on one and four
CPUs. On one CPU, forwarding returned while the request remained FORWARDED in the
HOST queue. At caller release, completed records had detached links, cleared input
loans and consumed owned output; READ retained its reservation through copying.
These observations do not claim that a second caller was serviced during the
observed outstanding transport. Early completion, immediate rejection and worker
initialization failure were reviewed by inspection without forced interleavings
or injected failure. No new tests, self-tests or boot/output automation were added.
Individual BSP operations remain non-preemptible; active deadlines and profiling
still incur HPET cost.

## Task 8 storage consolidation and task boundary

Each user task now provisions one reusable request allocation and one separate
persistent profiling allocation on the BSP before publication. The explicit
service catalog records every typed request's size, alignment and header offset;
initialization validates them against the heap alignment contract and computes
the maximum. Every preparation checks its layout, reserves the same allocation
and zeroes the selected typed range. There is no union or fixed page-size limit.
Submission allocates nothing, including cleanup operations under memory pressure.

The task layer no longer embeds service payloads or selects storage by service
tag. Its adapters reserve/release the area, expose the caller's current storage,
and register the deferred scheduler handoff. Profiling controls and accessors now
belong to `kernel/service/profile.c`. MEMORY, FILE and HOST aggregates retain their
independent caller scope and BEGIN/SNAPSHOT/END behavior across request reuse.
Kernel workers allocate neither user area and cannot enter the synchronous client
path. Launch captures/groups and HOST image captures keep their separate owned
lifetimes; this change does not fold them into the transient area.

A common 16-byte resource link replaces four identical file/console/process/pipe
records and their task APIs. Resource queues retain their existing locks and
ordering; a waker detaches the link before returning ownership. Process completion
also clears the removed next pointer. Console timeout cleanup removes any queued
link under its resource lock before reuse. The link remains separate from service
storage and the scheduler wait record; no deadline or early-wake semantics change.

Preparation failure, unpublished-task discard and retirement share cleanup of
request/profile storage, the kernel stack and task metadata. The failed preparer
still owns its inactive process. Cleanup asserts no active reservation, deferred
publication, timed wait or resource queue link remains, and request destruction
requires FREE state. Allocation-exhaustion paths were reviewed without injection.

Fresh ordinary GCC 16.2.0 builds compared merged main `e887df5` with this change:

| Storage | Before | After |
| --- | ---: | ---: |
| Task metadata, user or kernel | 7,392 B | 752 B |
| User request allocation | embedded records | 4,920 B |
| Persistent user profiling | embedded 816 B | 816 B |
| Combined user metadata/request/profiles | 7,392 B | 6,488 B |
| Combined kernel metadata/request/profiles | 7,392 B | 752 B |
| Resource links per task | 4 × 16 B | 16 B |

The largest request is HOST, requiring 8-byte alignment; kmalloc guarantees at
least 16. User storage decreases by 904 bytes and worker storage by 6,640 bytes.
These totals exclude allocator rounding/headers and unchanged 16 KiB kernel
stacks. User metadata now uses three heap allocations rather than one. Eager
request/profile provisioning remains an accepted cost even for users that never
access HOST or enable collection; revisit conditions are in technical debt.

### Validation and performance controls

Kernel/image builds passed without warnings using verified unchanged SDK,
userspace and ports bundles. Diff checks and independent read-only review passed.
No public ABI, dependency pin, compiler-container input, test infrastructure or
boot/output automation changed. Interactive validation used one and four CPUs,
nested KVM, fixed QEMU 10.2.2, CPU `max`, 256 MiB, matching OVMF, entropy and a
private virtiofsd 1.14.0 export with default cache policy; no network/block devices
or cache eviction. All QEMU/GDB/daemon jobs were stopped after validation.

GDB observed the same caller and allocation reused for HOST then RAM FILE service,
with FREE state and no reservation between them. Service ran in the BSP executor
under kernel CR3/IF=0; the caller was parked, and the executor's request/profile
pointers were NULL. Normal retirement reached cleanup with no outstanding wait,
reservation or resource link. Stepping through request and profile destruction
reduced the heap live-allocation count once for each. Invalid second-image launch
reached unpublished-task discard with a FREE request and separate profile block;
the first child's marker remained empty, followed by successful later commands.

Four-CPU memory and one-CPU display requests retained deferred publication under
the kernel root with the local current task cleared and the reserved allocation
still attached to the caller. The display caller was parked; Mandelbrot rendered
and Escape returned to the shell. A one-CPU HOST executable pipeline returned
`1349564844 32768`, followed by successful enumeration, rename, sync and removal.
Four-CPU `allocbench pages --profile` completed 64 allocations and releases with
4,194,304 bytes in each direction and no failures. Pipe transfer warmup and five
samples verified all 1 MiB with no errors. One-CPU IPC SEND warmup and five samples
verified eight 64-byte messages each, normal child exit and cleanup.

Growing HOST-to-RAM copies with both `--profile --host-profile` verified a warmup
and five samples on both CPU counts. Each measured pass kept 258 HOST READs and
258 successful transport completions independent of ten RAM replacements,
4,173,840 bytes of requested capacity and 2,084,880 copied bytes. The profiles
survived repeated HOST/FILE storage reuse. This is functional evidence, not an
unprofiled throughput comparison.

Fresh four-CPU HOST controls used the corrected task-7 main as baseline. Each
cell is median milliseconds (minimum–maximum), five verified 1 MiB samples after
one warmup. Read uses 4,088-byte buffers and reports payload time; prepared writes
and HOST-to-RAM copies use 4,080-byte buffers and report transfer time. Sync was
off, and no debugger was attached during either set. All 72 passes verified
contents and length with unchanged native/transport counts and no failures or
short transfers; read's final EOF check remained outside HOST profiling.

| Workload | Main `e887df5` | Consolidated storage |
| --- | ---: | ---: |
| Read, profile off | 87.080 (84.688–89.368) | 86.098 (85.249–93.095) |
| Read, profile on | 164.119 (155.568–168.088) | 156.455 (155.762–192.088) |
| Prepared write, off | 87.611 (84.755–88.829) | 87.048 (85.353–89.120) |
| Prepared write, on | 156.771 (155.896–165.279) | 158.575 (156.087–161.233) |
| HOST → prepared RAM, off | 86.011 (85.154–88.906) | 85.038 (84.796–85.288) |
| HOST → prepared RAM, on | 158.418 (156.641–167.654) | 162.378 (158.348–169.866) |

These controls show no material regression in this nested environment; they do
not establish owner-host performance. Profiling remains intrusive, with occasional
outliers, and no constant correction is justified. Early completion, allocation
exhaustion, concurrent console timeout/handoff and fatal-fault retirement were
reviewed by inspection rather than forced. Task 9 remains the combined validation
and milestone closure step.

## Validation and exclusions

Use ordinary builds and interactive QEMU with one and four CPUs. Exercise pipe
pipelines, object creation, RAM and HOST file operations, launching and batch
cleanup, private memory, mapped display, and exit/fault retirement. Use GDB to
inspect early versus parked completion, request reuse, worker sleep/publication,
and CR3/stack state at VM handoff. Review failure unwinding without adding fault
injection. Run existing allocation/file/IPC or TCP workloads where affected;
separate queue, service and resumption costs and report nested-VM measurements as
such. Inspect existing CI at the submitted revisions.

No new tests, benchmark infrastructure, CI or boot automation. No allocator locks
for cross-CPU allocation, remote VM mutation, new syscalls, public async API,
cancellation, priorities, worker pools, process threads or task migration. The
[future scheduling direction](scheduling-and-threads.md) remains separate and must
replace exclusive-process loan assumptions before allowing sibling execution.
