# Bounded retirement-debt carryover

Status: **policy accepted; implementation and bounded validation delivered**, 2026-10-02, after
[Pyxis #311](https://git.internal/PyxisOS/pyxis-os/pulls/311) merged. The owner accepted
the two-cohort cross-volume profile including orphan batches, derived funding,
healthy PENDING, complete final-release deletion, pool-wide checkpoint fencing,
bounded idle debt, I/O-free disposal and startup fencing. The pinned implementation
is merged in [filesystem #20](https://git.internal/PyxisOS/pyxis-fs/pulls/20) /
[Pyxis #312](https://git.internal/PyxisOS/pyxis-os/pulls/312);
its contracts follow the proof and recovery guarantees below. Task 7 / writable deployment
remain open; incremental allocation-map replacement is not authorized.
Confirmed merged: [filesystem #19](https://git.internal/PyxisOS/pyxis-fs/pulls/19)
and [Pyxis #310](https://git.internal/PyxisOS/pyxis-os/pulls/310). Baseline parent
`d5e92be` pins filesystem `fb8eec1`; filesystem merge `73a4885` has that same tree.
Source references below use the pinned revision. Read-only inspection and arithmetic
use the existing [matched RAM measurements](../../fs/docs/small-orphan-measurements.md)
and [bounded counters](../../fs/docs/measurements/small-orphan-cleanup.json).
No additional workloads were run for this investigation.

## Requirements preserved

Completed mutations remain individually durable after their replacement flush,
older-slot write and second flush. This policy defers **reclamation**, never
acknowledgment durability. Both retained states, live operation references and
outstanding I/O still protect physical storage. Every allocation uses the input
durable pair's already-reusable space; a candidate cannot allocate its own frees.

Confirmed operation progress, maintenance outcome and sticky pool health remain
separate. An admitted cleanup/fence is resource-complete without another application
mutation. Unexpected space, record, memory or generation exhaustion during that
work is an invariant failure, not successful safe refusal. The current conservative
read-error and adapter/operator recovery preconditions remain unchanged.

## Established cost and mechanism

The recorded compiler-style cases after combined small-orphan cleanup are:

| Population | Total submitted KiB | Drain metadata KiB | Drain / total |
| ---: | ---: | ---: | ---: |
| 32 | 6,528 | 3,520 | 53.92% |
| 256 | 12,756 | 7,540 | 59.11% |

Totals include final synchronization and all maintenance. They are observed RAM
write requests, not NAND amplification or NVMe performance. They do not establish
that this entire share can be removed.

[`build_publication`](https://git.internal/PyxisOS/pyxis-fs/src/commit/fb8eec1a609db8f4fbcc8f18581c6071c1d15b3d/core/writer.c#L387)
in the pinned immediate-drain baseline already frees eligible old retirements while constructing any publication.
However, admission allows only D volume retirement blocks owned by one volume;
preparation refuses pending debt; every committed batch immediately drains it.
The publisher also rejects freeing another volume's debt while changing a volume.
Thus existing user publications cannot absorb the next two reclamation stages.

For a continuous sequence of n retiring batches, the immediate policy can add
two standalone publications per batch. A rolling sequence can instead use later
individually durable batches to advance protection and publish frees, followed by
at most two tail publications. Explicit fences interrupt that amortization;
smaller reserve alternatives could require pressure fences too. This is a
calculated opportunity, not a measured savings estimate.
The remaining publications can become larger: additional retired-map boundaries,
free deltas and a second catalog path cost writes. A final checkpoint still pays
the tail; moving it outside the measured interval would be misleading.

## Accepted debt model

Use the existing allocation records as the durable authority: physical range,
owner, allocation birth, retirement generation and workspace charge. No persistent
queue, recovery log, timer or new on-disk field is needed. In-memory debt summaries
are derived from validated maps; they are not a second mutable authority.

Distinguish these states:

| Term | Meaning |
| --- | --- |
| Live | Required by the selected state; charged to permanent storage. |
| Retired, protected | Replaced in selected state, but the other retained state or an operation/I/O reference still protects it. |
| Retired, eligible | Neither retained map calls it live, neither has a live claim, and operation/I/O references have ended. Still charged until a free publication confirms. |
| Durably free | The confirmed selected map records free. |
| Reusable | Durably free **and** the other map is free or historically retired, neither state has a live claim and no runtime pin remains. Allocation rechecks this evidence. |

Retirement age bounds debt but does not prove eligibility. Preserve the complete
map/claim/owner/birth checks; do not reclaim by comparing generations alone.

Let V=128 and D=256 retain their current per-batch replacement and retirement
envelopes. Keep each mutation/orphan batch within one volume. This is the existing
operation boundary, not a restriction that all outstanding debt has one owner.

After a normal publication at selected generation g:

- Volume retirement belongs to at most two cohorts, introduced at g and g−1.
  Each contains at most D blocks, with one owning volume and one charge class.
  Total selected volume debt is at most 2D; its protected portion is at most D.
- A cohort may be empty. The two cohorts may belong to different volumes; a
  third volume can receive the next mutation. There is no per-volume multiplier
  or lifetime limit of two writable volumes.
- Each next publication frees **all eligible** old debt. With ended operation
  pins, the g−1 cohort is eligible against the input pair; the g cohort remains
  protected by the older root. The next batch adds at most D at g+1. The selected
  result therefore again contains at most two cohorts.
- Pool metadata debt remains at most 2H, with at most H protected, because every
  publication frees eligible pool debt and introduces at most H replacements.

The two-cohort allowance is derived from the retained-pair publication protocol
and D, not a chosen backlog watermark. The induction starts with a startup fence
that clears selected volume debt. Synchronous physical read/I/O borrows end before
mutation and reuse. Runtime view/operation holds can span multiple batches; they
retain object/orphan identity and lifetime through selected live records/claims,
not an old physical generation. A future asynchronous reader, larger
batch or public snapshot model requires a new debt proof.

### Cross-volume catalog closure

A normal publication changes at most two volume records: the batch's volume and
the owner of the old cohort it frees. Carried protected debt changes no counter.
A pure fence may free debt from both owners. Keep ordinary cross-volume rename
unsupported; reclamation bookkeeping does not add namespace authority.

Let L=8 be the existing catalog path envelope and Pcat=4N−2 its total-node bound.
Use the conservative union ceiling C=min(Pcat,2L). Shared nodes are counted once;
the exact two-path union can be smaller. C is a resource ceiling, not padded writes.

An implementable bounded mechanism uses the existing editor's private overlay:

1. Read the source paths for the changed IDs and deduplicate their published
   references before map planning. Determine the actual union size c≤C.
2. Retire each union node once; reserve exactly c new catalog IDs through the
   allocation-map planner. Recompute both volumes' accounting from the final map.
3. Perform fixed-key, fixed-length updates on **one** private catalog candidate,
   retaining its root, active slots and retirement list across both edits.
   Candidate-born nodes are read from staged slots; consumed private slots can
   be reused without publishing or retiring the intermediate private version.
4. Verify the final active-ID set equals the c reserved replacement IDs, and the
   published-retirement references equal the deduplicated source union, including
   birth/type identity. Encode/write/claim only the final active nodes. These updates do not
   split, collapse or change catalog occupancy.

This relies on existing [`edit.c` overlay behavior](https://git.internal/PyxisOS/pyxis-fs/src/commit/fb8eec1a609db8f4fbcc8f18581c6071c1d15b3d/core/edit.c#L38).
The second edit can reuse its consumed shared-path slots; its unpublished
intermediate root never becomes a retained state. Merely enlarging the current
single-path arrays, or independently building two catalog roots, is insufficient.
No general multi-volume mutation/editor framework is proposed.

## Accepted admission and funding

Re-derive the whole-map closure; do not keep today's H/S while merely removing
drains. Canonical coalescing, E/M, object limits, namespace occupancy/deletion
promises and the core memory ceiling remain unchanged in this candidate.

```
volume_debt_max = 2D
K = 1 + 2(E + M + 2D)
Rbase(H) = K + 2Pcat + 6H
S(H) = ceil(23(Rbase(H) + 2C + 4) / 21)
F(S(H)) + C + 1 <= H
Pmax = Pcat + H
```

Store the computed C with the plan limits and use it for the old-node, builder,
catalog-staging and publication checks; do not introduce independently copied
ceilings at those boundaries.

F is the existing shape calculation: ceil(S/46) leaves, then successive
ceil(previous/65) internal levels. Retain allocation-record/depth limits and
checked arithmetic. Scan H from C+2 through
ceil((K+2Pcat+23C+47)/15) for the first satisfying value, refusing profiles that
violate format/depth/memory/capacity bounds. The loose upper bound follows the
existing F(S)≤S/23+1 argument; C=8 reproduces the current +231 numerator.
Live/retired pool nodes still contribute at most 2Pcat+6H boundaries. The new
2D term bounds **all** volume retirement, not another allowance added per call.
Both opening and candidate admission require map_nodes≤H−C−1, replacing H−9,
and live_pool≤Pmax. Imported canonical maps need this explicit old-node cap;
they need not have the planner's compact shape. No unfunded normalization occurs.

| Resource | Accepted sufficient envelope |
| --- | --- |
| Volume replacements / new claims | V per mutation or orphan batch; none for a pure fence |
| Volume retirement | D per batch; 2D selected aggregate |
| Pool replacements / new claims | H per publication, including map, catalog union and pool root |
| Pool retirement | H introduced per publication; 2H selected aggregate |
| Map records | S(H) for every candidate, including startup/tail fences |
| Live claims | E+M+Pmax; retired ranges add map records, not live claims |
| Interval deltas | At most 3H+3D+V conservatively: 2H pool frees, 2D volume frees, H old pool retirements, D batch retirements, V allocations |
| Pure-fence interval deltas | At most 3H+2D |
| Delta storage | Existing 8(H+V+D) slots of 128 bytes suffices for those bounds |

The normal rolling case frees at most D volume blocks, but the conservative delta
bound also covers startup. Delta storage is reused after interval application for
H map descriptors/IDs/ranges (56H bytes under current layouts). It does not need
a second owner-dependent arena. Remove old live claims before adding replacements.

Accepted persisted-capacity requirements:

```
ordinary >= max(existing formatter floor 1024, 2D + V) = 1024
recovery >= max(existing formatter floor 256, 3H + 2D + V)
migration >= existing floor 1024; remains reserved and unused
Pmax + sum(max(Aeff_i, G_i)) + sum(recorded workspace capacities) <= U
Aeff_i <= Q_i
```

Ordinary debt retains ordinary charges; orphan debt retains recovery charges;
pool retirements remain recovery-charged. Mixed cohorts cannot exceed total 2D,
but enforce each occupied-charge limit independently. No reclassification or
borrowing migration/volume guarantees funds the accepted profile.

At most 2H+2D recovery occupation leaves H+V in the recovery envelope. Ordinary
occupation is at most 2D. The permanent-promise inequality and supported-pair
reconciliation reserve the remaining physical capacity, rather than treating a
guarantee as additional space. Selection must still establish H+V already-reusable
blocks before a volume batch and H before each pure fence. Complete cross-state
checking excludes older live allocations from selected free space; ended borrows
make that free space reusable. Same-candidate frees contribute zero to selection.
Consequently this conservative profile needs **no space-pressure pre-drain** in a
healthy admitted state. Failure to select the proved storage is an admission/editor
invariant failure and stops mutation with READABLE_STOPPED, escalating if integrity
or publication certainty is lost. This applies during preparation/planning before
any device write too; a defensive drain or retry must not conceal it. Ordinary
quota/profile/capacity admission refusal concerns the proposed new result, leaves
the writer healthy and is distinct from failing to obtain already-guaranteed workspace.

With other occupied charges bounded by their own capacities and live permanent
storage bounded by the reserved permanent promises, the free-space inequality is:

```
already_reusable >= recovery.capacity - recovery.occupied
                 >= (3H + 2D + V) - (2H + 2D) = H + V
```

The recovery minimum **increases** from 3H+D+V. That increase is accepted to
carry orphan cleanup debt too; no other reserve policy change is authorized. Existing
image capacities are never edited automatically. Images that fail the new computed
requirements are refused before writes, with diagnostics; no unfunded normalization
or reserve migration is assumed. Formatter defaults/floors and runner budgets are
not changed. Logical admission still does not reserve physical host space for sparse
images. An alternative retaining immediate orphan drains has a smaller recovery
volume envelope but sacrifices much of the intended amortization.

The caller reserves the same arena layout with newly computed H/S/Pmax:

```
240S + 288(E+M+Pmax) + 48*min(1048576,29M)
+ 4096(H+V) + 128*8(H+V+D) + 8 MiB scratch
```

Keep the three map/claim vectors and existing scratch separation: admission in
the lower half, sealed publication in the next quarter, mutation/editor in the
last quarter. Catalog staging grows only to C slots/references and two keys;
the existing editor workspace is reused sequentially. Verify disjoint-region size
assertions; introduce no extra allocation after admission or volume-count-sized
block buffers. Peak opening still includes both checker states and temporary
growth alongside the arena. No core cap increase or kernel-stack refactor follows.

Calculated examples, E=8192/M=4096 (the recorded comparison profile), with N varied
only to illustrate the bound; bytes below are arena storage, not measured peaks:

| N | H immediate → carryover | S immediate → carryover | Recovery blocks immediate → carryover | Arena bytes immediate → carryover |
| ---: | ---: | ---: | ---: | ---: |
| 1 (recorded comparison) | 723 → 729 | 32,256 → 32,843 | 2,553 → 2,827 | 30,198,688 → 30,372,016 |
| 2 | 723 → 736 | 32,265 → 32,907 | 2,553 → 2,848 | 30,202,000 → 30,426,384 |
| 16 | 726 → 751 | 32,407 → 33,150 | 2,562 → 2,893 | 30,268,432 → 30,581,952 |

These inputs are examples, not accepted product defaults or test expectations.
Actual catalog/map shapes determine writes; the ceilings reserve capacity.

### Generation funding

Retain conservative generation admission g+a+3T≤u64_max, where T is remaining
orphan work and a is zero for no volume debt, one for fully eligible debt and two
if any volume debt is protected. Explicit draining introduces no new volume debt
and finishes within a. Pool metadata alone remains the funded bounded remainder.

Each orphan batch reduces T by at least one. After its publication, the candidate
needs at most two terminal generations, so 1+2≤3 pays for that step. A requested
fence first consumes its already-funded a while T is unchanged, then cleanup
continues under the remaining budget. This bounds every intermediate interruption,
not just the healthy end state; combined small cleanup reducing T by two is safer.

For a new ordinary candidate, reserve its one publication, two terminal generations
and 3T_projected from the current confirmed generation. An ordinary request refused
before admission leaves the writer usable. Exact increments of one remain; no wrap or
assumption that a later application write will fund completion.

## Accepted drain triggers and caller results

| Trigger | Accepted behavior / reason |
| --- | --- |
| Next mutation, including another chunk or volume | Carry bounded debt; include eligible frees in the mutation publication. No mandatory standalone drain between healthy batches. |
| Resource pressure before staging | Refuse an ordinary request that cannot preserve quota/profile/record/generation promises, leaving the writer usable. The accepted workspace envelope guarantees H+V reusable input blocks; an unexpected selection failure is an invariant failure, not a reason to retry through a drain. |
| Funded orphan/fence resource failure | Stop under the invariant-failure rule. Do not count refusal as success or advance roots endlessly trying to reach zero pool retirement. |
| Held file/dir checkpoint | Establish the existing durability boundary and additionally settle selected **pool-wide volume debt**, with at most two pure publications. Success leaves only bounded pool-metadata retirement. |
| Final orphan handle release / unheld-victim cleanup | Finish the admitted orphan deletion, including data, grants and paired records, without requiring another application mutation. Permit resulting retirement debt to carry; named-object close creates no cleanup obligation. |
| Pool/volume close | Preserve I/O-free disposal. Bounded durable debt may remain; a caller wanting settled reclamation checkpoints before releasing its checkpoint-capable view. No new implicit-close recovery route. |
| Writable startup | Validate and fund the retained pair; fence existing volume debt, recover abandoned orphans, then perform a final fence before exposing the writer. |
| Idle healthy writer | Bounded debt may remain indefinitely. No background worker, timer or automatic access-triggered drain. An authorized checkpoint can settle it without another application mutation. |

Pool-wide checkpoint reclamation requires no new grants: it changes allocator
bookkeeping, not objects or retained-orphan lifetimes. The existing held
file.checkpoint/dir.checkpoint right is still required, with no read/lookup widening.
Checkpoint performs no orphan cleanup, provides no cross-volume
object authority or promise zero retired pool metadata. I/O-free disposal is
already the close contract; adding fallible close I/O would require a wider result
and lifetime decision and is not recommended here.

Use the accepted `PFS_MAINTENANCE_PENDING` result for a successful healthy
mutation/cleanup returning with funded volume debt, maintenance_status=OK and
health=READY. COMPLETE means a requested retirement fence settled volume debt.
NONE means this call reports no maintenance outcome; it does **not** certify debt
absence. Rejected calls, named-object closes and stopped release-only closes may
report NONE while debt remains. `drain_pending` independently reports debt presence;
READY may coexist with it. It no longer means the next mutation is prohibited.
Do not overload COMPLETE to conceal pending work or PENDING to conceal failure.
An accepted healthy mutation with no remaining volume debt reports NONE unless
an explicit fence completed. No-op calls need not report a global debt snapshot.

A healthy refusal after earlier batches confirmed partial progress may report
PENDING or NONE depending on its stage: planning can retain the earlier PENDING,
while commit-time admission can report NONE. Neither changes confirmed progress.
NONE does not establish debt absence; `drain_pending` reports last-confirmed debt
separately. PENDING describes outstanding work, not maintenance already completed.

An ordinary successful mutation still reports COMPLETE and confirmed bytes/length/
namespace regardless of healthy PENDING maintenance. Final orphan release still
consumes the accepted handle; PENDING means deletion finished durably but its
retirements are not yet free, not that the object will be deleted on a later write.
Stopped instances cannot perform any final-release/checkpoint writes.

### Failures and reopening

| Failure | Result / health |
| --- | --- |
| Ordinary quota/profile/capacity refusal before admission | The refused batch adds no progress; retain earlier confirmed progress and leave the writer healthy. Maintenance may be PENDING or NONE depending on the refusal stage. |
| Backing read error anywhere | ACCESS_STOPPED, including catalog union planning and checkpoint/startup reads. |
| Replacement write or pre-slot flush error | READABLE_STOPPED when confirmed integrity remains established. |
| Slot write or following flush error | UNKNOWN for that publication, ACCESS_STOPPED; do not retry the uncertain mutation. |
| Integrity failure | ACCESS_STOPPED. |
| Failure to obtain already-guaranteed publication workspace, or unexpected funded resource exhaustion | Invariant failure; READABLE_STOPPED, escalating if integrity is unestablished or publication uncertain. No retry to health. |

If a user mutation confirms and subsequent required orphan maintenance
fails, keep its confirmed progress and report the independent STOPPED/UNKNOWN
maintenance outcome. A larger call stops before its next batch and retains its
confirmed prefix. If previously funded cleanup stops before another batch, that
batch confirms no additional bytes/namespace. Preserve earlier call-prefix
progress. The most recent failure/uncertainty takes precedence over earlier PENDING
or COMPLETE maintenance information.

If the next **user** publication encounters I/O, integrity or funded invariant failure,
report its operation error/uncertainty and
sticky health; retain prior confirmed progress but do not invent a maintenance
failure. When no maintenance failed, that stopped result reports maintenance NONE,
not healthy PENDING. Diagnostic debt remains visible independently. Actual failed
or required-but-blocked cleanup reports STOPPED/UNKNOWN in maintenance fields;
stopped release-only close performs no such attempt and may report NONE.

For checkpoint itself, report COMPLETE only when its fence is confirmed;
STOPPED/UNKNOWN plus the corresponding operation and maintenance error when the
fence fails. It has zero byte/namespace progress. A no-publication checkpoint
still completes the fence. Preserve phase provenance: the immediate-drain baseline's
[`access.c` checkpoint wrapper](https://git.internal/PyxisOS/pyxis-fs/src/commit/fb8eec1a609db8f4fbcc8f18581c6071c1d15b3d/core/access.c#L1037)
passed status through a generic read-error stop helper. The implementation lets
the fence classify its errors; replacement/pre-slot maintenance failure must not
be converted to ACCESS_STOPPED by that wrapper.

Reopening recomputes debt from the explicitly durable retained image; there is
no in-memory credit to reconstruct or assume. Sticky failure ends only with a fresh
validated instance under the existing adapter/operator precondition. Unknown
publication may have added committed work; recovery never automatically retries
the caller's uncertain operation. Cached validation and later successful fsync
remain insufficient after backing writeback failure. Real-host interrupted-session
orphan recovery remains unsupported where healthy backing history is unestablished.
Pool status after uncertainty describes last-confirmed debt only; it cannot
certify the actual post-error durable debt until fresh validation.

Startup must distinguish checked media from runtime pipeline shape. Accepted
opening profile: each retained state has at most 2D volume debt and at most two
volume owners, with the existing pool/protection and computed capacity checks.
Volume debt must be ordinary/recovery-charged; migration stays unused and pool
retirement stays recovery-charged. Startup may have up to **2D protected** volume
blocks; do not apply the normal pipeline's D protection cap to that initial pair.
Debt age alone need not reject a checked pair. A pure first fence frees eligible
debt and carries the protected subset; after promotion the prior selected state
has retirement records, not live claims, for that subset. The next fence can free
it, using at most two catalog paths. Keep that first candidate under opening/fence
validation until debt is empty. Thus startup establishes empty selected
volume debt before orphan batching; the rolling induction then establishes its
stronger per-generation shape. Validate that shape for normal candidates, rather
than inferring it from a 2D total check. Do not apply a runtime-only age assumption
to an intermediate startup fence. Reader-valid images outside the funded writable
profile are refused, not called corrupt or repaired automatically.

## Worked traces

Letters name incarnations/ranges, not physical placement. Each row describes a
confirmed two-flush publication. R means retired, F durably free, P protected;
eligibility is always checked from both input maps/claims, never just the labels.
Pool metadata introduces/frees its own analogous H-bounded waves throughout.

### Consecutive overwrites

| Selected / older | Selected file | Old payload accounting | Reusable before the next publication |
| --- | --- | --- | --- |
| g / g−1 | A0 live | None | Unrelated proven free ranges |
| g+1 / g: overwrite A0→A1 | A1 live | A0 R,P by g | A0 unavailable |
| g+2 / g+1: overwrite A1→A2 | A2 live | A0 R,eligible; A1 R,P | A0 still unavailable: no free publication yet |
| g+3 / g+2: overwrite A2→A3 | A3 live | A0 F; A1 R,eligible; A2 R,P | A0 reusable after confirmation; g+3 allocated elsewhere |
| g+4 / g+3: checkpoint advance/free | A3 live | A1 F; A2 R,eligible | A1 reusable; A2 not yet free |
| g+5 / g+4: checkpoint free | A3 live | A2 F | A2 reusable; volume debt empty |

A4 in a later transaction could use a newly reusable range, after fresh checks.
The older state's historical retirement entry does not pin payload by itself.
Checkpoint publications preserve A3 in both states; the two differing A2/A3 states
are protected until the relevant older slot is replaced.

### Cross-volume mutations

| Publication | Volume debt after confirmation | Volume catalog records changed |
| --- | --- | --- |
| g+1 mutates A | A's cohort R,P | A |
| g+2 mutates B | A R,eligible; B R,P | B |
| g+3 mutates C | A cohort F; B R,eligible; C R,P | A and C, one union candidate |
| g+4 mutates A | B cohort F; C R,eligible; new A R,P | B and A |

A, B and C can all remain writable. C's g+3 replacements cannot use A's cohort
being freed by g+3. Quota/guarantee ownership and ordinary/recovery charges remain
per record; freeing A debt does not transfer object authority to C's caller.

### Orphan release and interruption

Unlink O with a held view at g+1 confirms namespace removal, but O's payload P
remains live and charged. Checkpoint may settle replaced namespace nodes, not P.
When the final view/operation reference ends, cleanup at h removes the one-block
mapping and object/marker atomically (or uses the existing bounded path for other
shapes). P becomes R,P in selected h because older h−1 still names O. Release
is confirmed and consumed; maintenance is PENDING. A following mutation at h+1
advances protection; h+2 can publish P free; h+3 may allocate it. Without another
mutation, a checkpoint can perform the same two debt-only stages.

If interrupted during the first maintenance publication's replacement writes,
durable slots still name the pre-maintenance pair: P remains protected. A reported
write error makes that instance READABLE_STOPPED, with confirmed unlink/release
unchanged. If interrupted at the maintenance slot/following flush, the durable
image may contain either pair; report UNKNOWN/ACCESS_STOPPED. Recovery validates
that image, determines whether P is still protected, eligible or free, and finishes
the funded fence. It never treats unconfirmed candidate frees as allocation input.
Already-written but unreachable replacement blocks do not authorize losing either
retained payload. Simulated recovery supplies an explicitly durable image; host
recovery still requires adapter/operator backing evidence.
At the second maintenance slot/flush interruption, P may already be durably free
or may remain retired in the selected image. Neither outcome permits allocation
from the failed instance. Fresh recovery distinguishes them from the durable maps;
the caller's confirmed unlink/release remains confirmed in both outcomes.

## Authorized coherent implementation boundary

One coherent filesystem PR is preferable to temporarily exposing unfunded carryover:

- Add the catalog-union planner and reuse the existing editor overlay; re-derive
  H/S, opening/candidate debt validation, occupation and generation checks together.
- Replace unconditional post-batch drain with the accepted pending policy.
  Propagate cleanup/debt maintenance results through file/namespace/multi-batch
  callers and keep resource admission distinct from funded invariant failure.
- Implement the agreed checkpoint fence and startup trailing fence; keep complete
  orphan deletion/final-release behavior and I/O-free disposal under the accepted policy.
- Add pending result/status formatting and update shared-core/host-tool contracts
  and maintained tests alongside the behavior. Publish dependency PR before the
  Pyxis pin. No allocator redesign, logging/replay, runner or budget changes.

Baseline assumptions replaced together: `plan.c`'s D record term, one-path +9/+20
closure and recovery minimum; `admit.c`'s H−9 map-node cap, one-owner/D checks and
remaining-generation logic; the map builder's eight-catalog-ID validation/output
array in `plan.c`/`plan.h`; `writer.c`'s single catalog index/path, uniform
volume-debt comment, pending prepare refusal and unconditional drain;
checkpoint's former no-op and its failure
wrapper; cleanup result aggregation/final returns and startup trailing behavior.
The milestone's maintenance, map-closure, reserve, generation, health/result and
validation sections and `fs/docs/core.md` reflect the same accepted change.
The 29/30 small-cleanup volume-edit proof remains; its former no-incoming-debt
assumption and D-based sequence/accounting envelope now use this proof. Format fields,
coalescing, namespace occupancy/deletion bounds and public authority stay unchanged.

Behavioral validation uses the real core and existing bounded failure adapter:
independent expected payloads for both retained states before each slot attempt,
including after replacement writes; healthy-trace fault cuts; same-volume and
cross-volume consecutive histories; shared/separate catalog leaves; pressure,
quota/profile refusal and generation boundaries; retained-handle removal; eligible
and excluded orphan shapes; startup from durable interrupted histories; checkpoint
idempotence and unchanged grants; I/O-free disposal with pending debt. Verify freed
space is durable/reusable and no write targets protected or same-publication-free
storage. Exercise funded ordinary/cleanup/fence sequences near computed minima
with formatter floors, requiring completion without new allocations. Test sticky
health/provenance and independently confirmed prefixes, not private-field assignment.
Do not require exact publication counts, generations, tree shapes or allocations;
the two-flush protocol and proved resource upper bounds remain deliberate assertions.

For the authorized implementation, run the existing quick/seed-1 extended
checks and required exact-head CI. Repeat two serial unchanged RAM baseline matrices
per revision with the existing commands/configured limits. Preserve history, oracle,
native operation/batch synchronization and final checkpoint; count total writes
through that fence and teardown, reporting preparation separately. Measure carried
debt and final state, total data/metadata and user/orphan/standalone-maintenance
traffic, extent/record counts and RAM timing variation. Compare byte totals with
immediate draining, not just a phase that received fewer writes.

The immediate-drain baseline's comparison observer used `drain_pending` to classify
callbacks (`tests/comparison.c:156`); that is incorrect when user publications carry
debt. Classification now follows the **active publication**, not debt presence, using
the existing sealed candidate/ordinary bookkeeping at the callback boundary.
Keep this a focused accounting correction in the existing tools, without new test
controls in production or new benchmark infrastructure. Workload inputs, independent
ledger and commands stay unchanged. A bounded retirement result is neither corruption
nor completion of a refused workload. No automatic resource-limit increase or disk
fallback; use the existing verified RAM/no-swap setup for any later execution.

## Accepted package and unselected alternatives

The recommendation is rolling two-cohort, cross-volume carryover for user **and**
orphan batches, with the derived larger recovery minimum; explicit healthy PENDING;
pool-wide checkpoint fences under existing grants; complete final orphan deletion
with possibly pending retirement; I/O-free writer disposal and startup fences.
The owner accepted these observable policies and assigned the coherent implementation
after merging #311. The alternatives below remain unselected.

Immediate draining remains simplest, minimizes outstanding volume debt and has
predictable close/checkpoint behavior, at the recorded write cost. Keeping one
debt owner and draining on volume switches reduces the catalog change but loses
cross-volume amortization. Keeping immediate orphan drains avoids the extra recovery
cohort but sacrifices compiler-style cleanup savings. A smaller/state-dependent
workspace profile could deliberately drain before resource pressure, but needs a
new threshold, prefix and reserve proof; it is not selected by this proposal.
The conservative profile's H+V guarantee makes such a trigger unnecessary.
A larger arbitrary queue or per-volume allowance adds record/workspace/generation
debt without evidence it is needed; neither is recommended.
No proposal removes necessary final/startup
fences, permits same-publication reuse or promises the whole 54–59% drain share.

Any further change to authority, lifetime, durability or supported resource policy
requires separate discussion. This assignment does not authorize allocator redesign.
Incremental allocation-map self-accounting requires separate design acceptance;
this correction reduces the number of whole-map publications, not their population-
sensitive cost. Writable deployment remains blocked.

## Implementation and matched validation

The merged integration #312 pins published filesystem
`5e44d6feee5a91ee167412bc957c93ab8542ed08` from merged #20.
The publisher, candidate/opening admission, plan limits, checkpoint/final-release
results, host reporting and active-publication counters change together. The
read-only kernel mode bridge dispatches fences through the existing private writer
boundary; the kernel still excludes the writable publisher.

All 125 quick groups and six seed-1 extended groups pass natively and under
ASan/UBSan using the existing strict bounded RAM/no-swap launcher. The complete
core archive cross-compiles with Pyxis GCC/kernel flags; the kernel's read-only
subset links with its existing memory primitives and no publisher dependency.
Tests independently verify retained payloads after replacement writes, durable
free transitions, cross-volume histories, nonadjacent opening, interrupted startup
and fences, checkpoint-only authority, complete final release and funded progress.
They preserve contract bounds and the two-flush protocol without requiring
incidental publication counts or placement. Near-minimum fixtures exercise small
reachable images, not every maximal profile/debt population.

Two serial unchanged 40-case RAM matrices before and after include terminal
checkpoints and all maintenance. Compiler totals fall 6,528→3,056 KiB at 32 files
(53.19%) and 12,756→5,236 KiB at 256 (58.95%). The
[measurement record](../../fs/docs/retirement-carryover-measurements.md) reports all
cases, preparation, phases, native durability differences, elapsed observations,
bounded backing and trace/teardown evidence. No swap/max/OOM event, trace loss,
fallback or resource increase occurred. ASan/UBSan runtimes were installed with
owner authorization after measurements; no runner configuration changed.

Pyxis still exceeds matched ext4 compiler totals, a longer-term comparison rather
than the revised initial deployment gate. All eight short cases beat recorded
Btrfs totals, but do not qualify the accepted Btrfs-comparable direction on
representative sustained/populated/pressure workloads. Whole-map representation
costs, larger pressure/profile qualification, real-host post-error recovery and native
writable integration remain unresolved. This corrective step does not close task 7
or writable deployment or assign another efficiency implementation. Exact-head
filesystem and parent CI are reported on their PRs.
The next [incremental-map design proposal](filesystem-write-efficiency.md#incremental-allocation-map-proposal)
records its separate mechanism and acceptance decisions; it does not authorize code.
