# Filesystem write-efficiency investigation

Status: research and proposals for review, 2026-10-02. No implementation,
admission change or additional experiment is authorized by this document.
[Task 7 and writable deployment](writable-filesystem-core.md#focused-tasks)
remain open. Preserve individually durable completed operations, both retained
states and the existing failure/recovery contract in the baseline proposals.

## Evidence and comparison

The code inspected is the current Pyxis pin, filesystem
[`dc63d62`](https://git.internal/PyxisOS/pyxis-fs/src/commit/dc63d62b81d7c6cca082eeb52842b84d8a1fa390),
whose tree equals filesystem main `87d2f20`. Parent main is `2da3e5f`.
The production findings in `/shared/pyxis-fs-contract-audit.md` still apply;
its older test findings were addressed by filesystem #18 / Pyxis #307.
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

## Current Pyxis writes

The following is inspection of
[`file.c`](https://git.internal/PyxisOS/pyxis-fs/src/commit/dc63d62b81d7c6cca082eeb52842b84d8a1fa390/core/file.c),
[`mutate.c`](https://git.internal/PyxisOS/pyxis-fs/src/commit/dc63d62b81d7c6cca082eeb52842b84d8a1fa390/core/mutate.c)
and [`writer.c`](https://git.internal/PyxisOS/pyxis-fs/src/commit/dc63d62b81d7c6cca082eeb52842b84d8a1fa390/core/writer.c).
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
[`plan.c` closure and arena](https://git.internal/PyxisOS/pyxis-fs/src/commit/dc63d62b81d7c6cca082eeb52842b84d8a1fa390/core/plan.c),
[`admit.c` capacity checks](https://git.internal/PyxisOS/pyxis-fs/src/commit/dc63d62b81d7c6cca082eeb52842b84d8a1fa390/core/admit.c)
and [namespace proof](writable-filesystem-core.md#agreed-removable-namespace-profile-byte-and-repair-proof).

| Topic | Genuine requirement / current proof dependency | Reconsiderable policy and proposal |
| --- | --- | --- |
| Map coalescing | Exact ordered partition; valid ownership, birth, retirement and charges; protected-state exclusion. Canonical records support `K = 1 + 2(E+M+D)` and the resulting `S(H)` record/workspace closure. | Maximal coalescing is a format policy. Preserve it in the first candidate. Compare a canonical incremental editor with an explicitly debt-bounded relaxed representation before choosing the map design. |
| Core memory | Finite caller-owned cap, checked arithmetic, charged allocations, refusal before admission; funded drains require no new allocations. | The enforced core-wide 1 GiB ceiling originated as tool policy, not a disk-format safety property. Propose caller-configured caps; keep host default and any host maximum separate. No default or budget increase is made here. |
| Reserve floors | Ordinary workspace must cover the proved batch envelope; recovery must cover the whole funded sequence. Persisted reservations still count against pool capacity. | Current admission inherits formatter floors of 1024 ordinary and 1024 unused migration blocks. Propose deriving admission from `V+D` and `3H+V+D`, without a minimum for unused migration. Do not borrow existing migration reservations. |
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

Under the current proof `V=128`, `D=256`, hence ordinary workspace needs 384
blocks and recovery needs `3H+384`. These are proof results for this publisher,
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

## Allocator self-accounting: unresolved design choice

Retiring an old map node changes its allocation record, which can be in another
leaf. Replacing that leaf retires another node. This dependency can span the
whole map despite each path's depth bound. Contiguous **new** map storage does
not constrain old-node accounting placement.

Compare these mechanisms before assigning the map replacement:

1. Compute incremental closure with an explicitly bounded and pre-funded
   fallback. This can reduce common-case writes, but permits population-sized
   worst cases; decide whether and when that fallback is acceptable.
2. Restrict allocator-metadata placement to an accounting region whose own
   accounting closes within a proved bound. Mere contiguity is insufficient;
   explain admission/reimport of incompatible layouts.
3. Separately manage allocator-metadata storage with a concrete ledger/bitmap
   and retained-generation publication protocol. A COW ledger allocated through
   itself only moves the cycle. Account for spare capacity, pins and recovery;
   no new logging or storage architecture is selected here.

Canonical versus deferred coalescing is a second choice, not a solution to this
cycle. Re-derive sequence-wide `H`, `S`, retirement, generation and memory funding
from the selected mechanism. The current two-drain / three-generation envelope
is a linked proof assumption across preparation, admission and execution; change
all of it coherently if the publication sequence changes.

## Recommended first implementation candidate, pending assignment

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
larger population-sensitive cost, but lacks the self-accounting proof above.

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
later map history can also change the remaining writes. This improvement alone
would still miss ext4 and leave append/overwrite map costs unresolved.

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

## Delivery and proposed measurement

Proposed sequence, each implementation requiring explicit assignment:

1. Review this candidate and the unresolved policy/mechanism choices on the PR.
2. If assigned, land the narrow combined cleanup with maintained behavior,
   failure, older-payload and near-minimum-resource coverage; no admission changes.
3. Review self-accounting and canonical/debt-bounded coalescing alternatives with
   revised proofs and write-cost budgets before assigning incremental-map code.
   Caller memory policy, formatter/admission floors and separated population
   limits can be independent focused proposals; none is silently changed here.
4. Land separately measured map/maintenance corrections, then repeat acceptance
   against agreed budgets. Intermediate improvements do not clear the blocker.

For the cleanup change, propose the **existing** RAM-only comparison, not a new
runner: two serial baseline samples per revision using
`sudo python3 tests/ram_run.py --suite baseline` in the filesystem repository.
It runs the unchanged two-population/four-case matrix and native operation/batch
modes; no compiler-only filter exists. Use configured budgets and no fallback or
automatic increases. Match compiler, VM/tool versions, image/profile, prehistory,
cache policy and concurrency; record any unavoidable difference from the old
baseline. Run the maintained `check` and seed-1 `extended` suites for correctness.
These additional runs are a proposal for a subsequent assignment, not performed
or approved by this investigation.

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
