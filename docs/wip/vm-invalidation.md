# BSP removal task 3: invalidation and shared kernel VM

Decisions 5–7 accepted (chronium, 2026-10-26). Implementation is assigned to
Codex alpha, with a single-run baseline before code. Root epochs and the
concurrent acknowledgement engine are merged in #1269; its delayed-ACK check
is recorded in #1271. Short shared query/publication guards are implemented
separately. Structural-owner and caller integration remain pending, including
the accepted lifetime/firmware integration rules below. This follows
[caller-CPU MEMORY](../development/experiments/memory-caller-cpu/README.md) and
[bounded frame batches](../development/experiments/frame-batches/README.md),
now merged as tasks 1–2 of [BSP removal](bsp-removal.md).

## What this task must establish

An executing CPU can own an exclusive VM transaction and retire a reused shared
kernel range without sending mutation through CPU0. Completion must prove both
that old users have stopped and that their translations cannot survive/reappear.
An IPI acknowledgement alone proves neither object lifetime nor device/DMA
retirement. Another CPU must never reuse an old frame, page-table record or VA
because a timeout was mistaken for success.

This task supplies the ownership/invalidation foundation. It does not move
kernel workers, IRQs, driver callbacks, ACPI/GPE or firmware operations; move
process preparation/reaping (task 4); add JIT rights; enable PCID/global pages;
or optimize scratch zeroing. Those retain their existing decisions.

## Source inventory

