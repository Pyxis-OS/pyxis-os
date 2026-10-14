# BSP pinning and interrupt-off inventory

Owner-requested planning-freeze exception, **2026-10-13**. Read-only inventory
at main `50e5411f4173122aeac27838eefaae0f84cadfe2`; no code, ABI, placement,
interrupt-routing or allocator contract changes. All three defaults below are
**accepted by the owner, 2026-10-13; not implemented**. The owner's pause on
new tasks remains in force. Installer task 5 stays parked on its FAT32 dependency.

The immediate problem is not a lack of allocator locks. Heap/PMM/scratch access
already works on other CPUs. Final process reclamation still runs to completion
with interrupts disabled in CPU 0's scheduler. Moving one worker without its
IRQ, request, lifetime and mapping ownership would leave the same problem or
introduce races. The cheapest first improvement is bounded, preemptible cleanup
under the existing BSP ownership contract; actual worker relocation follows a
separate fixed-owner contract.

## Evidence and scope

- **Native, owner-run diagnostic:** ThinkPad, diagnostic revision `e83ae1fc`,
  `LOG_LEVEL=trace`, 2026-10-13. EDuke32 (~160 MiB backed memory, per the
  native report) teardown is **21.801 ms**
  (`diag-1.log:16120`); the next audio observation gap is **28.837 ms**
  (`:16122`). Chocolate Quake teardown is **39.712 ms**
  (`diag-2.log:5457`). HDA had already failed codec setup in that second boot
  (`:491`), so its teardown sample is not an audio-trip attribution.
- **Existing diagnostic nested QEMU:** EDuke32 teardown **50.9 ms**, next
  horizon gap **56.4 ms**; Chocolate Quake teardown **95.4 ms**; ordinary
  commands about **1.1 ms**. A separate uncapped timedemo has a **23.0 ms**
  observation gap, without a traced coincident teardown. These are reported
  samples, not distributions or current-main performance measurements.
- Evidence pointers supplied by the audio work are
  `/shared/present/game-sound-native/{DIAG.md,diag/REVISION,results/diag-1.log,results/diag-2.log}`
  and `audio-recovery-proposal.md`. No raw logs, personal game data, serials or
  diagnostic instrumentation are committed here. The native values were checked
  against the existing log lines; this inventory runs **no new measurement**.
