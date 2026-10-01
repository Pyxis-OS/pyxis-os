# Runtime SMP and independent spaces

Status: agreed milestone direction, 2026-10-01; implementation not started.
Complete the [writable filesystem core](writable-filesystem-core.md) first.
This milestone replaces the earlier proposal to combine CPU-independent spaces
with multiple threads per process while retaining all BSP services.

The outcome is concrete: existing single-task processes share eligible CPU
capacity, spaces retain their identity and authority as tasks move, and private
memory allocation/release executes on the caller's CPU. Boot setup and selected
runtime services can remain BSP-owned. This is not a promise that every subsystem
becomes parallel or that resource isolation is complete.

Related: [current SMP rules](../kernel/smp.md), [private memory](../kernel/memory.md),
[BSP requests](../kernel/bsp-service-requests.md), [spaces](spaces.md),
[Neovim/libuv requirements](neovim-libuv.md) and [technical debt](../technical-debt.md).
Those subsystem references describe current behavior until implementation updates
land; the contracts below describe the agreed target.

## Placement and authority

A space owns processes and their resource/authority domain. A process owns its
address space and capabilities; it continues to have exactly one user task in
this milestone. The scheduler places tasks on eligible CPUs. Moving execution
must not change namespace bindings, terminal/display ownership, capabilities,
execution-group membership or the identity exposed by those objects.

All online CPUs, including the BSP, are eligible for userspace by milestone
completion. Intermediate PRs may keep AP-only placement while replacing ownership
assumptions; the single-CPU configuration must keep working throughout. Kernel
work must be considered when choosing placement. Eligibility is not a guarantee
that a busy CPU immediately runs a particular task.

Trusted boot configuration describes initial spaces by session identity, init
program and optional CPU set. Conceptually, not as selected command syntax:

```text
development: init=development.sh, cpus=all
readonly:    init=readonly.sh,    cpus=all
remote:      init=remote.sh,      cpus=all
```

Keep the existing initial session roles, but stop deriving their number or
identity from CPU count. Keep Caelum's log space and its restricted authority;
its special purpose does not give it ownership of CPU 0. Dynamic space creation
and destruction are outside this milestone.

Default affinity is all online CPUs. A space may instead use one CPU or a set;
sets may overlap and do not reserve CPUs exclusively. Children inherit their
space's boot-configured eligibility. No public affinity capability or per-process
setter is required now. An explicit set naming unavailable CPUs fails clearly;
never silently broaden it. Configuration names identify sessions, not authority.
The exact boot/make syntax and invalid-configuration diagnostic are task-1
contracts to settle before implementation. Replace CPU-index init selection
explicitly and update its consumers; do not silently reinterpret old indices.

Use initial per-task round-robin fairness and modest load balancing. A space with
more runnable tasks can receive more total CPU time. CPU masks provide placement
constraints, not CPU budgets, memory limits or protection against exhaustion of
shared services. Per-space resource accounting/quotas remain separate work.

## Evidence and safety boundaries

Read-only investigation used main `36a199a`, after filesystem task-1 acceptance.
These entry points identify the main coupling; recheck them against the completed
filesystem-core revision before implementation. No new speedup or contention
measurement is established by this investigation.

| Current coupling | Evidence at the inspected revision | Required change or retained guarantee |
| --- | --- | --- |
| One space per CPU; init and UI lookup by CPU | `kernel/space.c`, `kernel/user/boot.c`, `kernel/user/launch.c` | Independent registry/session configuration; explicit context for kernel services. |
| CPU equality in task preparation and execution-group admission | `user_task_prepare_on()` in `kernel/task.c`; `execution_group_check()` and `execution_group_launch_begin()` | Keep space/group authority while making execution placement mutable. |
| Ready queues, parking and stop publication | `kernel/task.c` | Preserve queue locking and early-wakeup handshake; publish no stack still executing. |
| Dispatch state and user entry | `arch/x86_64/user.c`, `arch/x86_64/syscall_entry.S` | Install destination CR3, entry stack and user CPU state; kernel GS remains CPU-local. |
| Heap/PMM/VM have one allocator owner and shared scratch aliases | `kernel/mm/heap.c`, `kernel/mm/pmm.c`, `kernel/mm/vm.c`, `arch/x86_64/paging.c` | Synchronize metadata, provide CPU-local scratch, prove growth/publication and mapping lifetime. |
| Serial services rely on BSP execution and IF=0 | `kernel/service/request.c`, `kernel/fs/native.c`, `kernel/fs/hostfs.c`, `kernel/virtio/blk.c`, `kernel/net/interface.c` | Retain ownership initially. Off-BSP workers need explicit cross-CPU handoffs, not just different affinity. |

### Scheduler and process lifetime

