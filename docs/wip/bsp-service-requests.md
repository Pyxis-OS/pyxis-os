# Task state and BSP service requests

Status: agreed next milestone after the completed
[read-only filesystem milestone](../filesystem-readonly.md). This document selects
the boundaries and scheduling policy; individual tasks still require their own
implementation PRs. It does not authorize concurrent allocation, task migration
or process threads.

## Problem and completion target

[`task.c`](../../kernel/task.c) combines execution state with capability growth,
directory allocation, file-buffer replacement, HOST requests, launch preparation,
memory/display operations, and pipe, endpoint and namespace creation. Request
payloads, queue links, service bodies and profiling state accumulate in `task`.
Both the scheduler loop and preemption decision enumerate subsystem queues.

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

Memory/display requests use deferred publication. The scheduler first switches to
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

- [ ] **1. Establish request ownership and one ordinary consumer.** Implement the
  narrow operation lifecycle and synchronous wrapper using pipe creation first.
  Put pipe creation/rollback in its subsystem. Storage can temporarily use the
  existing task record, and dispatch can temporarily run from the scheduler, but
  isolate those adapters. Preserve atomic installation and early completion.
  Record fresh structure sizes and existing workload measurements as the baseline.
- [ ] **2. Introduce the BSP executor and agreed scheduling policy.** Move migrated
  dispatch to a dedicated worker with FIFO, idle notification and conditional
  yield. Establish initialization/failure ordering, single-CPU progress, and the
  prohibition on self-submission. Keep pending-work visibility correct while
  older queues coexist; remove the migrated queue from scheduler/preemption logic.
- [ ] **3. Migrate private-memory and display handoffs.** Replace subsystem-specific
  scheduler payload/pending knowledge with one explicit deferred submission path.
  Keep authority and reply validation in callers, operations in their subsystems,
  inactive-root ownership and CR3 reload ordering unchanged.
- [ ] **4. Migrate capability and object creation services.** Move table growth,
  namespace and endpoint creation/export onto the mechanism with typed records.
  Preserve exclusive table loans, capability references and failure unwinding;
  remove their old task APIs and queues.
- [ ] **5. Migrate directory and file backing services.** Move entry/name allocation,
  discard and buffer replacement to their owning subsystems. Preserve logical
  file-operation ownership, guaranteed cleanup submission and profiling results.
- [ ] **6. Migrate launch preparation and HOST forwarding.** Preserve capture/group
  lifetimes, batch publication/rollback, HOST worker ownership, staged data and
  final completion. Keep launch-local operations direct on BSP, with no executor
  self-waits. These migrations may use separate focused dependency-free PRs.
- [ ] **7. Consolidate storage and finish the task boundary.** Provision the reusable
  user-request area, separate persistent profiling, and remove superseded payloads,
  queue links, service sweeps and task API dependencies. Review wait-link sharing
  without changing wait semantics. Confirm kernel workers pay no user-request
  storage cost, and task creation/retirement handles all ownership exactly once.
- [ ] **8. Validate and close.** Exercise migrated operations together; compare fresh
  sizes and existing benchmarks with the baseline. Update implemented SMP/memory
  and profiling contracts and technical debt. Move this document into `docs/`,
  replacing completed worklists with implemented behavior and retained limits.

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
