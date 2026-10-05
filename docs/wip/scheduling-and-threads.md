# Runtime SMP and independent spaces

Status: agreed milestone direction, 2026-10-01; implementation not started.
Task 1 is in progress. On 2026-10-05 the evidence was re-audited at main `83c08d6`
and the [pre-implementation baseline](../development/experiments/smp-task1-baseline/README.md)
was recorded. The [open decisions](#open-decisions) await owner answers.
The prerequisite [native filesystem writer](../devices/filesystem-native-adapter.md)
is complete.
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

Trusted boot configuration describes initial spaces by session identity and init
program, with an optional permitted CPU set. The launcher supplies the authority
ceiling; trusted init can request its space's affinity within that ceiling before
handing off to the ordinary session. For example, a launcher permitting CPUs 0–7
can authorize init to request CPUs 2–3. Configuration names identify sessions,
not authority. This requires a narrow native space-configuration operation and
script command, not an ambient right to change arbitrary spaces.

Keep the existing initial session roles, but stop deriving their number or
identity from CPU count. Keep Caelum's log space and its restricted authority;
its special purpose does not give it ownership of CPU 0. Dynamic space creation
and destruction are outside this milestone.

The default launcher ceiling is all online CPUs; absent an init request, the
space uses its permitted set. A requested set may contain one CPU or several;
sets may overlap and do not reserve CPUs exclusively. Children inherit the
space's effective eligibility, not necessarily init's setup authority. Requests
must remain within the ceiling. Explicit unavailable CPUs, an empty set or an
unauthorized expansion fail clearly; never silently broaden or partially apply
an invalid request. No general per-process affinity setter is required now.

Initial scope is affinity setup before session handoff, not arbitrary changes to
a populated running space. Task 1 must settle the capability's exact rights,
lifetime/delegation and handoff boundary, behavior for repeated setup requests,
and how the initiating task reaches an eligible CPU before successful completion
when it excludes its current CPU. This must agree with the migration-safe-point
contract below; a syscall cannot simply resume on an excluded CPU and claim
placement is already enforced. Also settle boot/make and script syntax and
single-CPU defaults before dependent implementation. Replace CPU-index init
selection explicitly; do not silently reinterpret old indices.

### Scrolling space bar

Keep fixed-width tabs and Super+Left/Right selection. Once the space list exceeds
the visible width, the tab bar is a viewport. Reserve left/right chevrons as
indicators: light when more spaces are hidden beyond that viewport edge, muted
when that end of the complete list is visible. The chevrons do not navigate.

When moving right, scroll as needed to keep the selected space and its next
neighbour visible; moving left is symmetric. At the actual end, selection can
reach the edge slot because there is no further neighbour to reveal. Keep the
selection visible even when only one tab fits; neighbour preview applies when
there is room. Selection and viewport track registry order, not CPU indices.
Caelum stays the first space in that order.

Use initial per-task round-robin fairness and modest load balancing. A space with
more runnable tasks can receive more total CPU time. CPU masks provide placement
constraints, not CPU budgets, memory limits or protection against exhaustion of
shared services. Per-space resource accounting/quotas remain separate work.

## Evidence and safety boundaries

The task-1 audit read main `83c08d6` (2026-10-05), after the native writer,
system-updates task 2, USB installer C.3/C.4 and default-on xHCI. It was code
reading only; no row was established by forcing a path at runtime. It replaces the
earlier `36a199a` table and adds the USB, per-space input/presentation and native
filesystem couplings that landed since.

| Current coupling | Evidence at `83c08d6` | Required change or retained guarantee |
| --- | --- | --- |
| One space per CPU; init, UI and Caelum identity by CPU index | `space_init_all()` in `kernel/space.c` stores each space in `arch_cpu_at(i)->space`; CPU 0 gets the log TTY and initial focus. `user_launch_initial()` in `kernel/user/boot.c` parses `init.N` options, chooses primary CPU 1 (CPU 0 on one CPU) and panics on CPU-0 init with several CPUs. `user_launch_init()` in `kernel/user/launch.c` takes the space from the CPU and withholds the title grant from CPU 0. | Independent registry and session configuration ([decision 1](#open-decisions)); explicit context for kernel services. |
| Boot session grammar is also a userspace and installed-media contract | `Makefile` (`INIT_DEFAULT`, `INIT_PRIMARY`, `INIT_CPUS`) and `scripts/configure-boot.sh` write `init=`, `init.primary=` and `init.N=`. The installer (`userspace/installer/main.c`) writes `init.primary=app://init-installed`, and `esp_read.c` recognizes an installed ESP by that token. The 0.0.1 stick carries it. | Replace the grammar explicitly, together with the installer and updater (decision 1); never reinterpret an old index. |
| Per-space console, display, keyboard and pointer objects are created by CPU index | `space_init_all()` creates them for each CPU; `keyboard_create()` and `pointer_create()` assert BSP. The BSP presenter `space_present_task()` drains the global keyboard/mouse queues into `active_space` and drops input aimed at CPU 0's space on multicore boots. `space_switch()` takes a CPU index, and the bar draws only the first `SPACES_NAV_COUNT` CPU spaces. | Create the same objects per session and route by registry order. The presenter may stay a BSP task. |
| CPU identity in launch and group admission | `user_task_prepare_on()` requires `process->space == arch_cpu_at(cpu_index)->space`. `user_task_publish_group()` asserts that a batch has one `cpu_index`. `execution_group_check()` and `execution_group_launch_begin()` compare the group's stored `cpu_index`. The launcher captures `arch_cpu_index()` into its request and relies on "placement cannot change while capture sleeps". | Keep space/group authority but remove CPU identity from admission; publish batches across several queues (task 3). |
| Ready queues, parking, stop and wake | `task->cpu_index` is fixed at creation. Wake re-enqueues on, and stop notifies, that CPU. Timed waits, sleeping kernel tasks, task reaping and object retirement run only on CPU 0. `kernel_task_create()` always enqueues on CPU 0. | Preserve queue locking and the early-wakeup handshake; change `cpu_index` only under the queue lock at agreed safe points. |
| Dispatch state and user entry | `arch/x86_64/user.c` saves FP/segment state eagerly per task and restores user GS. The syscall stack comes from `%gs` and is set at every dispatch. Reading found no other pinning there. | Install destination CR3, entry stack and user CPU state; kernel GS remains CPU-local. |
| Heap, PMM, VM, pressure and scratch aliases have one unsynchronized owner | `kernel/mm/heap.c`, `pmm.c`, `vm.c` and `pressure.c` take no locks; `pmm_alloc()` updates pressure state. `kmalloc()` and `pmm_alloc()` assert nothing themselves; callers keep BSP and IF=0. `map_scratch()` in `arch/x86_64/paging.c` asserts BSP and serves page-table walks, frame zeroing and space creation from two global slots, `SCRATCH_TABLE` and `SCRATCH_DATA`. | Synchronize metadata and statistics, including pressure state; provide CPU-local scratch; settle growth publication (decision 4). |
| Heap growth shares reusable kernel VM | `add_pool()` takes its range from the general first-fit kernel VM list, which also serves task stacks, DMA, display frames, image loading and npfs buffers. It backs the range eagerly and never removes a pool, so each pool keeps one of the 256 kernel range records. Other freed ranges coalesce and can be reused at once; unmapping is a local `invlpg`. Task-stack reuse relies on the CR3 reload at dispatch, with PCID and global pages off. | Decision 4; general kernel-range reuse stays with its existing owner and protocol. |
| BSP request executor and serial workers | The executor and its storage/completion rules are in `kernel/service/request.c`. The HOST transport worker is `filesystem_worker()` in `kernel/virtio/pci.c`. There is one virtio-blk worker per disk, one network worker (`kernel/net/interface.c`) and the terminal readiness worker (`kernel/user/readiness.c`), all BSP kernel tasks asserting BSP and IF state. | Retain ownership. Off-BSP workers need explicit cross-CPU handoffs, not just different affinity. |
| Native filesystem writer, cache and background flush | One BSP kernel task, `npfs_worker` in `kernel/fs/npfs.c`, owns every store call; `npfs_require_worker()` asserts CPU 0 with IF=1. `kernel/fs/npfs_store.c` allocates heap and kernel VM for metadata, cache, scratch, bitmap and write runs under IF=0. Flush (`CONFIG_NPFS_FLUSH_SECONDS`, default 30) and one-second maintenance use `task_wait_sleep_until()`, so they depend on BSP timed-wait expiry. Submission, forwarding, retirement and mount/disk object creation assert BSP; block waits and 1 ms retry sleeps run there too. | Retained as a BSP service; its deadlines must keep firing while the BSP also runs userspace (today's 1-CPU boot already does). Moving it is post-milestone and needs synchronized block clients. |
| USB host and storage | `xhci_prepare()` runs on the BSP before AP startup and allocates every ring, context, arena and record. MSI-X targets the BSP (`apic_bsp_msi_message()`). One BSP `controller_worker` per controller drains events and runs USB storage, waking on activity or at least every 10 ms (`USB_WORKER_POLL_MS`). `usb/block.c` and `storage/block.c` clients assert BSP. `storage/gpt.c` runs per-disk scan tasks and a USB discovery poll, and allocates during rescan. | Retained as a BSP service with no allocation elsewhere. Its recurring wakeups are BSP load that BSP-eligible userspace will share. |
| Device interrupts | Every MSI-X and I/O APIC route targets the BSP. | May stay on the BSP; wakeups must reach the task's current CPU. |

The baseline measured the consequence. With 4 CPUs, two and four concurrent
compute-bound sessions take about 2.1 and 4.2 times one session, while the
debugger shows CPUs 0, 2 and 3 idle. Under mixed load, BSP-serviced clients slow
by 2–3 times and TCP throughput falls to 0.42 of its single-client rate.

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

## Open decisions

Owner decisions on 2026-10-05:

- Native ThinkPad validation is required at task 8. An owner-run native check is
  recommended after task 4.
- The plan's follow-up says "declarative YAML init". The existing configuration
  is Lua: `app://config/session.lua` and `network.lua`, which session.pxe
  evaluates after init hands off. The [Lua direction](development-paths.md) also
  proposes Lua instead of YAML. This mismatch is recorded here and left
  unresolved. The syntax for describing initial spaces is a task-1 decision
  (decision 1).

The three decisions below are proposals for round 1; none is agreed. Each has a
recommended default. Decision 4 is round 2, to be asked after these. Answering
"defer" is fine, but each decision blocks the task named in it.

### Decision 1: initial session configuration and syntax (blocks task 2)

**Recommended default:**

- The kernel command line stays the trusted source. The kernel already parses
  it and the installer already writes it, so no new parser or file format is
  needed.
- Ordered `session.NAME=IMAGE` options replace `init=`, `init.primary=` and
  `init.N=`. The optional `session.NAME.cpus=LIST` is that session's launcher
  ceiling.
- The order of the options is the tab order after Caelum.
- NAME is 1–31 characters from `a-z`, `0-9` and `-`, and must be unique. It
  identifies the session in configuration and diagnostics only. Init still sets
  the title.
- LIST is a set of dense boot CPU indices, for example `0,2-3`. Script
  `affinity` (decision 3) uses the same syntax.
- In Make, `SESSIONS="development=app://init readonly=app://init-readonly
  remote=app://init-remote"` and an empty-by-default `SESSION_CPUS="NAME=LIST …"`
  replace `INIT_DEFAULT`, `INIT_PRIMARY` and `INIT_CPUS`. `INIT` still stages
  `app://init`.
- Every boot starts Caelum plus the configured sessions, whatever the CPU count.
  Spare CPUs no longer get idle spaces.
- The installer writes `session.pyxis=app://init-installed`. The updater accepts
  the 0.0.1 `init.primary=app://init-installed` token only to recognize an
  installed ESP, and then rewrites it. This migration is the one intended
  compatibility path. It is recorded as technical debt until the owner's 0.0.1
  stick has been updated.

This decides boot-time session identity only. The later Lua/YAML follow-up
configures work inside a session and is not decided here.

**Alternative:** describe sessions in a boot-archive file. That needs a parser in
the kernel, or a launcher that creates spaces, and dynamic space creation is
outside this milestone.

### Decision 2: single-CPU defaults (blocks task 2)

**Recommended default:**

- A one-CPU boot starts the same configured sessions as any other boot, each in
  its own space, all eligible on CPU 0. The primary-only fallback that shares
  Caelum's terminal is removed; Caelum still gets no title grant.
- Boot focus is Caelum on every CPU count, as it is on multicore boots today.
  On one CPU, this replaces landing directly in the shell.
- A session whose configured ceiling names an offline CPU does not start. Its
  tab and the log report why, while the other sessions and the boot continue.
  The set is never silently narrowed.

**Alternatives:** keep today's primary-only shared terminal on one CPU, or focus
the first session at boot on every CPU count.

### Decision 3: trusted-init affinity authority and handoff (blocks tasks 3–4)

**Recommended default:**

- Authority is a new `SPACE_RIGHT_SET_AFFINITY` on the existing `space` grant
  that each session init already receives. The kernel stores the space's ceiling
  from boot configuration, and no capability can change the ceiling.
- The setup window closes permanently when the first process is launched into
  the space. Scripts therefore run `affinity LIST` before `service start` and
  `session`. Closing the window at the first launch means no already-launched
  task ever needs to be re-placed. It also keeps delegation harmless: grants can
  be copied with equal or reduced rights, and `session` forwards the `space`
  grant for titles, but by then the window has closed. The handoff should still
  reduce that grant to `SPACE_RIGHT_SET_TITLE`.
- While the window is open, repeated requests are each validated and applied
  atomically, and the last successful one wins. An empty set, an offline CPU or a
  CPU outside the ceiling fails and changes nothing.
- The request commits under the scheduler queue lock. If the caller's current CPU
  is excluded, the scheduler moves the caller at its user-return safe point, so
  user code sees success only on an eligible CPU. This agrees with the migration
  rule above: a blocked syscall still resumes on its previous CPU. On one CPU,
  the only valid set is `0`.

**Alternatives:** keep the window open until session handoff, which requires
re-placing already-launched service tasks; or use a separate one-shot affinity
object instead of a right on `space`.

### Decision 4, round 2: heap-growth mapping (blocks task 6)

**Proposal:** a dedicated, never-reused heap arena.

- Reserve a fixed kernel virtual window for heap pools, outside the general
  first-fit list, and advance it monotonically under a growth lock that is
  separate from the heap lock.
- An allocation that misses drops the heap lock, takes the growth lock and
  rechecks, because another CPU may already have grown the heap.
- Each growth maps zeroed frames into virtual addresses that were never mapped
  before. It then publishes the new pool with `tlsf_add_pool()` under the heap
  lock.
- On failure, the growth unmaps locally and frees its frames. Its virtual range
  is retired, never reused.
- Pools are never removed, as today. A virtual address that is never reused
  cannot have a stale remote translation, and no other CPU touches a range
  before it is published. Publication therefore needs no shootdown.
- Heap pools also stop consuming the 256 general range records. The window size
  is a named layout constant, and running out of it is NO_MEMORY.

**Alternative:** keep using general kernel VM, together with a cross-CPU
invalidation protocol. That protocol is post-milestone work.

## Performance records and validation

The [task-1 baseline](../development/experiments/smp-task1-baseline/README.md)
(main `83c08d6`, nested KVM, 4 and 1 CPUs, xHCI active) provides the
pre-implementation numbers. Later tasks repeat its commands and configuration.

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
- RAM/HOST reads, growing writes and copies; native reads with unchanged fixtures;
  native writes and sync on RAM-backed disk images, with xHCI enabled and recorded.
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
CPU-independent session/input/display routing, tab overflow and both scrolling
ends, and counters returning to expected idle ownership. Check init requests
within/outside the launcher ceiling, single-CPU setup and session handoff. Distinguish code inspection from behavior actually observed.
Task 8 also requires native ThinkPad validation; an owner-run native check after
task 4 is recommended.

Success requires demonstrated parallel process execution and local private-memory
work without changing authority or lifetime guarantees. No speedup percentage is
promised before baseline measurements. Shared services may remain bottlenecks;
record them rather than broadening the milestone without agreement.

## Focused tasks

Tasks may be split further for review; do not start the next implicitly. Each PR
updates current subsystem docs only for behavior it implements.

1. [ ] **Rebase the investigation and capture the baseline.** After native writer
   completion, audit changed worker/memory/lifetime dependencies and record the
   bounded performance set above. Settle session configuration, trusted-init
   affinity authority/handoff, single-CPU defaults and the mapping-growth design
   before dependent implementation.
   Record any unresolved correctness decisions rather than inventing requirements.
   Audit and baseline are recorded; complete once the [open decisions](#open-decisions)
   are answered.
2. [ ] **Separate spaces and boot sessions from CPU topology.** Add independent
   lookup and update init selection, navigation, presentation/input and explicit
   service context. Implement the fixed-width scrolling bar and directional
   neighbour preview. Preserve session roles and Caelum authority; no dynamic
   space lifecycle.
3. [ ] **Separate launch authority from execution placement.** Keep space-scoped
   group admission, launcher ceilings and inherited effective affinity. Prepare
   the agreed trusted-init setup authority and session handoff; replace captured
   CPU identity where necessary and prepare atomic batch publication across queues.
   Preserve launch rollback and transitive completion/termination semantics.
4. [ ] **Enable safe placement and migration.** Use existing queue synchronization,
   bounded load-aware placement/balancing, remote notification and the agreed safe
   points. Enable the native init-affinity request and script command with the
   agreed completion boundary; invalid requests preserve existing placement.
   Include BSP userspace eligibility once its prerequisites hold; complete
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
   lifetime scenarios, native ThinkPad validation and matched final measurements.
   Document remaining serial services and accepted limits; rewrite this milestone as an implemented kernel
   reference, preserving thread/worker follow-ups in WIP and technical debt.

## Subsequent work

A declarative YAML init is an agreed follow-up direction, not an SMP dependency.
Existing configuration is Lua instead; that mismatch is recorded under
[open decisions](#open-decisions), not resolved.
A userspace launcher would interpret it and invoke the same native setup operations
as scripts: mounts, bindings, networking, affinity and final session launch.
Configuration requests resources within granted authority; parsing it grants none.
The kernel must not parse YAML or implement service-manifest policy. Exact schema,
parser dependency, failure/unwind behavior and selection of script versus YAML
remain a separate bounded design task. Endpoint exports, service dependencies,
supervision and service-address/port publication remain future work; no orchestration framework
or new network abstraction is implied by the initial configuration format.

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
