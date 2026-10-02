# Filesystem write-efficiency investigation

**Superseded 2026-10-02** by the [simple native filesystem](native-filesystem.md)
restart. Kept as history; no further work on this plan is assigned.

Status: research recorded on 2026-10-02. The narrow combined cleanup below is
implemented with unchanged admission and reserve policy. The owner subsequently
accepted the [retirement-debt policy and bounds](filesystem-retirement-debt.md)
after Pyxis #311; its coherent implementation and matched validation are merged
in [filesystem #20](https://git.internal/PyxisOS/pyxis-fs/pulls/20) and
[Pyxis #312](https://git.internal/PyxisOS/pyxis-os/pulls/312).
The incremental-map investigation below uses that merged baseline: Pyxis
`b13ecec`, published filesystem pin `5e44d6f` (the same tree as merged `e5b76c5`).
The owner accepts Btrfs-comparable matched submitted-write costs as the initial
deployment direction, and the topology-preserving approach with explicit funded
bulk fallback/global closure as an intermediate optimisation. This revision
specifies the requested fixed ID ordering, neighbouring-leaf redistribution and
cost reporting; the owner accepted that package after #313 merged and assigned
its implementation. The first implementation is recorded below; numerical
deployment criteria remain proposals. Other proposals remain unassigned; this document does not
authorize further implementation or experiments.
After filesystem #24 / Pyxis #325 merged, the owner assigned the
[bounded leaf-overflow split design](filesystem-overflow-split.md). After #327
merged, the owner accepted its fixed-surplus accounting/fallback package and
assigned the implementation and unchanged matched RAM-only validation below.
[Task 7 and writable deployment](writable-filesystem-core.md#focused-tasks)
remain open. Preserve individually durable completed operations, both retained
states and the existing failure/recovery contract in the baseline proposals.

## Evidence and comparison

The research inspected the then-current Pyxis pin, filesystem
[`dc63d62`](https://git.internal/PyxisOS/pyxis-fs/src/commit/dc63d62b81d7c6cca082eeb52842b84d8a1fa390),
whose tree equals filesystem main `87d2f20`. The research parent was `2da3e5f`.
The contract-audit findings on coalescing, memory ceilings, formatter floors and
object/extent limits are summarised inline below against their current sources.
Its older test findings were addressed by
[filesystem #18](https://git.internal/PyxisOS/pyxis-fs/pulls/18) /
[Pyxis #307](https://git.internal/PyxisOS/pyxis-os/pulls/307). Carryover
changes the historical immediate-drain observations, not the memory/floor/limit
policies discussed here.
This investigation used read-only inspection and the existing
[small RAM baseline](../../fs/docs/ram-baseline.md) and
[bounded counters](../../fs/docs/measurements/ram-baseline.json), not new workloads.

Linux sources below are pinned to v6.13 (`ffd294d`), a mechanism reference,
not the baseline's Linux 6.19.10. Apple's format reference is dated 2020-06-22.
None of these sources establishes Pyxis's two independently retained payload
contract; their mechanisms need a matching lifetime proof before transfer.

| Filesystem | Relevant mechanism | Durability and eventual cost | Lesson for Pyxis |
| --- | --- | --- | --- |
| XFS | Metadata redo logging; per-allocation-group free-space B-trees; CIL aggregation of repeated logged objects before a log force. | Ordinary transaction commit is asynchronous. Synchronous transactions / `fsync` force relevant logging; home metadata still needs writeback to reclaim journal space. | Local allocation updates avoid a whole-volume rewrite. Aggregation across already acknowledged durable calls would require durable redo plus replay, rather than simply delaying our publication. |
| F2FS | Stable node IDs resolved by NAT; SIT tracks validity; checkpoints choose table copies and carry bounded entry journals. | Selective `fsync` node chains support roll-forward; some conditions require a checkpoint. Table-page flushing, later checkpoints and cleaning remain real writes. | Indirection interrupts parent-pointer propagation but adds maintained state. Deferred cleaning and recovery must be charged, and protected checkpoint blocks cannot be reused merely because currently invalid. |
| APFS | Redirected metadata; virtual object IDs resolve through a transaction-keyed object map; checkpoint publication records roots and ephemeral state. | Object-map and checkpoint metadata themselves need persistence. The reference documents deferred free queues and reaper state, but does not establish Apple's exact per-call `fsync` write traffic. | Redirecting a tree or adding stable IDs does not eliminate allocation, indirection, publication or reclamation accounting. |

XFS's [logging design](https://www.kernel.org/doc/html/v6.13/filesystems/xfs/xfs-delayed-logging-design.html)
distinguishes CIL log checkpoints from AIL home writeback. Its
[`xfs_alloc_fixup_trees` / counter updates](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/ffd294d346d185b70e28b1a28abe367bbfe53c04/fs/xfs/libxfs/xfs_alloc.c#569)
modify address- and size-ordered allocation trees locally.
[`xfs_buf_item_log_segment`](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/ffd294d346d185b70e28b1a28abe367bbfe53c04/fs/xfs/xfs_buf_item.c#928)
tracks dirty 128-byte regions for logging: those are not 128-byte device writes.
[`xfs_file_fsync`](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/ffd294d346d185b70e28b1a28abe367bbfe53c04/fs/xfs/xfs_file.c#125)
orders data writeback, log forcing and required device flushes.

F2FS's [design](https://www.kernel.org/doc/html/v6.13/filesystems/f2fs.html#design)
and [original FAST '15 paper, §2.7.2](https://www.usenix.org/system/files/conference/fast15/fast15-paper-lee.pdf#page=7)
explain NAT indirection and selective roll-forward. Source makes the remaining
cost explicit: [NAT table/journal flushing](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/ffd294d346d185b70e28b1a28abe367bbfe53c04/fs/f2fs/node.c#3034),
[SIT flushing](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/ffd294d346d185b70e28b1a28abe367bbfe53c04/fs/f2fs/segment.c#4545),
[checkpoint fallback in `need_do_checkpoint`](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/ffd294d346d185b70e28b1a28abe367bbfe53c04/fs/f2fs/file.c#192)
and [live-block relocation](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/ffd294d346d185b70e28b1a28abe367bbfe53c04/fs/f2fs/gc.c#1713).
Hole reuse [unions current and checkpoint-valid maps](https://kernel.googlesource.com/pub/scm/linux/kernel/git/torvalds/linux/+/ffd294d346d185b70e28b1a28abe367bbfe53c04/fs/f2fs/segment.c#2892).
F2FS also allows policy-dependent in-place data updates; it is not uniformly COW.

The [Apple File System Reference](https://developer.apple.com/support/apple-file-system/Apple-File-System-Reference.pdf)
describes physical, virtual and ephemeral objects (pp. 7–10), mounting complete
checkpoints (p. 26), object maps keyed by object ID and transaction (pp. 44–50),
and deferred free/reaper records (pp. 158–168). Available read-only source
[`apfs-fuse` at `66b86bd`, object-map lookup](https://github.com/sgan81/apfs-fuse/blob/66b86bd525e8cb90f9012543be89b1f092b75cf3/ApfsLib/ApfsNodeMapperBTree.cpp)
corroborates ID/transaction resolution; it is not Apple's writer or evidence
of its flush ordering, transaction aggregation or measured amplification.

No contemporary XFS, F2FS or APFS measurements were performed here. Historical
paper numbers in the [milestone](writable-filesystem-core.md#write-efficiency-target-and-research-direction)
are specific to their versions, hardware and workloads, not current comparative
results or acceptance thresholds.

## Publication costs before carryover

The following is inspection of
[`file.c`](https://git.internal/PyxisOS/pyxis-fs/src/commit/dc63d62b81d7c6cca082eeb52842b84d8a1fa390/core/file.c),
[`mutate.c`](https://git.internal/PyxisOS/pyxis-fs/src/commit/dc63d62b81d7c6cca082eeb52842b84d8a1fa390/core/mutate.c)
and [`writer.c`](https://git.internal/PyxisOS/pyxis-fs/src/commit/dc63d62b81d7c6cca082eeb52842b84d8a1fa390/core/writer.c).
These inspected revisions precede the two delivered corrections below; carryover
changes reclamation scheduling, preserving each operation's durable publication.
Each row describes a committed batch; a larger request can commit bounded
prefixes. Compatible slices within a request are already combined when they fit.

| Operation | Volume edits before the common publisher | Eventual reclamation |
| --- | --- | --- |
| Small overwrite | Read partial old blocks, preserve outside bytes, COW each affected 4 KiB payload block, replace extent mappings and object path. | Retire replaced data and volume paths; drain after publication. |
| Append | Write new blocks and mapping/object paths; preserve partial EOF and clear newly exposed gaps as needed. | Retire changed metadata and any replaced partial block. Separate committed births can prevent extent merging even for contiguous placement. |
| Create | Insert directory entry and child object; update parent object. | Reclaim replaced volume paths. |
| Rename | Atomically edit source/destination names and relevant object paths. Replacement also detaches the victim and inserts its orphan marker. | Reclaim paths; victim cleanup waits for final live-reference release. |
| Unlink | Remove name, update parent, clear victim parent and insert orphan marker atomically. | After final reference release, bounded batches remove data/grants, then paired object/orphan records. An empty file still needs the lifecycle capacity promise. |

For every publication the common publisher writes replacement data/volume nodes,
**all** allocation-map nodes, the changed volume-catalog path and pool root;
flushes; writes the older superblock slot; flushes again. Confirmation follows
the second flush. The two ordering boundaries establish durability under our
current publication protocol. They are deliberate requirements, not a universal
minimum for every filesystem architecture.

Funded drain can then publish twice: advance the older retained state and
durably free volume retirements no longer protected. Both publications rebuild
the map. Confirmed user progress remains confirmed if maintenance fails. Reuse
requires eligibility against both retained maps and claims; newly freed space
cannot be reused in the same publication.

Payload preservation, consistent allocation accounting and ordered publication
are necessary under the baseline guarantee. Whole-map rebuilding, an object
rewrite immediately before its deletion, and separating every final data cleanup
from paired deletion are current representation/maintenance choices. The volume
catalog **already uses changed-path COW**; making it incremental is not a new
opportunity. Moving map writes to a later drain without reducing total work does
not improve the stated target.

The existing 256-file measurements submit 11,096 KiB for 256 KiB of small writes
(43.34375 total bytes/useful byte), versus ext4's 1,568 KiB under the recorded
per-operation synchronization. Pyxis drains account for 6,608 KiB. Overwrites
submit 5,624 KiB for 32 KiB useful bytes, including only 216 KiB of data.
The phase counters do not separate map, catalog, pool-root and volume-tree bytes;
their precise individual shares cannot be reconstructed. These are RAM submitted
write counts, not NAND wear or NVMe performance, and native recovery contracts
differ as recorded in the baseline. Batched native rows have weaker intermediate
durability and must remain separate.

## Contract audit: safety, proof inputs and policy

Changing a bound requires the affected proof, not preservation of its current
number merely because earlier documentation calls it a contract.
The relevant current sources are the [format](../../fs/docs/format.md),
[`plan.c` closure and arena](../../fs/core/plan.c),
[`admit.c` capacity checks](../../fs/core/admit.c)
and [namespace proof](writable-filesystem-core.md#agreed-removable-namespace-profile-byte-and-repair-proof).

| Topic | Genuine requirement / current proof dependency | Reconsiderable policy and proposal |
| --- | --- | --- |
| Map coalescing | Exact ordered partition; valid ownership, birth, retirement and charges; protected-state exclusion. Canonical records support `K = 1 + 2(E+M+2D)` under accepted carryover and the resulting `S(H)` record/workspace closure. | Maximal coalescing is a format policy. The incremental proposal preserves it. Relaxation would require a separate format/record-debt/funding proposal; it is not needed for this first candidate. |
| Core memory | Finite caller-owned cap, checked arithmetic, charged allocations, refusal before admission; funded drains require no new allocations. | The enforced core-wide 1 GiB ceiling originated as tool policy, not a disk-format safety property. Propose caller-configured caps; keep host default and any host maximum separate. No default or budget increase is made here. |
| Reserve floors | Ordinary workspace must cover the proved batch envelope; recovery must cover the whole funded sequence. Persisted reservations still count against pool capacity. | Current admission inherits formatter floors of 1024 ordinary and 1024 unused migration blocks. Any separate proposal to remove these floors must retain carryover funding of `V+2D` and `3H+V+2D`, without borrowing existing migration reservations. It is not assigned here. |
| Object/extent limits | Namespace occupancy/depth and permanent deletion promise; bounded map/claims/checker memory and integer arithmetic. | The shared 1,048,576 cap couples different populations. Separate namespace proof limits from extent product capacity before proposing new values from actual histories. Larger disks alone do not justify them. |

Coalescing is enforced by ordinary node decode, cross-leaf checking and the
private map planner, not merely a checker preference. Starting from canonical
records, an incremental edit can repair changed endpoints and adjacent records,
including neighboring leaves and balancing paths. This increases the edited
path envelope but does not intrinsically require rebuilding the map.

Relaxing coalescing needs more than checking a maximum record count. Redundant
free boundaries can accumulate independently of current E/M. A cap can safely
refuse a new operation yet leave no space for a previously funded drain's splits.
A relaxed design must bound redundant-boundary debt in addition to canonical
records and reserve records, paths, buffers, replacement and retirement space
through user publication, both drains and orphan cleanup. Deferred merging must
itself be funded. Reader/planner acceptance must change coherently; review the
format consequences explicitly rather than assuming either compatibility or a
version increment.

Under the accepted carryover proof `V=128`, `D=256`, hence ordinary workspace
needs `V+2D = 640` blocks and recovery needs `3H+V+2D = 3H+640`, before
the ordinary formatter floor. These are proof results for this publisher,
not proposed universal minima. The formatter's 256-block recovery floor is
already dominated. Reducing admission floors is a separate policy decision; the
first cleanup candidate leaves all floors, reservations and admission unchanged.

The six-entry namespace occupancy rule and variable-length insertion/deletion
repair proof make depth nine require at least `2*6^8 = 3,359,232` namespace
records. Objects include volume roots: namespace records total `O−N`. The
present global object cap safely implies the required bound; a new object cap
must be checked against actual namespace populations, not the audit's literal
object threshold. Permanent deletion capacity also depends on the proved
namespace-node bound including singleton root allowances, not merely live nodes.
Raising only E need not change this namespace proof, but the shared constant
currently changes several reader/checker limits together.

E counts file mappings, including inline mappings, not allocation-map runs.
One million separately committed contiguous 4 KiB appends consume about 4 GiB
of payload capacity in mappings; 256 KiB births represent about 256 GiB before
source files and churn. Birth distinctions remain necessary for incarnation
protection. Revisit map closure, claim capacity, arithmetic and actual histories
alongside any extent-limit proposal.

The reserved drain arena is not peak opening memory: both validated checker
states remain alive while the arena is allocated, checker vector growth has
temporary copies, and handles need headroom. An incremental disk editor may
still retain O(S) in-memory summaries. Neither a larger caller cap nor fewer
disk writes proves full-profile opening capacity.

## Incremental allocation-map proposal

The owner accepted this first approach and its funded bulk/global-closure limitation
as intermediate work after #313, then assigned its implementation. Delivered
follow-ups, including the one-leaf overflow extension, are recorded below;
deployment qualification and further structural work remain separate.
At the carryover baseline used for this investigation, the
[planner](../../fs/core/plan.c), [publisher](../../fs/core/writer.c)
and [admission](../../fs/core/admit.c) still rebuilt the entire map for every
publication, while the catalog replaced only changed paths. Carryover reduced
standalone fences without reducing each remaining publication's map work.
Full map/claim summaries and candidate validation remain population-sized in
memory and CPU in the implemented incremental path; this design reduces submitted
map writes, not all reads or planning costs.

The first implementation uses the [shared core's closed-path publisher](../../fs/docs/incremental-map.md).
It validates immutable source topology, regenerates selective retirement and fixed-prefix
claims on every growth pass, and preserves canonical seams and source fanout.
The existing physical E/M guards are established before relying on guaranteed
map storage; unexpected exhaustion of that storage stops mutation. Final
candidate admission retains ordinary profile/quota/capacity refusal. No admission,
reserve, memory-budget or runner policy changes are part of this delivery.
The [matched implementation record](../../fs/docs/incremental-map-measurements.md)
reports total traffic, local/bulk cost, fallback frequency, planning observations
and remaining coverage limits. [Filesystem #21](https://git.internal/PyxisOS/pyxis-fs/pulls/21)
publishes the implementation; this parent pins its published revision. Two unchanged
matched before/after matrices reduce submitted measurement-window writes by
7.68–40.41% at 256 files and 0–2.85% at 32, with 32-file compiler traffic unchanged.
RAM elapsed times increase with scans and optional diagnostic counting included. General structural editing is still unassigned.

### Format and local editing

Preserve the current format: an exact partition of `[1, B-1)`, maximally coalesced
adjacent allocation records with equal state/owner/birth/retirement/charge,
80-byte encoded records (46 per leaf), and fixed numeric separators (65 children
per internal node). Leaves must be nonempty; all internal nodes, including an
internal root, have at least two children. No new half-full occupancy rule is
proposed. Refer to the
[format](../../fs/docs/format.md), rather than treating these derived capacities
as new independent constants.

Logical interval changes are applied together to the immutable selected map,
checking their expected before-states. Canonicalize the resulting ordered stream
before packing, including predecessor/successor records across leaves. A changed
leaf requires new references along its ancestor path; an unchanged subtree keeps
its reference and birth. New records cannot be placed in an older unchanged
leaf: its containing birth must still bound every record's birth and retirement.

The eventual structural editor would split overflowing leaves, partition children
on internal overflow, remove empty leaves, and redistribute/merge when internal
occupancy would fall below two. Separator minima and all consumed sibling paths
must be replaced together; a singleton root collapses and growth stays within the
existing depth bound. Each consumed source node is retired once, while superseded
private nodes are scratch, never committed retirements. Canonical coalescing can
both remove a record in the next leaf and change its separator. Point insertion
alone is therefore insufficient; the current generic editor does not yet supply
this allocation-range operation.

The smallest proposed first PR avoids structural changes. It replaces a closed
set of leaves and ancestors **one for one in the source topology**, redistributing
canonical records within each contiguous dirty leaf run and extending a run into
neighbouring source leaves when needed. Failure to fit after bounded expansion
uses the explicit funded bulk path below. This is a
candidate intermediate improvement, not the final local structural editor;
useful locality and fallback frequency still require measurement.
General splits/merges, bulk fill-policy changes and new placement preferences or
restrictions are outside this first implementation.

### Self-accounting with fixed topology

Let `J` be the selected map's node count, `c` the actual changed catalog-path union,
and `P` a set of immutable source map nodes marked for replacement. Before any
replacement write:

1. Stage volume deltas, eligible input retirement frees and old pool-root/catalog
   retirements. Discover source leaves covering these changes and their ancestors.
   Reserve the first `H` already-reusable input block IDs in **ascending physical
   block order**, exactly the existing `select_free` ordering, excluding the
   batch's at most `V` new volume blocks. Fix this list before closure begins:
   neither newly declared frees nor changes to `P` may replace or reorder it.
   Reservation does not allocate the list. New placement heuristics are deferred.
2. In a scratch pass, claim exactly the first `q+c+1` IDs, where `q = |P|`:
   `q` new map nodes, `c` catalog nodes and one pool root. Retire exactly `P` and
   the old root/catalog union, keeping unmarked source map claims live. All new
   pool claims have the same owner, candidate birth and permanent charge.
3. Apply the complete disjoint deltas to the original map, canonicalize, and add
   to `P` every source leaf needed for those changes and every ancestor. Include
   an adjacent source leaf whenever a final canonical record **straddles an
   original dirty-run coverage endpoint**. Equality at the current seam alone is
   not a sufficient repair test. Repeat only when the source set grows.
4. At stable accounting/seam closure, scan maximal contiguous dirty leaf runs in
   ascending source-key order. For the first run whose `r` final canonical records
   fail `l <= r <= 46l`, add its immediately adjacent unmarked predecessor source
   leaf if present, otherwise its unmarked successor, and every ancestor. This
   fixed traversal convention applies to overflow **and underflow**, crosses
   parent boundaries, and does not filter source leaves by free-block eligibility.
   Merge touching dirty runs. Recompute the complete deltas from the immutable
   source with the larger `q+c+1` prefix and renewed retirement accounting and
   canonical seam closure, then reconsider all runs. Do not estimate neighbour
   slack once and skip the changes caused by its replacement.
5. Never remove a marked node or undo an expansion. If no adjacent unmarked leaf
   remains for a failing run, or closure reaches all `J` nodes, use the explicit
   funded bulk path. Expansion does not guarantee a fit. Otherwise, once every
   run satisfies `l <= r <= 46l`, its endpoints align with canonical boundaries.
   Partition those records into exactly `l` nonempty leaves in source order
   (for example, ceil remaining records / remaining leaves); update minima and
   references in exactly the marked ancestors, preserving internal levels and
   child counts. Assign final prefix IDs to nodes and seal only this final plan.

The live map still has exactly `J` nodes. Every marked source position has exactly
one reachable replacement, so emitted map nodes equal `q` independently of record
count. Prefix size grows with `P`; provisional node roles may change without
changing their identical allocation records. Accounting, seam repair and
redistribution all add source identities to the same set; each unsuccessful
growth round adds at least one previously unmarked node. There are at most `J`
strict growth rounds and one final decision pass, not separate iteration budgets
for each phase. This is a finite source-identity proof, not an
arbitrary iteration cap, assumed contraction or retry for space. The canonical
map and allocation claims must be checked against the sealed node inventory
before publication; unused live padding is prohibited.

For example, with 20 source map nodes and two catalog replacements, an initial
three-node path claims six pool blocks. Accounting for its old nodes may add
another leaf and ancestor, giving five replacements and eight pool blocks. If
that closure fits the run guards, 15 source map nodes remain shared. This is an
illustrative layout, not measured locality. Two dirty leaves can hold 92 canonical
records; 93 or one record requests neighbour expansion before structural fallback.
For example, adding a third leaf with ten unchanged records would give 103 or 11
records, each within the three-leaf range, **only if** renewed accounting and seam
closure do not change those counts. New retirements/claims can invalidate that
arithmetic, expand other runs or reach global closure. Failure to pack then uses
funded bulk construction, not an unfunded resource retry. Local closure size and
fixed-topology hit rate remain unmeasured: selective retirement can split an old
contiguous pool-allocation record where bulk retirement would coalesce it.

Self-accounting can still reach the whole tree: place each old leaf's physical
allocation record in the next leaf, closing a cycle. Retiring one leaf then
requires replacing every leaf and ancestor. Contiguous new allocation does not
constrain these old dependencies. No proposal preserving arbitrary current
layouts can infer a population-independent write bound from tree depth alone.

### Resource and sequence bounds

Retain the accepted carryover envelope and all existing policy in the first PR:

```
V = 128; D = 256; Pcat = 4N-2; Cmax = min(Pcat,16)
K = 1 + 2(E+M+2D)
Rbase = K + 2Pcat + 6H
S = ceil(23 * (Rbase + 2Cmax + 4) / 21)
F(S) + Cmax + 1 <= H; m = H-Cmax-1
Pmax = Pcat+H
ordinary >= max(1024, V+2D)
recovery >= max(256, 3H+V+2D)
migration >= 1024 (unused)
```

`H` is the current computed solution, not a new chosen limit; `F` is the current
bulk map shape function. Its computation, namespace/deletion proof, E/M limits,
record/depth maxima, memory ceiling, formatter floors and pool reservations do
not change. The inputs are current proof assumptions, not universal filesystem
requirements immune to later review. The source and every sealed candidate must
satisfy `J <= m`, independently of `S`: arbitrary sparse incremental trees need
not have the bulk shape `F(S)`.

| Resource | Bound and reason |
| --- | --- |
| Replacement / retired pool metadata | Each is `q+c+1 <= J+Cmax+1 <= H`; the bulk alternative also fits `H`. Shared map nodes stay live. Final live pool metadata fits `Pmax`. |
| Records and claims | Stream canonical deltas into the existing `S`-record vector. Unchanged pool nodes plus retired and replacement nodes remain within the existing `Rbase`/`S` closure; claims fit `E+M+Pmax`. Do not pack a transient uncoalesced vector and assume final coalescing will rescue overflow. |
| Retirement debt | Selected pool debt at most `2H`, protected pool debt at most `H`; selected volume debt at most `2D`, normally protected volume debt at most `D`. Opening/fences retain the broader `2D` protected-volume bound. Retire only marked map nodes, with recovery charge. Orphan volume retirements also retain recovery charge. |
| Catalog / cross-volume work | A new batch owner plus the eligible older volume cohort changes at most two catalog records; opening/fences can reclaim two owners. Their path union costs `c <= Cmax`. Pool-only self-accounting cannot add a third volume owner or catalog edit. |
| Reusable input and output | Reserve `H+V` eligible input blocks against both maps/claims and ended physical borrows. Newly declared frees are unavailable to this publication. Recheck the output admission guarantee of `H+V` for the next funded step; reduced retirement does not permit weakening it. |
| Generations | Opening retains `g+a+3T <= MAX`, with `a` the derived zero/one/two initial fences. User admission retains `g+3+3T_projected <= MAX`. Each orphan step decreases work and fits one publication plus two tail fences. A scratch rejection/bulk alternative uses the same publication generation, not an extra normalization commit. |

Recovery capacity still covers at most `2H` previously retired pool blocks, `2D`
recovery-charged volume blocks and `H+V` reusable replacement capacity. Ordinary
capacity covers the two `D` ordinary cohorts plus `V`. Permanent admission retains
`Aeff = A-Z+B` deletion headroom and the existing pool-capacity inequality; ordinary
growth cannot spend these reservations. None of this reserves physical host
space for a sparse image.

The record bound has a direct counting proof, rather than assuming the bulk
packing function applies to a sparse incremental tree. Removing pool storage
temporarily leaves at most `K` canonical volume/free records. Restoring at most
`Pcat+H` live pool blocks and `2H` retired pool blocks adds at most two boundaries
per block, so `Rfinal <= K+2Pcat+6H = Rbase <= S`. Establish the projected E/M,
debt and live-node guards before using that storage guarantee. General structural
trials must check total live nodes and these coupled guards too; a trial count
alone does not prove that its candidate fits.

Neighbour expansion does not add tree positions: final live map nodes remain `J`,
and newly marked leaves/ancestors still give `q+c+1 <= J+Cmax+1 <= H` in every
scratch round. The same retirement/debt bound, raw-delta bound and `m` source/run
descriptor slots cover accounting, seam repair and redistribution together.
No extra publication, generation, allocator reservation or workspace allocation
is needed. These bounds fund the attempt and the bulk path, not successful local
packing; unexpected failure to obtain guaranteed resources remains an invariant
failure.

Use the existing arena, retaining both input states and the third candidate:

```
240S + 288(E+M+Pcat+H) + 48objects
  + 4096(H+V) + 1024(H+V+D) + 8 MiB
```

A concrete implemented layout of the existing delta region is:

| Region | Reserved bytes |
| --- | ---: |
| Complete raw before/after deltas | `128(4H+3D+V)` |
| Source topology (reference, parent, level, child count, order, coverage, marks) | `128m` |
| Block-sorted source descriptor index | `8m` |
| Source worklist | `8m` |
| Dirty-run descriptors | `32m` |
| Replacement/role descriptors | `128H` |
| Reusable input IDs and ranges | `8H + 16H` |

The raw-delta count follows from at most `2H+2D` eligible frees, `H` old pool
retirements, `D` new volume retirements, `V` new volume claims and `H` new pool
claims. Total partitioned bytes are `664H+176m+384D+128V`, at most
`840H+384D+128V`, below the existing `1024(H+V+D)` region. Slots include alignment;
the implementation verifies descriptor sizes, checked offsets and nonoverlapping
lifetimes. A source descriptor can hold a 24-byte reference, parent/order and
coverage indices, counts/flags and replacement index within 128 bytes; it does
not cache all child blocks. Encode with depth-bounded buffers and rereads.
Output blocks fit `4096(H+V)`. Keep the existing scratch partition: lower half
for admission, third quarter for publication, final quarter for mutation staging.
The walker/encoder reuses publication editor/record buffers before catalog
encoding; it must not overwrite borrowed mutation or admission storage.
Abandoning the optimization releases its descriptor regions for bulk
planning, but never destroys either input summary or the batch's logical edits.

This layout adds no heap allocation to admitted work and no memory-budget increase.
For the illustrative existing profile `E=8192, M=4096, N=1`, current arithmetic
gives `H=729`, `S=32843`, `m=726`, 2,827 recovery blocks and a 30,372,016-byte
arena. The delta subregions use 726,520 of 1,139,712 reserved bytes, including
5,808 bytes for the source index within the same arena allocation.
These are calculated examples, not new defaults, observed peaks or test constants.
Opening still needs both checker states, their temporary vector copies, the arena
and handles within the caller cap and current 1 GiB ceiling. Full source traversal,
vector application and admission remain population-sized. The implemented
retirement-membership lookup uses binary search over a block-sorted descriptor
index constructed once with an in-place heapsort after full source validation.
Descriptor topology and authoritative marks stay in place. Index construction is
`O(J log J)`; membership contributes `O(J log J)` per accounting pass and
`O(J² log J)` across bounded closure growth, plus `O(J log J)` during sealing.
This replaces the prior `O(J³)` repeated-lookup contribution. Source-load duplicate
detection remains `O(J²)` once. These are calculated component costs, not measured
timings or a complete bound for all planning; source validation, canonical map
editing and admission add other work. CPU/read measurement remains necessary;
write reduction alone does not establish acceptable latency.

### Explicit bulk alternative

Before replacement writes, choose the current funded bulk construction when:

- A run still fails `l <= r <= 46l` after neighbour expansion is exhausted
  (report overflow/underflow and the last `l/r`, not a premature split/merge).
- Closure marks all `J` source nodes, giving no map-write locality benefit.
- The exact local candidate fails the required record, node, charge or next-step
  resource guard, while the original admitted logical work has a valid bulk plan.

Reset to original inputs and logical edits, retire the whole source map, and
construct one bulk candidate with the existing proof. Its explicit cost is all
new map nodes (at most `m`), changed catalog nodes, pool root and slot, plus later
retirement/fence work; all are funded by the same `H`/workspace envelope. This is
not a second publication or a retry after failed I/O. A malformed source, invalid
delta, I/O failure or inability to obtain already-guaranteed storage is not an
optimization miss and must not be hidden by fallback.

Global closure can also exhaust redistribution; record both conditions when they
coincide. The existing bulk shape/fill policy is unchanged. Additional slack might
improve the local hit rate but changes `F(S)` and funding inputs; that is a separate
proposal, not part of this step. There is no new placement scheme or arbitrary
dirty-node threshold/timer.

### Cost and planning measurements

Report per-publication source node count `J`, final closure size `q`, growth-pass
count, largest node addition in one pass and redistribution additions. Report the
chosen path and fallback reason(s), and their frequency over the completed
workload. Include map replacement nodes/bytes and total submitted data/metadata
bytes across user, orphan and maintenance publications through final checkpoints.
Maintain active-publication classification; debt presence is not a phase.
Report per-publication planning elapsed time alongside full operation elapsed
time, with map-closure, bulk and diagnostic counting time distinguished where
instrumented. Full map scans and neighbour expansions are real planning work.
Use existing host measurement tooling; no production clock dependency or new
benchmark framework is required by this reporting contract.

For a local plan, compare `q` with `Nb`, the map-node count of the **existing bulk
construction for the same immutable input and logical work**. Its preclaim base
must independently apply those logical edits, eligible input frees and retirement
of the full source map/root/catalog union. If that canonical base has `Rb` records,
the current builder emits `Nb = F(ceil(23*(Rb+2c+4)/21))`, not `F` of the local
or bulk final record count. Preserve current conservative sizing and balanced
nonempty packing; do not substitute a new fill policy. Its exact pool prefix has
`Nb+c+1` IDs from the same fixed input list.

Compute this reference count without backing writes or heap allocation, keeping
both retained inputs, the sealed incremental candidate and its descriptor inventory
intact within existing scratch lifetimes. A counting pass can reuse no-longer-
needed raw-delta storage and stream canonical output into a count; invoking the
current destructive bulk builder on the incremental buffers is not this diagnostic.
Include diagnostic overhead in instrumented planning totals and identify it
separately from normal/uninstrumented observations. This is a calculated reference,
not a second mutation workload or automatic cost-based path selection.

The signed immediate map-byte difference is `4096*(Nb-q)`. A local-path hit can
have `q == Nb` or `q > Nb`, particularly with a sparse source tree; report zero or
negative savings rather than calling every hit a win. Catalog/root/slot and
volume writes remain, while changed later histories/reclamation can alter net
savings. Only matched full-workload totals establish improvement. Frequent bulk
work or expensive planning can leave the deployment problem unresolved despite
a correct intermediate implementation. Do not freeze incidental node/publication
counts, hit rates, proposed benchmark settings or machine properties into tests,
or invent numerical performance thresholds. Deliberate format/topology/resource
invariants, including emitted map nodes equal to `q = |P|`, remain valid assertions.

### Structural follow-up outside the first step

General splits/merges need another self-accounting proof: claiming additional
metadata IDs can consume/coalesce a free suffix and **decrease** final record/node
counts. Iterating an estimated node count until it appears stable is not proven
to terminate. One finite reference construction enumerates exact new-map counts
`q = 1..m`. For each fixed prefix `q+c+1`, compute monotone source-retirement/seam
closure and dry-run a deterministic structural splice afresh from immutable
source. Every marked node must be replaced or removed and marks never shrink;
consumed balancing siblings and ancestors join the set. Accepted output references
none of those source nodes. Accept only if exactly `q` reachable new map nodes
and total live map nodes at most `m` result, with the record/resource guards.
Trials outside those bounds publish nothing. Otherwise
try the next exact count, then use the funded bulk construction. At most
`m(J+1)` closure passes are considered; arbitrary iteration caps, padding and
unfunded retry are unnecessary. Its potentially very high CPU/read cost makes
this a proof reference, **not the recommended routine structural algorithm**.
Review a more efficient constructive split/merge solver before assigning that
follow-up; source topology preservation avoids this search in the first PR.

Placement preferences could improve locality without changing aggregate-capacity
admission, but need a fixed-order eligibility proof and are deferred. A placement
restriction or separate allocator-metadata ledger changes writable admission/
reimport or representation and needs its own proposal. Neither is selected here. Relaxed
coalescing also needs explicit boundary-debt and funded-drain revisions; it is
not a shortcut around self-accounting.

### Preserved guarantees and implementation sequence

The sealed candidate is published through the same replacement writes, pre-slot
flush, older-slot write and final flush. Completed calls remain individually
durable. Both retained maps and payloads stay protected during replacement writes;
only input-proven eligible blocks are allocated. Confirmed progress, healthy
PENDING/NONE and `drain_pending` keep their independent meanings. Pool-wide
read failures stop all access; replacement/pre-slot failures stop mutation with
readability preserved; uncertain publication stops access. A failed instance
cannot retry itself to health. Unexpected exhaustion during funded work is an
invariant failure, never successful safe refusal. Recovery still requires the
adapter/operator's durable-backing precondition, not cached validation plus flush.

1. The owner assigned the first implementation after #313 merged: implement
   topology-preserving map splicing with fixed ascending input IDs, bounded
   neighbouring-leaf redistribution, selective claim retirement, explicit bulk reasons and
   sealed candidate checks together. Use the unchanged admission envelope and
   publisher protocol for user batches, orphan work, startup and terminal fences.
   Publish filesystem code/tests first, then the Pyxis pin and matched results.
2. Add contract-focused coverage in that same PR: independent canonical interval
   expectations; shared-subtree preservation; seam coalescing; redistribution
   across parent boundaries, renewed accounting and exhausted expansion; structural fallback;
   cross-volume/cohort histories; orphan/final-release/checkpoint/startup behavior;
   maximal reachable funded pressure; and adapter failures at healthy-trace cuts.
   Compare both retained payloads after replacement writes and before every slot
   attempt. Tests assert contents, eligibility, reachability, bounds and sticky
   outcomes, not physical IDs, incidental publication counts or packing choices.
   Local replacement tests can require untouched source subtrees to remain shared
   because this is the deliberately proposed optimization contract.
3. Capture the existing unchanged matched RAM comparison before code changes and
   repeat afterward, including every tail fence and final checkpoint. Report
   closure/redistribution size, fallback reasons/frequency, local versus bulk-node
   cost, reads/planning time and full submitted bytes, with active
   publication phase classification. For a fixed source, replacing `q` map nodes
   instead of a bulk plan's `Nb` estimates `4096(Nb-q)` fewer map-write bytes;
   catalog/root/slot and volume writes remain. Different resulting histories and
   later reclamation can change net savings, so this is not a measured percentage
   or exact test expectation.
4. Review structural split/merge accounting separately using first-PR fallback
   evidence. If a global closure remains routine, propose the smallest justified
   placement/representation correction before promising a local worst-case bound.
   No later step is assigned automatically by accepting an intermediate PR.

### Focused planning follow-up

After filesystem #21 and Pyxis #314 merged, the owner assigned phase-separated
diagnostics and indexed retirement membership. The focused implementation keeps
both within existing workspace and output budgets, retaining placement, topology,
authoritative marks, closure, funding, admission and durability policy. It changes
lookup work and visibility, not the allocation-map representation or fallback.

The [first matched record](../../fs/docs/incremental-map-measurements.md) aggregates
both phases, so it cannot establish the measurement-window local/bulk split. Nearly
constant fallback totals suggest preparation may contribute most, but that is an
unverified explanation. Its source maps reach only `J=10`: measured byte reductions
and increased instrumented RAM elapsed times do not establish scaling. Larger
populated maps and matched planning/latency measurements remain qualification
work, with configuration and RAM feasibility proposed separately. Neither a
proposed benchmark size nor a local-path hit becomes a correctness requirement.

New comparison records use one `map_plans` object whose `phase_order` is
`["preparation", "measurement"]`. Every scalar field is a phase pair; existing
array fields are pairs of arrays. This retains all cost, closure, fallback and
timing diagnostics without duplicating names or serializing another total. Each
phase includes its tail checkpoints/maintenance; verification disables observers.
Actual slot writes, the two-flush protocol and monotonic adapter flush counts
independently check attribution without fixing publication counts. Historical
combined records remain historical; new measurements do not retroactively split
them or qualify larger maps.

The [focused matched record](../../fs/docs/map-planning-measurements.md) preserves
two unchanged 40-case matrices before and after this follow-up. Pyxis submitted
bytes and reconstructed aggregate map counters agree across all samples; no write
savings are attributed to the lookup change. Mean primary RAM elapsed times are
0.06–1.35% higher, establishing no small-map speedup. At 256 files, preparation
accounts for 171 global fallbacks per case, versus zero or one in measurement
including checkpoints. The index's calculated work reduction is separate from
these observations and does not qualify larger populations or deployment.

### Sustained populated-map follow-up

After filesystem #22 and Pyxis #316 merged, the owner assigned a bounded
comparison extension and approved its matrix/resource estimate before
implementation. The extension uses the real shared core and the existing scoped
RAM/no-swap launcher, with no allocator, admission, reserve, memory-budget or
durability changes. This assignment qualifies observed costs for discussion;
it does not close task 7 or the writable-deployment gate.

The selected experiments hold 64 background files constant while varying their
total separately durable 4 KiB append history across 256, 2048 and 5120 blocks.
Fresh 1 GiB backends then receive three equal windows: 512 4 KiB appends, 512
1 KiB overwrites in a pre-existing 4 MiB target (alternating contained and
cross-block requests), or 128 create/write-4-KiB/close/rename/remove cycles.
Two serial repetitions compare individually durable Pyxis, ext4 and Btrfs.
These are configurable experiment parameters, not filesystem contracts or
deployment thresholds. No checkpoint divides the windows; preparation and final
fences remain explicit, and complete-history totals include setup and every
maintenance/synchronization write, including native verification/handoff/unmount.

The [sustained comparison contract](../../fs/docs/ram-validation.md#sustained-comparison)
records resource accounting and configurable commands. Independent byte mirrors
and namespace expectations verify confirmed results after the final boundary.
Healthy capacity refusal produces an explicitly incomplete prefix; I/O, stopped
health, OOM, lost tracing or exhausted execution budgets invalidate the case.
The existing maintained recovery and retained-payload campaigns remain separate.

The [sustained measurement record](../../fs/docs/sustained-map-measurements.md)
reaches source maps of 216 nodes, rather than the earlier ten-node maximum.
The largest append/overwrite histories submit about 867/887 MiB in Pyxis,
versus about 852/853 MiB in Btrfs, through all setup and trailing maintenance.
All 54 filesystem cases complete and verify, with identical Pyxis byte totals
across both repetitions. Smaller histories and compiler totals remain below Btrfs;
individual windows still expose exceptions. Append/overwrite allocation-map
emissions account for about 70–84% of window metadata traffic. Bulk construction
is uncommon in windows, and every observed local emission is cheaper than its
calculated bulk reference. Broad local closure, not fallback frequency alone,
remains the significant submitted-write cost.

Costs depend on history: the largest equal append windows grow from about
70 to 114 MiB, whereas overwrites can settle after earlier expansion. Compiler
window writes are non-monotonic with background size while planning time rises
substantially. Global validation/vector/admission work remains, and instrumented
planning intervals include adapter reads and optional reference counting.
These are RAM observations, not NVMe latency or a local worst-case theorem.

**Subsequently approved bounded improvement:** compare both immediate
clean neighbours using validated expanded-run occupancy before repair, including
any marked run bridged by the chosen neighbour. Keep a deterministic left tie
break, monotone marks, the fixed eligible-ID prefix and renewed accounting/seam
closure. Existing q/J/H resource and termination bounds can remain intact;
structural splits/merges, new placement, budgets and reserve/admission policy
stay outside the proposal. The measured redistribution marks justify investigating
this choice but did not establish that left preference caused them or predict
savings. After filesystem #23 / Pyxis #324 merged, the owner assigned this
correction, bounded repair/alternative-capacity diagnostics and the unchanged
matched histories through all maintenance. The selector scores the complete
expanded run's distance to `[leaves, 46*leaves]`, with left ties, before marking
one neighbour and ancestors and renewing accounting. Its fixed diagnostics do
not change arena layout or budgets. It does not remove global planning cost or
qualify deployment by itself.

Storage evidence separates member-cgroup peaks from independent tmpfs bounds.
Native backing/scratch end allocation exceeds the member peak in a recorded
case; native kernel/cache charging is not completely established. Do not use
that peak as whole-native-job RAM high-water evidence. The existing bounded
RAM/no-swap setup and configured budgets are unchanged, with conservative native
cache accounting retained. Further improvements or campaigns still need their
own assignment.

## Implemented occupancy-aware neighbour follow-up

After filesystem #23 and Pyxis #324 merged, the owner assigned the bounded
selector. [Filesystem #24](https://git.internal/PyxisOS/pyxis-fs/pulls/24)
publishes `4b1e81dfbc71d46ca287a93c69b7b83917a84f87`, which this parent pins. For the first failing
run, score both immediate clean neighbours against the complete expanded run,
including a marked run bridged through that neighbour; prefer fit/smaller deficit,
with left ties. Mark only the chosen leaf and ancestors and renew accounting and
seams. Source inventory, fixed eligible prefix and monotone marks retain the
existing termination/funding proof. No topology, packing, placement, reserve,
admission, caller-budget or runner-policy change is introduced.

The [matched record](../../fs/docs/neighbour-repair-measurements.md) retains the
before matrix, fresh before control, unchanged after histories, all maintenance,
repair diagnostics and bounded storage evidence. The results are mixed; this
intermediate selector is not a claim of uniformly lower traffic. All 54 after
cases complete and independently verify; paired Pyxis byte totals are identical.
The smallest overwrite improves 6.141%, the smallest append regresses 0.645%,
and the largest overwrite regresses 1.934%. Largest append/compiler totals improve
1.343%/0.438%, while their first/early windows become more expensive. The largest
append/overwrite remain above recorded Btrfs totals. Nearly all of the largest
regression is map-node traffic, including later history without new repairs.
Broad local
closure and population-dependent planning remain. Diagnostics describe observed
choices and costs, not an optimal alternative history. The maintained host suite
adds six deliberate fit/deficit/tie/bridging cases with independent candidate
coverage, immutable source/sharing checks and renewed self-accounting. Existing
recovery, retained-payload, authority and funded-cleanup coverage stays intact.
All 141 quick and six extended groups pass normally and with ASan/UBSan;
ordinary host tools and the complete shared archive with Pyxis freestanding/kernel
ABI flags compile. Local unprivileged `make -j16` builds and links Caelum with
this published pin through the existing read-only core subset; the writer remains
excluded. Task 7 and writable deployment remain open; no further mechanism is
assigned for implementation.

## Implemented bounded leaf-overflow split

After #327 merged, the owner accepted and assigned the
[bounded package](filesystem-overflow-split.md). Published
[filesystem #25](https://git.internal/PyxisOS/pyxis-fs/pulls/25), pinned at
`a250731`, implements it. The implementation offers one
extra leaf under an existing parent with room, only for the first failing run
when it overflows. It freezes the lowest-key eligible anchor, requires the
surplus to remain necessary/sufficient, packs evenly and renews self-accounting
and seams. There is no internal split, root growth, merge or cost gate.

Retired source count p remains separate from emitted n=p+1; the virtual node
uses output index p and catalog/root offsets use n. Final live J+1 must fit m.
The single seed restoration regenerates mutable accounting, claims, volume and
catalog state before split-disabled neighbour repair and explicit funded bulk.
The derived 2J-s+3 work bound and checked scratch layout fit existing envelopes.
Read, integrity, invalid-delta and guaranteed-storage failures remain errors;
ordinary refusal leaves the writer usable. Admission, reserves, placement, bulk
fill, memory ceilings and runner configuration are unchanged.

The [implemented proof](../../fs/docs/incremental-map.md) and
[matched measurements](../../fs/docs/overflow-split-measurements.md) record
source/retired/emitted inventory, selected/discarded trials, closure and fallback,
planning and complete-history writes through final maintenance. Tests preserve
independent retained payloads through replacement writes and slot attempts,
healthy-trace failure provenance and discarded-candidate catalog replay.
General structural solving, further improvements and deployment qualification
remain separate assignments; task 7 and writable deployment stay open.

All 54 unchanged after cases complete and independently verify. Complete-history
submitted bytes improve 20.373–41.378% in eight histories; the small compiler
history regresses 7.098%, exactly 1,579 extra map-node writes. Largest append,
overwrite and compiler totals fall 32.278%, 32.257% and 35.207%. Every Pyxis total
is below matched Btrfs in these samples, without implying a universal ratio or
broader acceptance. Preparation savings can hide more expensive windows; all
phase results and both repetitions remain in the record.

Largest append/overwrite map writes still account for 81.55%/80.30% of window
metadata, while planning occupies most elapsed RAM time. Full-parent skips and
history propagation limit this first solver; an immediate local-node win does
not establish a complete-history win. All 153 quick/six extended groups pass
normally and with ASan/UBSan. Unprivileged host builds and the complete Pyxis
freestanding archive compile. Strict RAM/no-swap/trace protection, unchanged
configured budgets and no fallback remain; no owned loops survive the matrix.
No further mechanism or campaign is assigned by these results.

## Proposed deployment comparisons and acceptance

**Agreed direction:** Btrfs-comparable total submitted-write costs on representative
matched workloads are an acceptable initial disk-deployment target. Lower costs,
including beating ext4, remain a longer-term goal. No universal amplification
ratio, numerical tolerance or new benchmark configuration is approved here.
Correctness and remaining recovery qualification are independent requirements.

The [carryover record](../../fs/docs/retirement-carryover-measurements.md) contains
two unchanged samples per revision. All eight recorded carryover Pyxis
rows are below recorded Btrfs totals, but exercise short histories and only
54–405 selected allocation records:

| Population | Case | Pyxis KiB | ext4 KiB | Btrfs KiB |
| ---: | --- | ---: | ---: | ---: |
| 32 | 64 × 4 KiB appends | 2,944 | 1,564 | 4,904 |
| 32 | 16 × 256 KiB appends | 4,732 | 4,444 | 5,708 |
| 32 | 32 small overwrites | 1,520 | 248–260 | 2,720 |
| 32 | 16 compiler histories | 3,056 | 1,772 | 5,864 |
| 256 | 64 × 4 KiB appends | 4,564 | 1,568 | 5,000 |
| 256 | 16 × 256 KiB appends | 5,208 | 4,448 | 5,804 |
| 256 | 32 small overwrites | 2,428 | 264 | 2,816 |
| 256 | 16 compiler histories | 5,236 | 1,776–1,848 | 5,896 |

Native columns show ranges across both after samples from the
[raw counters](../../fs/docs/measurements/retirement-carryover.json); Pyxis and
Btrfs reproduce these rows, while those two ext4 cases vary. The range is retained
rather than adopting the measurement narrative's abbreviated point totals.

Totals exclude preparation, include terminal Pyxis checkpoints/all maintenance
and native final synchronization/clean teardown. Native batch mode has weaker
intermediate durability and is separate. Pyxis preparation excludes formatter
traffic while native preparation includes it; their combined preparation totals
are not an equivalent operation comparison. Native recovery contracts differ;
RAM requests/timings do not establish NAND wear or NVMe performance.

Proposed later coverage, subject to owner agreement and RAM feasibility:

| Gap | Concrete comparison proposal |
| --- | --- |
| Sustained small writes | Fixed 256/512/1024-operation windows of individually durable 4 KiB appends and aligned/cross-block 1 KiB overwrites; at least two successive populated windows plus terminal fences. Report each window as well as totals. |
| Unrelated population and fragmentation | Proposed populations 32/256/1024 with matched target-file operations; vary unrelated population independently. Prepare matched create/delete/overwrite histories and report actual map records/nodes, extents and free-space fragmentation, not merely sparse image size. |
| Namespace churn and lifetime | Separate create, rename, requested replacement and unlink histories, larger directories, retained victims and final release. Match application observer lifetimes and include orphan cleanup, checkpoints and startup work. |
| Resource pressure | Near computed quota/reserve/profile/memory/generation boundaries, two maximum derived debt cohorts and cross-volume unions, including startup and terminal fences. Separate expected admission-refusal tests from comparisons that must complete. |

These proposed starting sizes do not establish representative scale by themselves.
Select populated source-tree and write-history cases from actual map/extent/node
populations, including cases approaching the admitted profile, after calculating
RAM feasibility. A large empty 64/256 GiB image is not population qualification.

The owner must choose the representative case set, steady-state windows,
interpretation/tolerance of “comparable,” exception policy and any read/latency/
space guardrails. A compiler aggregate win cannot hide a sustained-write loss.
Report each ratio and exception with matched operations/durability; report every
bulk trigger, frequency and byte cost. The proposed locality criterion is that
unrelated population must not cause routine linear map-write growth in those
representative steady-state cases. This criterion and its allowable exceptions
need review; it is not an already-approved threshold or worst-case theorem.

The existing small near-minimum fixtures do not qualify maximal debt/population.
Correctness acceptance additionally requires independently defined payloads and
namespace results, both retained payloads throughout replacement, no same-
publication reuse, corruption and sticky failure handling, and completion of
funded work. Safe ordinary refusal can pass its behavior test without completing
a requested comparison. Remaining core recovery coverage and later native writable
integration/host qualification are separate gates; performance cannot replace
them. Native integration remains its own milestone, including the recorded
kernel-stack prerequisite. Task 7 and writable
deployment stay open.

The original design authorized no additional campaign. The bounded sustained
follow-up above was subsequently assigned separately; further campaigns must
use the existing verified RAM-only/no-swap boundary and configured budgets, no
disk fallback or automatic increases. First account for RAM high-water backing
pages, source copies, independent payloads/extractions, bounded volatile logs,
build products, native preparation and traces. Filesystem frees do not release
tmpfs image pages; small payload/sparse geometry does not bound write traffic.
The original two-population/four-case matrix did not supply sustained cases.
The assigned extension supplies bounded histories, leaving larger profiles and
pressure campaigns for separate assignment. Trace loss/budget overflow or OOM
invalidates a measurement; a verified healthy refusal remains incomplete rather
than qualifying the requested history. Saved summaries and diagnostics remain
bounded.

**Review state:** the owner accepts the topology-preserving approach, explicit
funded bulk fallback and global closure as an intermediate limitation. Fixed
ascending IDs, neighbouring-leaf redistribution and cost/planning reporting are
accepted after #313 merged and assigned for this first implementation.
Deployment comparisons and the meaning of Btrfs-comparable still need separate
decisions and need not be settled to accept this intermediate optimisation.
General structural editing, bulk fill-policy changes and new placement schemes
remain outside the first step. Full structural editing needs its own reviewed
efficient self-accounting proof and separate implementation assignment.

## Assigned first correction: combined small-orphan cleanup

The owner accepted exactly the scope below. The
[complete transaction proof](../../fs/docs/small-orphan-cleanup.md) was established
before implementation, including tree repair, map/catalog/root closure,
accounting, generation funding and scratch. It tightens the conservative path
estimate below to 29 replacement volume nodes and 30 retirements including data;
the full plan fits existing limits without changing allocator, admission,
memory-ceiling or reserve policy. Other orphan shapes retain their existing path.

The correction and contract-focused tests are now in the pinned dependency.
[Matched RAM results](../../fs/docs/small-orphan-measurements.md) from two unchanged
40-case matrices per revision measure compiler totals, through all maintenance,
falling 7,744→6,528 KiB at 32 files (15.70%) and 15,708→12,756 KiB at 256 (18.79%).
Append/overwrite costs remain essentially unchanged. Task 7 and writable
deployment stay open. The original rationale, estimates and proof obligations
below distinguish the investigation from those measured results.

Combine final data cleanup with paired object/orphan deletion **only for an
unreferenced regular orphan with one inline one-block mapping and no
object-specific grants**. Avoid publishing the intermediate empty object.
Keep the existing funded path for other shapes. This is narrower than combining
arbitrary six-mapping or grant batches.

The recommendation follows the recorded compiler history: sixteen such files
currently take two orphan publications each. It targets a visible extra
transaction without changing durable public-call boundaries or the publisher.
Partial-block data COW is needed for preserved bytes; even hypothetically removing
all 184 KiB of overwrite data excess would address only 3.27% of the 256-file
total. Catalog paths are already incremental. Map replacement addresses the
larger population-sensitive cost; the incremental proposal above supplies a
first bounded self-accounting path, still requiring owner review.

The compiler counters contain 64 user, 32 orphan and 192 drain publications,
288 total, inferred from the two-flush protocol. Combining each final pair
would remove 16 orphan and 32 drain publications: **48 publications (16.67%)
and 96 flushes**, if all combined plans fit. This is a calculated transaction
opportunity, not a correctness assertion about every workload.

| Recorded population | Compiler total | Phase-average estimate of bytes removed |
| ---: | ---: | ---: |
| 32 | 7,744 KiB | `1024/2 + 4224/6 = 1216 KiB` (15.70%) |
| 256 | 15,708 KiB | `1920/2 + 9444/6 = 2534 KiB` (16.13%) |

These are **proxies, not measured or guaranteed net savings**. They assume removed
publications cost their phase average; combined paths, repairs, allocation and
later map history can also change the remaining writes. This historical estimate
still exceeds ext4 and leaves append/overwrite map costs unresolved. Exceeding
ext4 is no longer an initial deployment rejection criterion.

Before implementing, prove:

- The object is a valid parentless non-root orphan, no runtime references or
  grants remain, and the single mapping is removed completely. Stage paired
  deletions without an intermediate object update. Preserve independent namespace
  confirmation, final-release/startup semantics and failure provenance.
- The complete union of edits fits, including occupancy repair/root collapse.
  Two fixed-key deletions conservatively need at most 30 replacement volume
  nodes and 31 retirements including data, below V/D; verify complete deltas,
  map/catalog/root accounting and scratch fit as well. These path bounds alone
  are not the whole transaction proof. Preserve E/M and the deletion promise.
- Decrement live blocks, mappings and object count exactly once. Orphan work
  decreases by two: one data-block unit and one object/orphan-marker unit.
  Keep cleanup monotonicity. Retirements retain recovery charges. The existing
  conservative generation funding still covers the shorter sequence, with no
  new memory allocation on final release or funded drain.
- Determine eligibility from validated state before selecting the path. If an
  implementation speculates across multiple edits, it must restore the entire
  candidate before fallback; per-edit atomicity is insufficient. I/O, integrity
  or resource failure during funded work still stops the instance.
- Maintain both retained payloads before every maintenance slot attempt,
  including replacement writes. Use healthy traces for fault cuts and independent
  expected contents; do not freeze physical placement or new incidental counts.

## Delivery and follow-up choices

The combined cleanup and retirement carryover are delivered. The proposed next
sequence and owner decisions are in the incremental section above. Caller memory
policy, formatter/admission floors and separated population limits can be
independent focused proposals; none is changed here. Intermediate improvements
do not clear the deployment blocker.

The accepted cleanup assignment used the **existing** RAM-only comparison, not a new
runner: two serial baseline samples per revision using
`sudo python3 tests/ram_run.py --suite baseline` in the filesystem repository.
It runs the unchanged two-population/four-case matrix and native operation/batch
modes; no compiler-only filter exists. Use configured budgets and no fallback or
automatic increases. Match compiler, VM/tool versions, image/profile, prehistory,
cache policy and concurrency; record any unavoidable difference from the old
baseline. Run the maintained `check` and seed-1 `extended` suites for correctness.
The owner approved these runs in the subsequent cleanup assignment; their
results and verified storage boundary are recorded above. No additional campaign
is assigned here.

The accepted [bounded retirement-debt correction](filesystem-retirement-debt.md)
uses two derived retirement cohorts, multi-volume debt and a bounded union of
catalog paths, with revised map/workspace/memory/generation proofs and worked
recovery traces. The larger computed recovery minimum funds H+V already-reusable
blocks, so this conservative profile needs no pressure pre-drain. Healthy PENDING,
complete final orphan release with pending retirement, pool-wide checkpoint
fences under existing rights, I/O-free disposal and startup fences are accepted.
Individually durable completed operations, both retained states and the prohibition
on allocating same-publication frees remain requirements.

Two unchanged matched RAM matrices per revision measure compiler-history totals
through final checkpoints falling 6,528→3,056 KiB at 32 files (53.19%) and
12,756→5,236 KiB at 256 (58.95%). The
[measurement record](../../fs/docs/retirement-carryover-measurements.md) separates
preparation and active-publication phases, reports native contracts and bounded
RAM/no-swap evidence, and includes all eight Pyxis cases. Reclamation absorbed
into user/orphan metadata is still counted; the totals establish the savings.
Pyxis remains above the matched ext4 compiler totals of 1,772 and 1,776–1,848 KiB.
That comparison remains useful evidence, not the initial deployment gate.
Task 7 and writable deployment stay open pending the broader qualification above.
No allocator mechanism or subsequent
implementation is assigned by this correction.

Report total submitted block-write bytes through final synchronization and
maintenance/unmount, preparation separately, Pyxis data/metadata and
user/orphan/drain counters, publication/flush totals, final population/extent
counts and RAM elapsed observations. Check independent payload/namespace results
and storage-safety evidence. Trace loss/budget overflow invalidates measurement;
resource refusal is incomplete work, never a successful workload. Compare actual
bytes with the proxy, retain native durability distinctions, and do not infer
NVMe performance from RAM timings.

Durable redo or roll-forward would change recovery machinery and require a proof
that the current guarantees survive. Delaying durability across acknowledged
calls, overwriting protected data or dropping the older payload sooner would
change guarantees and require separate agreement. Neither recovery redesign nor
weaker guarantees are authorized follow-on implementation here.