- The forwarded nested startup starvation report remains separate evidence.
  Current MEMORY requests execute in the BSP executor/VM worker even for an AP
  caller ([memory execution](../kernel/memory.md#execution)); merely excluding
  CPU 0 from a game's affinity does not remove allocation or final teardown.
- `userspace: CPU N exited/terminated` identifies the task's **last execution
  CPU**, copied at [task.c:1285](../../kernel/task.c#L1285), not its reaping CPU.
  The native large samples label CPU 2, while the reclamation runs on CPU 0.

Coverage: all **19 kernel-task creation sites**, the closed **25-service BSP
catalog**, CPU-owner assertions, interrupt targets, scheduler/lifetime/VM paths
and related subsystem debt. Tables group services sharing an invariant; they
are not a timing audit of every vendor loop. Unmeasured cost stays unmeasured.
Code references below use the inventory revision's line numbers. Size is a
relative implementation estimate: **S** a narrow call/handoff, **M** one subsystem
plus its handoffs, **L** several owners/scheduler/lifetime paths, **XL** general
VM/device ownership changes. It is not an elapsed-time estimate.

## What the scheduler already does

Every [kernel_task_create](../../kernel/task.c#L954) asserts BSP creation and
queues on scheduler 0. Workers start with interrupts enabled
([task.c:903](../../kernel/task.c#L903)); BSP kernel execution can be preempted,
but AP kernel execution and user syscall continuations cannot
([task.c:1494](../../kernel/task.c#L1494)). Existing workers never migrate.

User placement chooses least load, then idle SMT siblings, then the parent's
CPU, then lowest AP index, then BSP ([task.c:369](../../kernel/task.c#L369)).
AP preference is a final tie-break, not BSP exclusion. An idle BSP steals only
when its own ready queue is empty ([task.c:820](../../kernel/task.c#L820)).
Wake appends to an ordinary FIFO ([task.c:335](../../kernel/task.c#L335));
interrupt return already schedules when an eligible task has local ready work
([task.c:1518](../../kernel/task.c#L1518)). There is **no worker-first dequeue**.
The new part of Claude's held precedence proposal is choosing workers ahead
of queued users, not adding next-interrupt preemption that already exists.

## Kernel workers and runtime owners

All rows inherit the placement rule above. IF=0 means local interrupt exclusion;
it serializes multiple BSP actors today but is not a lock against an AP actor.
Moving a worker normally also requires cross-CPU wake/queue synchronization and
changing [BSP-only completion](../../kernel/service/request.c#L261), with no
access after notification transfers the request. A waiting timeout does not
return DMA or borrowed-object ownership.

| Worker/system; creation or owner reference | Why it is on BSP today | Consequence/bound today | What relocation requires; risk and size |
| --- | --- | --- | --- |
| Common request executor, [request.c:439](../../kernel/service/request.c#L439) | Single typed FIFO and serialized object/VM/lifetime mutations. | Entire service executes IF=0; yields only after a request ([389–420](../../kernel/service/request.c#L389)). A request's CPU work has no time budget. | Split independent reads from owner mutation; per-service ownership and guaranteed cleanup records. Keep mutation executor BSP initially. **High, L/XL**. |
| VM worker, [process_vm.c:393](../../kernel/process_vm.c#L393) | Process admission/roots/borrowers, page tables and wait-token ownership remain BSP. | Pending drainage does not block executor; one-second budget bounds waiting for drainage, not subsequent page work. Mutation itself is IF=0 ([352](../../kernel/process_vm.c#L352)). | Closed admission, acknowledged root departure, context pins, token/withdrawal ownership, per-space mutation owner and remote invalidation rules. **High, XL**. |
| HDA/audio, [audio.c:467](../../kernel/audio/audio.c#L467) | Sole controller/mixer owner; IRQ and request queues use BSP IF=0. Session queue allocation/cleanup belongs to it. | IF=1 worker wakes on IRQ or running-only 5 ms watchdog; observes a 20 ms horizon in today's profile. Mix/DMA commit is IF=0 ([316](../../kernel/audio/audio.c#L316)); post-commit guards are not a maximum masked-time guarantee. Native/nested missed-service evidence above. | Fixed CPU owner, IRQ/request/exit-cleanup handoffs, protected sessions and DMA publication; retain BSP allocation/mapping service where needed. High direct benefit, not an affinity-only change. **High, M/L**. |
| Presenter, [init.c:130](../../kernel/init.c#L130), [space.c:1473](../../kernel/space.c#L1473) | One owner for input/layout, leased TTY/app images, GPU queues, resize, flips and capture. VirtIO runtime asserts BSP/IF=1 ([virtio_gpu.c:199](../../kernel/display/virtio_gpu.c#L199)). | About 16.7 ms service cadence even when skipping pixels. Composition/copy mostly IF=1; snapshots/resize/capture publication have IF=0 spans. Existing native skipping cuts idle pixel work about 99%, not motion work. | First split immutable leased RAM rendering from owner publication; pointer snapshots retain image references and are not pure generation reads. Whole move needs GPU/IRQ/resize/panic/capture lifetime handoffs. **High, M for rendering; L for whole owner**. |
| Network or network-debug, [interface.c:281](../../kernel/net/interface.c#L281) | Single lwIP/NIC/protocol owner; packet budgets, queues and retirement retain BSP IF=0; some notifications already locked across CPUs. | IF=1 loop handles up to eight loopback packets, bounded NIC rings, protocol timers and UDP logs; yields when busy ([213](../../kernel/net/interface.c#L213)). No isolated current CPU cost. | Keep one stack owner; synchronize packet allocation/accounting, incoming and retired requests, driver IRQ/control, debug and panic takeover. **High, L**. |
| Each VirtIO block disk, [blk.c:752](../../kernel/virtio/blk.c#L752) | Preallocated device slots/rings; BSP clients and local IRQ exclusion serialize posting/completion. | IF=1 worker yields or sleeps to IRQ/earliest five-second DMA watchdog ([604](../../kernel/virtio/blk.c#L604)). Sleeping is not a five-second masked span. | Per-device slot/state owner or locks, IRQ/completion/cancel/recycle handoffs and registry/shutdown coordination. Preallocated DMA makes this plausible without general VM relocation. **Medium/high, M/L**. |
| Each NVMe controller, [nvme.c:978](../../kernel/storage/nvme.c#L978) | Same block-client ownership plus admin/I/O queues and shutdown state. | IF=1 sleeping polls/deadlines; slot scan/post is IF=0 ([760](../../kernel/storage/nvme.c#L760)). One worker per controller, not per namespace/queue. No native worker cost established. | As above, including admin completion and normal/forced shutdown ownership; pins on timeout remain. **High, M/L**. |
| NPFS/FAT32/native storage, [npfs.c:1186](../../kernel/fs/npfs.c#L1186) | Sole store worker; admission, job/node retirement, pressure and shutdown queues remain BSP. Backing cache paths use BSP kernel VM ([npfs_store.c:145](../../kernel/fs/npfs_store.c#L145)). | Worker is IF=1, with one job plus maintenance then yield ([874](../../kernel/fs/npfs.c#L874)); no whole-job CPU budget. Heavy validation/workspace cost is separate from CPU placement. | Store owner + synchronized admitted jobs/retirement, block-client interface, object publication and kernel mapping/reclamation service. RAM/initrd metadata creation is also BSP-owned. **High, L/XL**. |
| VirtIO FS/HOST transport, [pci.c:529](../../kernel/virtio/pci.c#L529) | One FUSE/session/scratch owner; incoming/retired/wait links use BSP IF=0 ([hostfs.c:37](../../kernel/fs/hostfs.c#L37)). | IF=1 exchanges have five-second deadlines; idle sleeps. A continuously nonempty queue has no explicit fairness yield per request ([pci.c:486](../../kernel/virtio/pci.c#L486)). | IRQ/queue notifications, typed loans, capability/object publication and cleanup synchronization; preserve retained mappings and unknown-outcome rules. **High, M/L**. |
| Each xHCI controller: HID/hubs/BOT/Bluetooth, [xhci.c:4042](../../kernel/usb/xhci.c#L4042) | Private prepared controller/DMA, but local notification/wait state requires BSP; shared vector wakes all controllers ([1152](../../kernel/usb/xhci.c#L1152)). Runtime USB helper requires owning BSP worker with IF=1 ([2144](../../kernel/usb/xhci.c#L2144)). | Event/HID/Bluetooth/storage loop waits on a 10 ms service poll; enumeration/transport deadlines mostly sleep. BOT work runs synchronously in this same worker ([block.c:327](../../kernel/usb/block.c#L327)); movement does not remove per-controller head-of-line delay. | Per-controller IRQ/request ownership plus shared USB registry, block, Bluetooth and input handoffs; preserve retirement/DMA failure retention. **High, L**. |
| ACPI: AML/GPE/Notify/EC/battery/power, [worker.c:78](../../kernel/acpi/worker.c#L78) | Sole interpreter, SCI and power/snapshot owner; host mutex/map assumptions and GPE firmware exception ([uACPI header:358](../../third_party/uacpi/include/uacpi/kernel_api.h#L358)). | IF=1 worker, battery cadence five seconds. EC busy-polls up to 500 ms per byte, up to 32 queries/work item ([ec.c:29](../../kernel/acpi/ec.c#L29)); this is not a universal IF=0 claim. Failed S5 has a ten-second IF=0 wait. | **Keep GPE methods on BSP deliberately**; Notify may run elsewhere, but splitting needs serial AML, SCI/power and allocator/map handoffs. **High, L**. |
| Entropy, [random.c:433](../../kernel/random.c#L433) | Single generator/backend health owner; static request staging and cross-CPU slot lock already exist ([30](../../kernel/random.c#L30)). CPU-source state currently names BSP as sole owner. | Yields after work; seed acquisition may retry until five-second deadline, with bounded individual instruction retries ([303](../../kernel/random.c#L303)). No current isolated service cost. | A fixed AP with validated CPU source is a comparatively small worker pilot; VirtIO RNG IRQ/backend must retain or transfer explicit ownership. Do not casually migrate across different CPU features. **Medium, M including scheduler foundation**. |
| Readiness, [readiness.c:300](../../kernel/user/readiness.c#L300) | Cross-CPU locked notification, but BSP incoming list, active snapshots and service completion. | IF=1 worker yields after progress or sleeps to deadline ([277](../../kernel/user/readiness.c#L277)); includes graphics pointer, terminal spatial and other waits. | Synchronized incoming/active/retirement and snapshot lifetimes, completion handoff; no hardware IRQ retarget needed. **Medium/high, M**. |
| Clipboard, [clipboard.c:1364](../../kernel/object/clipboard.c#L1364) | Locked stores, but authoritative keyboard/input synchronization, receiver/space lists and completed requests share BSP ownership. | Worker scans receivers/spaces and paste state under IF=0 ([1272](../../kernel/object/clipboard.c#L1272)); no elapsed budget; pending synchronization can retry each millisecond. | Split private store/validation work from BSP input/selection authority; retain activation, receiver and transaction lifetimes. **High, M/L**. |
| Initial-disk GPT workers + sequential USB scanner, [gpt.c:485](../../kernel/storage/gpt.c#L485), [490](../../kernel/storage/gpt.c#L490) | Block clients, shared scratch/publication and registry are BSP-owned. | Initial scans are one-shot; USB discovery polls each 10 ms and scans serially with a 30 s deadline. | Block/publication handoffs and owned scratch; little steady-state benefit. **Medium, S/M after block foundation**. |
| TCP and debugger identity helpers, [identity.c:71](../../kernel/net/lwip/identity.c#L71), [network.c:95](../../kernel/debug/network.c#L95) | Generic task policy; entropy and network notification already provide synchronized interfaces. | One-shot entropy wait up to five seconds, immutable release publication, then exit. | Good low-risk proof of fixed placement/retirement, but no recurring latency benefit. **Low/medium, S after scheduler foundation**. |
| Initial launch worker/Caelum boot init, [boot.c:46](../../kernel/user/boot.c#L46), [space.c:293](../../kernel/space.c#L293) | BSP boot ownership and Caelum's CPU0-only allowed set; prepares trusted initial grants/space setup. | One-shot launch work; boot init itself is explicitly CPU0-only, unlike normal user spaces. | **Keep deliberately** initially; moving startup does not remove runtime serial services. **High, L if changed**. |

## BSP-only operations without a separate worker

The [25-service catalog](../../include/kernel/service/request.h#L9) is completely
accounted for here or in the worker table: PIPE_CREATE, TERMINAL_CREATE, DISPLAY,
MEMORY, POINTER, AUDIO, CAPABILITY_GROW, NAMESPACE_CREATE, ENDPOINT_CREATE/EXPORT,
RAMFS, LAUNCHER, HOSTFS, NPFS (including FAT32), READINESS, SYSTEM_INFO_MEMORY/
POWER/HOSTNAME, POWER, SCREEN_CAPTURE, BLUETOOTH_HCI, CLIPBOARD, THREAD,
INPUT_POLICY and COMPLETION. Forwarding changes the servicing owner, not the
BSP completion/lifetime contract.

| Operation; code invariant | Consequence today | What changing ownership needs; risk/size |
| --- | --- | --- |
| Final task/process reap, [task.c:1267](../../kernel/task.c#L1267), [process.c:246](../../kernel/process.c#L246) | CPU0 drains a whole completed snapshot before choosing runnable work ([1406](../../kernel/task.c#L1406)), IF=0; final task invokes whole process destruction. Native 21.8/39.7 ms above. | Retain task/control/group/cleanup ownership, private pins and immutable results through bounded stages. No result merely on enqueue. **High, M/L**. |
| Retired objects/capabilities/endpoints, [object.c:431](../../kernel/object/object.c#L431), [capability.c:459](../../kernel/object/capability.c#L459), [endpoint.c:657](../../kernel/object/endpoint.c#L657) | Whole object snapshot/callbacks, whole capability capacity and endpoint owner lists run IF=0. A VM-only reaper fix leaves these tails. | Budget slot/list scans and expensive callbacks without releasing reachable storage; retain execution-group cleanup counts and endpoint loans. **High, L**. |
| Process VM allocation/release/thread context, [vm.c:350](../../kernel/mm/vm.c#L350), [process_vm.c:176](../../kernel/process_vm.c#L176) | IF=0 page zero/map/free, range metadata and wait-token withdrawal. BSP work remains for AP callers; native MEMORY handoff adds about 1.6–4.2 µs/mutation in [task 4](../development/experiments/threads-task4/README.md). | Preserve transaction/zero-root-borrower gate, mutator-only reply, context pins and token lifetime. General remote mutation is **High, XL**; bounded BSP work **M/L**. |
| General kernel VM and shared-range retirement, [vm.c:38](../../kernel/mm/vm.c#L38), [SMP flush protocol](../kernel/smp.md#memory-and-output-boundaries) | BSP owns reused ranges. Cross-CPU flush requires prior quiescence, IF=1/kernel root and acknowledgements; timeout retains backing. Task-stack retirement has its own CR3 departure rule. | One accepted mapping/reuse owner and invalidation protocol, not an allocator lock around remote use. **Keep BSP initially; High, XL**. |
| General `vm_query`, [vm.c:719](../../kernel/mm/vm.c#L719), [SMP query contract](../kernel/smp.md#memory-and-output-boundaries) | Contract remains BSP-only; arbitrary query is not a remote snapshot. Active user-buffer validation is an any-CPU exception using recursive mappings and a short lease. | Explicit stable-root/mapping lease or owned snapshot; reuse current user-copy exception where applicable, not a blanket relaxed assertion. **Medium/high, M**. |
| Image/launch/batch preparation and abort, [spawn.c:274](../../kernel/user/spawn.c#L274), [image.c:65](../../kernel/image.c#L65), [abort:112](../../kernel/user/spawn.c#L112) | Child zero/map/copy/protect/startup/grants with IF=0; ≤256 MiB image span plus ≤8 MiB stack. Unwind synchronously destroys partial mappings; a batch may abort up to eight children. | Unpublished child owner, resumable page/table cursors and bounded rollback, then atomic group publication. Image FILE operation/loans must survive waits. **High, L**. |
| Capability growth, namespace/pipe/terminal/endpoint construction, [capability.c:35](../../kernel/object/capability.c#L35), [namespace.c:124](../../kernel/object/namespace.c#L124), [catalog dispatch:324](../../kernel/service/request.c#L324) | Growth copies/initializes the whole table, while constructors prepare backing and exact destination claims. Heap safety alone does not make table/owner-list mutation parallel. | Preserve guards/slot reservations and cleanup under failure; prepare storage outside short guards, publish once. **Medium/high, M/L**. |
| RAMFS names/entries, directory metadata, FILE backing replacement, [ramfs.c:55](../../kernel/fs/ramfs.c#L55), [file.c:340](../../kernel/object/file.c#L340), [directory.c:501](../../kernel/object/directory.c#L501) | Captured entry/name creation and detached disposal are BSP services; replacement lends FILE operation ownership. Ordinary RAM-file reads/overwrites already use the calling CPU. | Separate owned allocation from structural mutation; maintain held object identity, topology/cycle checks and no use after loan publication. **High, M/L**. |
| Completion admission and execution-group metadata, [completion.c:147](../../kernel/service/completion.c#L147), [execution_group.c:121](../../kernel/object/execution_group.c#L121) | BSP creates operation/group storage, owner lists and exact result/delivery reservations; native completion is not permission to release unobserved work. | Keep issuer/gate/retirement correlation and stage cleanup evidence, with synchronized immutable publication. **High, M/L**. |
| Space registry/geometry, display/cursor acquisition and resize, [space.c:219](../../kernel/space.c#L219), [resize:799](../../kernel/space.c#L799), [display.c:129](../../kernel/display/display.c#L129) | BSP IF=0 prepares all per-space TTY/navigation/cursor/staging backing, then commits validated geometry. Full geometry cost is unmeasured. | Held old/new leases, source snapshots, acknowledged shared-range retirement and one input/display owner. Raster work alone can be leased elsewhere. **High, M/L**. |
| PS/2, USB-normalized input, focus/layers/selection/lock/power overlay, [input.c:51](../../kernel/input.c#L51), [pointer.c:357](../../kernel/pointer.c#L357), [power_overlay.c:123](../../kernel/ui/power_overlay.c#L123) | BSP IF=0 source/button snapshots, routing/epochs and fixed queue drains; no separate PS/2 worker. Presenter supplies normal cadence even with skipped frames. | One routing/activation authority and synchronized source/IRQ/worker handoffs. Overlay must remain independent of wedged application writers. **Keep owner BSP initially; High, L**. |
| Capture completion, [capture.c:171](../../kernel/display/capture.c#L171) | Admission/completion BSP IF=0; finish copies the full image into immutable FILE at [196](../../kernel/display/capture.c#L196). GPU waits/normal pixel copy elsewhere are mostly IF=1. | Frozen cursor/frame lease through confirmed flip/fallback and FILE/result installation; no stale/doubled cursor. Chunk RAM copy separately from owner publication. **High, M**. |
| System-info/input-policy/hostname publication, [system_info.c:60](../../kernel/object/system_info.c#L60), [input_policy.c:50](../../kernel/object/input_policy.c#L50) | Memory snapshot unnecessarily takes a BSP round trip despite any-CPU PMM stats; power/input/hostname depend on serialized owner state. | Memory-stat read is a **Low, S** direct-call candidate under existing PMM lock and user-copy lease. Other snapshots need immutable/locked publication; hostname remains one-time authority. **Medium, S/M**. |
| PCI claims, BAR/ECAM mutation, DMA setup and MSI/MSI-X routing, [resources.c:16](../../kernel/pci/resources.c#L16), [msix.c:10](../../kernel/pci/msix.c#L10), [apic.c:73](../../arch/x86_64/apic.c#L73) | Prepared before AP use, writes gated by claim owner and BSP IF=0. External device IRQs target BSP APIC ID, not literal APIC ID zero. | Quiescent device/IRQ owner transfer, validated target, cross-CPU notification and DMA ordering before re-enabling delivery. A register rewrite alone is unsafe. **Keep setup BSP initially; High, M/L**. |
| Shutdown/restart, [worker.c:475](../../kernel/acpi/worker.c#L475), [NVMe shutdown](../devices/nvme.md) | BSP coordinates user hold, flush/drain/controller shutdown and firmware entry; failed S5 can mask interrupts for ten seconds. Overlay offers forced disposition after refusal. | Preserve normal-versus-force outcome and independent overlay, DMA retention and firmware locality; not ordinary runtime work to spread. **Keep firmware action BSP; High, L if split**. |
| Boot preparation, [init.c:95](../../kernel/init.c#L95), [SMP handoff](../kernel/smp.md#boot-handoff) | BSP prepares CPU records, permanent mappings, device ownership and shared publication before AP service. AP startup/TSC calibration may busy-wait; boot-only. | **Keep deliberately**, with current bounded/error handling. Parallel boot is not an audio remedy. **High, L**. |

## Other interrupt-off or polling blockers

These are distinct from placement: work on an AP can still delay CPU0 through a
shared lock. A bounded byte/page/poll count is not a measured wall-time bound.

| Span | Bound or scaling and effect on CPU 0 | Required treatment / risk / rough size |
| --- | --- | --- |
| Reaper and object snapshots | IF=0, all completed tasks/callbacks before dequeue; backed/reserved pages and page-table topology scale work. Proven large native gaps. | Page **and table-entry** budgets plus retained cleanup owners; cap/object tails separately. **High, M/L**. |
| Live VM, launch, failure unwind, table growth | IF=0 throughout each operation; a drainage deadline does not bound zeroing/copy/free. Even failed launch can destroy a large prefix. | Resumable work/rollback with admission closed; do not merely sprinkle STI into an owner transaction. **High, L**. |
| Heap growth and PMM search/free | Any CPU, IF=0. [Heap growth:88](../../kernel/mm/heap.c#L88) holds growth ownership while mapping/zeroing every pool page; [PMM:194](../../kernel/mm/pmm.c#L194) scans fragmentation and marks/free-validates requested frames under its lock. CPU0 may do this or spin for an AP. | Preserve already accepted allocator locks/order and never-reused heap arena. Bound heavy work separately if measured material; no casual new allocator lock. **High, L**. |
| Shared spinlocks / queue and wait-list scans | [spinlock.h:17](../../include/kernel/spinlock.h#L17) spins with IF=0 without a time/retry bound. Ready/timer/stop/parking/resource lists can grow with tasks/registrations. | Existing order forbids logging/allocating/waiting across leaf locks. Separate scans/claims from publication with lifetimes intact, not unlock-and-retry borrowed pointers. **High, M/L**. |
| User syscall entry | [syscall_entry.S:13](../../arch/x86_64/syscall_entry.S#L13) masks IF via FMASK; continuations remain non-preemptible. Native RAM/IPC copying and heap growth can execute on CPU0 when its user task runs there. | Identify and bound actual direct spans; MEMORY's large mutation is deferred BSP work today. CPU0 exclusion reduces only user-originated interference. **High, L for general preemptible syscalls**. |
| Audio mix/commit, clipboard/input scans, capture/resize snapshots | IF=0 work described above. Audio native 20 ms/QEMU 1 ms post-commit guards detect lateness; they do not prevent it. Capture copy scales with full frame, not cursor size. | Precomputed immutable work where possible; bounded authoritative publication with no reference or geometry race. **High, M/L**. |
| Logging/serial/early-log replay | Logging runs on **calling CPU**, IF=0 ([format.c:209](../../lib/format.c#L209)); output lock can wait indefinitely for another CPU ([log.c:60](../../kernel/log.c#L60)). Serial polls up to 100,000 times **per character**; [early replay:17](../../kernel/log.c#L17) walks retained output under the lock. | Do not label this a missing BSP log worker. Avoid large synchronous dumps; changing drain semantics/fatal bypass needs a separate contract. **Medium/high, M**. |
| EC/AML and firmware power waits | EC's 500 ms/byte polls are normally IF=1 worker work; preemptible kernel work still consumes CPU. S5's ten-second IF=0 failure wait is deliberate terminal firmware behavior. | Keep firmware/GPE ownership; bound/yield non-terminal work without changing transaction semantics. **High, M/L**. |
| Clock reads, HPET maintenance, IPI sends | Clock reads already any-CPU. [HPET CAS:159](../../arch/x86_64/clock.c#L159) and high/low/high read retry have no retry-count cap. [Reschedule send:287](../../arch/x86_64/apic.c#L287) masks IF for up to 1,000,000 pending polls. BSP performs extension maintenance, not all clock reads. | Retain support-gap/monotonic and target ownership rules; investigate only with actual contention evidence. **Medium/high, M**. |
| Panic/fault/debug stop | Fatal CPU intentionally masks interrupts and attempts bounded serial/NIC/framebuffer reporting; failed fatal delivery must not await BSP progress. Debugger stop deliberately prevents service. | **Keep on faulting CPU**, including APs; not schedulable work to unpin. Panic paints every possible owned front. **High if changed**. |

Local APIC timers/deadline expiry, reschedule IPIs, TLB acknowledgements,
user-buffer validation, heap/PMM/per-CPU scratch access, RAM-file data and normal
log calls are **already per-CPU or any-CPU**, not missing BSP migrations.
BSP-only HPET maintenance is a designated single owner; moving that tiny cadence
has no established benefit. GPE AML, early initialization, PCI ownership setup,
shared VM reuse and final firmware power actions stay BSP deliberately for the
first slice. IRQ service and worker location need not be identical eventually,
but today local interrupt exclusion relies on their common BSP.

## Benefit/cost ranking

| Rank | Candidate | Why / remaining limitation |
| --- | --- | --- |
| 1 | Bounded interrupt-on BSP reclamation | Directly removes proven 21.8/39.7 ms native scheduler blackout without reopening general VM ownership. VM pages/table entries, task/cap/object tails must be distinguished. **M/L, high safety risk but bounded scope**. |
| 2 | Bound other IF=0 owner operations | Launch/rollback, VM allocation, capture, table growth and input/clipboard scans can still starve every BSP device. Moving a worker is not required to reduce these spans. **M/L per operation**. |
| 3 | Worker selection precedence with user progress | Cheap queue-policy change reduces runnable delay after interrupts resume; not an IF=0 remedy or audio deadline guarantee. Native queue-delay attribution is not established. **S/M, policy/fairness risk**. |
| 4 | Small direct immutable snapshots / fixed worker pilot | SYSTEM_INFO_MEMORY can use existing any-CPU PMM stats (**S**, low benefit/cost). Entropy or one-shot identity is a **M** pilot for AP kernel workers; little measured steady benefit, lower shared-state exposure. |
| 5 | Audio off BSP with its IRQ/queue ownership | High benefit isolating steady stream service from remaining BSP blackouts; does not fix other devices or blocked producers. **M/L**, requires AP worker and cleanup foundation first. |
| 6 | Device-private block queues, then leased CPU rendering | Preallocation helps block ownership; immutable render jobs can reduce visible-frame work. Whole-frame skipping already removes idle rendering. **M/L**, DMA/capture/panic hazards. |
| 7 | Network, xHCI services, native/HOST filesystem | Larger shared lifecycle graphs; splitting controller/store head-of-line work may matter more than its CPU. **L/XL**, existing cost is not isolated. |
| Keep | General VM reuse, GPE firmware methods, early boot, fatal reporting | Deliberate invariants above. Broad removal requires accepted ownership/invalidation changes; no evidence that wholesale unpinning is a small first slice. |

Existing native display records put this ranking in context: [frame skipping](../development/experiments/presenter-frame-skipping/README.md)
cuts idle compose/copy elapsed per 30 s window from 3.85–3.95 s to 33.9–38.0 ms;
continuous motion still submits full frames. [Renoir poll split](../development/experiments/renoir-poll-cost/README.md)
cuts measured observation elapsed/frame from 3.393–3.415 ms to 1.116 ms.
These spans include preemption and are **not total BSP CPU profiles**. Historical
audio/USB/filesystem throughput figures likewise do not establish current
per-worker CPU savings, so they are not summed into an invented BSP budget.

## Small first slice and coordination

Claude owns HDA recovery, codec-setup retry and then bounded teardown; this
inventory must not duplicate that implementation. Accepted first-slice direction:

1. Move final reclamation out of the scheduler's uninterrupted drain into a
   **BSP, IF=1 reaper**. Scheduler handoff is allocation-free and retains the
   retired task/process/control/group records until consumed. No general VM
   mutation runs on an AP.
2. Reclaim by bounded pages/table entries, restoring IF/yielding only after
   unmapping scratch slots, releasing locks and leaving consistent ownership.
   One range/process per yield is insufficient: a range can contain hundreds
   of MiB. Preflight, page-table recursion, failure unwind and deferred callback
   work must have explicit coverage or stay an honestly documented residual.
3. Preserve existing completion boundaries: thread JOIN after its task/context
   retirement; process WAIT after VM/resource teardown, not queue admission;
   group cleanup remains pending through deferred object callbacks. Retain
   task-local cleanup attribution ([task.c:213](../../kernel/task.c#L213)) and
   no borrowed task/request pointers after publication. Do not add a promise
   that process WAIT already waits for every asynchronous object callback.
4. Qualify normal exit, stop, fault and failed launch with large backing and
   group observers while audio/input/presentation are active. Reuse the existing
   diagnostic method for before/after masked spans and report total reclaim time
   separately. No new instrumentation or measurement is merged by this inventory.

**Status:** items 1–3 are implemented by the bounded-teardown change; see its
[record](../development/experiments/bounded-teardown/README.md) for nested
qualification, residual interrupts-off tails and the native run. Worker
precedence (decision 2) remains a separate follow-up.

**After the first slice the owner can:** quit or terminate a large game while
CPU0 continues taking device interrupts and scheduling between reclaim chunks,
with unchanged native retirement results. This is the planned outcome, not
qualified delivery or a promise that all remaining audio misses disappear.

## Owner decisions — accepted 2026-10-13

All three defaults are **accepted, not implemented**:

1. **First slice: retain BSP ownership, make reclamation preemptible and bounded.**
   Coordinate with Claude's bounded-teardown work as above, preserving VM,
   process/thread/group and callback lifetime contracts; disclose remaining IF=0
   tails. Defer off-BSP destruction and allocator/VM ownership changes.
2. **CPU0 worker precedence follows bounded teardown.** Only as a separate,
   measured follow-up after bounded teardown; keep the existing queue policy
   for that first qualification. Propose a fairness bound before code. The
   accepted direction prefers FIFO worker selection rather than LIFO head
   insertion; this inventory does not settle the user-progress bound or remove
   the independently observed uncapped-load queue-delay risk.
3. **Fixed worker owner CPUs.** Never migrate or work-steal live kernel workers.
   Start a separately authorized AP-placement pilot with entropy/identity after
   its backend/IRQ ownership is proved. Audio is the first substantial candidate
   after that foundation. VM/object mutation, GPE and firmware actions stay BSP.

Acceptance does not authorize implementation. The owner's pause on new tasks
still holds. Each later move needs its own accepted ownership/baseline/native-
qualification scope; this is an inventory and first-slice choice, not a new task
backlog.