Historical inspection at main `1c71d1bb` (including #1254 and #1263); source
facts at that revision, not a new performance or native measurement. #1269
supersedes the root/residency and singleton-engine gaps in this table.

| Code | Current proof and the remaining gap |
| --- | --- |
| [process_vm.c](../../kernel/process_vm.c), `process_vm_dispatch`, `process_vm_root_depart`, `process_vm_memory_call` | One per-process owner and aggregate root/borrower counts. Admission reserves before CR3; departure acknowledges after leaving the root. No CPU-residency mask or translation generation exists. MEMORY is already caller-CPU, with whole-process drainage and mutator-only reply. |
| [task.c](../../kernel/task.c), `task_schedule` | Captures the root actually dispatched, loads CR3 before task-stack access, and leaves it on the permanent scheduler stack before departure/reaping. A task's requested next mode is not evidence of which root it just left. |
| [paging.c](../../arch/x86_64/paging.c), `arch_space_activate`, `affects_active_space` | Every activation reloads CR3, with PCID/global translations disabled. A mapping helper invalidates only the executing CPU. Shared intermediate tables remain allocated; an assertion's removal cannot make remote unmap/protect safe. |
| [smp.c](../../arch/x86_64/smp.c), `arch_kernel_flush_remote` | BSP-only singleton busy flag/generation. Broadcast, full current-CR3 reload, exact-generation ACK, one-second deadline. No mapping changes occur in this helper. Independent initiators cannot share its request state. |
| [apic.c](../../arch/x86_64/apic.c), `try_send_ipi` | Existing 32-bit APIC destinations, bounded xAPIC delivery attempt and x2APIC MFENCE/LFENCE-before-WRMSR publication ordering. Reuse these transports; no new interrupt routing. |
| [vm.c](../../kernel/mm/vm.c), `require_kernel_owner`, `kernel_ranges`, `vm_free`, `vm_query` | General kernel metadata and reused ranges assert BSP ownership; up to 4096 static range records. There is no any-CPU structural owner or query/commit exclusion. `vm_free` unmaps and immediately frees backing, so it cannot be exposed to remote users by dropping the assertion. |
| [space.c](../../kernel/space.c), `resize_display` / `vm_kernel_flush_remote` call | TTY locks and presenter-frame boundaries first relinquish old buffers and prevent refills. The old mappings stay present through the flush. ACK success permits free; timeout retains one old batch and disables resize. Preserve this proven ordering. |
| [task.c](../../kernel/task.c), `free_task`, and [process.c](../../kernel/process.c), destruction/reaping | Scheduler-side task-stack release cannot sleep. Actual results must still follow retirement. The stack's leave/reload proof is special; it is not a general shared-range reclamation proof. |
| [npfs_store.c](../../kernel/fs/npfs_store.c), `store_vm_allocate/free`; [resources.c](../../kernel/pci/resources.c), `pci_unmap` | Existing IF=0 callers either collapse failures to NO_MEMORY or assert free success. Introducing owner contention requires admission/deferred cleanup at their outer boundary, not a new surprise failure under a lock. DMA/BAR lifetime remains the device owner's responsibility. |
| [renoir_otg.c](../../arch/x86_64/amd/renoir_otg.c), mapping validation; [virtio_gpu.c](../../kernel/display/virtio_gpu.c), backing queries | A stable mapping query must not start failing because an unrelated range has a long-running mutator. A query describes a mapping; it does not itself retain the physical resource afterward. |

The heap's never-reused arena and general kernel VM occupy disjoint PDPT entries
under a boot-created shared PML4 slot (`paging.c`, bootstrap near line 454).
Heap growth retains its separate existing owner and never-reused-address proof.
Do not replace that with the general VM owner or take an allocator lock while
waiting for acknowledgements.

## Decision 5 — private roots and the acknowledgement engine

**Accepted (chronium, 2026-10-26):** keep whole-process root/borrower drainage for private
mutations in this task. Add a non-reused translation generation and a live CPU
residency mask to that existing process/VM transaction. Reserve a root's CPU bit
before CR3 activation; publish departure only after kernel CR3 on the permanent
scheduler stack. Capture the generation of the actually dispatched root, not the
task's later requested mode. Private page-table changes remain under the one
exclusive owner, with no live sibling root or borrower. The mutator's reply
reentry and every later entry load the committed root generation and reload CR3.
Generation exhaustion refuses before effects as NO_MEMORY, like existing
non-reused identity exhaustion. It must not wrap or identify a different root incarnation.

With PCID/PGE disabled, an acknowledged departure already discarded that CPU's
private translations; the implementation should use that proof rather than send
redundant private IPIs. Be explicit: this adds generation/residency coordination,
**not live-root private mutation or a private shootdown speedup**. Siblings still
quiesce. Moving preparation out of closure remains task 4.

For actual remote invalidation, use permanent **per-CPU requested/acknowledged
epochs** and a globally ordered non-wrapping epoch allocator. No BSP executor,
singleton busy flag or transient request/root pointer in the IPI handler. An
initiator publishes the relevant closure before issuing its epoch, publishes
its target CPU requests, then sends through the existing APIC path. The handler
snapshots the request, reloads its current CR3, release-publishes ACK and returns:
no allocation, locks, logging, queue wake or unbounded drain loop. It preserves
the active-root identity and interrupt frame.

A higher epoch may cover an older request only with an explicit publication
proof: issuing epochs is acquire/release ordered, and mailbox updates preserve
that ordering. A later completed flush must cover the earlier initiator's
closure even if the earlier initiator was delayed before ringing its doorbell.
`ACK >= requested` is not safe merely because both numbers increase. Coalesced
or delayed IPIs must not lose a request; the waiting initiator rechecks and
re-signals unacknowledged targets within its one deadline. CPU membership stays
fixed. Shared-range requests conservatively target every online CPU unless an
existing, explicit lifetime proof excludes a CPU; no locality guess from where
an allocation originated. Independent initiators do not wait on one another
with interrupts disabled.

Alternative: allow sibling execution through additive or destructive private
mapping changes now. That also needs read/copy leases, concurrent page-table
walkers, rollback and permission-transition semantics; it changes the task 1
quiescence contract. It is not required to remove the remaining BSP owner and
is not the recommended first delivery.

## Decision 6 — shared ownership, queries and safe admission

**Accepted (chronium, 2026-10-26):** one CPU-neutral logical structural owner for the
general kernel VM, with short guards for range metadata and mapping
publication/query snapshots. Ownership is retained across preparation and
bounded steps, not represented by a spinlock held across allocation, copying,
IPI waits or a yield. The mutation's actor remains retained on its fixed CPU.
This serializes one shared range registry initially; it does not serialize
unrelated private address spaces or the never-reused heap arena.

Prepare frames, needed page tables and retained-cleanup storage before the
nonfailing commit. Publish new ancestors and leaf changes under short guards
that allocate nothing. Shared intermediate tables stay retained as today;
this task does not reclaim them behind concurrent walkers. Stable unrelated
queries use the short snapshot guard, not the long-lived mutator owner. They
must retain their existing consumer mapping/resource lifetime proof beyond the
snapshot, and must not gain spurious BUSY/unavailable results because another
range is being prepared.

Every reused/unmapped/protected shared range also needs its **consumer's access
and refill exclusion**, including cached pointers, render leases and device
ownership where applicable. The VM owner does not discover all C pointers or
make a GPU/DMA operation complete. Retain the existing TTY/presenter proof;
new users must supply an equally explicit ownership handoff. Never classify an
old range as unpublished merely because its current owner is the BSP. Permanent
scheduler stacks, executing stacks, CPU/IPI records, kernel text and the heap
arena are not generic retirement targets; mutation and acknowledgement code
must remain mapped independently of the range being changed.

Integrate admission at audited outer boundaries before state publication:

- A sleep-safe kernel actor can park using retained per-task waiter storage,
  with interrupts serviceable and no output/object/queue/allocator lock held.
- Scheduler/final stack cleanup cannot sleep or silently enable interrupts in
  its critical section. Try admission; if busy, keep the object on the existing
  owned retirement path and retry later. Do not release pins, publish completion
  or discard a stack before reclamation actually finishes. Its existing
  scheduler root/stack leave proof remains a valid special case without a new
  broadcast on each task exit.
- A fallible preparation path can refuse before effects using existing BUSY
  semantics. Do not collapse ownership contention into NO_MEMORY or let an old
  `KASSERT(vm_free(...) == MM_OK)` turn it into a panic. Non-fallible cleanup
  needs retained/deferred ownership, not fabricated success.
- Boot-before-AP and demonstrably unpublished paths keep explicit fast proofs.
  Direct architecture map/unmap callers must be enrolled too; a VM wrapper alone
  cannot enforce the contract. Device register/firmware callbacks keep their
  fixed existing CPUs; this changes only VM ownership.

There is no new public object, grant, MEMORY operation, broker or worker. If a
call-site audit reveals a new externally visible refusal/retry or a cleanup
lifetime that these rules do not settle, return to chronium before changing it.

Alternative: per-range parallel structural mutators immediately. The current
single ordered metadata list and missing-table construction would need a wider
concurrency rewrite before their ownership is proven. A global spinlock around
`vm_alloc/free` is not an acceptable alternative.

## Decision 7 — timeout and uncertain completion

**Accepted (chronium, 2026-10-26):** preserve and generalize the current **confirm before
unmap/protect** sequence. Prepare and prevalidate first, then stop accesses and
refills; keep old PTEs, page tables, frames and owned records intact while
requesting the flush. Owner admission has a **one-second budget** before it
refuses unchanged. Once preparation under that owner finishes, use a separate
absolute **one-second closure/dispatch/ACK budget**, with **1 ms sleeping polls**
where a task context may wait. These are two explicit bounded waits, not a
restarted budget for every target CPU. Preparation/stepped commit retain their
own existing work bounds; this is not a public syscall completion deadline.
The IPI handler stays bounded and lock-free. Scheduler-only contexts use the
nonblocking/deferred rule, not a timed spin.

After all required acknowledgements, apply the prevalidated PTE transition in
bounded IF=0 steps while refills remain excluded. Only then release backing,
recycle VA/records and reopen/publish the new state. No fallible IPI wait follows
the first PTE effect in this slice. Private mutation uses its already-drained
root proof; public MEMORY's BUSY/NO_MEMORY/all-or-nothing outcomes stay unchanged.

Distinguish these internal outcomes:

1. **Unchanged refusal:** admission/preparation could not proceed, before any
   mapping effect or irreversible consumer publication. Preserve the old object
   and mappings, release only unpublished preparation and report/defer honestly.
2. **Retained, unconfirmed retirement:** closure/ACK deadline or epoch exhaustion
   prevents proof. Leave the old mappings/backing/reservations owned and mapped;
   keep a preallocated VM-owned retained record independent of the initiating
   task's stack, process result or eventual stop. Release the structural owner
   so unrelated ranges can progress. If the consumer can abort back to its
   unchanged old state, it may resume using that same owned old mapping; it may
   not free or reuse it. If it already switched state (as resize does), keep
   that old batch quarantined, disable further destructive changes for that
   consumer and retain it until reboot. No background retry or late-ACK cleanup.
3. **Confirmed commit:** only this outcome permits reclamation or publication
   as completed. A late ACK cannot retroactively turn a returned retained result
   into it. Permanent per-CPU mailboxes contain no freed request/root pointer;
   interrupted initiators must join the commit/unwind or transfer retained
   ownership before their storage/observers retire.

Routine timeout does not panic the machine, pretend nothing happened after an
irreversible consumer switch, return a success-shaped result or free uncertain
storage. Existing invariant corruption remains a kernel bug, not a new recovery
status. If an actual call site requires a fallible wait after PTE effects,
stop and bring that separate uncertain-outcome decision before implementing it.

Alternative: write PTEs first, invalidate afterward and force-stop a private
process or quarantine a changed kernel mapping on missing ACK. That introduces
post-effect uncertainty and cannot preserve today's ordinary MEMORY failure
contract without additional policy. Infinite waits or routine global panic are
not the recommended recovery policy either.

## Delivery and checks after acceptance

1. Root-generation/residency accounting and concurrent-initiator epoch engine.
   Check reserve-before-entry, actual-root departure, root switches during IPI,
   coalescing, delayed publication, 32-bit APIC destinations and both xAPIC/x2APIC
   transports. Keep PCID/PGE disabled; no generation/request-pointer reuse.
2. Shared structural owner, query/commit guards and caller admission audit.
   List every migrated direct VM/architecture caller and its lifetime proof;
   retain deliberate device/BSP owners. Integrate the existing resize retirement
   rather than add a second bypass. Never hold short locks while waiting.
3. Disposable QEMU qualification at 1 and 4+ CPUs: independent initiators,
   unrelated private mutations, root entry/exit races, shared range reuse with
   distinct patterns, permission reduction and both W/NX-to-R/X directions under
   owned access exclusion. Check page-table bits and normal permission faults
   in disposable user children, without adding executable rights to MEMORY.
   Stop/cleanup/results must join actual retirement. Mixed display/TTY output,
   stable device queries and shutdown/reaper paths detect unsafe admission or
   broad ownership stalls. Record matched baseline/regression controls.

**Scoped failure-path approval (chronium, 2026-10-26):** one disposable RAM-only
QEMU delayed-ACK check, preferably by pausing a vCPU through GDB rather than a
kernel hook. The [production-kernel check passed](../development/experiments/vm-epochs/README.md#delayed-acknowledgement-production-kernel-qemu-pass):
timeout retains the existing resize consumer's backing, fences further resizing,
and a late ACK cannot reclaim storage. No hook or test code is merged. This does
not qualify the remaining shared-owner integration or authorize other fault
scenarios/native fault tests. Raise those scopes separately when reached.
Native work goes through the orchestrator after ordinary QEMU qualification.
The task 1/2 A/B/C native packet has no task 3 implementation or instrumentation.

## Accepted caller integration rules (chronium, 2026-10-26)

**Accepted (chronium, 2026-10-26), implementation assigned.** The source audit
at main `d69b24c6` found two concrete extensions to general admission/timeout. Ordinary fallible
preparation already has the accepted answer: preserve BUSY through internal
image/VM results to the native caller, instead of converting it to an invalid
image or NO_MEMORY. No new public operation or grant is needed. Actor identity
must distinguish a retained task from its CPU's permanent scheduler context;
being on the same CPU never grants recursive ownership to a different actor.

### 8. Final cleanup that cannot return BUSY

`free_task` destroys request/profile/CPU state before its VM free, so scheduler
admission must precede detaching `completed_head`, process-member removal and
pin/result publication. The existing task/object retirement queues can retain
their entries unchanged on contention, as decision 6 already requires.

Other final-release paths have no queue or refusal result: display-frame final
references ([display.c](../../kernel/object/display.c)), selected-image capture
([image_capture.c](../../kernel/user/image_capture.c)), later prepared-task/
batch discard, and temporary/cache/pool frees in
[npfs_store.c](../../kernel/fs/npfs_store.c). Returning early then clearing their
descriptor would lose the only owner. Letting the ordinary one-second admission
budget turn these void operations into apparent completion is also incorrect.

**Accepted:** extend retained try/defer cleanup to these concrete items. Transfer
the whole cleanup record, its backing and cleanup attribution to its existing
BSP retirement owner; use the existing process-reaper task for items needing
bounded, sleeping/interruptible work, not a new worker or a generic callback
framework. A busy owner leaves the item pending beyond the ordinary one-second
fallible-admission budget. Do not detach pins, release capture ownership or
signal joined process/group cleanup until actual reclamation finishes. The
presenter can continue after transferring its final frame reference, without
waiting on shared-VM admission. Optional cache eviction may instead leave its
entry intact and try later. Never claim a range was freed just because cleanup
was queued. This is owner contention, not permission to retry an unconfirmed
ACK retirement or reclaim quarantined storage on a late ACK.

Cleanup/result observation and RAM retention can last longer than one second
while another structural owner is busy. The whole cleanup record remains owned;
task 4 moves heavy reclamation off the BSP without weakening joined completion.

### 9. Runtime firmware windows must not fail from unrelated owner contention

[ACPI map](../../kernel/acpi/map.c) can run inside uACPI's IF=0/locked callbacks;
its map API can return only a pointer or NULL. Treating owner contention as NULL
becomes `UACPI_MAP_FAILED`, potentially aborting a real AML operation. Admission
around a whole ACPI turn is not a simple fix either: power handling waits for
NPFS flushes, whose worker can itself need shared VM, creating an owner cycle.
GPIO and late LPSS assignment also fill boot-reserved native windows after AP
startup. Their fixed device owners and existing pin-on-failure policies stay.

**Accepted:** make the ACPI/GPIO boot-reserved, never-reused windows explicit
permanent mapping domains. Prepare their page-table ancestors before AP startup;
seal the reservation against generic release/reuse. The current fixed firmware/
native owner may fill fresh slots and publish their leaves under the short
mapping guard, without acquiring the long-lived general range-registry owner.
It may undo only unpublished partial fills, consuming those slots permanently;
there is no remap/protect/recycle operation. Existing aliases keep working,
and runtime callbacks need no allocation, sleeping admission or new firmware
failure from another range's owner. ACPI's 64 MiB VA allowance stays unchanged;
the necessary ancestor tables become boot-lifetime storage even if unused.
Device physical-range/claim checks remain separate and unchanged. LPSS can wait
at its descriptor worker's safe outer boundary: it still acquires the general
owner before its overlap check, attempted-assignment record, physical claim,
mapping and BAR publication. A missed admission leaves that kernel preparation
pending rather than pretending the hardware is unsupported; no partial hardware
operation is retried. The permanent-window exception does not bypass that full
assignment scope or authorize arbitrary BAR writes.

This is a narrow permanent-owner exception, like the separately owned,
never-reused heap arena; general shared VM still requires its structural owner.

## What chronium can do after task 3

Exercise reused shared kernel mappings from an audited any-CPU owner and run
concurrent private memory transactions with explicit root generations, without
a CPU0 invalidation executor. A missing acknowledgement retains old storage and
reports the affected consumer's limitation while unrelated work continues.
Private siblings still quiesce; device workers and process preparation/reaping
have not moved. The next task can move their RAM work onto this ownership base.
