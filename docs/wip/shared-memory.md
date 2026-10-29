# Native shared memory

Assigned to Codex beta; decisions 1–3 accepted and implementation authorized
(chronium, 2026-10-29). Baseline precedes kernel changes. Task boundaries below
are delivery/review boundaries, not permission to change unrelated subsystems.
New material behavior, authority, lifetime or policy questions return to chronium
when reached.

## Outcome and boundary

Two independent processes can map the same normal-RAM region, one read-only and
one writable, pass its capability through existing launch/IPC/environment paths,
and revoke a branch's mappings. Creator exit alone does not destroy legitimate
recipients' access. Application buffers are the first consumer; compositor pixel
buffers and zero-copy protocols come later. No private application was inspected.

This is a native capability facility, not POSIX shm_open/mmap. It adds no
compositor protocol, GPU/scanout/DMA sharing, execute permission, shared mutex or
notification service. The [VM ownership contract](../kernel/vm-ownership.md)
remains the basis for mutation, drainage and retirement.

## Starting point

Main `20c99b3fa` includes completed BSP removal. MEMORY manages private allocations
only; caller-owned frames already have VM map/unmap primitives, but there is no
shared object or alias registry. The process VM gate admits the current user
caller, not an external actor withdrawing another live process's aliases.
Forwarder CUT revokes operations without persistent mapping subscriptions or
unmap joins. General process/space RAM quotas do not exist; JIT quotas and
logical FILE-retention accounts are separate policies.

## Accepted decisions (chronium, 2026-10-29)

### 1. Native fixed region and declared creation authority

Add independently delegable CREATE_SHARED on native MEMORY, separate from
MANAGE. Existing MANAGE grants do not widen. Trusted startup and bundle
declarations delegate creation explicitly; authority never comes from a
call-site wrapper. CREATE publishes one native region with READ/WRITE rights.
Receiving and mapping a region requires no creation right.

Backing is eager, fully zeroed, page-rounded WB RAM of fixed size; physical
contiguity is not promised. Return the rounded extent. Failure publishes no
partial region. No resize, sparse backing, fixed VA, partial view, execute,
sealing or device import in this slice. Separate JIT authority does not permit
shared aliases to become executable.

READ allows R/NX mapping; READ+WRITE allows RW/NX. Ordinary attenuation derives a
read-only grant. This is not a snapshot: other legitimate writers can change
its bytes. DESCRIBE reports type/size/rights, not physical addresses or a global
object identity. Names are ordinary capability-valued environment entries.

MAP chooses a disjoint caller address and returns a non-reused process-local
identity with address/size. Its owned source grant/lineage and backing reference
persist until unmap, revocation or process retirement. Closing a region handle
alone does not unmap it. MEMORY MANAGE RELEASE_RESERVATION accepts shared-view
identities to remove aliases/drop references without freeing object-owned pages;
this permits unmapping after the region handle closes. Private RELEASE/DECOMMIT
cannot alter shared backing. The caller still needs declared MEMORY MANAGE to
explicitly release its view.

COPY/IPC/launch preserve attenuation and lineage. Raw handles, addresses and
mapping identities convey no cross-process authority. Regions inherit creation
grant lineage; mappings retain their selected source lineage. Plain independent
grants stay irrevocable. RETARGET does not redirect an existing mapping.

### 2. Strong explicit CUT and owned automatic withdrawal

Successful explicit CUT means every mapping derived through that branch is
inaccessible. Independent branches and already-copied bytes are unaffected.
MAP atomically enrolls a persistent owned registration before exposure, so
CUT either refuses the mapping or includes it in withdrawal. Registrations hold
logical holder authority/storage, never controller authority. Last-controller
close still auto-CUTs revocable branches; ordinary creator exit does not revoke
plain recipients.

Add externally requested retained process-VM transactions on existing fixed RAM
actors. Close each target's root admission, drain acknowledged roots/borrowers
and finish any preceding mutation before private PTE changes. Own one process
mutation at a time; never wait for another while holding one. Pending work does
not park the sole RAM actor behind a process. No short guard spans a wait and
no CUT callback mutates remote VM under the forwarder guard.

Reusing the any-CPU invalidation engine does not replace private-root drainage.
Retain PCID/PGE-off reload and generation proofs. Access/refill closure and
actual retirement precede backing/VA reuse; normal exit removes aliases before
ordinary space destruction.

