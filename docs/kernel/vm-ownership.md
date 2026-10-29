# Kernel VM ownership and retirement

Private memory operations, process/image/thread preparation and DISPLAY mapping
work execute on the calling CPU. Heavy RAM reclamation uses fixed per-CPU
reapers. Programs need no new grant or operation to use these paths. Sibling
threads still quiesce during private mapping changes; competing mutation or
bounded pre-effect admission can return BUSY. Thread/process retirement and
unleased DISPLAY joins complete after actual cleanup, not queue acceptance.
Selected-image backing can retire independently after a successful launch,
retaining its existing group cleanup attribution.

Device, firmware, IRQ, presentation and display-control owners stay fixed.
This removes CPU0 as the general RAM/page-table executor, not as the owner of
those services. Ordinary MEMORY remains RW/NX; executable backing requires the
separate [JIT authority](../interfaces/jit.md).

## Execution and private ownership

A requesting user task captures input into retained per-task storage. The
scheduler leaves its private root on the permanent scheduler stack, loads kernel
CR3 and acknowledges departure before running that task's kernel-root
continuation on the same CPU. The continuation can park or yield between bounded
steps without retaining a scratch alias, short lock or borrowed user pointer.
Its task, request, FP/TLS state and admitted operation remain owned throughout.

One process VM transaction serializes MEMORY, JIT, DISPLAY mapping work and
thread-context changes. It closes new root admission, permits already-admitted
syscalls to finish bounded copies and drains roots and short borrowers before
effects. A staged I/O wait holds no mapping lease. Only the mutator may reenter
its root to revalidate and copy the final result; ordinary dispatch resumes
after that cleanup. Independent address spaces can mutate on different CPUs.
This is not live-root mutation.

Root entry reserves the CPU residency bit before CR3 activation; departure
publishes only after leaving the actually dispatched root. Non-reused translation
generations identify commits. Every later root entry reloads CR3 and observes
the committed generation. With PCID and global translations disabled, drained
private roots need no redundant remote flush. Identity/generation exhaustion
refuses before effects rather than wrapping.

