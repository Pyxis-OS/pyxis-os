# Remove BSP-owned allocation and VM mutation

Decisions 1–3 accepted (chronium, 2026-10-26). Task 1 is merged in #1254;
tasks 1/2 hands-free native A/B/C qualification is recorded in
[#1266](https://git.internal/PyxisOS/pyxis-os/pulls/1266). Task 2 measurement and
bounded-batch implementation are merged in #1260/#1263.
Task 3 [decisions 5–9](vm-invalidation.md) are accepted (chronium, 2026-10-26);
shared-owner integration and QEMU qualification are merged in #1500. Later
tasks raise decisions when reached. XSAVE/AVX is merged and natively qualified; its qualification
record is in [#1247](https://git.internal/PyxisOS/pyxis-os/pulls/1247).
Chronium revised the sequence (2026-10-27): the [JIT grant](jit-grant.md)
follows merged task 3 (#1500), before tasks 4–5 resume. This does not complete
BSP removal; JIT uses the conservative caller-CPU quiesced path.
Tasks 4–5 are assigned to Codex beta (chronium, 2026-10-28). Task 4 starts
with the accepted cleanup-placement/lifetime decisions below; task 5 raises its own
remaining-consumer decisions when reached. Task 4 implementation and qualification
are complete in #1554, awaiting merge; its native launch benefit and IPC cost are
recorded in the experiment below.

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

Pre-task1 source inspection at main `a5cf48f8`; this table is the original
work inventory. Task 1's caller-CPU path supersedes its MEMORY-routing rows;
the post-task1 measurement and remaining work are recorded below.

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

## Accepted decisions (chronium, 2026-10-26)

### 1. Execute on the calling CPU without a new worker layer

**Accepted:** the requesting user task runs an explicit kernel-root
continuation on the CPU where it entered the syscall. The scheduler first saves
its stack/state, leaves the private root on the permanent scheduler stack and
acknowledges departure. Only then may the continuation become runnable. Keep
the continuation on that CPU. It can
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

**Accepted:** first unpin private MEMORY operations while retaining
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

**Accepted:** task 1 allocates/zeros/frees backing on its executing
CPU using the existing PMM lock and local scratch slots. This is CPU-local
execution, not a lock-free or cached PMM. Do not introduce per-CPU frame budgets,
remote-free queues or new free-memory accounting in that task.

Task 2 starts with a measured decision point: after task 1 removes the
dispatcher, measure the shared PMM path and bring chronium a decision on whether
to move frame allocation into per-CPU caches or another local mechanism. Do not
assume caches are required or unnecessary. Any proposed cache/refill/drain
budget and low-memory behavior must account for every free frame and preserve
a way to reclaim cached frames under pressure. This does not drop the
milestone's per-CPU allocation goal. Alternative: design those budgets and cache lifetime
before task 1, expanding its scope while the actual PMM contention is unknown.

## Task 2 measured allocation decision (accepted 2026-10-26)

The [matched post-dispatcher measurement](../development/experiments/caller-cpu-pmm/README.md)
uses three fresh nested-KVM boots each at 1/2/4 independent mutators, with
instrumentation disabled/full/sampled controls. At four 256 KiB clients, sampled
PMM wait/hold account for about 7.9%/6.8% of MEMORY-body elapsed ticks;
zero/scratch accounts for about 77.4%. The full counter build materially inflated
contention and is not the attribution baseline. All backing allocations/frees
still enter the global PMM once per frame; sampled searches examine two words.
No native PMM attribution or fragmented/pressure timing is claimed.

**Decision 4, accepted (chronium, 2026-10-26): use bounded caller-owned frame
batches first, with no retained per-CPU free cache.** Add internal allocate/free vector helpers
capped at the existing 64-frame VM run. Claim arbitrary free frames in first-fit
order under one leaf-lock acquisition; release the lock before zeroing/mapping
or pressure notification. Returned frames belong to that in-flight transaction
and count as allocated immediately. They are never an idle CPU pool. A partial
claim follows the existing map/unwind path; public allocation remains all-or-
nothing. Validate the complete bounded free vector before clearing its bits,
including duplicate/ownership corruption checks. Keep exact global statistics,
reserved-frame exclusions, bounded-address DMA behavior and pressure policy.
Existing single-run callers remain supported. No CPU0 refill/remote-free owner,
new grant, allocation refusal, cached-frame reserve or wait-for-refill path.

Apply batching to ordinary and sparse private VM backing/unwind within existing
step bounds. Preserve zero-before-publication, root/borrower drainage and joined
stop cleanup. Compare matched disabled-counter elapsed and sampled lock costs,
including real exhaustion/rollback and a second CPU freeing backing. Do not
claim an improvement if contention merely moves into a longer lock hold.

Alternative: keep the allocator exactly as it is and move directly to scratch/
page-table work; it avoids new PMM code but leaves per-frame lock traffic.
Alternative: retained per-CPU free-frame caches; they need a separate decision
on budgets, refill/drain/pressure and cross-CPU return before implementation.
The present measurements do not justify silently reserving such pools.
The later scratch/page-table task must attribute the larger measured zero/scratch
interval separately to zeroing and temporary scratch mapping/invlpg before
choosing an optimization. Scratch layout/recursive walking and live-root
invalidation remain separate;
this recommendation does not reverse the earlier recursive-walk preference or
claim to finish all per-CPU allocation work. No allocator implementation begins
beyond this accepted scope without a further decision.

The merged [implementation and matched result](../development/experiments/frame-batches/README.md)
reduce the four-client large-allocation lock cost without retained free pools.
Real exhaustion/rollback, bitmap accounting and completed cross-CPU frees pass.
The [native A/B/C record](../development/experiments/memory-native-abc/README.md)
closes the task 1/2 functional packet; it does not establish a native batch
speedup or multi-CPU scaling.

## Implementation sequence

Tasks 1–2 are merged; task 3's [decisions](vm-invalidation.md) are accepted (chronium, 2026-10-26). Each row is a delivery/review
boundary; later tasks raise their behavior, lifetime and policy decisions when
reached. Task 2 begins with the explicit measure-then-decide point above.

| Task | Concrete delivery and check | What chronium can do afterward |
| --- | --- | --- |
| 1. Private MEMORY on its caller CPU | Per-space owner proof, stable queries, counters/identities and pin synchronization; kernel-root continuations; all existing ordinary/guarded/sparse MEMORY operations. Preserve the BSP clients' use of the same gate. Check real AP execution, unrelated processes mutating concurrently, same-process competition, parked I/O and stop/unwind. | Run memory-heavy processes on APs without CPU0 servicing their MEMORY bodies; sibling quiescence still applies. |
| 2. Frame allocation on each CPU | First measure PMM lock/search/zero/map costs with the routing bottleneck gone and bring chronium the decision whether to move frame allocation out. Propose any cache budgets/refill/remote-free/pressure rules before code; retain exact allocator statistics and zero-before-publication. | Exercise parallel backing and pressure without hidden stranded frame pools or a CPU0 refill owner. |
| 3. Invalidation and shared kernel VM (merged, #1500) | CPU-neutral private and shared mapping coordination; root generations/residency publication; acknowledgement and refill exclusion; safe general kernel range reuse. Define timeout/uncertain outcomes before effects can escape. Exercise permission reduction as well as map/unmap. | Reuse mappings and change permissions without stale remote access or a privileged CPU0 mutator. |
| [x] 4. Preparation and reclamation (#1554; residual accepted, #1588) | Off-BSP process/image/task preparation, context pin/unpin, unpublished rollback, and heavy retirement. Propose cleanup placement/lifetime when reached (exit continuation or fixed-owner reapers); keep device-owner callbacks separate and results tied to real retirement. | Launch and retire large processes without funneling their RAM/page tables through CPU0. |
| 5. Remaining VM consumers | Route DISPLAY's VM segment and every existing metadata/query/mapping caller through the common interface; hardware stays with its owner. Audit hidden BSP dependencies and qualify the internal range-permission primitive. | Complete the remaining VM-client audit and migration; the earlier JIT task already uses the common caller-CPU transaction. |

## Task 4 scope and decisions — accepted (chronium, 2026-10-28)

Runtime implementation uses caller-CPU preparation, fixed RAM reapers and
confirmed shared capture retirement. See [SMP ownership](../kernel/smp.md),
[memory](../kernel/memory.md) and the
[baseline/qualification record](../development/experiments/bsp-preparation.md).
Device hooks and actual object destructors retain their fixed owners. Six native
vela A/B boots pass functionally. Chronium accepted the remaining 4 KiB SEND
cost for merge (2026-10-29), superseding the earlier parity requirement; track
its consequence, ruled-out causes and revisit point in
[debt #1588](https://git.internal/PyxisOS/pyxis-os/issues/1588).

### Decision 10 — fixed RAM cleanup owners

**Accepted (chronium, 2026-10-28):** one fixed RAM-reaper task per online CPU, prepared before
runtime user-task admission. Retired task storage stays with its departure CPU;
record the last departing task's CPU as the process RAM-reclamation destination
before task storage disappears. Later cleanup-pin release uses that recorded
CPU; an unpublished rollback with no departing task uses its preparation CPU. No migration, stealing, CPU0 fallback or free-frame cache. Prepare the
mandatory actors under the existing core-worker boot-failure policy; do not
partially enable offload when an actor cannot be created. Their stacks/metadata
are a new boot resource cost, to be recorded from actual artifacts.

The permanent scheduler stack transfers whole owned retirement records after
kernel-root departure. It does not free its executing stack, run a large cleanup
batch or wait for VM admission. Fixed RAM actors can park for admission and yield
between existing bounded steps. This requires deliberately supporting those
fixed AP kernel actors' wait/yield paths; it is not a device-worker placement
change or generic scheduler migration/priority facility. No allocation is needed
to enqueue an already-owned terminal item. A task's structural owner token is
released before handoff; the receiving actor acquires its own token.

Keep fixed-owner session/endpoint hooks and object/device destruction with their
existing owners and in their existing order. Move private backing/page tables,
request/profile/FP storage, context-pin retirement, private allocation records,
capability/environment walks and their storage onto the audited RAM owner.
Synchronous logical grant-close callbacks still run under their existing guards;
queueing actual destruction is not a substitute for those callbacks.
Keep the process, controls, reservations, cleanup pins and group attribution
owned across every phase. Thread/process observers complete after their actual
storage/VM/record work, as now; execution-group completion additionally joins
attributed deferred device/object cleanup. Do not newly make process WAIT promise
that every asynchronous device callback has finished.

### Decision 11 — preparation actor and captured-image retirement

**Accepted (chronium, 2026-10-28):** extend the existing retained caller-CPU, kernel-root
continuation to heavy launch/image/startup/task preparation and its unpublished
rollback. Keep the chosen child CPU/affinity and publication/stop linearization
rules unchanged. Boot preparation keeps its explicit boot owner. No new public
job, grant, cancellation mode or retry. A stop cannot discard admitted captures,
provisional roots/tasks, reservations or cleanup attribution: finish commit or
unwind, or transfer the complete owned rollback record to its RAM owner before
retiring the caller. Preserve current pre-effect refusals and joined-cleanup
semantics rather than making queued rollback look reclaimed.

Keep the complete immutable selected-image snapshot. Transfer a single owned
read phase from its fixed file/I/O owner to the preparation continuation; after
handoff the producer retains no pointer to refill it. The capture remains owned
through all loader reads. If the capture's shared virtual address has been read
on an AP, close further reader admission and use the accepted shared-range
confirmation/ACK protocol before unmapping or reusing its VA/backing. Never send
it through the old BSP-only no-remote-translation retirement shortcut. On
unconfirmed retirement, retain the whole range/cleanup attribution until reboot,
with no late-ACK reclamation; any joined group cleanup remains pending rather
than reporting the retained storage freed. These are the existing uncertainty
rules applied to this newly shared consumer.

Use the existing CPU-local frame aliases for bounded private-page image/startup
copies, unmapping before yield; do not hold the general kernel structural owner
for the entire unrelated private image load. This is not a new scratch allocator,
recursive walker, live-root mutation or shared-table reclamation optimization.
Fixed device/file workers keep their I/O owners. Ordinary shared-owner admission
may still serialize the small kernel-range preparation/retirement portions.

### Task 4 delivery and qualification

- [x] Fresh pre-code baseline at main `af35537d6`: exact builder/config/flags and
  archive/workload hashes, one/four-CPU warmups and three fresh samples each.
- [x] Caller-CPU preparation and whole-record rollback; existing local frame
  aliases, bounded image/startup backing and root teardown, native thread/context
  preparation under the common drained gate. Preserve creator FP state,
  zero-before-publication, pre-effect BUSY/NO_MEMORY and publication/stop guards.
- [x] Mandatory fixed RAM actors with explicit AP wait/yield and no terminal-queue
  allocation; real last-departure CPU independent of BSP queue order; unchanged
  fixed device/endpoint phases. Ordinary shared capture ACK/reclaim, transferred
  batch rollback, exhaustion/reuse and thread stop/close pass in QEMU/GDB.
- [x] Matched nested after cohorts and interleaved A/B, with host-load limits and
  reproduced IPC cost recorded. Untimed GDB proves distinct AP preparation and
  records a placement mechanism without claiming full phase attribution.
- [x] Hands-free vela native A/B: same frozen workload archive/config/flags,
  actual AC/HWP/XSAVE/TSC checks, three interleaved fresh boots each, outcome and
  final-drain checks. The [sealed packet](../development/experiments/bsp-preparation.md#native-handoff)
  passes both final-pair QEMU collectors; the orchestrator completed all six
  native boots with identical 2880×1800 PXE resolution and restored the default.
  New deliberate fault/uncertain-ACK scenarios still need separate scope; ordinary
  successful ACK checks do not qualify the timeout/quarantine path.

In the original candidate the placement code was unchanged, but its inputs changed: caller
preparation counts as running load on its AP, unlike an old caller parked for
BSP preparation. It consequently selects a different child CPU, so task 4 has
not preserved decision 11's placement behavior. That candidate's native launch improves 11.8%
and pages are flat, but CALL64/4096 completion rises 32.6%/45.4%; SEND completion
rises too. That trade-off is not accepted.

- [x] Resolve the placement regression and repeat matched QEMU and native IPC
  controls before qualifying task 4. The configuration-only isolation in the
  [experiment](../development/experiments/bsp-preparation.md#ipc-regression-attribution)
  removes the large IPC penalty with all kernel checks unchanged; it is not a
  delivered configuration workaround.

### Initial placement after caller preparation — accepted

**Accepted (chronium, 2026-10-28):** for initial child placement only, exclude the actual
executing preparation actor from the load and sibling-idleness scoring view.
Keep the real scheduler running state truthful; keep affinity, preferred CPU,
idle stealing and wakeup rules. Boot/fixed-worker publication gets no exclusion.
Count every queued batch member normally. Apply
the same rule to native-thread creation, which also moved from a parked creator
to its caller CPU. This is a narrowly scoped scoring rule;
it adds no per-message check, placement reservation or retained load snapshot.
It favors the existing parent-CPU tie-break when other allowed CPUs are equally
idle; it does not reproduce every historical destination under arbitrary CPU0
or SMT load. An independently runnable child can still be stolen by an idle CPU.
The implementation and both full QEMU packet collectors pass at `4c4b6625e`;
three normal-affinity A/B pairs no longer reproduce the old threefold CALL
penalty, with substantial A-side variation. The new sealed native six-boot
pair is `/shared/present/bsp-removal/task4/native-placement-fix/`.
Corrected native launch improves 15.4% and CALL is at parity in chronium's
assessment; SEND retains a nonoverlapping 9–11% completion cost. The residual
does not reproduce in the subsequent QEMU controls, and no cleanup/check
change is justified by the current observations. See the
[residual investigation](../development/experiments/bsp-preparation.md#residual-investigation--2026-10-29).
**Accepted (chronium, 2026-10-29):** retain the 15–18% native launch gain and
accept the remaining roughly 8–10% 4 KiB SEND cost. Its underlying source remains
unproved; this decision permits task 4 to merge without a speculative fix.
[Debt #1588](https://git.internal/PyxisOS/pyxis-os/issues/1588) carries the
consequence, ruled-out list and revisit point. Do not run the CPU0-only control.

Keep the VM, endpoint and task-lifetime guards protecting live operations.

### Native SEND result — temporary instrumentation removed

Chronium authorized both bounded native controls on 2026-10-29. The first
localizes the sampled increase to copy/release tails; the second separates
store completion from release acquisition. In the second pair all 385 sampled
acquisitions per variant succeed on their first exchange. B's plain acquisition
is 110.11 ns versus A's 25.82 ns; after the explicit store-drain control it is
19.60 versus 19.56 ns, with B's tails appearing in the fence window instead.
This strongly supports preceding-store completion/ordering cost at the first
atomic acquisition, not an observed busy-spin or added borrower-check cost.
See the [release control](../development/experiments/bsp-preparation.md#native-releasestore-control).

The fence also drains marker stores, and cases observe different messages.
Neither individual tails nor mean costs are conserved between cases. The exact
task-4 allocation/cache/preparation change causing the underlying delay remains
unproved; no safe specific fix within task 4 is established. A production fence,
guard removal or speculative padding/copy rewrite as a fix is not justified. The design
does not require new per-message release work, but that is not proof of a fix.
Native SEND completion rises 8.1% in this instrumented pair. The residual is
accepted for task 4 under the 2026-10-29 decision and debt #1588 above.

Both temporary timing probes, their local branches/worktrees and their images
are removed; only methods/results remain. The ordinary main-plus-process-size
control is now negative: native SEND4096 0.677 → 0.675 ms, admission unchanged
at 0.095 ms. This excludes the 40-byte growth alone, not other boot allocations
or preparation/reclamation chronology. Its source/worktrees/images are removed.
Source-level attribution remains open; no validated fix is established. The
proposed CPU0-only control is cancelled by chronium's acceptance. See the
[control and #1584 integration record](../development/experiments/bsp-preparation.md#native-process-size-control).

The rebased integration keeps #1584's bounded producer waits and task 4's
narrow shared-stack/device admission, caller-owned rollback and AP capture
retirement. Installed/RAM/pipeline checks pass at 1/4 QEMU CPUs; the broader
16-command cohort passes at four. Native parity on the rebased head is not
claimed; chronium accepts the recorded residual and task 4 is ready for merge.

**You should know:** fixed cleanup actors add boot storage. Shared stack/capture
admission can return BUSY after the existing bounded wait; cleanup contention can
delay real completion. Unconfirmed shared capture retirement retains memory and
joined cleanup until reboot. No public authority/status, worker migration or new
source of executable memory is added.

Task 5 next audits DISPLAY's VM segment, stable queries, NPFS, DMA/PCI/scanout
and direct architecture callers. Keep device callbacks/IRQs, firmware windows and
the heap's separate owner; raise each new placement/lifetime/refusal choice when
its caller is reached. Task 4 does not implement those migrations.

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
directions with independent state checks. Private fault-injection/failure scopes
need explicit authorization. Chronium approved the bounded, RAM-only delayed-ACK
check on 2026-10-26: [production #1269 kernel, GDB-paused CPU, PASS](../development/experiments/vm-epochs/README.md#delayed-acknowledgement-production-kernel-qemu-pass).
The existing resize consumer retains five ranges/2,940 backed pages, fences
further resizing, and does not reclaim on a late ACK; private MEMORY/Remote keep
working. No kernel hook or test code is merged. Task 3's shared-owner integration
and other failure scenarios remain unqualified by this check.

Native windows go through the orchestrator on live RAM/owned workspaces.
Include the existing audio/network/display mixed load to detect CPU0 starvation
and verify XSAVE state across the new continuations. No raw-device writes,
firmware changes, new scheduler precedence or native action is authorized here.

### Native tasks 1–2 (chronium/orchestrator, 2026-10-26)

[Hands-free vela A/B/C](../development/experiments/memory-native-abc/README.md)
passes every boot's functional packet and single-run workload checks. A/B/C
small elapsed is 3.964/2.808/2.807 ms; long control 104.976/95.582/94.662 ms.
This closes that native packet, with no native variance/scaling claim. Native
unrelated mixed-load stress and the separate masked UDP capture remain open;
Remote kernel-ring evidence is retained and identified honestly.

## What stays separate

The fixed-worker placement rule, device IRQ/MSI targets, audio/network/USB owner
moves, ACPI/GPE/power behavior, RAM above the current physical coverage, kernel
heap reclamation and JIT authority each keep their own contracts. This milestone
is not complete while an undocumented CPU0 requirement remains in its VM paths;
remaining deliberately owned services must be named rather than hidden by an
"any-CPU" wrapper. Completing the milestone does not itself grant executable
memory or start the JIT implementation.


## Task 5 scope — proposed (2026-10-29)

Task 4 is ready in #1554, with its accepted SEND residual tracked by
[debt #1588](https://git.internal/PyxisOS/pyxis-os/issues/1588). Task 5 starts
after that integration; this note proposes its remaining placement/lifetime
choices, not new public operations or executable authority.

### Inspected remaining consumers

Source inspection uses task-4 runtime `badd1d1c5484` on main `1394079d5e94`;
the task-4 acceptance head changes documentation only.

| Consumer | Remaining task-5 work / retained boundary |
| --- | --- |
| `kernel/object/display.c`, `kernel/process_vm.c` | ACQUIRE/REPLACE/RELEASE still execute allocation, private mapping changes and display-state changes together on CPU0. `prepare_slots` reads live geometry; frame lists/refcounts, presenter, pointer and clipboard state are BSP-owned. Split the VM work from that state rather than move the whole executor. |
| `vm_query`, `arch_page_query`, `vm_get_stats` | Already use short stable snapshot guards. Audit retained mapping/frame ownership at callers; queries must not acquire structural admission or gain unrelated BUSY failures. A snapshot remains distinct from a lease. |
| `kernel/fs/npfs_store.c` | Allocation already waits for bounded shared-owner admission before IF=0 work; free transfers a retained intrinsic range. Keep the filesystem worker and its existing cleanup destination; no filesystem-worker migration. |
| DMA, virtio-net storage, scanout and PCI/BAR/ECAM | Preserve boot-only allocation/unpublished rollback and fixed device/reset/cache-attribute proofs. Runtime DMA storage remains pinned as today. PCI overlap selection and claim retain one owner scope; no new snapshot-to-claim API. |
| ACPI/GPIO, heap, image/architecture callers | Keep permanent prebuilt firmware windows and the heap's separate never-reused arena. Confirm private queries retain their preparation or mutation proof; remove only obsolete VM-routing assumptions, not hardware ownership. |
| Internal range permissions | `vm_protect_begin/step` already exists and is reached by merged JIT. Qualify its existing drained-private, eager-range, stepped commit contract and both permission directions; no new public MEMORY permission operation. Reuse the recorded task-3/JIT evidence where inputs and proof match, rather than repeat a new mechanism. |

### Decision 12 — DISPLAY control and VM split

**Proposed default:** keep display ownership/slot/refcount/presenter/input state
on its existing BSP actor. A fixed control phase retains the request, reserves
the operation and copies a complete geometry/owner/mapping-identity snapshot.
The requesting task's existing kernel-root continuation performs backing
preparation and private mapping work on its caller CPU under the common drained
process transaction. A fixed commit phase publishes the prepared slot set;
SIZE/SUBMIT and hardware presentation keep their existing control path.

Do not carry the structural VM owner across either actor handoff. Acquire it
only for bounded shared-VM steps; no short lock, borrowed geometry pointer or
scratch mapping crosses a wait. Keep whole-process quiescence for this task;
shortening the closed interval or permitting live-root updates is separate work.

Resolve geometry/ownership/stop changes before old mappings or display state
are withdrawn: stale preparation returns the existing BUSY/denied outcome,
leaves the old display intact and reclaims the new unpublished set. Once effects
start, retain the admitted transaction through nonfallible commit or complete
owned unwind; do not introduce a post-effect timeout, implicit retry or partial
successful replacement. Preserve output revalidation, sibling holds and the
mutator-only reply rule. A stop cannot drop a reserved operation or its cleanup.

### Decision 13 — detached display RAM ownership

**Proposed default:** record the preparation CPU for each new slot set. Keep
published frame references and consumer leases with the BSP display owner;
unpublished rollback stays on its preparation actor. When the final published
reference is released, transfer the whole detached backing/cleanup record to
that preparation CPU's existing fixed RAM reaper. Use a specific owned record,
not a generic callback executor, a new worker, migration or a CPU0 fallback.

Do not move a presenter/device callback to the RAM actor. Backing remains alive
until all private mappings, drawing roots and presenter/scanout leases have
ended. End shared-pointer access/refills and confirm remote translation
retirement before reuse where required by the existing shared-range contract.
Unconfirmed retirement retains backing and joined cleanup until reboot; there
is no late-ACK reclamation. Preserve existing process/thread/group completion
boundaries and final-holder accounting; queue acceptance is not reclamation.
If a concrete consumer requires a new outcome after an irreversible switch,
bring that decision separately before implementing it.

Process exit is included in this split: fixed session/reference changes stay
on their owner, while the process's recorded RAM actor unmaps its private slot
aliases before the fixed phase drops those references. Retain the whole process
and cleanup record between phases. Unleased display RAM must finish reclaiming
before process completion; an independently retained presenter frame keeps its
existing separate lease and joined group cleanup. Do not add a process WAIT
promise that every deferred device callback has finished.

### Delivery and measurement after decisions

1. Capture a fresh task-4-integrated baseline before implementation, using the
   existing launch/pages/IPC cohort and display resize/presentation tools.
   Record actual AP execution and source/config/compiler identity; the accepted
   task-4 residual is the new baseline, not a promised task-5 improvement.
2. Deliver the DISPLAY caller/control split and owned rollback/retirement in a
   focused kernel PR, updating SMP/memory/interface ownership documentation.
3. Close the remaining caller inventory with concrete source proofs. Preserve
   already integrated NPFS/queries and deliberate boot/device boundaries; raise
   any newly reached runtime allocation/refusal or lifetime choice at that point.
4. Qualify 1/4+ CPU QEMU: AP acquire/replace/release, independent spaces,
   resize/geometry changes, ordinary stop/exit and failed preparation, mixed
   display/TTY/input with stable queries and network/audio controls, and actual
   process/group cleanup. Reuse existing private permission fixtures only within
   their approved scope; no new fault injection or public test ABI.
5. Repeat matched performance checks and prepare the hands-free native vela
   A/B through the orchestrator. Attribute material regressions before delivery;
   do not inherit an exception for a new task-5 regression from debt #1588.

**You should know:** this proposal keeps the existing public DISPLAY ABI and
whole-process quiescence. Geometry changes may still return BUSY; uncertain
shared-range retirement still retains memory. It proposes an explicit RAM
cleanup destination for display backing, while hardware and control ownership
stay fixed. No implementation or native/PXE action has started for task 5.