Reserve records before mapping exposure. Explicit CUT waits for withdrawal with
one absolute 1 s observation budget, not one budget per target. After logical
cut, an unconfirmed result is OUTCOME_UNKNOWN where the revoker can safely
return, never unchanged BUSY or success. Repeated CUT joins the same retirement,
not a no-op success while views remain. Non-mapping CUT keeps its existing path.

If the revoker is itself affected it leaves its private root before waiting;
no reply exception can reopen a root with revoked aliases still usable. The
observation budget is not guaranteed syscall return. **Chronium's change:**
irrecoverably uncertain withdrawal terminates the affected process, including
the self-targeting revoker, instead of holding it until reboot. Only uncertain
backing/frames and quarantine bookkeeping stay retained until reboot; no late
ACK permits reuse. Termination still respects actual root/task departure and
never frees executing storage. Unrelated processes/cleanup continue.

Automatic CUT on controller close/exit queues the same pre-reserved owned work;
destructors cannot sleep. CLOSE is not an unmap fence: old executing roots can
have a drainage interval. Existing cleanup attribution joins outstanding work
where applicable. Access after withdrawal is an ordinary protection/unmapped
fault. No implicit retry or new pointer-recovery API. Do not add per-message
checks/fences to unrelated IPC operations.

### 3. Origin accounting and creator-independent lifetime

Charge page-rounded physical region backing once to a retained originating
process/space accounting record, surviving creator exit without retaining the
whole process/VM. Copy/export/mapping neither refunds nor transfers it. Release
the charge only after the last handle/mapping and confirmed reclamation;
quarantined frames stay charged. Unpublished failure unwinds new charges.

Track each process's virtual view bytes and mapping/page-table metadata
separately: aliases do not duplicate physical backing but do consume those
resources. Do not reuse JIT quota or FILE logical-byte retention accounts.
No new general hard process/space quota is added. Physical, virtual and metadata
exhaustion remain honest failures; accounting does not reserve core RAM or
isolate untrusted holders. A future hard budget needs a separate decision.

## Native interface shape

| Operation | Captured input | Result / authority |
| --- | --- | --- |
| MEMORY CREATE_SHARED | Nonzero size, rounded to pages; unused words zero | One owned region handle and rounded size; CREATE_SHARED required |
| Shared region MAP | R or RW request, unused words zero | `memory_reservation` identity/address/size in the caller; READ or READ+WRITE required |
| MEMORY RELEASE_RESERVATION | Exact caller-owned view identity | Status only; MANAGE required, aliases removed and region reference dropped |
| Generic COPY / DESCRIBE | Existing capability ABI | Attenuated native rights or provider-free size/type metadata |

Numeric protocol/operation identifiers and fixed-layout structs are assigned with
their implementation; no unsupported operation or placeholder is exposed just
to reserve a future ABI number. MAP reserves its persistent registration and
cleanup ownership before any user mapping effect.

## Coherency and synchronization

Normal WB CPU mappings are coherent. Initialization is visible at MAP completion;
subsequent stores imply no transaction, ordering protocol or notification.
Clients need explicit ownership and release/acquire coordination. Lock-free
aligned native atomics are building blocks, not a promise that libc/C++ locks
are process-shared. Shared views initially cannot enroll in private address
parking or guarded thread stack/TLS pins. Use separately held endpoints/pipes
for notification. GPU/cache/DMA and compositor frame-reuse protocols stay later.

## Delivery tasks

Each task updates its checkbox and records validation, limits and exact heads.
Focused dependencies publish before a parent pin; do not expose revocable MAP
without its connected withdrawal implementation.

- [x] **1. Baseline and concrete interface closure.** The
  [before-code 1/4-CPU controls](../development/experiments/shared-memory.md)
  pass at clean main `20c99b3fa`, refreshed at `8d4f8b3d1` after process-listing
  changes with the exact same frozen apps/archives. Both completion boundaries
  are accepted. Source/compiler/configuration and observed ranges are recorded;
  routine native layout follows the contract below without compatibility readers.
  After this task chronium can review the before-code costs and the exact
  public authority/mapping contract.
