# Bounded allocation-map overflow split

Status: **design proposal for owner review**, 2026-10-02. The owner assigned this
investigation after [filesystem #24](https://git.internal/PyxisOS/pyxis-fs/pulls/24)
and [Pyxis #325](https://git.internal/PyxisOS/pyxis-os/pulls/325) merged. Baseline
Pyxis `fcfe1b7` pins published filesystem `4b1e81d`; filesystem merge `8ddcdb7`
contains that production code. This task authorizes documentation and arithmetic,
not implementation or new mutation workloads. The proposed choices below are
not accepted merely because they are specified precisely.

## Evidence and purpose

The [matched neighbour-repair record](../../fs/docs/neighbour-repair-measurements.md)
completes and independently verifies all 54 after cases, including all final
maintenance. Every observed repair selection is overflow. Allocation-map writes
remain about 70–84% of append/overwrite window metadata. The largest overwrite
regresses 1.934%; added map-node writes explain 99.0% of its net increase, including
a later window with no new repair selections. Largest append window redistribution
adds 47,340 source identities against closure sum 54,295; additions include
ancestors and discarded attempts, not just leaves.

The current editor holds leaf count fixed, so overflow can consume neighbouring
leaves and paths even when a single extra leaf could supply capacity. This
motivates a bounded structural trial; it does not establish its hit rate or
complete-history savings. Local runs are already packed evenly. Further neighbour
scoring, a new fill policy or a CPU-only scan correction is not selected here.

**Recommended scope for review:** permit one additional leaf in one dirty run,
beneath an existing parent with a spare child slot. Every node keeps its level;
only that parent gains a child. Share untouched subtrees. No internal split, new root, root-height
change, leaf removal or merge. The single surplus is a deliberate first-solver
restriction: it makes emitted-node accounting constructive without searching
guessed counts. It is not a disk-format limit, admission restriction, product
capacity or workload parameter. Other shapes retain the existing planner.

The format remains an exact maximally coalesced allocation partition. Leaf and
internal capacities derive from its byte layout, currently 46 records and 65
children. Nonempty leaves and at least two children per internal node remain the
format rules; no half-full profile is added. Ascending eligible-ID ordering,
placement, bulk fill, H/S computation, reserves, admission, memory ceilings,
caller budgets and runner configuration remain unchanged.

Preserved agreed requirements include individually durable completed calls,
both retained states and payloads, no same-publication reuse, existing authority
and handle lifetimes, confirmed progress independent of maintenance/health, and
resource-complete admitted cleanup. No new public right, grant widening, on-disk
feature or version is proposed.

## Proposed selection and fallback order

1. Validate source topology and retained inputs, discover the actual catalog union
   `c`, and reserve the same ascending list of H eligible input IDs before closure.
   Exclude staged volume blocks and all retained/live/I/O protection. The list
   is fixed; its consumed prefix can grow. Candidate-created frees cannot enter it.
2. Run ordinary one-for-one accounting and canonical seam closure until no new
   source marks are needed. At the first failing run in key order, offer a trial
   only if it is overflow and no neighbour expansion has occurred. A single-leaf
   source root, global closure, a first underflow or no eligible parent skips the
   trial. Require `J+1 <= m` before constructing split accounting.
3. In that run choose the lowest-key source leaf whose existing parent has room
   for one additional encoded child. Freeze this source leaf and parent identity;
   the parent may be an internal root. Snapshot the authoritative seed marks and
   closure state. Do not search different anchors on a miss.
4. Claim one structural surplus and regenerate the candidate from immutable
   source. Source retirement/seam marks may grow; do not add neighbour-repair marks
   inside the trial. Find the current dirty run containing the frozen anchor,
   including any runs joined by subsequent closure. Insert one virtual output
   leaf immediately after the anchor's replacement in that run's key order.
5. Once accounting and seams stabilize, let that run contain `l` original source
   leaves and `r` canonical records. Require `46*l < r <= 46*(l+1)`. Otherwise the
   added leaf is unnecessary or insufficient and the trial misses. Every other
   run must fit its original `l <= r <= 46*l` interval. Global closure also misses.
   Never pad unused live metadata or remove a marked source reference to fit.
6. Pack the anchor run evenly into its `l+1` ordered output positions using the
   current ceil-remaining/remaining-leaves policy; other runs retain their original
   positions and packing. Encode all replacements and the virtual leaf, construct
   exact claims and catalog updates, and admit the complete candidate before any
   replacement write. A predicted fit alone cannot authorize publication.
7. On a named topology miss or the existing candidate-local resource-admission
   miss class, restore the seed and regenerate ordinary accounting with split
   attempts disabled. Resume current occupancy-aware neighbour repair. Its
   existing global/exhausted/resource triggers lead to the funded bulk path.
   Bulk still starts from original logical inputs, with no candidate-born claims.

This ordering offers one trial before capacity-driven neighbour expansion. It
preserves the baseline repair opportunity after a miss. It also adds planning
work, especially for unsuccessful trials; neither timing nor eventual savings
is established. A successful split is selected without computing a second full
alternative history. Comparing it with the calculated bulk reference remains
diagnostic, not an automatic cost gate.
That comparison alone does not include the different retirement histories or
later costs of an extra live leaf. A selection gate would be a separate policy
choice; the proposed trial keeps the current structurally defined local-path
selection and exposes those costs for matched measurement.

## Emission, reachability and seams

Let `P` contain marked immutable source nodes, `p=|P|`, `J` the source node count,
and `n` the number of newly emitted map nodes. In the trial:

```
n = p+1
new pool prefix = n+c+1
newly retired pool blocks = p+c+1
final live map nodes = (J-p)+n = J+1
```

Every marked source position has one reachable replacement; the virtual leaf is
the sole additional position. Unmarked source references and births remain shared.
The validated source topology and block-sorted retirement index remain immutable;
do not insert the virtual leaf into that index or retire it as source metadata.
Source replacements retain their validated descriptor ordering. Give the virtual
leaf output index `p`, using eligible ID `ids[p]`; rebuild that index as p grows.
Freeze its source anchor, not a speculative physical ID. Its logical child position
is immediately after the anchor even though its output index follows all source
replacements. This preserves ascending eligible-ID assignment without a new
placement heuristic.
Catalog IDs start at prefix offset `n`; the pool root uses offset `n+c`.
The allocation stream must describe exactly those emitted nodes, catalog nodes
and root, with no unused live prefix IDs or references to marked source nodes.
The current [publisher](../../fs/core/writer.c) and
[encoder](../../fs/core/incremental_map.c) use marked count for both retirement
and output offsets. The implementation must change those consumers together;
incrementing a fanout without changing claim/offset accounting is insufficient.

Complete-record comparison still marks both sides when a final canonical record
straddles an original seam. Stable outer run endpoints protect neighbouring shared
subtrees; interior boundaries may move. The anchor run may cross source parents:
all repacked leaves and their ancestors are already marked. Only the frozen
parent gains a child; other marked parents update references/minima at unchanged
fanout. Each new boundary lies between complete canonical records. Propagate exact
minima through the marked ancestor union once, without duplicate retirements.

Encode the virtual leaf before its parent. Reverse source traversal alone cannot
prove this ordering because it has no source descriptor. All `n` emitted nodes
must be uniquely reachable with valid levels, byte fit, containing births and
coverage. Superseded private nodes/buffers are scratch, never durable retirement.
An encoding/inventory discrepancy is an invariant or integrity error, not a miss.

## Termination and restoration

The surplus is fixed at one; `n` is not fixed, because `p` can grow. Every trial
growth evaluation adds a previously unmarked identity from the J-node source.
Renew complete accounting and seams after each growth. If added claims coalesce
records so the anchor run no longer needs a split, discard this trial rather than
shrinking/growing a guessed count or retaining needless live padding.

For seed size `s`, initial closure has at most `s` growth evaluations, the trial
at most `J-s`, and restored split-disabled repair at most `J-s`. Include one
terminal evaluation per phase: at most `2J-s+3` closure evaluations, plus one
bounded snapshot and restore. This derives a finite bound from source identities
and a single proposed attempt; it is not an arbitrary retry cap. The two post-seed
phases are separately monotone. Source marks may shrink only at that deliberate
scratch restoration, so the old whole-publication monotonicity statement needs
mode-specific wording.

Restoration must restore authoritative marks/count and seed planner state, discard
trial runs, replacement indices, stable/fallback flags and encoded output, and
rebuild the flat candidate, claims and volume accounting from immutable source
plus the same logical batch. Sealing can change candidate volume counters and
catalog buffers; restoring marks alone is insufficient. Generation, retained
inputs, staged volume edits, owner/catalog union and eligible-ID list do not change.
Keep writer diagnostics unchanged until a path is chosen; do not turn a trial miss
into a final bulk reason. Separate bounded trial observations survive restoration.

## Funding and scratch derivation

Use the existing envelope in the [write-efficiency plan](filesystem-write-efficiency.md#resource-and-sequence-bounds):
`m=H-Cmax-1`, `c<=Cmax`, `K=1+2(E+M+2D)`, `Rbase=K+2Pcat+6H<=S`.
H/S are computed, not new chosen limits. The extra live-node guard gives
`n=p+1<=J+1<=m`, so both emitted and retired pool work fit H.

| Resource | Trial bound and obligation |
| --- | --- |
| New map/catalog/root blocks | `n+c+1 <= H`; reserve against the input pair, not candidate frees. |
| Newly retired pool blocks | `p+c+1 <= H`, with recovery charge. The extra leaf adds a claim, not an old retirement. |
| Final live map / pool / claims | `J+1<=m`; at most `Pcat+(J+1)+1<=Pcat+H` pool blocks and `E+M+Pcat+H` live claims. Check actual candidate inventories. |
| Debt and recovery | Selected/protected pool debt stays within `2H/H`; volume cohorts stay within `2D`, including cross-volume and recovery-charged orphan batches. Recovery remains `max(256,3H+V+2D)`. |
| Canonical records | Removing pool storage leaves at most K volume/free records. Restoring at most `Pcat+H` live and `2H` retired pool blocks adds at most two boundaries each: `Rfinal<=Rbase<=S`. Establish physical E/M and live/debt guards before relying on this candidate/preclaim storage bound. |
| Raw deltas | `2H+2D` eligible frees, at most H old pool retirements, D volume retirements, V volume claims and H new pool claims: at most `4H+3D+V`. |
| Output and reusable capacity | Existing `H+V` block buffers/eligible capacity cover final output and staged volume work. Recheck the next funded step's `H+V` eligibility and deletion/permanent capacity at final admission. |
| Owners and catalog | The extra leaf is pool-owned. No new volume owner is introduced; actual cross-volume catalog union remains `c<=Cmax`. |
| Generations and lifetime | Trial, restored repair and bulk use the same `g+1`. No extra publication, durable acknowledgment or orphan step occurs during planning. Existing startup, checkpoint, final-release and `3T` orphan/tail-fence headroom remain. |

If `J=m`, skip the trial and use the existing planner; do not refuse the mutation
solely because this optimization has no headroom. Ordinary/migration floors,
deletion promises and sparse-host physical-space limitations remain unchanged.
This funds a bounded attempt and the existing fallback; it does not guarantee a
local split, remove resource-pressure refusal before admission, or reserve host
disk space.

Each successful trial increases live map inventory by one. Repeated successes
can fill parents or reach m; they also change later allocation and retirement
costs. The proposed termination bound covers planning evaluations, not a local
replacement-size guarantee. Allocation self-accounting can still produce global
closure. With calculated bulk map size `Nb`, immediate map-write difference is
`4096*(Nb-n)` bytes; that comparison omits later histories and is not a measured
benefit. Complete-history measurements must include the additional leaf's later
replacement/reclamation cost as well as any avoided neighbour expansion.

Keep current raw deltas, source descriptors/index/leaf list, runs, H output
descriptors and H IDs/ranges in their existing delta subregions. A concrete
proposed extension appends `8m` bytes for seed marks, a split descriptor within
128 bytes and one planner rollback header within 256 bytes:

```
664H + 184m + 384D + 128V + 384
  <= 848H + 384D + 128V + 384
  <= 1024(H+D+V)
```

Computed profiles have `H>=Cmax+2>=4`, establishing the last inequality. The split
descriptor can hold anchor/parent/insertion/output identities and bounded trial
observations. The header stores one authoritative planner snapshot (eight existing
size counters, repair diagnostics and flags); it does not duplicate a full writer
snapshot. Restore derived fields by regeneration. Existing output descriptors
represent the virtual leaf; no second map, topology or emitted inventory is needed.
Bulk may reuse the dead front of this region while IDs/ranges remain disjoint.
Static size checks, aligned checked offsets and the publication scratch-quarter
check must establish actual implementation fit; failure of those proofs requires
review, not an automatic budget increase or post-admission heap allocation.

For the existing illustrative `E=8192,M=4096,N=1` profile, computed
`H=729,m=726` gives 732,712 proposed delta bytes within 1,139,712 reserved bytes.
These are calculated examples, not defaults, measured peaks or test expectations.
The overall arena and caller/current core ceilings remain unchanged.

## Illustrative traces

These are conditional accounting examples, not measured fixtures or a promise
that renewed canonicalization produces particular record counts.

| Situation | Required result |
| --- | --- |
| J=10, c=1, seed p=2, one source leaf with 47 candidate records and a spare parent slot | Fixed accounting uses four prefix IDs. Trial p=2 emits n=3 and uses five; old pool retirement remains four. If renewed seams then add another nonadjacent source leaf, p=3 emits four and uses six, retiring five. Seal only if anchor-run records still require/fit its two output leaves and the other run fits; final live map is 11. |
| Extra claim consumes a free suffix and anchor-run count drops into its original capacity | Do not keep an unnecessary split. Restore seed, discard all speculative allocations/claims/counters, regenerate baseline accounting and continue ordinary repair with the trial disabled. |
| Full parent, source leaf root, another failing run, insufficient one-leaf capacity, J=m or global closure | Skip or abandon according to the named guard, then use baseline repair/bulk. No internal split, normalization publication or increased limits. |
| Healthy-trace read error during trial encoding/catalog sealing | Stop access with existing read-error provenance. No restoration back to health or hidden fallback. |
| Replacement-write/pre-slot error or uncertain slot publication after selection | Existing READABLE_STOPPED/ACCESS_STOPPED distinctions apply. If maintenance follows a confirmed user commit, preserve that progress independently. Durable simulator restart protects both retained histories under existing recovery preconditions. |

## Failure contract, implementation and review choices

Only specified topology misses and the baseline exact candidate-local
`PFS_LIMIT`/`PFS_NO_SPACE` admission-miss class can abandon a trial. Malformed source,
invalid deltas, read failures and encoding/claim inconsistencies remain errors.
Exhaustion of guaranteed canonical/preclaim/raw-delta/output/workspace storage is
an invariant failure that stops mutation, escalating if integrity is unestablished.
It is not a fallback or successful safe refusal. Ordinary pre-admission quota,
profile and capacity refusal leaves a healthy instance usable. An admitted fence
or orphan cleanup must finish through the funded path absent I/O/integrity failure.
The healthy PENDING/NONE distinction and separate `drain_pending` diagnostic
remain. Recovery retains the adapter/operator durable-backing precondition;
real-host post-error qualification stays deferred and uncertain mutations are
not automatically retryable.

After owner approval, the smallest coherent implementation PR would update
publisher emission/retirement accounting, the one-leaf virtual encoder, seed
restoration and diagnostics together. Replace global assumptions that emitted
nodes equal marked nodes with mode-specific invariants: baseline `n=p`, trial
`n=p+1`. Keep source retirement membership distinct from output allocation and
move catalog/root offsets to n in every consumer. Bulk sizing/fill and all public
mutation/result/recovery interfaces remain unchanged. Publish filesystem code
and tests first, then its published Pyxis pin and milestone/results.

Tests land with that behavior: independent canonical interval expectations,
cross-parent minima/coverage and retained sharing; necessary/sufficient split,
coalescing-induced miss, full parent, one-leaf root and live-node cap; exact
reachability/prefix eligibility; failed-trial restoration compared with fresh
baseline repair; near-minimum funded user/orphan/fence/startup work; cross-volume
debt and final-release/checkpoint behavior. Use healthy traces for adapter failure
cuts, including trial reads and discarded-candidate catalog reads. Compare both
independently expected retained payloads after replacement writes and before each
slot attempt during maintenance. Refusal is incomplete, not corruption or completion.
No physical IDs, incidental publication counts, source fixture shape, machine
properties or benchmark parameters become correctness requirements.

Use current recorded histories first. Later approved implementation measurements
repeat the unchanged matched RAM matrix through all final maintenance, reporting
source/retired/emitted counts separately, total/user/orphan/drain/data/metadata
writes, trial opportunity/success/miss reasons, discarded trial work, chosen-path
closure, local versus calculated bulk cost and planning/full elapsed time. Savings
use emitted n, not retired p. Include failed trials and baseline repair in planning
time and classify callbacks by the active publication. Use existing infrastructure
and configured RAM/no-swap/trace budgets; no new framework, fallback or campaign.

**Owner decisions before implementation:** the fixed one-leaf surplus; a trial
only when the first failing run is overflow; lowest-key eligible anchor and
insertion order; necessary-split rule and balanced
packing; one trial with seed restoration before existing repair/bulk; selecting a
successful trial without an alternative-history cost gate. These are the recommended
package, with unknown useful hit rate and possible planning/write regressions.
General structural solving, new placement/coalescing policy and budgets are separate.
Task 7 and writable deployment remain open independently of design approval.
