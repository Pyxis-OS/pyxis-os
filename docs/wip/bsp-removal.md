# Remove BSP-owned allocation and VM mutation

Proposal only (chronium assignment, 2026-10-26). No implementation is authorized
by this document. XSAVE/AVX is merged and natively qualified; its qualification
record is in [#1247](https://git.internal/PyxisOS/pyxis-os/pulls/1247).
The accepted JIT grant remains after this milestone.

## Goal and boundary

Different address spaces must be able to allocate, query, change mappings and
retire concurrently on their executing CPUs, without sending that work through
CPU 0. One address space still has one structural mutator. Caller-local frame
allocation and acknowledged invalidation replace CPU identity as the proof of
ownership. Shared kernel mappings have a separate exclusion/retirement protocol.

This is not worker migration, a scheduler-priority change, a new allocator
framework or a POSIX VM API. Network/audio/USB/filesystem/presenter owners, device
queues, IRQ routing, GPE methods and firmware actions keep their current owners.
Moving RAM/page-table cleanup does not authorize running a device destructor on
an arbitrary CPU. Public MEMORY rights and results stay unchanged; executable
memory still needs the separate JIT grant.

## Current code: what actually funnels through the BSP

Source inspection at main `a5cf48f8`; no new timing or native run for this proposal.

| Layer | Current invariant and consequence | Work this milestone owns |
| --- | --- | --- |
| Heap, PMM and scratch | Already any-CPU under short locks/CPU-local scratch, IF=0. Heap growth uses its own fresh-address arena; PMM uses one bitmap lock. [SMP](../kernel/smp.md#memory-and-output-boundaries). | Reuse these paths. Do not add another allocator lock or call an any-CPU primitive a per-CPU cache. |
| Private VM | `vm_space` range/extent lists have no concurrent structural owner; global wait-token allocation is BSP-only (`kernel/mm/vm.c:42-108`). Sparse reserve/update asserts BSP (`:618`, `:639`, `:838`). Private page walks already use per-CPU scratch; their mechanical portability is not a metadata-ownership proof. | Per-space exclusive ownership, stable queries, synchronized non-reused tokens and explicit caller-owned frame lifetimes. |
| Reservation/context registry | Private identities are a BSP-owned global counter; pinned records are raw process-owned pointers (`kernel/mm/private.c:17-27`). `private_context_unpin` changes a non-atomic pin on CPU0 (`:312-320`). | Protect identity/pin/registry transitions before an AP mutator can race existing BSP thread retirement. Removing assertions alone is incorrect. |
| Process mutation gate | `OPEN → DRAINING → MUTATING → REPLY` counts roots/borrowers, holds siblings and admits only the mutator for reply. Requests and pending-list execution are BSP-owned (`kernel/process_vm.c:177-228`, `:278-410`). | One CPU-neutral transaction and notification path shared by MEMORY, DISPLAY, thread contexts and later JIT. |
| Request execution | MEMORY calls stage through `bsp_request_prepare(BSP_SERVICE_MEMORY)` (`kernel/object/memory.c:205`), then use the CPU0 executor/VM worker. | Execute private-memory work on the requesting CPU while preserving staging, output checks, stepping and cancellation. |
| Launch and retirement | Image/process/task preparation and guarded contexts use the same serial ownership. `reclaim_process` releases VM in bounded BSP steps before publishing completion (`kernel/process.c:329-363`). | Move heavy RAM/page-table work without publishing results or releasing captures/observers before actual retirement. Audit every unwind too. |
| Shared kernel VM | Reused ranges require remote exclusion, not just a page-table lock. The existing helper is a BSP-initiated, one-generation all-CPU flush after callers stop refills. [SMP contract](../kernel/smp.md#memory-and-output-boundaries). | An any-CPU owner/invalidation protocol, with old backing retained until actual access and translation retirement are proved. Do not turn the current singleton into concurrent private shootdown by dropping its BSP assertion. |

The old native MEMORY comparison measured about 1.6–4.2 µs added per mutation
([record](../development/experiments/threads-task4/README.md)). That is historical
evidence, not today's baseline or a predicted gain. Bounded teardown reduced long
interrupt-off stalls but retained CPU0 ownership; this proposal does not undo it.

## Decisions needed now

### 1. Execute on the calling CPU without a new worker layer

**Recommended default:** the requesting user task runs an explicit kernel-root
continuation on the CPU where it entered the syscall. The scheduler first saves
its stack/state, leaves the private root on the permanent scheduler stack and
acknowledges departure. Only then may the continuation become runnable. Keep the continuation on that CPU. It can
park/yield in bounded work without entering the private root; final reply uses
the existing mutator-only reentry and fresh output validation.

Use retained, preallocated per-task request/cleanup storage and the same process
mutation gate. `vm_continuation` currently means an admitted syscall allowed to
drain; it is **not** already a kernel-root execution mode. Preserve that meaning
and distinguish the rootless actor explicitly. No mapping borrower or spinlock
crosses a wait, and stop cannot discard an admitted transaction before its
commit/unwind and joined cleanup finish.

Alternative: fixed per-CPU VM workers, each with a queue. They also remove the
CPU0 funnel but add worker stacks, placement and queue ownership before they are
needed. Neither option permits live kernel-worker migration or work stealing.

### 2. First task keeps the proven quiescence semantics

**Recommended default:** first unpin private MEMORY operations while retaining
whole-process root/borrower drainage, competing-mutation BUSY, the current
pre-effect quiescence budget, all-or-nothing backing and bounded stepping.
The same owner token also excludes existing BSP DISPLAY/thread mutations; those
are remaining clients, not a second registry or an AP-only protocol.

The milestone must subsequently add targeted invalidation/commit coordination
and shared-kernel retirement before it is declared complete or JIT starts.
Moving execution alone does not mean sibling threads run through a mutation.
A later task will propose how to move preparation outside the closed interval
and shorten the commit hold; this round does not silently permit live mapping
changes or change failure/timeout outcomes after effects.

Alternative: introduce live-root mutation and caller-CPU execution together.
That couples new scheduling, page-table ownership and stale-translation failure
semantics in the first change. Keeping all VM clients on the BSP until every
part is rewritten is also possible, but provides no reviewable unpinning step.

### 3. Reuse the any-CPU allocator before designing caches

**Recommended default:** task 1 allocates/zeros/frees backing on its executing
CPU using the existing PMM lock and local scratch slots. This is CPU-local
execution, not a lock-free or cached PMM. Do not introduce per-CPU frame budgets,
remote-free queues or new free-memory accounting in that task.

Measure the shared PMM path once the dispatcher is gone. The frame-allocation
task below then proposes any local cache/refill/drain budget and low-memory
behavior just in time; it must account for every free frame and preserve a way
to reclaim cached frames under pressure. This does not drop the milestone's
per-CPU allocation goal. Alternative: design those budgets and cache lifetime
before task 1, expanding its scope while the actual PMM contention is unknown.

## Implementation sequence after acceptance

Each row is a delivery/review boundary, not permission to start it. Later tasks
raise their own behavior, lifetime and policy decisions when reached.

| Task | Concrete delivery and check | What chronium can do afterward |
| --- | --- | --- |
| 1. Private MEMORY on its caller CPU | Per-space owner proof, stable queries, counters/identities and pin synchronization; kernel-root continuations; all existing ordinary/guarded/sparse MEMORY operations. Preserve the BSP clients' use of the same gate. Check real AP execution, unrelated processes mutating concurrently, same-process competition, parked I/O and stop/unwind. | Run memory-heavy processes on APs without CPU0 servicing their MEMORY bodies; sibling quiescence still applies. |
| 2. Frame allocation on each CPU | Attribute PMM lock/search/zero/map costs with the routing bottleneck gone. Propose any cache budgets/refill/remote-free/pressure rules before code; retain exact allocator statistics and zero-before-publication. | Exercise parallel backing and pressure without hidden stranded frame pools or a CPU0 refill owner. |
| 3. Invalidation and shared kernel VM | CPU-neutral private and shared mapping coordination; root generations/residency publication; acknowledgement and refill exclusion; safe general kernel range reuse. Define timeout/uncertain outcomes before effects can escape. Exercise permission reduction as well as map/unmap. | Reuse mappings and change permissions without stale remote access or a privileged CPU0 mutator. |
| 4. Preparation and reclamation | Off-BSP process/image/task preparation, context pin/unpin, unpublished rollback, and heavy retirement. Propose cleanup placement/lifetime when reached (exit continuation or fixed-owner reapers); keep device-owner callbacks separate and results tied to real retirement. | Launch and retire large processes without funneling their RAM/page tables through CPU0. |
| 5. Remaining VM consumers and JIT gate | Route DISPLAY's VM segment and every existing metadata/query/mapping caller through the common interface; hardware stays with its owner. Audit hidden BSP dependencies and qualify the internal range-permission primitive. | Start the separately accepted JIT kernel task as a consumer of the common VM transaction, rather than add a BSP-only JIT service. |

## Proof obligations, not shortcuts

- Owner tokens serialize a space across bounded steps; short spinlocks protect
  publication/counters only. Never hold a VM/queue ownership spinlock across
  allocation, copying, a context switch, IPI dispatch or an acknowledgement wait.
  Preserve the existing allocator-internal lock order and growth-lock exception.
- Existing per-CPU scratch slots cannot be retained across yielding/migration.
  Queries of inactive or remote roots need stable ownership/leases and tables;
  a current-root recursive mapping is not a general remote walker.
- Dispatch reservation must precede CR3 activation; departure acknowledgement
  follows kernel CR3 on the permanent stack. PCID/global pages remain disabled.
  A racing reentry or delayed IPI must not resurrect an old mapping generation.
- Independent mutators must not deadlock while each has interrupts masked and
  waits for the other's IPI. Wait with serviceable interrupts and no spinlock;
  the handler allocates nothing, takes no VM owner lock and never waits on an
  initiator. Coalesced IPIs and late acknowledgements must retain request storage.
- No frame, page table, range record or context identity is reusable before all
  relevant roots, borrowers, wait registrations, pins and owner/device leases
  relinquish it. Address reuse never reuses an address-wait incarnation.
- A failed pre-effect hold may report BUSY with mappings unchanged. A timeout
  after effects cannot silently be called that same outcome. The invalidation
  task must settle its retained-state/recovery policy before implementation.
- Future JIT W→X and X→W must exclude both stale writable and stale executable
  translations; a local `invlpg` or PTE update is not sufficient. The first
  quiesced path remains the safe reference until a replacement is proved.
  No executable right is added to ordinary MEMORY by testing an internal helper.

## Qualification

Capture a fresh matched baseline before implementation, using existing
allocbench, ipcbench, native-thread/Go workloads and ordinary interactive
QEMU/GDB. Record revisions, CPU/HWP/XSAVE configuration, actual execution CPU,
backing, repeated samples and variation. Separate queue/hold/zero/map/retirement
where current profiling permits; do not invent an attribution from total time.
Use 1 and 4+ CPUs and multiple independent processes, not only many threads in
one address space. No new benchmark framework or merged boot self-test.

Cover allocation and sparse rollback, guarded stack/TLS pin lifetime, queries
while another process mutates, address-wait withdrawal/reuse, caller stop during
stepped work, batch-launch refusal/unwind, actual process/group retirement and
old output/late-reply guarantees. Invalidation qualification includes concurrent
initiators, root entry/exit races, shared kernel range reuse and both permission
directions with independent state checks. Any private fault-injection or
failure hook needs separate explicit authorization; no such run is included here.

Native windows go through the orchestrator on live RAM/owned workspaces.
Include the existing audio/network/display mixed load to detect CPU0 starvation
and verify XSAVE state across the new continuations. No raw-device writes,
firmware changes, new scheduler precedence or native action is authorized here.

## What stays separate

The fixed-worker placement rule, device IRQ/MSI targets, audio/network/USB owner
moves, ACPI/GPE/power behavior, RAM above the current physical coverage, kernel
heap reclamation and JIT authority each keep their own contracts. This milestone
is not complete while an undocumented CPU0 requirement remains in its VM paths;
remaining deliberately owned services must be named rather than hidden by an
"any-CPU" wrapper. Completing the milestone does not itself grant executable
memory or start the JIT implementation.