- [x] **2. Region ownership and backing.** Fixed zeroed native region storage,
  READ/WRITE attenuation and DESCRIBE, retained origin accounting and bounded
  last-reference reclamation are implemented. Ordinary build and source review
  pass; [native controls](../development/experiments/shared-memory.md#task-2-native-controls--2026-10-29)
  are at parity on vela (one fresh A1/B1 pair). Nested IPC changes did not
  reproduce natively and remain unattributed. No public CREATE/MAP or creation
  delegation is exposed. Constructor/view lifetime runtime qualification follows
  with the connected mapping/withdrawal and consumer tasks; mapping cleanup
  storage must be reserved before those tasks expose views.
  After this task chronium can inspect real backing/ownership and allocation
  failure handling without an unrevocable public mapping escape.
- [ ] **3. Mapping and external withdrawal.** In progress on
  `kernel/shared-memory-withdrawal`, stacked above #1616; neither branch is
  requested for merge before the 0.0.10 tag. Internal external-VM admission,
  persistent lineage enrollment, view cleanup and CUT joins are implemented
  for integration. Public CREATE/MAP dispatch, reservation-release wiring and
  new-region runtime qualification are still pending; no public grant widened.
  Process-owned view identities and
  RELEASE_RESERVATION/exit removal, persistent lineage enrollment, external VM
  admission, explicit CUT joins, auto-close withdrawal and uncertain-process
  termination with backing quarantine. Implement the safe target/root/cleanup
  handoff before exposing views. No unrelated scheduler or IPC rewrite.
  After this task chronium can exchange a region and withdraw a branch across
  processes, with real unmap completion rather than future-call denial.
- [ ] **4. SDK/libc and declared grants.** Native libpyxis and libc Pyxis-extension
  wrappers; startup/bundle creation-right declarations and one small real shared
  buffer consumer. No POSIX emulation or malloc backing change. Deliver userland
  PRs before their parent pins, with linked ABI dependencies and merge order.
  After this task chronium can use shared buffers through normal native program
  grants, IPC and launch inheritance.
- [ ] **5. Qualification and close-out.** Ordinary builds; 1/4-CPU QEMU shared
  contents/rights/lifetime/revocation checks, matched controls and shared-buffer
  costs, then a quick hands-free native vela packet through the orchestrator.
  Record exact-head CI, measured variation and any accepted limits as debt.
  Rewrite this WIP into the implemented interface when the milestone completes.
  After this task chronium can rely on the qualified first shared-memory profile
  and see its performance and remaining limits explicitly.

Qualification includes zeroed padding, RO-write faults in disposable children,
independent addresses, attenuation/IPC/inheritance, creator exit, handle close,
alias counts, exact-once charge release, MAP/CUT and pending-operation races,
branch-local revocation, self-CUT, automatic controller close/exit, target exit,
missing authority and no stale-frame reuse. Verify inaccessibility independently
of status. Existing launch/pages/IPC controls must not materially regress without
attribution and chronium's decision; #1588 is not a waiver for new regressions.
No new benchmark framework or implicit fault-injection/native interruption scope.

## You should know

Shared views can outlive creator/handle close. Read-only does not mean immutable.
Explicit CUT is the unmap fence; automatic CLOSE is not. Irrecoverable withdrawal
uncertainty terminates affected processes, including a self-targeting revoker,
while uncertain backing/frames remain quarantined until reboot. There is no new
general hard RAM quota or implicit notification, shared mutex or GPU authority.

## Accepted completion boundaries (chronium, 2026-10-29)

These refine the termination change without weakening strong CUT or quarantine.

1. **Departure proof still governs reclamation.** Request termination immediately
   and preserve the existing first terminal-result election. Keep still-live
   task/root/stack/VM records and WAIT pending until actual permanent-stack and
   kernel-root departure is proved. If it never becomes provable, WAIT remains
   pending until reboot; no NMI, CPU-offline or machine-reset policy is added.
   Once departure is proved, detach uncertain shared backing independently.
2. **Safely detached quarantine does not hold process/group completion.** After
   ordinary departure and safe detachment, release process/group cleanup joins.
   Only the independent quarantine record, uncertain frames and their origin
   charge remain until reboot. WAIT may report termination while those frames
   remain charged; it must not report RAM freed. CUT reports uncertainty rather
   than successful reclamation. Normally pending cleanup still joins actual
   reclamation; this exception is specific to safely detached quarantine.

Source boundary: [process stop and cleanup pins](../../kernel/process.c),
[permanent-stack departure](../../kernel/task.c) and
[group cleanup accounting](../../kernel/object/execution_group.c).