The allocation registry and context pins have synchronized ownership. Guarded
stack/TLS pins retain backing through native task retirement; final unpin is the
last record access because another CPU may then release it. Address-wait
incarnations are never reused when addresses are recycled. See
[private memory](memory.md) for operations, exact failure semantics and page-step
bounds, and [SMP ownership](smp.md#memory-and-output-boundaries) for lock order.

## Backing and queries

The existing PMM and heap work on any CPU. VM backing allocates or frees vectors
of at most 64 arbitrary frames under one PMM-lock acquisition, then zeroes/maps
outside that lock. Frames belong immediately to the in-flight transaction;
partial claims unwind under the existing all-or-nothing publication rule. Free
validates the complete vector before changing accounting. There is no retained
per-CPU free-frame cache, refill owner or hidden free-memory reserve.

Each CPU's scratch slots are used with interrupts disabled and removed before
yielding. Private inactive-root queries require the preparation/mutation owner;
active-root user-copy checks retain their separate short-lease proof. General
kernel queries use short guarded snapshots, independently of the long-lived
structural owner. A snapshot is not a resource lease: consumers must separately
keep their frame, device mapping or other backing alive. An unrelated mutation
does not make a stable query spuriously unavailable.

## Shared kernel mappings and invalidation

One CPU-neutral logical owner serializes the general kernel range registry.
Actor identity distinguishes a retained task from the permanent scheduler
context even on the same CPU. Short guards protect metadata and mapping
publication; no spinlock spans allocation, copying, a context switch or an ACK
wait. Needed frames, page tables and retained-cleanup storage are prepared before
nonfailing bounded commit. Shared intermediate page tables remain retained.

Reusing, unmapping or protecting a shared range additionally requires the
consumer to stop access and prevent refills, including cached pointers,
presenter leases and device/DMA access. VM ownership cannot discover these
pointers or prove a device has finished. Keep old mappings and backing intact
while requesting invalidation; only confirmed retirement permits reuse.

Remote flushes use permanent per-CPU requested/acknowledged epochs and one
ordered, non-wrapping epoch allocator. Requests target all online CPUs unless an
explicit existing lifetime proof excludes one. Publication ordering lets a
higher acknowledged epoch cover an older closure. The IPI handler snapshots its
request, reloads current CR3 and publishes ACK without allocation, locks,
logging or transient request/root pointers. Initiators recheck and re-signal
unacknowledged targets within one absolute deadline. CPU membership stays fixed.

Admission has a one-second pre-effect budget. Closure/dispatch/ACK uses a
separate one-second absolute budget with 1 ms sleeping polls in task contexts;
these are implementation budgets, not a public operation deadline. Scheduler
cleanup uses nonblocking defer rather than sleeping or spinning. Waiters retain
their owned records with interrupts serviceable and no short lock held.

There are three distinct outcomes:

- **Unchanged refusal:** admission or preparation fails before effects. Preserve
  the old object/mappings and release only unpublished preparation; fallible
  callers report the existing BUSY or allocation failure.
- **Retained retirement:** ACK or epoch proof is unavailable. Retain the old
  mappings, frames, reservations and cleanup attribution independently of the
  initiating stack. Release the structural owner so unrelated ranges progress.
  A consumer may resume its unchanged old state only if its own protocol permits
  that. An irreversibly switched consumer quarantines the old batch and disables
  further destructive changes until reboot. Late ACKs do not reclaim it.
- **Confirmed commit:** apply the prevalidated transition while refills remain
  closed, then reclaim/recycle and publish completion. No fallible IPI wait
  follows the first PTE effect in this path.

Runtime contention is not a panic or fabricated free. Final cleanup without a
refusal result retains the whole record beyond the admission budget until it
can really finish. Unconfirmed retirement instead remains retained until reboot.

## Preparation and RAM reclamation

Launch/image/startup and thread-context preparation run in the retained caller's
kernel-root continuation. Boot preparation retains its explicit boot owner.
Shared task-stack reservation and fixed space/device setup acquire their own
admission at safe outer boundaries; private image backing/copies do not hold
the general kernel owner. Atomic child/batch publication runs on the preparation
CPU under the existing group/queue guards. Release structural ownership before
actor handoff.

Selected images remain immutable, owned snapshots through all loader reads.
Their producer transfers a single read phase and cannot refill after handoff.
Captures read on an AP, or retired away from their producer CPU, require confirmed
shared-range retirement before address/backing reuse. Unpublished rollback
retains provisional processes/tasks, reservations, parent cleanup pins, captures
and group attribution until actual unwind. Stop does not discard an admitted
preparation or turn queued rollback into completion.

Each online CPU has one mandatory fixed RAM reaper prepared before user
admission. The scheduler hands it whole task storage only after permanent-stack
and root departure. Stack/request/profile/FP release and context unpin precede
thread completion. The last departing task records the process RAM destination;
later cleanup-pin release uses the same destination. Unpublished rollback with
no departing task uses its preparation CPU. No actor migrates or steals work.

Process cleanup alternates fixed session/device/reference phases with recorded-CPU
private unmapping, backing/page-table reclamation and allocation/table cleanup.
Pending display cleanup transfers process ownership to its join rather than
parking the RAM actor behind one process. Actual storage and records retire
before process completion; execution-group completion additionally joins
attributed deferred object/device cleanup. Process WAIT does not promise that
all independent asynchronous device callbacks have finished.

Initial child-placement scoring discounts the executing preparation caller's
one transient running contribution. Global scheduler load accounting remains
truthful. This changes neither affinity nor child publication/stop rules.

## DISPLAY mapping and backing lifetime

ACQUIRE/REPLACE/RELEASE use the common drained process transaction. CPU0 reserves
a complete geometry/owner/mapping snapshot and validates it before old mappings
are withdrawn. The caller CPU prepares backing and private aliases; CPU0
publishes the prepared set. Both typed headers live in provisioned shared task
request storage, never a borrowed remote stack. Geometry/ownership changes can
refuse before effects with the old display intact; once effects begin the
admitted transaction completes or fully unwinds. SIZE/SUBMIT, input routing,
reference changes and hardware presentation keep their fixed owner.

Each frame records its preparation CPU. Final reference release transfers the
whole detached backing and existing group attribution to that CPU's RAM reaper.
No device callback moves with it. Private aliases, drawing roots and consumer
leases must have ended before retirement. Unleased backing joins actual caller
or process completion; an independently retained presenter frame keeps its
separate lifetime and group cleanup. ACK uncertainty retains backing and its
join until reboot while unrelated RAM cleanup can continue. See
[display ownership](display.md#transactional-local-resizing) and the source contracts in
[display_retire.h](../../include/kernel/display_retire.h).

## Deliberate fixed and permanent domains

Device queues, IRQs, network/audio/USB/filesystem workers, ACPI/GPE and firmware
actions keep their existing owners. DMA/BAR/ECAM and scanout obey their device,
cache-attribute, claim and boot/reset lifetimes; runtime DMA is not generically
freed by this milestone. PCI overlap selection and claim retain one owner scope.

ACPI/GPIO use sealed boot-reserved never-reused windows with ancestors prepared
before AP startup. Their fixed owner fills only fresh slots under short mapping
guards, without general-owner admission inside locked firmware callbacks.
Unpublished partial fills consume their slots permanently; these domains offer
no generic remap/protect/recycle. LPSS assignment still holds the general owner
through overlap check, physical claim, mapping and BAR publication. The heap's
never-reused arena has its own owner. Executing/permanent stacks, kernel text
and CPU/IPI records are not generic retirement targets.

## Qualification and remaining limits

[Caller-CPU MEMORY](../development/experiments/memory-caller-cpu/README.md),
[frame batches](../development/experiments/frame-batches/README.md) and
[native MEMORY A/B/C](../development/experiments/memory-native-abc/README.md)
record allocation, rollback and accounting. The
[invalidation record](../development/experiments/vm-epochs/README.md) separates
ordinary runs, AP/permission checks and the production-kernel delayed-ACK
resize check; late ACK did not reclaim its retained backing. Internal permission
changes use the same drained proof as JIT, with no execute right added to MEMORY.

[Preparation measurements](../development/experiments/bsp-preparation.md) record
native launch improvement and the accepted 4 KiB SEND residual; source attribution
of its rare stalls remains open in [#1588](https://git.internal/PyxisOS/pyxis-os/issues/1588).
[DISPLAY qualification](../development/experiments/bsp-display.md) records
1/4-CPU QEMU render/resize/owner-exit recovery and the six fresh native vela A/B
boots: all cohorts and fixtures pass, with no material task-5 regression.
These results do not establish exhaustive lifetime or native mixed-load coverage.

Remaining limits and revisit points:

- [Shared serialization and retained cleanup, #1514](https://git.internal/PyxisOS/pyxis-os/issues/1514):
  one general structural owner, bounded pre-effect BUSY, cleanup waits and
  reboot-lifetime uncertainty/permanent-window retention.
- [Native mixed-load and masked UDP capture, #1600](https://git.internal/PyxisOS/pyxis-os/issues/1600):
  unrelated combined audio/network/display stress and the separate capture are unrun.
- [Extended timeout and retirement races, #1601](https://git.internal/PyxisOS/pyxis-os/issues/1601):
  every ACK/join schedule, presenter/exit overlap and partial-allocation unwind
  is not covered by the successful ordinary runs.

Fixed worker placement, PMM search, scratch false sharing, physical coverage and
never-reused heap capacity retain their separate [SMP limits](smp.md#remaining-serial-services-and-limits).