Start with the existing per-CPU ready queues and one short global queue lock.
Keep placement/state changes under that lock, preserve resource/group-to-queue
lock ordering, and never hold it across a context switch. Per-queue locking is
not a prerequisite; revisit it if measurement identifies contention.

Allow initial load-aware placement and migration of runnable user continuations
at safe user-preemption boundaries. The source must leave the task stack and
private root before publishing it elsewhere. Initially, a blocked syscall or
service-request continuation resumes on its previous CPU; migration becomes
eligible again after user return. Do not migrate parked, loaned, executing or
retired tasks. Audit retained CPU-local pointers and captured placement decisions.

Preserve wake-before-park notification, remote wake/stop delivery and exactly one
executing context per task. Idle balancing must also account for a running task:
today preemption can skip scheduling when the local queue has no competitor.
Choose bounded balancing triggers that can correct placement without transferring
a live context or sending unnecessary IPIs. Exact load scoring and trigger
mechanics belong to the scheduler task, not a new policy framework.

Batch launch must enroll all members and publish the complete prepared batch
before any child can execute, even with several destination queues. Preserve
execution-group sealing, termination, loan unwind and completion only after
attributed cleanup. The existing serial reaper may remain on the BSP; moving a
task must not allow its process, request storage or stack to be freed early.

### Allocation, private roots and shared kernel mappings

Use a synchronized shared TLSF allocator and bitmap PMM first, not per-CPU heaps
or caches. Locks protect allocator metadata and statistics; frame zeroing and
page-table walks do not belong inside the PMM lock. Heap growth needs a separate
serialized reserve/map/publish/unwind sequence and a recheck after dropping the
heap lock. Define lock ordering and interrupt state; do not wait while holding a
lock needed by the completing CPU. Kernel workers cannot use the current
user-task-only synchronous request path as a pool-growth fallback.

Give each CPU distinct scratch aliases with their required page-table ancestors
established before use. Only the owner accesses/remaps its aliases, with local
invalidation. Private VM records and capability tables retain exclusive ownership
by the sole task or a deliberately inactive loan; object refcounts alone do not
make capability-table growth/resolve/close concurrent-safe.

For single-task migration, the source leaves its private root and the destination
reloads CR3. With current PCID/global-page settings, that supports exclusive-root
handoff without introducing concurrent sibling execution or a private-root
shootdown solely for migration. Audit all other borrowers and teardown paths.

Shared upper-half mappings need separate treatment. Current task-stack reuse
relies on dispatch CR3 reloads, and heap pools remain mapped. General kernel
unmap/remap, permission changes and physical-frame reuse cannot become safe merely
by adding an allocator lock. CPU-local invalidation is insufficient when another
CPU can retain or use the mapping.

Before enabling concurrent heap growth, settle its precise mapping publication
and failure-unwind contract. A retained, never-reused heap virtual arena is one
candidate; general first-fit VM allocation is not automatically such an arena.
Another solution must supply equivalent invalidation/quiescence evidence. Do not
silently adopt a new VM reservation policy while implementing the allocator task.
General kernel-range reuse stays with its existing owner/protocol until a wider
contract is established; BSP ownership by itself is not proof of safe remote use.

After those prerequisites, MEMORY ALLOCATE/RELEASE can mutate the sole caller's
private root locally. Preserve eager zeroing, permission checks, disjoint allocation,
exact release, rollback, and the prohibition on accessing released request memory.
No other task or outstanding loan may concurrently use that root. Preserve stop
handling and profiler meaning while retiring the obsolete BSP memory request.
Local capability growth and other allocation-backed operations are later bounded
follow-ups unless required for this memory path; allocator availability alone does
not authorize replacing their ownership contracts.

## Performance records and validation

Capture a baseline before implementation and a comparable result after the
milestone, plus matched before/after records for substantial intermediate changes
that could affect performance. Use existing tools and report results in the
relevant PR and a concise record linked here. No benchmark framework, permanent
boot automation or new CI gate is authorized by this milestone.

Record exact parent/dependency revisions, build options, guest CPUs/RAM/devices,
QEMU version/accelerator, host or nested-VM environment, workload commands and
sizes, warm/cold preparation and repetitions. Preserve individual samples plus a
summary and spread. Change one relevant condition at a time where practical;
separate unavoidable environment changes rather than claiming a matched speedup.
Keep unprofiled elapsed controls when profiling affects execution. Unexpected
regressions require explanation and an owner decision, not automatic unrelated
optimization. Documentation-only work needs no performance run.

Select a small reproducible set from [allocbench](../development/allocation-profiling.md),
[I/O and IPC tools](../development/io-ipc-baselines.md) and existing TCP measurements:

