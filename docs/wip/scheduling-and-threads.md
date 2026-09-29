# Spaces, CPU scheduling and process threads

Status: future design direction, 2026-09-29. Discuss decoupling spaces from CPUs
alongside multiple threads per process and scheduling across CPUs. Keep the BSP's
current service responsibilities initially. The stages below are proposals, not
implementation authorization; [block storage](persistent-storage.md#agreed-block-storage-foundation)
remains the active track.

Related: [current SMP ownership rules](../smp.md),
[private-memory handoff](../memory.md), [spaces](../spaces.md) and
[Neovim/libuv requirements](neovim-libuv.md).

## Direction

- A space owns processes and their resource/authority domain. A process owns its
  address space, capability table and threads. The scheduler places runnable
  threads on eligible CPUs.
- Space identity, namespace, terminal/display resources and authority do not
  change when execution moves. CPU count must no longer define the space registry
  or space identity. This does not itself introduce unlimited space creation.
- Keep scheduler and architecture execution state CPU-local. Kernel GS remains
  CPU-local; a running user task obtains its owning space through its process.
  Kernel services need explicit space context where applicable, not an accidental
  association with whichever user task last ran on their CPU.
- Initially keep allocation, VM mutation, deferred destruction and existing BSP
  workers on the BSP. Proposed placement is userspace on eligible APs in multicore
  configurations, with the existing single-CPU fallback. Exact affinity rules and
  any BSP userspace exception remain decisions.

This extends existing preemptive per-CPU scheduling with migration, load balancing
and eventually concurrent sibling threads. It does not replace the kernel with
a multikernel or move all services off the BSP.

## Proposed focused stages

1. **Separate space identity from CPU topology.** Give spaces independent lookup
   and lifetime rules. Remove CPU-index assumptions from ownership, presentation,
   navigation and launch. Define how trusted init selects spaces and their initial
   resources without treating a CPU index as a space identifier. Preserve existing
   visible sessions while changing those associations.
2. **Migrate single-threaded processes.** Move only runnable tasks whose previous
   execution has fully stopped. Define ready-queue ownership, destination wakeups
   and simple balancing across eligible CPUs. Preserve the early-wakeup/parking
   handshake and update CPU-local entry state at dispatch; do not move a stack
   still executing or a process undergoing a BSP loan. Priorities and elaborate
   placement policy are outside the initial slice.
3. **Introduce shared-process threads and runtime safety.** Specify thread
   creation/join/exit, stacks, TLS, synchronization, shared capability access,
   libc state and process-wide fault/last-thread cleanup. One possible first
   placement rule is one CPU per multithreaded process, with migration of those
   processes deferred until safe group coordination exists. This restriction is
   proposed, not selected, and does not eliminate the shared-state problems.
4. **Run sibling threads concurrently across CPUs.** Establish address-space
   activity tracking, mutation coordination and translation invalidation before
   allowing simultaneous execution in one private root. Account for threads
   blocked in syscalls, pending operations and references held by kernel services,
   not only threads currently executing user instructions.

Each stage needs a concrete contract and smaller PR tasks before implementation.
Normal preemption, completion notification and wait handoffs should remain
recognizable; thread migration is not permission to weaken their ownership rules.

## BSP loans with sibling threads

Today parking the sole user task allows the BSP to borrow an inactive private
address space or exclusively grow its capability table. With siblings, parking
one caller no longer establishes either condition, even on a single CPU.

A proposed first VM-mutation policy is to park all siblings at safe points,
prevent re-entry into the private root, and drain or account for kernel accesses
before granting a process-wide loan. After mutation, refresh translations on
every CPU that could retain stale mappings before allowing affected execution or
frame reuse. The exact CR3/shootdown and acknowledgement protocol is undecided.

Do not wait for this work until stage 4: shared-process ownership coordination is
required in stage 3. Capability tables and object lifetimes also need protection
against concurrent or interleaved resolve/close/growth. Parking user threads alone
does not prove that another kernel operation has stopped using their memory.

Resolve lock ordering, pending syscalls, wakeups during quiescence, process exit
and failure paths before implementation. Whole-process parking must not deadlock
behind a thread or service whose progress depends on the pending mutation.

## Remaining policy and scope decisions

- CPU eligibility, migration points, ready-queue locking, load balancing and
  affinity. Start with a simple policy; NUMA, CPU hotplug and tickless scheduling
  are not prerequisites.
- Process/thread exit and faults, cancellation, authority to create threads,
  stack ownership/limits, TLS representation and shared-runtime synchronization.
- Scope/lifetime of private-VM loans, retained user buffers, capability-table
  mutation and translation invalidation. Keeping allocators on BSP does not make
  the old single-task ownership model safe for siblings.
- Space enumeration and boot/session selection independent of CPU count. Existing
  CPU-selected init configuration needs an explicit replacement, not a silent
  reinterpretation of its indices.
- Fairness and resource isolation. Equal per-thread scheduling lets a space with
  many runnable threads obtain more CPU time. Per-space CPU accounting/budgets
  and protection against exhaustion of shared BSP services require separate
  policies; neither migration nor address-space separation provides them.

The intended result is stable space ownership with flexible execution placement.
The first implementation is not selected here, and no Neovim port, complete
resource-containment system or distributed allocator is implied.