- Warm heap allocation, fresh backing growth and page allocation/release.
- RAM/HOST reads, growing writes and copies; native reads with unchanged fixtures.
- TCP throughput with matched payload and connection configuration.
- Concurrent sessions running allocation and I/O/network work, recording per-client
  latency/throughput and aggregate progress as well as single-client results.

Use remote-terminal command/group completion instead of prompt scraping. Do not
attribute all TCP or HOST latency to the BSP: host services, transport and nested
virtualization remain confounders. The existing prompt BSP notification improvement
is already in the baseline; do not count it as a result of this milestone.

Validate ordinary builds and interactive boots with 1, 2, 4 and a higher CPU count,
matching devices and accelerator to the feature. Inspect migration, GS/TSS/entry
stack and FP/FS/user-GS preservation; wake-before-park; remote termination while
blocked; batch rollback/publication; loans; and task/object retirement. Exercise
repeated memory growth/release, allocation failure unwind, affinity restrictions,
CPU-independent session/input/display routing and counters returning to expected
idle ownership. Distinguish code inspection from behavior actually observed.

Success requires demonstrated parallel process execution and local private-memory
work without changing authority or lifetime guarantees. No speedup percentage is
promised before baseline measurements. Shared services may remain bottlenecks;
record them rather than broadening the milestone without agreement.

## Focused tasks

Tasks may be split further for review; do not start the next implicitly. Each PR
updates current subsystem docs only for behavior it implements.

1. [ ] **Rebase the investigation and capture the baseline.** After writable core
   completion, audit changed worker/memory/lifetime dependencies and record the
   bounded performance set above. Settle concrete session/affinity configuration,
   single-CPU defaults and the mapping-growth design before dependent implementation.
   Record any unresolved correctness decisions rather than inventing requirements.
2. [ ] **Separate spaces and boot sessions from CPU topology.** Add independent
   lookup and update init selection, navigation, presentation/input and explicit
   service context. Preserve session roles and Caelum authority; no dynamic space
   lifecycle or public affinity API.
3. [ ] **Separate launch authority from execution placement.** Keep space-scoped
   group admission and inherited affinity. Replace captured CPU identity where
   necessary; prepare atomic batch publication across possible destination queues.
   Preserve launch rollback and transitive completion/termination semantics.
4. [ ] **Enable safe placement and migration.** Use existing queue synchronization,
   bounded load-aware placement/balancing, remote notification and the agreed safe
   points. Include BSP userspace eligibility once its prerequisites hold; complete
   it no later than task 7. Record matched scheduling/concurrency results.
5. [ ] **Prepare architecture and physical allocation for concurrency.** Add
   CPU-local scratch mappings, synchronized PMM operations/statistics and explicit
   interrupt/lock rules. Retain existing mutation call sites until the full memory
   path is safe; validate zeroing, rollback and ownership.
6. [ ] **Enable safe concurrent heap growth.** Synchronize TLSF/stats and kernel-VM
   bookkeeping, implement the agreed growth/publication/unwind contract, and keep
   general mapping reuse constrained. Review the full lock graph and allocation
   recursion; record matched allocation results.
7. [ ] **Make private MEMORY operations local.** Remove the BSP request/loan for
   this exclusive single-task path, preserving behavior, profiling and cleanup.
   Verify concurrent callers on distinct roots, migrated callers, termination and
   BSP eligibility. Record backing-growth/page-operation and mixed-load results.
8. [ ] **Validate and close.** Run the CPU/device matrix, independent-space and
   lifetime scenarios and matched final measurements. Document remaining serial
   services and accepted limits; rewrite this milestone as an implemented kernel
   reference, preserving thread/worker follow-ups in WIP and technical debt.

## Subsequent work

After this milestone, establish general kernel mapping invalidation/reclamation
and off-BSP kernel-task scheduling, sleep and preemption rules. Then move one
substantial serial service to an explicit owner CPU. Network and native filesystem
workers are candidates, selected using mixed-load evidence; filesystem movement
also requires synchronized cross-CPU block clients. IRQ routing may remain on the
BSP initially if notifications are correct. Preserve each lwIP/filesystem instance's
single-owner contract; neither device ownership nor exclusive instance access
inherently requires CPU 0. Do not move all workers or destructors together.

Multiple user threads remain a separate milestone for native consumers including
Neovim/libuv and Go. Specify thread creation/join/exit, TLS/errno, synchronization,
libc shared state, faults and last-thread/process cleanup. Siblings invalidate
exclusive VM/table loans even if scheduled on one CPU: parking one caller does
not quiesce the process, outstanding kernel operations or retained user buffers.
Require shared-table lifetime protection and process-wide VM activity/translation
coordination before enabling them. No fake threading or separate-process substitute
for shared-pointer worker callbacks.

Per-space budgets, advanced scheduler policy, NUMA, CPU hotplug and tickless timers
are not prerequisites. Preserve these as later decisions, not placeholder APIs.
