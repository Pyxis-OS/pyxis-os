# Writable filesystem core and recovery

Status: task-1 design accepted, 2026-10-01, against `pyxis-fs` commit
`82cc242b3d9773d21c0f7e7a71ec9ca9ccb937ed`. The policy, concrete mechanisms,
bounds and validation plan below are accepted as the implementation contract.
Task 2 now implements private COW tree/map planners, canonical/orphan codecs and
checker support, formatter namespace packing, and the initial maintained Unity
suite in the pinned `pyxis-fs` dependency. Tasks 3/4 add explicit writable opening,
retained summaries, admission, ordered publication, synchronous funded drain and
simulated durable recovery. Task 5 adds public regular-file creation, writing and
resizing, live directory continuations, and the corresponding healthy host commands.
Task 6 adds directory creation, removal, same-volume regular-file rename/replacement,
retained object authority and funded orphan cleanup on final release and durable
reopening. Combined extended validation remains task 7; native writable integration
remains a separate milestone.
Implementation proceeds one explicitly assigned task at a time.

Design navigation: [live interfaces](#agreed-live-interfaces-and-reference-ownership),
[persistent additions](#agreed-persistent-additions-and-supported-media),
[admission proof](#agreed-writable-profile-and-admission-proof),
[namespace proof](#agreed-removable-namespace-profile-byte-and-repair-proof),
[host writer and recovery limit](#agreed-ordinary-host-writer-and-deferred-recovery),
[host tests](#agreed-maintained-host-tests-and-ci),
[failure model](#agreed-host-failure-validation-model) and
[delivery boundaries](#agreed-delivery-boundaries-and-design-acceptance). Task 1 is
complete; tasks 2–5 are delivered by the dependency and pin PRs below.

The revised requirements include bounded multi-block writes, deletion capacity
protected from ordinary growth, durable-state recovery evidence after writeback
errors, and resource-complete funded drains. Whole-map rebuilding for every
publication is accepted only as the first correctness implementation. Maintained
host tests, the Unity setup and a per-filesystem-PR CI merge gate are agreed, as
are the stronger namespace profile and tighter deletion reservation. The ordinary
host writer uses the healthy-session contract below; real-host post-error recovery
qualification is deferred.
No E/M default or finalized allocation strategy is accepted.

This is the first of two storage milestones. Its result is a shared core that
mutates existing Pyxis volumes through bounded COW transactions, durably
publishes them, reopens them and safely reclaims storage. The following
[native persistent volumes milestone](native-persistent-volumes.md) delivers
the visible edit, sync, reboot and read-back workflow in Caelum. FUSE is a later
adapter, not a prerequisite for either milestone.

Read the [agreed storage direction](persistent-storage.md),
[identity and creation policy](users-and-authority.md),
[implemented read-only core](../devices/filesystem-readonly.md) and authoritative
[format](../../fs/docs/format.md) and [core contracts](../../fs/docs/core.md).
Preserve their decisions rather than introducing a second filesystem model.

## Agreed boundary

- One exclusive, serial writer per pool, using the existing one-device format.
  No independent host or guest instance may access changing media concurrently.
- Operate on existing formatter-created volumes. Their guarantees, quotas and
  ordinary/recovery/migration reserve budgets remain enforced. Volume growth and
  shrink are allocation and reclamation within the pool, not partition movement.
  Runtime volume creation, policy administration and format migration are deferred.
- File creation, bounded writes and resize; directory creation and removal;
  regular-file removal and same-volume rename/replacement. Directory moves,
  cross-volume moves and public snapshots are outside this first writer.
- Correct fragmented extents, partial-block updates and zero-filled sparse
  growth. No defragmentation requirement or whole-file rewrite for a small edit.
- Stable identities, parent-controlled creation ownership, held authority and
  destination subtree exposure follow the agreed policy. Ownership alone does
  not grant access. Copies get new identities; rename preserves identity.
- Implement in `pyxis-fs`, shared with its host adapter and later Caelum adapter.
  No FUSE, installer, NVMe driver, authentication service or background writeback.

## Agreed initial execution model

Each admitted bounded mutation is committed before successful acknowledgement.
This stronger initial behavior fits the existing public promise that ordinary
write success need not imply durability; applications should still explicitly
sync when durability matters. Large writes may span transactions and report
committed partial progress. Combine compatible slices within one request into
bounded multi-block transactions when their actual plan fits all admitted limits.
Do not delay durability or batch work across completed calls. Large shrinks likewise publish successive bounded
steps and report the last confirmed length, as specified below. Do not promise
whole-write or whole-shrink atomicity or acknowledge an uncertain publication
as successful.

Publication remains pool-wide and serialized. A volume checkpoint covers all
accepted changes in that volume before its ordering point; sibling-volume
changes may be included. Close is not a substitute for checkpointing. The
progress and failure contract below separates operation outcomes from pool
health; the specified live interface below gives those facts separate fields.

Live object handles retain identity and granted authority while observing the
latest committed state. They do not permanently pin the generation at open.
Each operation protects the state it uses. The enumeration and diagnostic-view
contract below keeps ordinary operations live and immutable diagnostics exclusive
to read-only instances; neither permits independent opens of changing media.

Removal leaves retained object handles usable. Storage remains charged while an
object is unlinked but retained. Persistent orphan bookkeeping must let restart
reclaim objects whose runtime owners no longer exist, without treating a named
or otherwise protected object as garbage. The retained-file and directory
behavior and orphan-index lifecycle below are agreed; the specified mechanisms
below specify encoding, reference accounting and workspace bounds.

Writable opening requires fully understood, validated media. Refuse writable
access to degraded or unsupported retained states; keep read-only inspection
available under its existing contract. A full validation pass at writable open
is acceptable initially. It is not a requirement to scan the entire pool for
every mutation or reclaim batch. Refusal is not permission to repair or discard
a damaged state automatically.

## Agreed mutation rights and retained files

| Operation | Required held authority |
| --- | --- |
| Write within the existing file length | `file.write` on that file |
| Write that extends the file | Both `file.write` and `file.resize` on that file |
| Explicitly grow or shrink a file | `file.resize` on that file |
| Create a file or directory | `dir.create` on the parent |
| Remove a file or empty directory | `dir.remove` on the parent |
| Rename a regular file within its volume | Source-parent `dir.remove` and destination-parent `dir.create` |
| Replace an existing destination during rename | Rename authority above, plus destination-parent `dir.replace` |

Replacement also requires the caller's explicit `replace=true` request; merely
holding `dir.replace` does not request displacement.

Directory operations act on component names through already-held parent handles.
Obtaining those handles through path traversal requires normal lookup authority.
Removing or replacing an entry does not require read or write rights on the
affected file. No operation gains authority merely from recorded ownership.

Write authority alone permits overwriting existing bytes, not extending the
file. Check the requested range against the current committed length before
the first write transaction: if the request would extend the file and resize
authority is absent, reject the entire request without modifying its in-range
prefix. Explicit growth exposes zero-filled space; shrinking requires no write
permission. Authorized writes still obey bounded transactions, admission and
the confirmed-progress/failure contract below.

Creation by itself produces an empty object under the parent's creation policy;
it does not implicitly grant a child handle or read, write or administration
rights. Creating and returning a child handle additionally requires subtree
scope, `dir.lookup` on the parent, and every requested child right within that
parent handle's held authority. Check those requirements and reserve resources
for the returned handle before publication. Do not create the entry first and
then discover that the requested child authority cannot be returned. Publication
and cleanup failures retain the agreed outcome semantics below.

Removing or replacing a regular file preserves existing handles to the old
identity and their granted read, write and resize rights. Those handles observe
the object's latest committed state, subject to ordinary quota, admission and
pool-health checks; storage remains charged while the unlinked file is retained.
Fresh ordinary acquisition by path or object ID cannot recover the unlinked
object. Permitted delegation of an existing handle remains possible without
enlarging its rights. Reusing its former name does not retarget retained handles.

## Agreed partial large shrinks

Shrink a large file through successive bounded transactions from
the tail toward the requested length. Each transaction publishes a valid shorter
file and retires only the storage covered by its admission bound. Bound both
metadata edits and physical blocks retired: one extent can describe far more
data than a transaction can charge to workspace. Sparse ranges need no data
retirement, but still obey the metadata and publication bounds.

A successful resize reaches exactly the requested length. If a later step fails,
return the last confirmed committed length, operation outcome and pool health
independently. Earlier shrink steps remain committed; there is no rollback to
the original length. Stop at the first failed or uncertain step. An uncertain
publication may have committed a further reduction beyond the confirmed length;
it is not automatically retryable, and ordinary access stops until recovery.
Post-commit maintenance failure preserves the confirmed length and follows the
existing cleanup/read-availability rules. A crash can leave any committed
intermediate length. A shrink that fits one admitted transaction has one
publication boundary.

Every intermediate state must satisfy the existing file-range contract: extent
mappings fit the published length rounded up to blocks. Do not publish the final
short length while leaving out-of-range mappings attached for later cleanup.
Bytes discarded from a retained partial block must not reappear on later growth;
growth remains zero-filled. Retained handles observe the latest committed length
under the existing live-handle contract.

This agreement introduces no persistent detached-tail representation for atomic
large shrinks. The orphan index remains bookkeeping for unlinked objects, not
for a named file's discarded tail.

## Agreed checkpoint authority

Require `file.checkpoint` through a file handle and an
explicit `dir.checkpoint` through a directory handle. Both request a checkpoint
of the containing volume, covering accepted changes before its ordering point;
pool-wide publication may include sibling-volume changes. This confers no access
to other objects or their contents.

Checkpoint rights grant no read, lookup, write, resize or namespace-mutation
authority, and those rights do not implicitly grant checkpoint authority. A held
directory handle needs neither subtree scope nor lookup rights to request its
checkpoint. Retained unlinked files and detached directories can still request
one when their handles hold the respective right. Ordinary pool-health rules
apply: checkpointing fails while mutation is stopped and cannot clear an
uncertain outcome. A read-only inspection instance does not provide the writable
checkpoint operation.

Adding `dir.checkpoint` does not widen existing grants. Existing images may still
authorize checkpoints through their `file.checkpoint` grants; the absent
directory bit grants nothing. New formatter-created root grants may explicitly
include the new directory right. Do not silently rewrite old grants on open.

The authoritative [unknown-rights rule](../../fs/docs/format.md#rights-and-trusted-acquisition)
requires rejection of unsupported grant bits, not a read-required feature for
every new independent allow right. Add no feature declaration or structure-version
bump solely for `dir.checkpoint`. Existing decoders reject an encountered unknown
grant bit as `CORRUPT`; opening alone does not inspect every grant. The corrected
rule makes no compatibility promise to those older implementations. It does not
change the separate orphan-format requirement below.

The subsequent native persistence milestone must explicitly map the settled
core rights to OS grants rather than import the current native interface's
bundled permissions.

## Agreed directory removal and name reuse

- Removal requires an empty directory. The volume root cannot be removed.
  Successful removal detaches the directory from its parent namespace.
- Retained handles keep the detached directory's identity and granted rights.
  Metadata and empty listing remain usable when the handle holds the respective
  rights. Creating or moving entries into the detached directory is rejected;
  it cannot acquire new children through a retained handle.
- Creating a directory at the former name creates a new object with a fresh
  identity under the parent's creation policy. Existing handles never retarget
  to the new object. Ordinary lookup of that name resolves the new directory.
- Authority held specifically on the removed directory does not transfer to its
  replacement. A held parent-subtree capability may derive authority to the new
  directory through the usual lookup and rights checks.
- The detached directory remains charged to its volume while retained. After
  its last handle closes, it becomes eligible for reclamation subject to the
  retained-root and operation/I/O protections. Recreating the name neither
  prolongs the old object's lifetime nor couples it to the new object's lifetime.

For example, opening `home://notes` retains directory A. Removing that empty
directory and creating `home://notes` again produces directory B. The old handle
still observes empty A and cannot insert children; a new lookup observes B.

The existing format requires a read-required feature and explicit orphan-root
semantics before unlinked objects can persist; these directory rules do not by
themselves define that encoding.

## Agreed persistent orphan lifecycle

Use a
per-volume orphan index keyed by object ID. Entries retain objects in the
volume's existing object index; this is accounting and recovery metadata, not
a hidden directory that applications can traverse.

Unlink removes the naming entry, clears the object's parent association and
records the orphan in one transaction. Rename/replacement does the same for
the displaced destination while atomically publishing the source's new name.
Always record the orphan, including when no runtime handle remains: deleting
a large file must not require freeing all its extents before the bounded
namespace transaction can complete.

Retained handles keep the object alive with their agreed rights, and its storage
remains charged to the volume. After the last handle and operation/I/O reference
ends, cleanup removes the object's remaining data mappings and grants through
bounded transactions. Keep the orphan entry and object record until final
removal can publish them together. Every intermediate committed state must be
valid and resumable; failure follows the agreed publication and pool-health
rules. Removed blocks become retired and follow ordinary retained-root
reclamation. Deleting the orphan record does not itself make those blocks
reusable or end protection by an older retained state.

Validation must establish separately for each retained committed state:

- Every non-root object is either named exactly once with the matching parent,
  or present exactly once in the orphan index with no naming entry or parent
  association. It cannot be both named and orphaned in that state.
- Every orphan entry identifies an existing object in the same volume.
- Orphan directories are empty, and the volume root is never an orphan.
- An unreachable object without an orphan entry is corruption, not permission
  to delete it.

After the adapter establishes the appropriate ordinary-open or durable-recovery
boundary, writable reopening first validates both retained states, then drains abandoned
orphans from the selected state in bounded transactions before exposing the
writable instance. Old runtime handles do not survive the quiesced close/reopen
boundary; retained on-disk states still protect storage under the usual rules.
At a qualified durable recovery boundary, an interruption resumes from the last
committed state. Insufficient recovery workspace at opening admission causes
refusal, not unsafe deletion; exhaustion after admission is an invariant failure.
Degraded or unsupported
retained states do not authorize cleanup. Read-only inspection reports orphans
without cleaning them up.

Opening may take substantial time after many removals or a large abandoned file.
This first writer completes abandoned-orphan cleanup before applications gain
access, avoiding concurrent startup cleanup. Exact record encoding, per-batch
work bounds and admission costs are specified below; existing reserve defaults
do not establish writable sufficiency.

## Agreed live enumeration and diagnostic views

Each directory listing page observes one committed state. Continuations retain
no old generation or storage references between calls. Validate their directory
binding and freshness before interpreting a position in the current tree.

| Event between listing calls | Continuation behavior |
| --- | --- |
| Entry created, removed, renamed or replaced in this directory | Return `CHANGED`; require an explicit restart from the beginning. |
| File contents change without changing its directory entry | Continue normally. |
| Another directory or sibling volume changes | Continue normally. |
| Checkpoint or reclamation leaves this directory's entries unchanged | Continue normally. |
| This directory is removed | Return `CHANGED` for the old enumeration; restarting through its retained handle lists the empty detached directory. |
| Publication becomes uncertain | Apply the pool access-stop rule; do not report the failure as `CHANGED`. |

Invalidation is directory-local, not tied to every pool generation. Entry
replacement invalidates enumeration even when the visible name and kind stay the
same. A stale continuation returns no entries and does not advance. The caller
must explicitly restart with the initial continuation; the core and adapters do
not silently restart and combine pages from different attempts. End-of-directory
is repeatable only while the directory remains unchanged. A listing over several
calls is not a snapshot, and continuous modification may cause repeated restarts.
Ordinary rights and the agreed pool read-availability rules apply to every page.

Existing immutable diagnostic views and cursors remain available only through
read-only instances. Close the writable instance and all its handles before
opening the inspector or an immutable diagnostic cursor; close read-only
instances and their retained views before opening a writer. There are no
externally retained immutable diagnostic snapshots inside a writable instance.
Writable-open validation and bounded internal checks run within the writer's
serialized operations and do not expose retained diagnostic views. This keeps a
forgotten diagnostic cursor from indefinitely pinning old generations and COW
workspace. The initial limitation is that full offline inspection interrupts
writable access; concurrent diagnostic snapshots would require a separately
agreed retention and admission policy before introduction.

The specified live continuation below supplies binding and freshness. The existing
immutable tree-position token cannot be used against a changing tree unchecked.
The native ABI mapping belongs to the subsequent milestone.

## Agreed progress, failures and read availability

An operation result preserves three independent facts:

- Confirmed progress: for a write, the contiguous byte prefix whose transactions
  completed both required flushes. No byte in an uncertain transaction contributes
  to that count. For a shrink, the last confirmed committed length, excluding any
  uncertain step. Namespace operations report whether their transaction is
  confirmed committed.
- Operation completion: complete, stopped with a known failure, or outcome
  unknown for the transaction being published. Preserve the failure cause.
- Pool health and any maintenance failure: whether ordinary access and mutation
  remain available. Cleanup failure cannot erase confirmed progress or turn an
  already confirmed user transaction into an uncertain one.

A large write stops at its first failed or uncertain transaction. Earlier
transactions remain confirmed; later transactions are not attempted. An unknown
outcome may include additional committed bytes beyond the confirmed prefix.
Even zero confirmed bytes does not establish that nothing changed. The core and
adapters must not automatically retry an uncertain mutation, including just the
unconfirmed suffix; recovery and reconciliation must establish what happened
before the caller decides its next operation.

The publication uncertainty window starts when the older superblock-slot write
is attempted and ends only when the following flush succeeds. A failed or short
slot write, or failure of that final flush, is uncertain. Failure of replacement
writes or their first flush, before any slot write is attempted, cannot publish
that candidate. It may leave unreachable bytes in previously free storage.

| Failure point | Operation outcome | Pool access afterward |
| --- | --- | --- |
| Ordinary permission, quota, workspace or memory refusal before admission | Current transaction not committed; retain any earlier confirmed progress | Reads and later mutations remain available. |
| Replacement write or first flush fails before slot publication | Current transaction not committed; retain any earlier confirmed progress | Stop mutation until recovery. Reads, metadata, listing and lookup may use the last confirmed state while its integrity remains established. |
| Slot write or final flush has an uncertain outcome | Current transaction unknown; retain any earlier confirmed progress | Stop all ordinary access until recovery, including reads, metadata, listing, lookup, new acquisition and derivation through existing handles. |
| Cleanup replacement write or first flush fails before its own slot publication | User commit remains confirmed; report cleanup failure separately | A cleanup write/flush failure stops mutation until recovery. Reads may use the last confirmed state, including the confirmed user commit, while its integrity remains established. |
| Any backing read fails during an ordinary operation, publication planning or maintenance | Current publication is not confirmed; retain all earlier confirmed user and cleanup progress | Enter `ACCESS_STOPPED` pool-wide, even for a transient read error. |
| Unexpected resource exhaustion during an admitted drain | Preserve confirmed user and cleanup progress; report an admission/editor invariant failure | Enter `READABLE_STOPPED`; escalate to `ACCESS_STOPPED` if integrity is no longer established or publication is uncertain. No maintenance retry in this instance. |
| Cleanup's own slot publication becomes uncertain | User commit remains confirmed; cleanup outcome is unknown | Stop all ordinary access until recovery, even if cleanup was intended to change only allocation bookkeeping. |

An ordinary mutation may be refused before admission. Once a batch is admitted,
its reserved drain must finish without additional application mutations, absent
I/O or integrity failure. Unexpected space, memory, record-capacity or generation
exhaustion during that drain is a failed admission/editor invariant. Defensive
reporting must preserve confirmed progress, keep storage protected and enter
`READABLE_STOPPED`; escalate to `ACCESS_STOPPED` if integrity is no longer
established or publication is uncertain. That instance cannot retry maintenance,
checkpoint or a mutation back to health.
A safe refusal at that point is still a validation failure, not successful
pressure handling. Final orphan release has the same funded-progress requirement;
a live retained handle may defer release, resource exhaustion may not.

These access restrictions are pool-wide, including sibling volumes. In the
readable but mutation-stopped state, ordinary rights still apply, no read uses
the unpublished candidate, and no operation advances retained roots or frees or
reuses blocks. A checkpoint fails while mutation is stopped; it cannot clear the
failure or promise renewed storage health. The initial writer uses the accepted
conservative read-error policy: any backing read failure during ordinary access,
publication planning or maintenance enters `ACCESS_STOPPED`, even if transient.
A fresh validated reopen under the existing adapter recovery preconditions is
required; clearing the backing error does not restore the failed instance.
Integrity failure also stops ordinary access; do not serve an unproved state or
switch generations under existing handles.

After uncertain publication, even cached object data is unavailable through
ordinary operations. In-memory outcome/health reporting and handle closure remain
available. Closing releases runtime resources without performing recovery writes
or retrying failed cleanup; persistent orphan bookkeeping must survive that
release. Recovery requires quiescing outstanding I/O, closing the instance,
establishing the adapter's durable-state recovery boundary and reopening through
validation. Ordinary host close/reopen is not that boundary after writeback error.
No independent inspector may open changing media.
Reopening may refuse writable access to degraded or unsupported retained states;
read-only inspection keeps its existing contract. Neither close nor reopen
silently repairs a damaged slot or discards a retained state.

For example, if a write commits its first chunk and the second chunk's final
flush fails, report the first chunk's bytes, an unknown outcome for the second,
and unavailable pool access. Recovery may find only the first chunk committed,
or both the first and second. If the entire write instead commits and a later
reclaim publication fails, report the full confirmed byte count and completed
write, together with the cleanup failure and resulting pool health. Never
collapse those facts into zero progress, an uncertain user write, or an
unqualified healthy success.

## Agreed synchronous maintenance and stopping condition

Use the [format publication/reclamation envelope](../../fs/docs/format.md#future-publication-and-reclamation-envelope).
Reclamation must progress without another application write. Internal
maintenance commits may advance the older retained slot while preserving the
current namespace and file contents.

| Step | Effect |
| --- | --- |
| Commit a user mutation | Replaced blocks become retired; the older root can still protect them. |
| Publish a maintenance generation | Advance the older slot without requiring another user mutation. |
| Publish a reclamation batch | Mark eligible retired ranges free after checking both retained roots and operation/I/O references. |
| Allocate in a later transaction | Reuse those ranges only after durable free-map publication and a fresh protection check. |

Maintenance obeys the ordinary COW accounting and two-flush publication protocol.
Its replacement allocation-map nodes and pool root come from storage already
proven reusable; it cannot allocate from ranges it is freeing in that same
publication. Its own replaced metadata is retired and protected normally.

Cleanup does not try to reach zero retired blocks, because replacing cleanup
metadata can itself retire more metadata. Complete maintenance once eligible
user-data and orphan cleanup is drained and the remaining retired maintenance
metadata fits a strictly bounded pool-wide remainder, fully charged to workspace.
That bound covers the accumulated remainder across successive maintenance cycles;
it is not another allowance added after each write. Ineligible storage remains
protected and accounted for, rather than being hidden in the maintenance
remainder. The admission proof below bounds repeated editing explicitly.

Use synchronous maintenance in this first writer:

- After each confirmed user or orphan-cleanup batch, drain that batch's retired
  volume storage before admitting the next batch. This applies between chunks
  of a large write or shrink as well as between separate calls.
- On the last release of an orphan, drain its cleanup through bounded
  transactions once handle and operation/I/O references have ended, even if no
  further user write follows.
- During writable reopening, drain abandoned orphans as already agreed.

Reserve the complete mutation-and-maintenance sequence before admitting a batch.
After its commit, end operation/I/O references protecting the replaced state,
advance the older retained root through maintenance, then separately publish
eligible storage free. Only a later batch may reuse that storage after fresh
protection checks. Do not admit another user or orphan-cleanup batch while the
previous batch's retired volume storage remains undrained. This does not require
deleting live unlinked objects retained by handles; their storage remains live
and charged until their references end.

Each publication also frees all eligible retired pool metadata, subject to the
same evidence and reference checks. The two maintenance publications replace
their own metadata and leave the bounded pool-wide remainder described above.
A batch needing both publications therefore performs six flushes in total:
two for its mutation and two for each maintenance publication. This is a
protocol count, not a latency measurement. Large writes, shrinks and orphan
cleanup repeat the sequence across batches. The first writer accepts this cost;
combining batches is a later optimization requiring an equivalent debt bound.

The admission proof below supplies the conditional `2H` remainder and
`3H` pool-workspace construction, permanent metadata allowance, record closure
and writable-opening checks. Existing percentage reserves alone prove none of
those requirements. Refuse images that cannot satisfy them; do not silently
change persisted reservations or assume normalization is affordable.

The last close of a large unlinked file may take substantial time: each
transaction is bounded, but the complete cleanup can require many transactions.
Closure releases the runtime reference even if cleanup fails; persistent orphan
state and committed cleanup progress remain recoverable. Confirmed user progress
and maintenance failure are reported independently. The existing pool-health
rules take precedence: stopped mutation or uncertain publication does not permit
cleanup writes during close or an automatic retry of failed maintenance. Closing
still does not substitute for an application-requested checkpoint.

An admitted drain either completes, stops for I/O/integrity failure under the
health table, or encounters a resource invariant failure and enters
`READABLE_STOPPED` (escalating to `ACCESS_STOPPED` when required above). Stop the
enclosing multi-batch operation before its next batch and preserve confirmed
progress. Ordinary refusal before admission leaves an otherwise healthy writer
usable; it is not a possible resource outcome of a correctly funded drain.
At a completed call boundary, a `READY` writer has no outstanding required drain;
the permitted pool-metadata retirement remainder is not such a drain. A stopped
instance may report pending drain diagnostically but cannot resume it. Leftover
retirement can be processed only by a fresh validated writable open after the
adapter/operator establishes the required backing precondition, never by retrying
maintenance in the stopped instance or admitting another application mutation.

## Agreed whole-map fallback

Provide a
bounded whole-allocation-map rebuild when incremental map edits cannot close
within their admitted bound. A tree-depth bound alone does not bound allocator
self-accounting: retiring one old map node changes the leaf describing that
block, whose replacement can require another leaf to change. Valid physical
layouts can extend this dependency through every map leaf. Contiguous allocation
of new nodes does not eliminate the dependency through old nodes.

The fallback constructs a complete candidate map from bounded, validated
allocation intervals. It does not rewrite file data or require a whole-filesystem
scan for each transaction. Allocate its replacement map nodes, changed catalog
path and pool root from storage already proven reusable. Include every new
allocation and old retirement in the candidate; allocate nothing from storage
being freed in that same publication. Plan the map shape before allocating its
nodes, so accounting for those nodes cannot cause unbounded iterative growth.
All allocated nodes must be reachable; unused live padding is not permitted.

The following construction establishes a finite per-publication envelope for
a maintenance rebuild with at most one fixed-size volume-catalog update:

- Form a canonical base map after recording old map/root/catalog-path retirements
  and eligible frees, but before allocating replacement pool metadata. Let `R`
  be its record count and `C <= 8` the replacement catalog-path block count.
  The base has no live pool allocation born in the candidate generation.
- The existing 4 KiB layout fits 46 allocation records per leaf and 65 numeric
  child references per internal node. Choose
  `S = ceil(23 * (R + 2*C + 4) / 21)` and `L = ceil(S / 46)` leaves.
  Let `F(S)` count those leaves and their internal levels, with each next level
  using `ceil(previous / 65)` nodes, stopping at one root. Redistribute children
  as needed to avoid a single-child internal node.
- Allocate exactly `N = F(S) + C + 1` blocks, including the new pool root.
  Even arbitrarily fragmented placement adds at most `2*N` map records.
  Candidate-generation births prevent new pool allocations from merging across
  old non-free boundaries, so the final count is between `R` and `R + 2*N`.
  Since `F(S) <= 2*L - 1 <= ceil(S / 23)`, the chosen `S` bounds that final
  count. Also `L <= R`, so records can fill every planned leaf without empty
  nodes; underfilled nonempty leaves are valid in the current format.

For example, `R = 4096` and `C = 8` give `S = 4508`, 98 leaves, three internal
nodes and `N = 110` replacement blocks: 440 KiB of new pool metadata. This is
layout arithmetic, not a measured implementation cost or a total reserve minimum.
It excludes new volume metadata/data, accumulated retired storage and the
additional publications needed for retained-root advancement and safe reuse.

Admission checks record/depth limits, planning memory, permanent metadata growth,
workspace and old sparse maps under the specified profile below. The one-publication
construction alone does not prove reserve sufficiency. No format change follows
merely from using this fallback.

Whole-map rebuilding for every publication is accepted as the first correctness
implementation, not the finalized allocation strategy or desired performance.
Its pool-wide metadata writes scale with map fragmentation even for a small data
edit; synchronous drain can rebuild the map three times for one user batch. The
example above would write about 1.29 MiB of pool metadata over three comparable
publications for a 4 KiB edit (330 metadata bytes per useful data byte), before
volume metadata and slots. That is calculated amplification, not a measurement.
Measure metadata bytes written per useful data byte, latency and throughput on
the populated workloads below, distinguishing user publication from drain costs.
Revisit incremental editing/allocation strategy after correctness, retaining a
bounded fallback and equivalent admission guarantees.

## Agreed live interfaces and reference ownership

These mechanisms are accepted design, not implemented behavior. Give `pfs_pool` internal
state an explicit read-only or writer mode, selected by separate open calls.
Keep its caller-owned handle and the opaque `pfs_view` lifetime rules. Ordinary
`pfs_view` handles use immutable or live behavior
according to that mode, with shared grant evaluation. Immutable diagnostic
objects/cursors remain restricted to read-only pools.
The trusted adapter supplies exclusive backing ownership, exact read/write/flush
callbacks, strong random bytes for new IDs, and the existing capped memory owner.
Establishing a durable recovery boundary is an adapter/operator precondition,
not a fact the core can infer from valid bytes and successful callbacks. There is
no new recovery-history flag or open-time declaration. The read-only interface
still contains no write callback.

The initial live operation surface is acquisition by trusted context/path/ID,
held lookup and delegation, metadata/read/list, create, write, resize, remove,
regular-file rename/replacement, checkpoint, status and close. Namespace mutation
accepts held parent views plus validated single component names. Rename also
takes an explicit `replace` boolean, false by default; authority alone does not
request replacement. Creation takes kind and optional requested child authority; new ownership defaults to the
parent's policy owner, with no new explicit grants. Preallocate a requested
returned view before publication. Generate object IDs with strong randomness,
reject zero and collisions in both retained states and retained runtime objects,
and stop after the existing 16-attempt bound. No fallback identity source.

All calls are serialized by the embedding adapter. Core entry rejects reentry
with `BUSY`; callbacks cannot call back into the instance. A view owns an immutable
rights/scope copy and a reference to a shared runtime object entry keyed by volume
and object ID. That entry counts views and active object operations. Operations
borrow the selected generation; synchronous I/O returns all borrowed buffers before
its call returns. End old-generation references before maintenance; retaining a
view does not retain a generation. No asynchronous I/O or concurrent operations
are introduced. A future concurrent adapter must supply an equivalent protocol
before enabling concurrency.

An accepted view close consumes the view and clears the caller's pointer even if
orphan cleanup fails. Its result has `released=true`, cleanup outcome/status and
pool health; `BUSY` leaves the view live with `released=false`. Retrying that
close is not a recovery mechanism. Closing while access is stopped only releases
runtime state. Writer close requires all volume handles, child views and active
calls to end,
otherwise returns `BUSY`; it performs no repair or implicit checkpoint. The
backing adapter outlives all core objects and releases its exclusive ownership
only after the writer and outstanding callbacks are gone.

### Results and status

Every valid mutation call initializes a result with these separate fields:

| Field | Meaning |
| --- | --- |
| `completion` | `COMPLETE` for the request, `STOPPED` for a known stop, or `UNKNOWN` for an attempted user publication. |
| `operation_status` | Original operation failure cause, or `OK`; it never becomes a cleanup error. |
| `confirmed_bytes` | Committed contiguous prefix of this write request; zero for other operation kinds. |
| `confirmed_length_valid`, `confirmed_length` | Last confirmed file length for resize, initialized from the starting committed state. |
| `namespace_confirmed` | True only when this call's namespace transaction completed both flushes. False means not confirmed, including `UNKNOWN` outcomes that may have committed; it does not establish that nothing changed. |
| `maintenance_completion`, `maintenance_status` | Separate `NONE`, `COMPLETE`, `STOPPED` or `UNKNOWN` cleanup outcome and cause. |
| `health` | `READY`, `READABLE_STOPPED` or `ACCESS_STOPPED`, copied from the writer after the call. |

A completed user operation remains `COMPLETE` if its cleanup fails. For a larger
request with more chunks outstanding, a drain failure stops the request with
its confirmed prefix/length intact; the failure cause lives in maintenance fields.
A call's scalar status returns its operation error first, otherwise its maintenance
error, otherwise `OK`. Callers must consume the result even on error. Invalid API
arguments return `INVALID` without changing the result or media. Creation can
return its preallocated view after a confirmed commit even when later cleanup
fails; under `ACCESS_STOPPED` that view supports closure only. Unknown creation
returns no usable child view and must be reconciled after recovery.

Add specific statuses for `EXISTS`, `NOT_EMPTY`, `DETACHED`, `CHANGED`, `NO_SPACE`,
`QUOTA` and `RECOVERY_REQUIRED`. Insertion through a detached parent returns
`DETACHED` after checking authority. `LIMIT` continues to identify profile/count/depth/memory
cap exhaustion; `NO_MEMORY` identifies allocation failure below the cap.
`NO_SPACE` includes insufficient admitted workspace or permanent pool headroom.
Mutation in `READABLE_STOPPED`, and ordinary access in `ACCESS_STOPPED`, returns
`RECOVERY_REQUIRED`. A status query copies in-memory health and last failure
without touching media. Pending drain is diagnostic in a stopped instance, not
permission to resume maintenance; `READY` has no required drain pending at a
completed call boundary. Ordinary view results disclose no sibling identities or pool generation. Health is sticky
until a fresh validated reopen satisfying the adapter/operator precondition; no
call clears it by retrying. Loss of established integrity escalates to `ACCESS_STOPPED`.

Check rights before inspecting otherwise unauthorized names or doing any mutation.
Create at an existing name returns `EXISTS`. Removing a nonempty directory returns
`NOT_EMPTY`; the volume root cannot be removed. Rename accepts regular files only,
with either absent or regular-file destination. Same-parent/same-name rename is a
no-op after validating the source and source-remove/destination-create authority.
For a distinct existing regular-file destination, `replace=false` returns `EXISTS`
without mutation, even if the caller holds `dir.replace`. With `replace=true`,
displacing that destination additionally requires `dir.replace`; missing authority
returns `DENIED`. An absent destination needs only
ordinary rename authority, irrespective of `replace`. The same-name no-op needs
no replacement authority. Other directory moves/replacements return `UNSUPPORTED`.
Zero-length writes and same-length resizes are no-ops after argument, authority and health checks, and do
not advance generation or directory change tracking. No-op namespace success has
`namespace_confirmed=false` with `completion=COMPLETE`, since it publishes no
transaction. By contrast, `completion=UNKNOWN` with `namespace_confirmed=false`
may already have changed the namespace and is not automatically retryable.

Checkpoint is a serialized volume ordering barrier. After prior batches and their
required drains have completed, their two-flush commits already establish it;
checkpoint need not emit a redundant generation. Serialization means no earlier
call has an active required drain when checkpoint starts on a `READY` writer. It
fails in either stopped state, even if earlier data was confirmed, and never
retries diagnostic pending maintenance. This uses only the held checkpoint right
and exposes no sibling data.

### Live directory continuation

Use a 64-byte copied core token: 16-byte writer-instance nonce, 16-byte volume
ID, 16-byte directory ID, `u64` directory change serial and the existing `u64`
tree position. All-zero starts enumeration. The live page operation takes this
token type; the read-only page operation retains its immutable token. This is
not a native ABI change or persistent record. Tokens own no references or rights.

Generate a fresh random nonce at writable open. Maintain a bounded runtime table
of changed directories keyed by volume/object ID. Untouched preexisting directories
have serial zero. A never-wrapping global counter supplies fresh serials for new
directories and confirmed local entry create/remove/rename/replacement or detach.
Update each affected directory once per transaction. Keep entries until object
deletion or writer close, even when no view remains; cache eviction must not
invalidate an unchanged directory's continuation. Reserve table capacity and
counter increments before publication. File data/length edits and unchanged
maintenance leave these serials unchanged.

Check listing rights and health first, then token binding and freshness, before
interpreting the tree position. Wrong instance/object binding or malformed position
is `INVALID`; a changed serial is `CHANGED`. Both return zero entries and leave
the continuation unadvanced. End repeats only with unchanged binding and serial.
Tokens can be copied/replayed; a forged valid position merely selects a point in
the directory already authorized by the view. Listing still returns names/kinds
only, not child IDs or authority. Changed-directory table capacity is bounded by
the writable object bound below, including retained detached directories.

## Agreed persistent additions and supported media

Use volume read-required feature bit 0, `ORPHANS`; this bit remains unsupported
in pool feature masks. With the volume bit set, the existing
24 reserved bytes at volume-record offset 424 hold a nullable orphan-index root.
The volume record stays 448 bytes. Add tree kind 8 and leaf record type 8, ordered
by object ID, with owning volume set and owning-object ID zero. Its 32-byte leaf
record is the existing 16-byte record header followed by the object ID. References,
checksums, internal ID keys and the eight-node depth limit are unchanged. An empty
orphan index has a null root. Its entry count cannot exceed the object count.

Allow a non-root object's zero parent only with this feature; full validation
must prove the named-or-orphan invariant in each retained state. Object count
includes orphans. Cleanup progress is the remaining mappings and grants in the
ordinary object record/indexes: no additional progress counter or hidden file.
Remove at most the admitted tail/grant batch, retaining the object and orphan
entry until their final joint deletion. Inline/none canonical reduction remains
mandatory. An orphan directory is already empty and owns no directory tree.

A supported writer may enable `ORPHANS` in the same transaction as a volume's
first unlink/replacement; the older retained state is validated with its own
feature mask. The bit stays enabled after cleanup. New formatter output may
explicitly enable it with a null root. No automatic write occurs merely to enable
it on open. Before enabling it, reserved bytes must be zero. Feature recognition,
record codecs, object-parent validation, tree traversal and complete checking
must land together before a writer can produce this representation. No structure
version bump is required for this declared use of reserved bytes.

`dir.checkpoint` uses directory-right bit 6. It has no feature bit and adds no
rights to stored grants; new formatter grants may explicitly include it. This
follows the agreed unknown-rights contract above.

The first writer accepts structure version 1, 128-byte common headers,
superblock/pool-root used lengths 192/288, and the documented exact fixed record
lengths. Variable name/key records must have exactly their defined prefix plus
payload rounded to eight bytes. Tree records end at the declared used length.
All otherwise reserved metadata fields, alignment and unused bytes must be zero, except
for the declared orphan root. Pool feature masks are zero; volume masks allow
only read-required ORPHANS, with write-required/optional masks zero. Grant bits
must be within the known file, directory and administration masks. Check this
profile in both retained states. Refuse unknown extension semantics or bytes
instead of erasing them during COW. Read-only
inspection keeps its existing broader compatibility contract. Do not rewrite
unknown grants, widen masks, repair slots or update unrelated repository pins.

## Agreed tree edits and file batching

Keep uncommitted blocks private, propagate exact minimum keys and collapse a
single-child root. Distinguish the existing sparse fixed-key editor from the
accepted stronger namespace editor below. Newly created private nodes superseded
within a candidate are discarded, not durably retired.

| Single-record edit, maximum depth eight | New nodes | Old nodes retired |
| --- | ---: | ---: |
| Insert, including permitted root growth | 15 | 8 |
| Same-key, fixed-length update | 8 | 8 |
| Sparse fixed-key delete (object/extent/grant indexes) | 14 | 14 |
| Namespace delete with six-entry occupancy repair | 15 | 15 |

The sparse fixed-key editor removes an empty leaf's reference and repairs a
one-child non-root internal node using an adjacent sibling: merge when that sibling
has two children, otherwise redistribute into two nodes with at least two children
each. This does not increase live node count. It remains sufficient for object,
extent and grant deletion. Namespace trees use the stronger rule and proof below;
variable-name directory deletion may split nodes when separator lengths grow.
That growth is funded by the permanent namespace envelope, not assumed absent.

### Agreed removable namespace profile: byte and repair proof

The six-entry profile, its bounded editor and formatter implications are accepted
for the initial writer, but are not implemented. Apply it to every directory-entry index and
per-volume orphan index, including both retained states. Other indexes keep their
existing shape rules. Require at least six leaf records or six child references
in every non-root namespace node. A root leaf may hold any positive count; an
internal root needs at least two children. Empty indexes have no root. Roots and
non-roots still obey actual byte fit, exact minimum keys and uniform leaf depth.
There is no artificial twelve-entry maximum: short records may pack more densely. Do not
apply the rule to volume-catalog leaves, whose 448-byte records fit only eight
per node; those immutable catalogs are outside this namespace profile.

The pinned version-1 codec uses a 192-byte tree header, four-byte slots, eight-byte
record alignment, and four extra alignment bytes when the count is odd. For n
records of encoded lengths l_j, node size is exactly
`192 + sum(l_j + 4) + 4*(n mod 2)`. The canonical writable profile excludes unknown
record extensions, so these maxima are complete:

| Namespace record | Encoded bytes | Bytes including its slot |
| --- | ---: | ---: |
| Directory leaf, name length 1..255 | align8(40 + name length), 48..296 | 52..300 |
| Directory internal minimum-key/reference | align8(48 + key length), 56..304 | 60..308 |
| Orphan leaf, specified ID record | 32 | 36 |
| Orphan internal ID/reference | 64 | 68 |

Twelve maximum-size internal records occupy 3888 bytes; thirteen occupy 4200 and
may not fit. Thirteen maximum-size directory leaf records fit exactly 4096.
Consequently any twelve records of any namespace node type fit, independent of
name lengths and order. A byte overflow has at least thirteen records.

Use a bottom-up, adjacent-sibling editor. Insertion changes one leaf, then at
most two child references in its parent at each level. Deletion can leave five
records/children in a non-root node: combine it with one adjacent sibling under
the same parent. If the ordered combined sequence fits one node, merge it;
otherwise repartition into two. An overfull single node is likewise split into
two. Never borrow a single record without checking the resulting bytes and counts.
Recompute both exact minima, then continue with their one parent. The parent can
lose one child, gain one child, or replace at most two references. One level's
repair therefore induces only one next-level repair, not a branching traversal.

The required ordered partition always exists. Put C = 3904 (block minus header)
and w = 308 (largest record plus slot). An underfull node has exactly five items;
its sequence plus a fitting sibling has weight at most C + 5w. Updating at most
two parent references produces weight at most C + 2w, a smaller case. If a combined
sequence fits, merging preserves occupancy: five plus at least six gives at
least eleven. Otherwise it has at least thirteen items. Choose the largest prefix
that fits while leaving at least six items. The first six always fit. If the cut
is limited by leaving six, that suffix fits. Otherwise the next item fails byte
fit, so prefix weight exceeds C - (w + 4). The suffix's weight plus its possible
alignment is then below `5w + (w + 4) + 4 = 1856`, well below C. Both outputs
therefore fit and have at least six items. This proof covers variable-length
records, key growth, leaf repair and internal redistribution; it does not assume
fixed record counts imply byte fit.

At the root, drop an empty leaf, collapse a one-child internal root, or split an
overfull root into two legal non-root nodes plus a new two-child root. At depth d,
a tree with this occupancy has at least `2*6^(d-1)` leaf records when d >= 2.
Depth nine needs 3,359,232 records, exceeding the 1,048,576 global object cap;
every namespace entry names a non-root object. A valid result at that count cannot
require depth nine, including a deletion-induced minimum-key split. There is no
routine structural-limit deletion refusal for an admitted namespace under this
profile. Other authority, durable-recovery and lifetime-generation preconditions
remain explicit; this is not a claim of unconditional success through I/O failure.

Six is derived for this particular local editor, not guessed. Five would allow
`2*5^8 = 781250` records at depth nine, so the count proof fails. A concrete depth-
eight counterexample can have thirteen root children, each with the minimum
`5^7` leaf records, plus one extra record in the first leaf: 1,015,626 total. Its
root fits twelve 255-byte minima and one short minimum (3952 bytes). Removing
the short minimum from the six-entry leaf leaves five, so no underflow propagates,
but replacing that separator with a 255-byte name makes the root 4200 bytes.
Splitting that root would create a ninth level. This is a counterexample to the
occupancy-only depth argument and this split-on-overflow algorithm, not
an impossibility proof for every five-entry editor: additional root-child
compression could avoid that split. Seven, conversely, cannot split thirteen
overflowing maximum-size internal records into two nodes of at least seven without a wider
repair algorithm. Six meets both the depth and local partition requirements
without that extra repair case; this is not a claim that it optimizes every possible tree editor.

For one namespace delete, each non-root level retires at most the changed node
and one sibling, producing at most two nodes. A root that does not grow adds at
most one old and one new node: at depth eight this is 15/15. If a root grows,
old depth is at most seven and the new count is at most `2*d + 1 <= 15`, including
the additional root. Insertion needs no sibling repair, so retires at most eight
nodes and also creates at most fifteen. Fixed-key orphan deletion cannot overflow
an ancestor on minimum-key change: merge or redistribution never increases live
node count. Directory deletion may increase it but remains inside these bounds.

### Writable admission and formatter implications

Complete writable admission must check this occupancy in every namespace tree
of both retained states before any writes, alongside existing depth/byte/ownership
validation and the deletion-capacity promise. A sparse but format-valid namespace
that violates it is unsupported by this writable profile; report the profile
limit without calling the image corrupt. Do not normalize it automatically or
accept only the selected root. Ordinary read-only access and complete format
checking retain their existing broader shape contract.

The current pinned formatter does not guarantee this profile. Its `plan_tree`
uses greedy byte packing and repairs only a final one-child internal node by
moving one child; leaves can end with one through five records, and internal
nodes with two through five children. No claim that today's populated images
are already writable under the stronger profile follows from a clean check.

Extend namespace bulk packing, when implementation is assigned, to
repair each short final group with the preceding group using the same ordered
merge/partition lemma. A nonfinal greedy group contains at least twelve items,
so a tail of one through five either merges or can split with at least six on
each side. Recompute minima and repeat at each parent level; keep a sole root
leaf small and collapse a sole internal child. This is bounded bulk planning,
not a runtime repair pass. Recalculate exact node counts, allocation-map shape,
quota/headroom and memory before creating output, using the existing plan/build
flow. Fresh formatter plans intended for this writer must also demonstrate the
chosen E/M and permanent deletion promises, not occupancy alone. Existing images
may already pass; others require explicit fresh formatting/reimport, not in-place
migration in this milestone.

This is a writable implementation profile within existing version-1 encodings.
It changes neither read semantics nor authority; no occupancy feature bit or
version bump is needed. The specified ORPHANS feature remains required
for orphan records. Formatter/editor changes and their validation belong to the
task 2 pyxis-fs implementation PR, including short-tail packing and its contract
tests, now delivered; profile/headroom enforcement remains tasks 3/4.

Set hard per-batch limits `V = 128` new volume blocks and `D = 256` retired volume
blocks, including both data and metadata. Pool map/root/catalog blocks have their
separate `H` bound below. Check actual plans against these caps before any writes.
A write transaction combines consecutive compatible slices from the current
request, including multiple logical blocks, while its actual data, metadata,
changed paths, splits and retirements fit V/D and every other admitted bound.
Plan the largest prefix found by the bounded planner; maximal packing is not a
correctness requirement, but do not deliberately commit every block separately
when a compatible multi-block plan fits. Partial-block COW and an old partial
EOF may consume additional blocks. A single-block slice remains a fallback,
not the transaction definition. Batch shrink and
orphan work at no more than six mapping/grant deletions and 128 retired data blocks;
metadata retirement must still fit the total `D`, not an extra allowance.

These caps cover the fixed representations above. Conservative volume-only
examples are create 38 new/24 retired blocks, replacement rename 70/63, a write
slice including old-EOF handling 48/33, and a metadata-only shrink batch at most
100 metadata blocks plus 128 retired data blocks. Grant cleanup with six deletes
and final object/orphan removal fits 113 new/113 retired metadata blocks. Include
canonical extent-tree collapse and actual operation variants in the final plan; these are
source/layout bounds, not measurements. Unlink fits 46 new/39 retired blocks;
a cross-directory rename without a victim fits 54/47. Replacement's 70/63 covers
source-directory delete (15/15), destination same-key update (8/8), orphan insert
(15/8) and four object updates (32/32: source, victim and both parents). Final
orphan release covers six grant deletes (84/84), object delete (14/14) and orphan
delete (15/15). Same-parent/shared paths can only reduce these conservative sums;
superseded private blocks do not become published allocations. All remain inside
V = 128 and D = 256, so no transaction/workspace-cap expansion is proposed.
Generation and counter increments,
quota, depth and record capacity are independent admission checks.

Shrink removes mappings from the high logical end and trims the boundary mapping
without copying the retained partial data block. Reads cannot expose bytes beyond
the committed length. Before later growth exposes any old-EOF suffix, COW that
block and zero the newly exposed bytes, combining the caller's payload when its
write targets the same block. Holes remain implicit zeroes. This preserves zero
growth without making shrink add an extent. If a large write extends the file,
check resize authority for its entire requested range before its first slice.
Each successful transaction reaches its committed length; an explicit sparse grow can
publish its new length in one batch after the bounded old-EOF treatment.

Shrinking and orphan cleanup modify only fixed-key extent/object/grant/orphan
indexes. Enforce node-count-nonincreasing deletion and canonical inline/none
reduction. Therefore they never increase the number of live data extents or live
volume-metadata blocks. Variable-name directory edits occur only in separately
admitted user mutations; orphan directories already have no directory tree.
This property is essential to the cleanup proof, not an optional optimization.

## Agreed writable profile and admission proof

The first writer uses caller-selected limits `E` on live file mapping extents
and `M` on live volume-owned metadata blocks, across the pool, including orphans.
Each inline extent and each extent-tree leaf mapping counts once toward E, even
when several mappings share one coalesced allocation-map run. No E/M product
default is accepted; the former 1024/1024 suggestion is withdrawn. Require explicit
profile selection for this design, with `E <= 1,048,576` and `M <= U`, fixed for
the instance and within existing record/memory limits. These are writable-open
options, not persisted format fields or quota changes. M also funds the deletion
reservation below, rather than merely today's actual metadata. Report the selected
profile and computed reserve/permanent requirements before attempting mutations.
Reopening needs a profile that covers both retained states and their promises.
Disk capacity does not determine extent/object capacity or transaction size.

Let `N` be the fixed volume count, at most 256. Both retained states must have the
same volume identities/names and persisted quotas, guarantees and workspace
capacities; this writer does not administer them. Bound each nonempty catalog by
`2N - 1` nodes, because it has at most N nonempty leaves and every internal node
has at least two children. Thus `Pcat = 4N - 2` bounds both catalogs together.
A fixed-size volume record update replaces at most eight catalog-path nodes;
volume names and catalog shapes do not change during this milestone.

All published live allocations are permanent. Retired pool metadata is charged
to recovery workspace; user-batch volume retirements to ordinary workspace;
orphan-cleanup volume retirements to recovery workspace. Migration capacity
remains reserved and unused. A validated selected state contains at most `D`
retired volume blocks, belonging to at most one volume; drain them before starting
another user/orphan batch. Live retained-orphan data remains permanent until a
cleanup batch actually retires it.

### Record closure and pool-metadata size

Temporarily treat all live and retired pool metadata as free to count the remaining
intervals. At most `E` live data extents, `M` live volume-metadata blocks and `D`
retired volume blocks contribute boundaries, giving:

```
K = 1 + 2 * (E + M + D)
Rbase(H) = K + 2 * Pcat + 6 * H
S(H) = ceil(23 * (Rbase(H) + 20) / 21)
```

`Rbase` restores at most `Pcat + H` active pool-metadata blocks and `2H` retired
pool blocks, charging at most two boundaries per block. The complete base map
already includes the planned volume changes and old pool retirements. The
whole-map construction above with `C <= 8` then needs at most `F(S) + 9` pool
blocks. Choose an integer `H >= 10` satisfying:

```
F(S(H)) + 9 <= H
S(H) <= 4,194,304
rebuilt tree depth <= 8
```

The computation terminates without allocating media or iterating map allocation.
Let `Hclosed = ceil((K + 2*Pcat + 231) / 15)`. Scan integers from 10 through
`Hclosed` for the first satisfying choice; stop/refuse if record/depth or capacity
limits preclude a profile. The loose bound follows from
`F(S) <= ceil(S/23) <= S/23 + 1` and the ceiling in `S`; `Hclosed` satisfies the
size inequality absent other limits. Use checked arithmetic throughout.

The baseline publisher uses the whole-map construction for every publication.
Use the actual base record count and changed catalog path to choose that
publication's shape and block count; S(H) and H are ceilings, not padding to
allocate. Every planned leaf must be nonempty and every new node reachable.
This makes both new and old map-node bounds explicit: each admitted retained map
has at most `H - 9` nodes. Adding the root and a catalog path gives at most `H`
new and `H` retired pool blocks per publication. The agreed incremental fallback
remains the eventual optimization boundary; the initial implementation does not
need a second allocator before the conservative path is validated. Acceptance
of this initial approach does not accept its performance as the final target.

Every user candidate must preserve `E`, `M`, global record limits and this pool
bound. Cleanup cannot increase E or M, and each batch drains before another, so
it cannot strand itself at an allocation-record cap: `S(H)` covers the entire
sequence, not just the first candidate. Existing sparse maps that exceed the
old-node cap are refused even if their record count alone fits. No unfunded
normalization pass is assumed.

### Workspace and permanent capacity

Opening establishes at most `2H` selected-state retired pool blocks, with at most
`H` protected by the older root. Each publication frees every eligible old pool
retirement using validated evidence and ended operation/I/O pins. Only the most
recent at-most-H retirement can remain protected, and the new publication retires
at most another H. This preserves the accumulated `2H` bound indefinitely.

Reserve another H of already reusable storage for the next publication. With
`V = 128`, `D = 256`, require persisted capacities of at least:

```
ordinary workspace >= max(1024, V + D) = 1024 blocks
recovery workspace >= max(256, 3*H + V + D) = 3*H + 384 blocks
migration workspace >= 1024 blocks (reserved, unused)
Pmax = Pcat + H
Aeff_i = A_i - Z_i + B_i
Pmax + sum(max(Aeff_i, G_i)) + sum(R_j) <= U
Aeff_i <= Q_i
```

`A_i`, `G_i`, `Q_i`, `R_j` and `U` have their existing format meanings. Enforce
individual occupied-charge limits as well. The recovery allowance covers the
pool remainder, its next replacement and one recovery-funded orphan batch;
ordinary admission reserves volume replacements and retirement conservatively.
Capacities are not fresh allowances per transaction. Existing occupied charges
reduce availability. In particular, opening with `2H + D` recovery-charged blocks
still leaves H + V for a candidate. Check actual already-reusable physical ranges,
not just counters, before allocation. No same-publication free range is usable.

The minima above include existing formatter floors, not its larger proportional
defaults. At 4 GiB, 64 GiB and 256 GiB the current default ordinary/migration/recovery
budgets are respectively 32/32/16 MiB, 512/512/256 MiB and 2048/2048/1024 MiB.
Persisted capacities must satisfy computed requirements even where those defaults
are insufficient. No formatter/QEMU/build default changes are proposed here.

`Pmax` reserves permanent pool metadata growth independently of temporary workspace.
The additional deletion promise uses Aeff below; the old inequality with actual A
alone did not fund it. Check both for the starting state and every projected user
result. An overwrite can still fail if extent metadata would grow beyond ordinary
capacity. No cleanup borrows unused volume guarantees or migration reserve.

### Protected permanent capacity for deletion

**Agreed requirement:** an admitted writable volume must not become unable to
remove an empty file solely because entering the orphan lifecycle requires more
permanent metadata. Ordinary growth must not consume that capacity. Unlink need
not free a retained file's contents, reduce its charge or make room for an unrelated
write immediately. Its final release and reserved drains must be funded too.

**Agreed reservation under the stronger namespace profile:** for each volume
let O_i count all live objects and J_i all live directory objects, both including
the root and retained orphans. Let `r_i = O_i - 1`. Exactly r_i records appear
across directory-entry indexes and the orphan index. Let Z_i be their actual live
node count. J_i counts even empty/unlinked directories; do not substitute the
current number of nonempty trees, which can increase during unlink. Derive J_i
during full opening validation and maintain it in candidate summaries; no new
persisted counter is needed.

For a nonempty tree with r records, L leaves and I internal nodes, an internal
root has at least two children and other internal nodes at least six. Counting
edges gives `L + I - 1 >= 2 + 6*(I - 1)`, hence `I <= (L + 3)/5`. Non-root leaves
have at least six records, so `r >= 6L` and total nodes are at most `(r + 3)/5`.
A root-only leaf instead has one node, bounded by `(r + 4)/5` even when r = 1.
Thus every nonempty tree has at most `(r + 4)/5` nodes. There can be no more than
`min(r_i, J_i + 1)` nonempty trees: one per directory and one orphan index.
Summing and rounding down gives the monotone permanent envelope:

```
B_i = floor((r_i + 4*min(r_i, J_i + 1)) / 5)
Z_i <= B_i <= r_i
```

An empty volume namespace has r_i = B_i = 0. The former `2*r_i` envelope remains
safe but is unnecessarily loose after this stronger occupancy rule. Merely using
r_i/5 would be wrong: many one-entry directory root leaves need one block each.
The root allowance above funds new orphan-root creation and arbitrary retained-
handle partition changes without assuming a single spare leaf suffices.

Reserve `B_i - Z_i` additional permanent blocks. The volume's effective promise
is `Aeff_i = A_i - Z_i + B_i`; require Aeff_i <= quota and use it in the pool
inequality above. Across the pool require
`sum(M_nonnamespace_i + B_i) <= M`, where M_nonnamespace excludes exactly the
Z_i nodes. Both retained states must satisfy these promises. This reserves
capacity, not preallocated on-disk padding or a change to actual allocation charges;
report actual use and protected headroom separately. Enforce global object/orphan
record limits for the same object population. The record closure based on M and
the Pmax allowance then covers the additional allocation-map/catalog costs too.

Unlink and replacement move records between these indexes without increasing O_i.
Object parent/count updates are fixed-length; their changed paths replace nodes
without increasing their live count. Thus namespace splits and changed paths may
consume the protected capacity, but cannot exceed B_i. Unlink and replacement
leave both O_i and J_i unchanged. Ordinary creates must fund their projected
O_i/J_i/B_i before admission; file growth and writes must preserve the full
promise. A fixed pool of spare orphan leaves would fail under repeated unlinks
with retained handles, whereas this reservation persists for every live object.
Removing the final orphan marker/object decreases O_i and, for a directory, J_i.
B_i is monotone in both inputs; fixed-key data/grant/object cleanup cannot increase
other live metadata or data, so the promise cannot increase. Floor rounding means
it need not strictly decrease at every individual release.
Temporary old/new paths and retired blocks still require V/D/H workspace; that
separate proof does not replace permanent deletion capacity.

The guarantee includes empty-file unlink, empty-directory removal and the victim's
orphan transition in regular-file replacement. Renaming into a longer name can
change directory shape, but total namespace records still stay within the envelope.
It does not promise immediate content reclamation with live handles, cross-volume
or directory moves, success without authority, or operation through I/O/integrity
failure. The stronger occupancy profile is checked before writable admission;
unsupported existing shapes are refused there, with no automatic normalization.
Once admitted, namespace removal does not encounter a ninth-level refusal under
the count and repair proof above. Generation reservation still funds already-
admitted orphan lifetimes, not every future unlink near counter exhaustion.

The new envelope uses at most one 4 KiB block per non-root object, versus the old
two-block bound, and substantially less when there are fewer directories than
files. This is total namespace capacity, including existing Z_i; only B_i - Z_i
is additional headroom. This accepted admission reservation does not change on-disk
quota accounting. Enforce the stronger admission rule and tighter reservation
together: the tighter bound is not valid for arbitrary sparse read-only trees.

### Generation headroom

Generation headroom also covers recovery. Let T count live orphan data blocks,
their grant records and orphan objects, including retained orphans. Every orphan
batch must reduce T by at least one before its at-most-two drain publications;
there are no metadata-only progress stages that consume generations without
reducing T. After runtime pins end, let a be zero with no selected volume
retirement, one if that retirement is already unprotected by both roots, and
two otherwise. Writable opening requires checked `generation + a + 3*T` to fit
in `u64`. A new user batch requires `generation + 3 + 3*T_projected` to fit.
Use exact generation increments of one. This reserves the ordinary batch/drain
and later orphan recovery, and remains sufficient at intermediate crash points
as T and the remaining drain count decrease. Refuse exhaustion before writing;
never wrap or rely on a future migration to finish an admitted sequence.

### Workloads, capacity and development validation profiles

V/D bound one transaction, not total file or filesystem capacity. Mapping records
also preserve birth generation: even physically adjacent logical runs written in
different publications cannot simply merge across different births. A large
sequential request can produce one extent per transaction when allocation is
contiguous and the mappings are compatible. Combining blocks therefore helps
large writes. Separately committed 4 KiB appends to distinct blocks still produce
one birth/run per call; delaying acknowledgement or merging those calls is not
allowed. Random overwrites can split older extents, and repeated edits to the same
block replace its mapping rather than necessarily adding one forever. Capacity
planning must describe write history, not just final bytes or sparse logical size.

A read-only Git-blob census supplies a concrete source workload: parent e4a83ba8
(423 files), fs 82cc242 (48), userspace f24e9d9 (244), and locally available ports
fc728f7 (75; not a claim about the pinned ports revision). Imported under four
prefixes, these total 790 nonempty files, 132 directories including the common
root, 922 objects and 12,460,032 payload bytes (11.883 MiB); rounding each file to
4 KiB takes 3516 data blocks (13.734 MiB). This excludes Git history, submodule
contents beyond those listed, generated output and toolchains. The census is
measured from tracked blobs; writer layouts and costs below are calculations.
An initially dense import has about 166 volume metadata blocks (33 object-index,
132 directory-index and one grant block). Under the accepted stronger
packing, J = 132 and r = 921 give B = 290 namespace blocks; with the modeled 34
nonnamespace blocks, effective metadata use is about 324 blocks rather than 1876
under the former envelope. Actual formatter output must be checked for the new
occupancy rule. The old 1024/1024 suggestion still does not cover the write histories
below and is not restored as a normal product default.

Use 8 GiB RAM as the normal QEMU development validation baseline, 64 GiB representative
disk images, 4 GiB smaller recovery/round-trip images, and 32 GiB RAM / 256 GiB NVMe
as the first physical target. These are design/validation targets only; this PR
changes no QEMU defaults or build configuration. Use the following explicit
validation profiles, with N = 16 as a conservative volume-count allowance; compute
requirements from the actual N at opening:

| Workload target | E | M | H | S | Recovery minimum | Pmax | Reserved arena | Validation core memory cap |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 4 GiB recovery image | 8192 | 4096 | 726 | 32407 | 2562 blocks / 10.008 MiB | 788 | 28.87 MiB | 128 MiB |
| 64 GiB development image, 8 GiB RAM | 262144 | 65536 | 18570 | 840527 | 56094 blocks / 219.117 MiB | 18632 | 435.05 MiB | 768 MiB |
| 256 GiB physical target, 32 GiB RAM | 524288 | 131072 | 37105 | 1680103 | 111699 blocks / 436.324 MiB | 37167 | 812.81 MiB | 1 GiB |

The arena calculation is itemized below and excludes opening-check scratch and
live handles; all share the selected cap. These validation caps leave room to
investigate that additional cost, not proof that every profile-limit population
passes complete validation. The existing 1 GiB maximum is not increased. If
validation does not fit, lower the admitted population or return to review with
measured memory/layout evidence; do not skip checks or claim the table guarantees
full occupancy. Views cannot spend the arena reserved for drain. Raising the
normal cap above the existing 128 MiB default is an explicit writer option in
this design, not a change to host or QEMU defaults.

Representative histories to validate, with independently expected bytes:

- Small image: the source census, then a 1 GiB sequential file written in 256 KiB
  committed chunks (4096 additional extents in the contiguous case), plus 2000
  separately committed 4 KiB appends. About 6886 mappings fit E only in the
  contiguous model; the task-7 run below shows that the initial writer does not
  complete this history under the E=8192/M=4096 profile. M must cover
  directory/orphan reservation and the resulting extent trees. This exercises
  real data and history within 4 GiB, rather than an empty sparse image.
- Development image: twenty copies of that source population, 32 GiB of sequential
  output in 256 KiB transactions, and 20,000 separately committed 4 KiB appends.
  With the extra source roots and two output files, 166872 mappings and 18443
  objects precede further overwrite/rename/retained-unlink churn. The namespace
  reservation with J = 2641 is 5802 blocks; a dense modeled 3345 other metadata
  blocks gives an effective M use of about 9147, below 65536. Measure actual layouts.
- Physical target: forty source populations, 96 GiB of sequential output at the
  same granularity, and 50,000 separately committed 4 KiB appends: 474816 mappings
  and 36883 objects before churn. With J = 5281, the 11602 namespace-reservation
  blocks plus a dense modeled 9199 other metadata blocks give about 20801 effective
  M use, below 131072. Larger source trees, build artifacts, fragmented placement
  and longer histories must be
  admitted from their measured counts, not assumed to fit this example.

These are planned validation workloads, not executions or promises of contiguous allocation.
A 256 KiB transaction uses 64 data blocks, leaving up to 64 of V for volume paths
and splits; use the actual plan and reduce the prefix if needed. Fragmentation,
partial EOF work and separate small calls can raise E or transaction count. With
256 KiB births, filling 64 GiB sequentially alone takes 262144 mappings before
source trees/history; filling 256 GiB takes 1048576 before any other files. The
profiles intentionally do not promise arbitrary full-disk write histories. One
GiB appended as 4 KiB calls alone takes 262144 mappings despite the small payload;
a formatter-created contiguous file of the same size can start with one.

At N = 16, even E = 1048576 and M = 262144 would reserve about 1568.36 MiB before
opening scratch, exceeding the existing 1 GiB cap. More RAM or a larger empty
image does not bypass that contract. Consider higher caps or a more compact
summary/allocation strategy only with workload evidence and a focused subsequent
proposal; neither is silently required to implement this first writer. The E/M profiles and their memory/workspace ceilings remain unchanged despite the
tighter namespace promise; those are conservative configured capacities. Record
actual object/directory/mapping/metadata counts and peak memory after the stated histories.

### Bounded memory and maintained validation evidence

Reserve all commit/maintenance memory before exposing the writer. A concrete
arena budget is three S-entry allocation vectors (80 bytes per entry), three
live-claim vectors (96 bytes per entry, each bounded by `E + M + Pmax`), a
48-byte changed-directory slot per possible object, `(H + V)` block buffers,
`8*(H + V + D)` plan/delta slots of 128 bytes each, and 8 MiB of fixed scratch
for paths, codec buffers and bounded in-place sorting. The delta allowance covers
new allocations, old claim removal/retirement, eligible frees and unchanged
fragments of split claims without allocating a fourth complete state vector.
At most `min(1,048,576, 29*M)` objects fit the object-index leaves. Check these
products and their sum against the existing memory cap and actual allocation
results. Implementation structures must fit these slots; otherwise revisit the
bound before implementation is accepted. Views use remaining capped memory and
cannot consume the reserved arena. No temporary-file spill in the core.

Writable opening first runs complete validation of both retained states, including
namespace/orphan rules, all grants, counts and cross-state incarnation/overlap
checks. Its scratch is separately charged to the same memory owner; failure to
fit yields refusal, not partial validation. Build two summaries containing root
identity/generation, sorted allocation intervals and sorted live physical claims
(owner, object/logical mapping or metadata role, range and birth). No file payload
scan or checksum is implied. Establish E/M/map/pool/debt/charge limits in both
states, with the selected-state protected-tail and free-workspace checks above.
Reject live workspace-charged allocations and occupied migration charges in this
initial profile. Reject unsupported/degraded/incomplete states without writing.

Ordinary reopening and recovery after an actual backing writeback error have
different adapter preconditions. The abstract adapter must establish that the
state supplied for recovery is durable, not merely readable through surviving
caches. The initial host adapter does not establish that boundary by closing,
reopening, validating and successfully calling `fsync`. An error can leave bytes
cache-visible but no longer flush-pending; later flush success need not persist
them; this failure mode is documented in
[writeback-error experiments](https://www.usenix.org/system/files/atc20-rebello.pdf).
Linux writeback errors are also observed relative to per-file error
cursors, not a permanent clean-history certificate; see the
[Linux errseq contract](https://www.kernel.org/doc/html/latest/core-api/errseq.html).
Read-only inspection never flushes and makes no historical durability claim.

**Agreed recovery restriction:** after actual backing writeback error, refuse a
host writer unless a documented durable-state recovery boundary is established.
An in-memory failure flag is insufficient across host-tool restarts. No automatic
repair, retry of uncertain mutations or rewrite of a possibly lost publication.
The abstract simulator's durable image can establish such a boundary; that does
not prove ordinary host recovery. The previously proposed mandatory registry and
refusal after every interrupted session are not accepted requirements.

### Agreed ordinary host writer and deferred recovery

Real-host post-error recovery qualification is deferred. The initial host writer
supports ordinary serial operation on a healthy backing image; it does not supply
a recovery route after actual backing writeback error. No persistent session
registry, mandatory O_DIRECT, special XFS configuration or full-image direct-I/O
provisioning is required. These scope decisions do not weaken the abstract adapter's
durability contract or the core's publication, admission and recovery rules.

#### Supported ordinary operation

Use a fixed-size regular image on a local Linux filesystem whose file writes and
`fsync` provide the ordinary data/metadata durability contract through its backing
storage. Buffered positional I/O is allowed, including sparse images; logical
pool capacity and logical admission/deletion guarantees do not reserve physical
host disk space. Host ENOSPC or quota exhaustion on write/flush is a backing I/O
failure, not a core pre-admission refusal; apply the same stop rules and post-error
reopening restriction. No full physical preallocation is required. The host must
honor successful flushes. Arbitrary network, synthetic or remapped backends are not certified by
this statement. No physical power-loss qualification is claimed.

Supported starting points are successful fresh formatting/publication or ordinary
reopening after a healthy completed session, with no known or suspected backing
writeback failure. Ordinary reopen checks geometry and both retained states and
performs the normal opening flush before exposing a writer or doing cleanup.
Those checks detect current failures and invalid media; they do not certify past
I/O history. The supported workflow's healthy history is an operating precondition,
not something inferred from a new descriptor or a successful opening flush.

Retain one exclusively locked image descriptor through validation, callbacks,
drain and close. Cooperating tools must honor the lock; external writers, writable
mappings, truncation, hole punching and other changes to the image are excluded
while it is owned. Advisory locking cannot prevent a noncooperating process.
Keep callbacks synchronous and serial, check image geometry and exact transfer
results, and keep the core's first and final `fsync` for every publication. A
successful write syscall alone is not a durability barrier. Durable creation also
requires syncing the containing directory, as specified by
[fsync(2)](https://man7.org/linux/man-pages/man2/fsync.2.html).
No delayed durability or batching across completed application calls is introduced.

After a negative or short write result, return failure to the core without
completing a possibly changed suffix or replaying the transaction. This is the
initial writer's conservative adapter policy; the pinned formatter's existing
write loop is not evidence that the new writer already implements it. A failed
write may have changed part of the range, and errors can be reported later by
flush or close; see [write(2)](https://man7.org/linux/man-pages/man2/write.2.html).
A flush error ends the attempt; do not loop until a subsequent flush succeeds.

#### Stop-on-error behavior and restart boundary

Preserve the agreed outcome/health table. Replacement-write or first-flush failure
stops mutation; confirmed-state reads remain available while integrity is established.
Uncertain slot publication stops all ordinary access. Failure during cleanup does
not erase confirmed user progress; cleanup replacement-write/first-flush failure
permits confirmed reads, while uncertain cleanup publication stops all ordinary
access. Any backing read failure during ordinary access, planning or maintenance
stops all ordinary access pool-wide, even if transient, and requires a fresh
validated reopen under the existing adapter recovery preconditions. Unknown
outcomes may contain additional committed bytes and are not automatically retryable.
An ordinary pre-admission quota/profile refusal is not a backing writeback error.

Once stopped for backing I/O failure, issue no further mutation or recovery writes,
including on checkpoint or close. Quiesce callbacks before releasing their buffers
or exclusive ownership; a hung synchronous call is not quiesced by a timeout.
Report the error and confirmed progress independently, close resources, and check
close results. Close is not a durability barrier and a close error is not retried;
see [close(2)](https://man7.org/linux/man-pages/man2/close.2.html). A late close error
must make the host command fail and disclose the backing error without rewriting
earlier confirmed core results as zero progress or issuing repair writes.

Known or suspected writeback failure leaves that image outside the initial host
writer's supported reopening workflow, including after the tool exits, the path
changes or the image is copied from surviving caches. Refuse a recovery request
whose history is known; no retry, repair, force-clear or flag asserting that a new
`fsync` cured the failure. The implementation can enforce the stopped state during
its session, but without a durable external history mechanism it cannot reliably
discover a previous process's error. Explicitly document this detection limit:
there is no claim of automatic cross-process quarantine or safe writable reopening
of arbitrary images. The caller/operator must retain the failure context and must
not present an affected image as an ordinary healthy reopen. An in-memory flag is
not the recovery boundary, and process restart does not remove the restriction.

There is no persistent ACTIVE marker or blanket permanent ban after every
interruption. An interruption before any mutation does not itself taint a healthy
image. An interrupted mutating session whose backing outcome/history is unknown
is not demonstrated healthy by close/reopen, cached validation or a later successful
flush. This milestone makes no real-host recovery claim for that case either;
inspection may report readable structure/content without certifying durability.
A mutating host session that ends without a successful writer close is outside
the supported ordinary reopening workflow, including interrupted-session orphan
recovery. The core still performs orphan recovery with an adapter satisfying its
precondition, including in the simulator. Do not turn an observed successful reopen into
post-error recovery evidence.

#### Deferred qualification and retained findings

Follow-up work must establish a documented durable-state boundary for a concrete
host/kernel/filesystem/storage configuration, including failed or partial writes,
quiescence and process restarts, before adding supported real-host post-error
recovery. Retain the independent old/new payload comparisons and uncertain-progress
reconciliation; qualification must never authorize replay of an uncertain mutation.
This follow-up is not a prerequisite for implementing ordinary host writing or
closing this core milestone with its simulated recovery evidence.

The earlier investigation found two candidates, neither qualified here. A restricted
direct-I/O path would still need alignment discovery, exclusion of buffered fallback
and mixed access, stable host mappings, explicit flushes, outstanding-I/O quiescence
and a documented stable backing view after errors. O_DIRECT alone supplies none
of the missing post-error durability proof. The inspected Linux v6.12
[XFS](https://raw.githubusercontent.com/torvalds/linux/v6.12/fs/xfs/xfs_file.c) and
[iomap](https://raw.githubusercontent.com/torvalds/linux/v6.12/fs/iomap/direct-io.c)
paths were investigation evidence, not a required deployment or qualified target.

A registry could preserve refusal history but would need independently reliable
storage outside the image's failure domain, stable identity across path changes,
rollback protection and an explicit way to resolve harmless interrupted sessions.
An ordinary sidecar does not establish those properties, and a registry cannot
supply the missing durable image contents. Neither approach is selected as follow-up
policy. Record the qualification gap in [technical debt](../technical-debt.md#deferred-real-host-post-error-recovery-qualification);
do not add a registry service, cache manager, repair tool or special host filesystem
requirement to this milestone. Simulator recovery remains required under its
explicit durable-state premise; no real-host failure experiment is claimed.

### Candidate planning and publication

Use the third vectors for a private candidate. Compute projected counts and
admission from bounded deltas first; remove replaced candidate claims before
inserting final new claims, leaving both durable-state vectors immutable. Build
the canonical base allocation map by a coalescing merge of old intervals and
planned changes; intermediate duplicate claims or uncoalesced records may not
overflow a vector whose final state fits. Known editors preserve unchanged
validated subtrees and check each changed key, reference, namespace relationship,
claim and accounting delta against the old summary. Preserve one-name-or-orphan
uniqueness, reference contexts and compatible cross-state data mappings. Verify
all new live allocations are reachable and every replaced incarnation is retired.
Sort and reconcile the candidate using bounded in-place work; no whole-filesystem
rescan is required per batch. Reclamation checks both interval maps and live-claim
vectors, plus ended runtime pins; retirement generation alone is never proof.

Finalize all candidate buffers before device writes; emit each allocated block
exactly once, with at most H + V replacement blocks. Flush, write exactly one
older-slot block, then flush again. For an identical-generation initial pair, choose slot zero
first. No speculative rewrite passes or failed-write retries. The platform's 16-block transfer limit bounds each callback, not the
transaction. Reserve enough generation numbers for the batch and both maintenance
publications before starting; refuse exhaustion rather than leave an admitted
drain unable to publish. Slot-write atomicity is never assumed.

After both flushes succeed, rotate the candidate into the selected summary and
drop the overwritten slot's old summary. Pre-publication failure discards only
the candidate. Uncertain publication keeps all storage protected, marks access
stopped and exposes neither guessed state. Reopening reconstructs evidence from
media. Drain any selected-state volume retirement before abandoned orphans; finish
orphan cleanup before returning a usable writer. Allocate no further heap storage
inside an admitted publication/drain. I/O or integrity failure can still stop it
under the agreed health rules; resource sufficiency is not a guarantee of device
success.

Writable-open failure returns no usable pool. Its trusted diagnostic reports the
validation/admission cause, last confirmed cleanup generation and cleanup
outcome/health, including uncertainty. Earlier confirmed startup cleanup is not
rolled back. Release private core state after callbacks finish, without further
writes; the adapter can then release exclusive backing ownership. A fresh open
must satisfy the adapter recovery boundary and validate again. Ordinary view
results do not expose these pool diagnostics.

## Agreed maintained host tests and CI

The shared filesystem core will have maintained host tests that run in CI and
gate merges. This explicitly supersedes the repository no-tests/no-CI restriction
for this filesystem work only. It does not authorize implementation in this PR,
a general testing framework, kernel self-tests or unrelated repository changes.
The owner configures Forgejo branch protection; the agreed check below must
become required when delivered. Task 1 design is accepted; implementation remains
separate.

The governing rule is:

> Host tests must validate filesystem behavior, documented invariants, corruption
> handling and recovery semantics, not duplicate incidental implementation details
> or current on-disk fixture contents. Prefer small synthetic fixtures and assert
> only properties that are part of the filesystem contract. Implementation refactors
> or harmless fixture/layout changes should not require broad test rewrites.
>
> Exact bytes and offsets are appropriate assertions when specified by the disk
> format. Expected results must be independently defined: agreement between our
> writer, reader and checker alone is insufficient.

Tests land with the behavior they exercise. A bug fix includes a focused regression
case; a contract change updates its documented expectations in the same PR. Do not
postpone the suite until milestone closure, preserve accidental behavior as a
fixture requirement, or weaken an invariant assertion merely to pass a refactor.

### Agreed small host setup in pyxis-fs

Use [Unity](https://www.throwtheswitch.org/unity) for assertions, per-case
setup/teardown and result reporting, with a small `tests/` entry point and focused
case/support files linked to the same `libpyxis-fs.a` built by the ordinary Makefile.
Unity's core is one C source and two headers and integrates directly with Make;
we need not maintain our own assertion/reporting framework. This Unity setup is
accepted for implementation with the behavior it tests; no dependency or test code
is added by this specification PR.

When implemented, vendor Unity's small required source subset under
`third_party/unity/` in pyxis-fs, pin a reviewed upstream release/commit and retain
its MIT license/provenance and upstream formatting. Record any local changes; no
floating checkout or dependency download during a test run. Compile it only into
the host test executable. Core sources keep their freestanding flags and production
headers/binaries gain no Unity dependency. The core is linked normally, not rebuilt
with fault switches. No Ceedling/CMock, generated mocks, Ruby tooling, new build
system or reusable framework wrapper is part of this setup.

Keep only filesystem-specific support ours: synthetic fixtures, independent expected
state, the callback adapter, bounded traces and a short explicit case/suite list.
Use Unity's assertions and case reporting directly. Group cases by contract
(format/corruption, operations/authority/lifetime, admission/recovery), not by a
mirror of core source files or every private helper. Teardown must own the scratch
files and core handles even when an assertion aborts a case. Callback adapters
record violations and return through the normal core call before the test asserts;
do not let an assertion jump out through an active core operation and bypass its
unwinding. Convert test failures
or ignored cases to a reliable nonzero process result; do not allow a large failure
count to wrap the shell exit status to success. No second assertion layer, discovery
system, plugin layer or separate filesystem implementation.

Use ordinary core interfaces wherever available. Before public mutation exists,
private editor/planner entry points may be exercised for their documented input,
result and resource invariants; do not export test-only production APIs or assert
private structure layouts. No mock writer, allocator algorithm or checker replaces
the real code being tested. The bounded failure adapter below is shared support in
this runner, replacing the separately proposed manual failure utility. Extended
campaigns use that same runner, fixtures, independent oracles and adapter.

All failure controls stay in test-owned implementations of the existing platform
callbacks: exact block I/O, flush, memory allocation and randomness where supplied.
There are no production fault switches, injected branches or kernel hooks. Supply
fixed test identities/data and bounded allocation caps; do not replace production
randomness or allocation policy outside the harness. Count actual allocations and
I/O ranges only to check documented bounds, ownership or protocol ordering, not
an incidental exact allocation sequence.

Construct small synthetic images afresh in private temporary directories. Respect
format geometry and formatter floors: a 64 MiB sparse logical image with a few
populated blocks is a small fixture, not a claim that a smaller unsupported volume
is valid. Use bounded disk-backed state for larger cases and delete successful
scratch files; retain only failing case metadata, traces and bounded reproducer
artifacts. Cases have isolated state, fixed seeds and no wall-clock/random-host
input dependence. No root privileges, mounts, QEMU, network or special host filesystem
are prerequisites for the core suite. Its simulated durable state is defined
relative to simulated cuts, not actual host power loss.

### Independent expectations and coverage

Define the expected namespace, identities, granted rights, file lengths and bytes
in each scenario before invoking the implementation. A small list of objects and
expected byte arrays/ranges suffices; ordinary host files can hold larger payloads.
Update expected state from the scenario's operations and declared commit boundaries,
not by asking the core what it wrote. Compare complete relevant payloads and state,
including permitted alternatives after uncertainty. Writer/reader/checker agreement
is useful additional evidence, never the sole oracle.

Use a few independently authored format vectors for specified magic, endianness,
field offsets, checksums and record/reference encodings. Generated valid fixtures
may use core construction, but independent expectations must still catch mutually
consistent writer/reader mistakes. Targeted malformed cases change specified fields;
where a deeper invariant is under test, maintain a valid enclosing checksum so an
earlier checksum rejection does not falsely satisfy the case. Keep checksum known
answers/reference calculation and mutation offsets independent of the encoder under
test. Do not freeze whole populated images, allocator placements, split choices,
exact tree shapes, private arena layouts, diagnostic wording or raw event ordinals
as golden outputs. Discover referenced blocks through format-defined references
when a scenario needs a particular role; the assertion is the resulting contract.

| Contract area | Small cases required as that behavior lands |
| --- | --- |
| Ordinary operations | Open/read/list; create, write and resize with holes, partial blocks, old-EOF zeroing and multi-block/partial progress; rename with and without explicit replacement and checkpoint; expected bytes, length, identity and namespace after reopen. |
| Authority and lifetime | Denied operations leave state unchanged; held rights do not widen; extending writes require resize before any progress; explicit checkpoint rights; retain/unlink/name reuse; detached empty directories; last-reference cleanup and live continuation invalidation. |
| Malformed and unsupported input | Invalid checksums, lengths, alignment, bounds/overflow, keys/references, cycles, namespace/orphan relations and live-allocation overlaps; incompatible retained claims, torn/degraded peers, unknown rights/features. Assert the documented corruption/unsupported/limit outcome, incomplete validation where required, and absence of unauthorized writes. |
| Admission and funded cleanup | At and below quota/profile/workspace/memory/generation requirements, including formatter floors and protected deletion capacity. Refusal occurs before admission; admitted mutation and final orphan release finish their funded drains without new application work. Unexpected resource refusal during drain fails the test; additionally check preserved progress and sticky `READABLE_STOPPED`/required escalation, never resumed maintenance. |
| Publication and recovery | Before/after each protocol phase, selected short/torn writes and failed flushes, confirmed partial progress, unknown additional progress, read availability after replacement-write/first-flush failure, pool-wide access stop after any backing read failure (including planning and maintenance), post-uncertainty access stop, post-commit cleanup failure and no implicit retry. Include cache-visible nonpending writes, the distinction between core health and adapter recovery preconditions, and both retained payloads through maintenance replacement writes. |
| Namespace/editor invariants | Small variable-length split/merge/redistribution/root cases and formatter tails; preserved ordering, byte fit and admitted occupancy; upper bounds on new/retired nodes rather than one mandated shape. Check writable admission against both retained states. |

For corruption cases isolate the intended defect, or assert only the documented
set of outcomes if the contract permits more than one first failure. Check output
and ownership/lifetime behavior on failure, not just a nonzero status. Add bounded
memory-callback failure cases before admission and verify teardown; once an admitted
drain starts, the promised arena must suffice without further allocation requests.
No test should equate safe resource refusal during that drain with success. If
an invariant failure is encountered, check the defensive stopped-state contract
and absence of retry writes too; correct refusal still leaves that drain case
failed. Ordinary pre-admission refusal must instead leave the healthy writer usable.

Exercise rename with an existing destination and `replace=false` both with and
without replacement authority; both return `EXISTS` after base rights checks.
With `replace=true`, check `DENIED` without replacement authority and confirmed
displacement with it, plus absent-destination and same-name cases.

### Commands, per-PR gate and extended campaigns

The following names are the agreed command contract in the `pyxis-fs` repository.
Ordinary builds, the quick gate, extended campaigns and the recovery workload are
implemented. The development/physical profiles remain planned and the runner
rejects them until delivered. Provision temporary storage as documented in
[maintained testing](../../fs/docs/testing.md#extended-campaigns-and-recovery-workload).

| Command, run from pyxis-fs | Purpose and limit |
| --- | --- |
| `make -j16` | Preserve the ordinary freestanding core archive and host tools build. |
| `make check` | Build/run `build/pyxis-fs-tests --suite pr`: deterministic small contract cases and the bounded failure scenarios required for implemented behavior, including both retained payloads. This is the every-PR gate. |
| `make check-extended` | Build/run the same runner with `--suite extended --seed 1`: longer reuse/retained-orphan histories, near-maximum depth/count pressure, broader deterministic cut/promotion combinations and adversarial namespace shapes. No exhaustive-failure claim. |
| `build/pyxis-fs-tests --suite workload --profile recovery --seed 1 --source PATH` | The documented populated 4 GiB history with streaming expected contents and resource accounting; run after building the runner with `make check`. |
| `build/pyxis-fs-tests --suite workload --profile development --seed 1` | Planned: the populated 64 GiB / 8 GiB RAM development validation workload. Explicitly provision its disk, memory and time budget. |
| `build/pyxis-fs-tests --suite workload --profile physical --seed 1` | Planned: the representative 256 GiB / 32 GiB RAM profile on host storage. This exercises capacity/history, not native NVMe or device recovery qualification. |

Keep `make check` small: fixed cases/seeds, bounded operation counts and temporary
storage, with a design target below one minute of test execution on the configured
CI runner, excluding compilation. Measure this when first delivered; no runtime
is claimed now. Use a five-minute CI job timeout, with timeout or infrastructure
failure failing the check. Do not hide missing assertions or skip mandatory recovery
cases to meet a time target. Large populations and broad campaigns belong in the
explicit commands above; the small suite still covers each implemented contract
category and representative resource/recovery boundaries on every PR.

Add a Forgejo workflow in `pyxis-fs` with workflow name `Filesystem` and job ID/name
`host-contract`, running the ordinary `make -j16` and `make check` on every PR and
main update. Do not apply path filters that can omit the merge gate. Build from the
submitted revision with the supported GNU C23 host compiler, without rebuilding
the compiler container. Zero executed cases, unexpected skips, mismatches, crashes,
timeouts or infrastructure errors must not produce success. Initially register
only behavior actually implemented; do not add passing placeholders for future
tasks. Record the executed case list/count so scope remains reviewable as it grows.

The owner should require **`Filesystem / host-contract (pull_request)`** in Forgejo
branch protection after the workflow lands, confirming the emitted context matches
that name. Require a successful run for the exact candidate revision; a successful
older run or parent-only build is insufficient. This document does not configure
branch protection, and the current Pyxis `Build Pyxis / build` check is not the new
filesystem test gate. Workflow/tests land in `pyxis-fs` before its parent pin update.
The parent pin PR also checks the dependency's exact revision and ordinary integrated
build status. No duplicate suite or general orchestration service in the parent.

Extended runs are explicitly invoked rather than a second mandatory per-PR gate.
Require relevant campaigns before closing the milestone and when a change affects
their resource bounds or recovery coverage; report commands, exact revisions,
profiles, elapsed time and coverage. Publish named failing cases, seed, selected
cut, confirmed outcome/health and bounded traces for reproduction. Choose cuts by
protocol phase/block role using the callback trace, not a saved ordinal from a
previous allocator layout. Event numbers are diagnostics. Do not enumerate every
callback combination in the quick gate or claim that the extended set is exhaustive.

### What a passing suite establishes

Passing establishes agreement with independently specified expected behavior for
the executed fixtures, bounds and modeled failures at that revision. It provides
maintained regression evidence for the shared core, including the exercised retained
payloads. It does not prove all trees, write histories, schedules or failures; replace
neither the admission/editor proofs nor review with a green check. Structural checking
alone still does not authenticate file data.

It does not qualify Linux direct-I/O fallback behavior, host filesystem/device cache
semantics, physical power-loss recovery, performance or production-data safety. The
simulator's declared durability boundary is an input assumption. Its substituted
callbacks do not test the real host adapter's short-write, flush-retry or late-close
error policy; that policy needs implementation review and ordinary host validation,
and is not established by a green core suite. Ordinary host-tool
use, freestanding compilation and the existing parent kernel build remain necessary;
host libc linkage of the runner must not leak into the core. Target cross-compilation
continues through the parent integration using the pinned dependency. Later native
writable adapters still require guest/QEMU and appropriate device validation in
the native-persistence milestone. This host-test decision does not add kernel
self-tests or substitute for that eventual guest evidence.

## Agreed host failure-validation model

Integrate this bounded model into the maintained host runner above, linked to the
real shared core. Keep one fixed scenario table and one test-owned adapter at the
existing exact I/O callbacks, reused by quick and extended runs. No standalone
parallel validation implementation, production failure switches or kernel probes.
The ordinary host writer uses the healthy-session I/O and durability contract
above; simulated recovery does not qualify its post-error backing state. The
detailed simulation mechanism below is accepted design, not implemented here.

The simulation uses a sparse durable image and a separate disk-backed volatile
write log. A log record contains block range, payload, pending/cache-only state
and trailing length so reads can search backward for the latest write to each
block, then fall back to durable storage. This avoids an image-sized RAM buffer or bitmap. Use one 64 KiB transfer
buffer and fixed control records outside separately capped core memory. Bound
unflushed log payload by `(H + V + 1) * 4096` and record count by `H + V + 1`.
Successful promotion removes the corresponding overlay records; cache-only records
are retained for the bounded failed-session scenario. Cap exhaustion or real host
I/O failure is an infrastructure failure, not a successful simulated-device result.

A successful callback write copies input into the pending log. Successful flush
replays only pending writes in order into the durable image, calls actual `fsync`,
and removes promoted records. A selected cold cut discards the cache overlay and
runtime state, then inspects only the explicitly durable image. Deliberately
promoted bytes are host-flushed before inspection. Close never promotes writes;
merely killing a process while Linux caches survive is not this cold-cut model.

The targeted buffered-writeback-error counterexample leaves a complete new slot
cache-visible but neither durable nor flush-pending after a failed final flush.
First verify the failed core instance stays `ACCESS_STOPPED`: reads, mutations
and checkpoint cannot clear it or emit retry writes; status and closure remain
available. After quiescence, separately exercise the test backend's flush, which
returns success without persisting that slot. This is not a flush requested through
the stopped core. On unchanged snapshots, use read-only validation/extraction to
show the warm cached view selects the new generation while the explicitly durable
image retains the old generation and independently expected old payload. This is
a concrete counterexample to cached validation plus successful flush establishing
durability, not a requirement for a fresh core to discover hidden failure history.

Discard the overlay and reopen a fresh core on the simulator's explicitly durable
image; assert the permitted generation and independent contents. Also exercise
failed writes that remain pending. A simulator adapter may separately refuse open
because its own retained scenario history lacks a recovery boundary, including
across adapter instances. Label that assertion adapter enforcement, not evidence
of core detection. With identical valid input bytes/callbacks, the core has no
basis for distinguishing unsafe cached provenance. No recovery flag or implicit
history detector is added, and neither scenario permits automatic mutation retry.

Separately model an adapter with an explicitly established durable-state boundary:
completed writes reach stable storage, failed requests may leave stable old/new/
partial bytes, and recovery reads only that durable state after all I/O ends.
A fresh instance validates both retained states or refuses a torn/degraded peer.
Exercise harmless interruption as well as error refusal. This validates core
behavior under the abstract guarantee, without selecting direct I/O, XFS or a
registry as its concrete mechanism. The simulated backend retains its declared
provenance across adapter instances; the ordinary host adapter cannot infer that
provenance from a new descriptor. Deferred real-host qualification must supply its
own evidence and does not remove any of these simulated cases.

Number callback events and record kind, block range, flush ordinal, selected cut
and returned status. Slot ranges are the fixed first/last pool blocks. Fixed
scenario boundaries distinguish user publication, retained-root advancement and
free-map publication without hooks inside the core. Exercise these selected modes:

- Replacement-write failure before transfer and after a whole-block prefix.
- First-flush failure with no promotion, a selected whole-block prefix promoted,
  or all pending replacements promoted before returning `IO`.
- Slot-write failure leaving the old slot, making the whole new slot durable,
  or promoting a selected byte mixture of old and new slot contents.
- Final-flush failure before, partway through and after promotion, including a
  completely durable publication followed by an error return.
- Selected pre-first-flush crashes with one replacement data or metadata block
  promoted early; acknowledged successful flushes are never undone.

For torn-slot cases, mix bytes inside the active header, such as a 32- or 64-byte
new prefix with the old remainder. Verify the resulting bytes differ from both
complete slots and fail checksum; do not silently count a valid complete slot as
a tear. The current superblock uses only 192 bytes, so copying its first 512 bytes
can produce the entire new slot. These selected byte tears promise no hardware
sector atomicity and do not enumerate every possible failure.

The core must still report unknown outcome once a slot write was attempted,
even when the simulator knows it transferred nothing. Private simulator knowledge
cannot weaken the real callback contract or permit automatic retry.

| Scenario | Required observation |
| --- | --- |
| One bounded overwrite/create/rename | Cuts around replacement writes, first flush, slot write and final flush leave an allowed complete old/new state, never mixed contents or half a rename. |
| Later chunk of write/shrink | Confirmed progress excludes the uncertain chunk; recovered bytes/length match a declared committed boundary. Shrink then growth never exposes discarded partial-block bytes. |
| Post-commit maintenance | Fail/cut in both maintenance publications; preserve user progress, report cleanup separately and enter the specified sticky stopped state. Compare both currently retained payloads after replacement writes and before each maintenance slot write. No next batch or maintenance retry in the stopped instance. |
| Reuse | Trace one range through older-live/newer-retired, retired in both, durably free, then new-live in a later publication with a new birth. |
| Sustained edits and pressure | Populated source trees plus sequential and separately committed small-write histories reuse physical addresses; quota/profile/depth/memory/workspace refusals occur before admission. Exercise whole-map rebuilding, supported sparse old maps and refusal beyond the old-node bound. Resource exhaustion after admission fails validation. |
| Funded drain at minimum resources | For ordinary mutations and final orphan release, admit near computed requirements and formatter floors, then complete every reserved publication without new application mutations or more resources. Space/memory/record/generation refusal during drain is an invariant failure. |
| Deletion capacity | Fill ordinary growth to its permitted quota/profile/pool boundary, then unlink empty files across orphan split and directory changed-path cases, including many retained victims and replacements. Protected deletion capacity remains available; final release drains with no new mutations. |
| Older retained payloads | Quiesce overwrite/reuse while two retained roots still differ; independently compare both states' complete file contents before any maintenance may drop the older root. |
| Retained unlink/replacement | Old views retain identity/bytes/rights; recreated names name new identities. Retained orphan storage stays live and charged. Detached directories remain empty and reject insertion. |
| Final release and reopening | Interrupt bounded orphan batches; marker/object stay paired, cleanup resumes after full validation, and writer access is withheld until startup cleanup completes. |
| Adapter recovery boundary | Failed-instance health is sticky. Cold recovery uses the explicitly durable image. Warm cached validation plus successful backend flush may disagree with durable generation/payload. Any refusal based on simulator history is adapter enforcement, not fresh-core detection; no new flag or registry prerequisite. |
| Recovery refusal | Torn/degraded or unsupported peer, insufficient reserves, unknown grants/extensions and incomplete validation prevent writable opening without changing either slot. Read-only inspection follows its own contract. |
| Authority and continuations | Missing resize rejects an entire extending write; checkpoint rights stay independent; local directory edits invalidate pages while unrelated edits do not. |

Namespace validation must cover 1- and 255-byte names, mixed-length separators,
the exact thirteen-long-directory-record fit, twelve-versus-thirteen long internal
references, and all tail occupancies one through five in bulk packing. Exercise
six-to-five underflow with minimal and byte-full siblings, merge, redistribution,
minimum-key growth splits at successive levels, root split/collapse and empty-root
removal. Use near-limit depth/count fixtures satisfying the profile, and verify
actual new/retired node counts against 15/15 plus the operation envelopes. An
admitted deletion hitting a structural resource limit is a failed editor proof,
not an accepted refusal. Verify writable rejection of an underfilled but otherwise
valid retained tree while read-only checking still succeeds; both roots matter.
Include many tiny directory roots and changing orphan-root presence when checking
the tighter reservation, plus final releases where floor rounding leaves B_i
unchanged. Do not count only dense formatter output as editor validation.

For non-crashing failures, immediately attempt representative read, metadata,
lookup, mutation, checkpoint and close calls. Verify permitted confirmed reads
before publication failure, and blocked cached reads after uncertainty. Trace
closure to prove no retries or recovery writes occur in stopped states.

Use independent expected host files and a namespace/identity manifest. Update
expected bytes/lengths with ordinary host operations, explicitly zeroing regrown
tails, and compare extraction with bounded streaming. Declare each cut's finite
set of permitted complete states; do not accept arbitrary bytes after a confirmed
prefix. Run full checking of both retained states on the unchanged durable image.
A deliberately torn peer is an expected failed/incomplete check, not a clean
writable recovery; require writable refusal and, where selectable, correct
read-only extraction separately.

For at least one overwrite/reuse history, independently compare complete expected
file contents of both retained states at three quiescent cuts: after the user
publication before maintenance starts, then after replacement writes and their
first successful flush but before the slot write in each maintenance publication.
The latter two cuts are mandatory: checking only before maintenance writes cannot
detect premature reuse by replacement metadata. Use bounded streaming comparisons
against independently defined contents, not allocator placement assertions.

For example, after user publication g+1, the first maintenance pre-slot cut still
retains g and g+1, whose payloads deliberately differ. Allow the first maintenance
publication g+2 to complete in a separate run of the history; the second maintenance
pre-slot cut then compares g+1 and g+2. Those payloads may agree, but both retained
mappings must remain intact. Do not demand protection for g after it has legitimately
ceased to be retained; determine the applicable pair from completed publications
and the scenario's independently expected state at each cut.

For the initial post-user-publication cut, stop before the first maintenance
replacement write transfers bytes. At each later maintenance cut, the adapter stops the slot callback before it
transfers any bytes, after all replacement bytes have reached simulated durable
storage. Close the stopped core without writes and keep the image quiescent under
the runner's ownership while comparing both retained payloads and checking the
original two-slot structure. A test-only read adapter may mask the newer slot to
select the older root through the existing degraded read-only path; it changes no
image bytes and is not a clean two-slot writable recovery. Use separate runs from
the same initial scenario to reach later cuts, or a fresh validated open through
the simulator's durable boundary, never a retry in the stopped instance. Required
comparisons occur before the corresponding slot write can drop the older root.
Then exercise reuse only after durable free and fresh protection checks. This
scenario lands with the real publisher and runs in the quick maintained suite.

Near-minimum scenarios must use reachable, fully validated states, with workspace
capacities at `max(formatter floor, computed requirement)` and cases just below
required admission where representable. Include the floor-dominated ordinary
workspace minimum and computed recovery minima at small and large profiles,
occupied retirement tails, fragmented reusable ranges, near-limit record/metadata/
deletion promises, and arenas with only
the reserved budget. Exercise split/changed paths in an ordinary mutation and
last-handle release of an orphan with data/grants through final object/marker
removal. Include generation headroom at its accepted boundary. Refusal before
admission is expected where the proof cannot fund the work; an admitted sequence
must finish absent injected I/O/integrity failure. A defensive `NO_SPACE`, `LIMIT`
or `NO_MEMORY` during its drain is a failed experiment even if no corruption
occurs. Formatter floors alone neither establish nor replace computed sufficiency.

Record exact revisions, geometry, sector/transfer sizes, persisted budgets,
profile/memory/log limits, event traces and content results. Structural checking
does not checksum file payloads; selected-state extraction does not prove payloads
of every retained state. This model establishes only exercised serial failures,
not exhaustive scheduling, actual-device power loss, performance or production-data
safety. Keep ordinary host builds/manual operations and freestanding target
compilation alongside the maintained suite. Implemented coverage and limits are recorded with the focused tasks below;
future-operation scenarios land with their corresponding behavior.

## Agreed delivery boundaries and design acceptance

The authoritative format/core implementation belongs in `pyxis-fs`; this document
is the parent milestone's review contract. Publish dependency commits and PRs
before a separate Pyxis pin/checklist update, and state merge order. No compiler
container rebuild, userspace port, native writable mount or FUSE work is implied.

1. Task 2: private COW tree/map planners, fixed envelopes, bounded arena and
   canonical codecs/checker support. Land the small runner, synthetic-fixture and
   independent-oracle support, `make check` and the filesystem CI workflow in this
   PR, including the accepted framework dependency and its license/provenance.
   Implement formatter namespace tail packing/root repair and its small boundary
   fixtures in task 2, using the same accepted occupancy/byte-fit rules. Start
   with a small existing read-only/format/authority baseline and contract cases
   for the new editors, encoding, occupancy and bounds. Keep the public product
   read-only; no usable writer without admission/reclamation. The owner makes the
   emitted host-contract check required. No empty or future-behavior placeholders.
2. Tasks 3 and 4 together: exclusive writable open, validation summaries, ordered
   publisher, workspace/permanent/profile admission, drain and recovery. Combine
   these into one implementation PR because publication without resource and reuse
   guarantees is unsafe. Add the bounded failure adapter to the same runner and
   gate representative publication/recovery, corruption, resource-boundary and
   funded-drain cases immediately. Use a small synthetic retained-data fixture and
   the real admitted publisher to replace/reuse a range and compare both retained
   payloads through both maintenance replacement/pre-slot cuts without needing a
   public file-write API; a no-op checkpoint is not evidence of publication. The real host checkpoint/reopen command follows the
   ordinary healthy-session contract, with post-error qualification deferred; it
   remains separate from test controls. Add no fake file mutations or public test
   transaction API.
   Until task 6 supplies orphan cleanup, refuse nonempty orphan indexes before
   writes and test that refusal; never pretend startup cleanup succeeded.
3. Task 5: file create/write/resize, creation-time directory serial updates,
   old-EOF zeroing and partial results. Land their ordinary, rights, multi-block,
   partial-progress, content and failure tests in this same PR; exercise older
   retained payloads again through public overwrite/shrink operations. Add the
   relevant large sequential/small-write workload cases to the shared runner.
4. Task 6: directory/remove/rename, retained authority, live continuation changes
   and orphan cleanup/reopening. Land namespace/retained-handle/deletion-headroom,
   final orphan release, funded recovery and malformed orphan tests with these
   operations. Expand quick coverage and the same extended scenario table; no
   second orphan/crash harness.
5. Task 7: run and record combined extended pressure, populated-workload and failure
   campaigns, close remaining coverage gaps and document measured costs/limits.
   The required quick CI gate already exists and remains maintained after closure;
   this is not the task that first adds tests. Preserve independent host builds,
   target compilation and later native-persistence guest validation.

The namespace profile, bounded repair/formatter rules, tighter permanent deletion
reservation and Unity host-test/CI setup are accepted design. Real-host post-error
recovery qualification is deferred; ordinary host writing has the agreed scope
above. Whole-map rebuilding remains accepted only as the initial correctness
approach.

Task 1 accepts the consolidated specification, including the workload validation
profiles/options, concrete representations/interfaces and detailed failure model.
The profile examples are not product defaults or measured capacity guarantees.
The proofs depend on enforced editor and representation bounds; implementation
review must check those invariants, and any violation requires correcting the
bound or design before delivery. Passing host tests alone is not their proof.
Tasks 3/4 were assigned and delivered together. Tasks 5/6 are delivered below.
Task 7 is assigned in bounded steps: the first adds extended campaigns and runs the populated 4 GiB recovery profile. The
64 GiB/256 GiB profiles and milestone closure require a later assignment.

## Focused tasks

1. [x] **Accept the consolidated writable contract.** Accepted policy, concrete
   mechanisms, numerical bounds, failure model and delivery boundaries are
   specified above. No writable implementation is claimed.
2. [x] **Implement bounded COW tree and allocation updates.** Add the required
   index edits, path replacement/splitting and allocation-map accounting, including
   its own replacement blocks. Keep uncommitted changes private and unwind
   failures without modifying a published tree. Deliver the maintained runner,
   initial contract tests and per-PR filesystem CI gate alongside these changes.
   Include formatter namespace tail packing/root repair and boundary cases.
   Delivered in [pyxis-fs #11](https://git.internal/PyxisOS/pyxis-fs/pulls/11); see
   [implemented planners](../../fs/docs/core.md#private-candidate-planning) and [maintained test coverage](../../fs/docs/testing.md).
   The emitted required-check pattern in `pyxis-fs` is
   `Filesystem / host-contract (pull_request)`; the owner configures branch protection.
   Pyxis also runs the pinned dependency's host build and suite inside its required
   `Build Pyxis / build (pull_request)` check, so a failing filesystem pin fails
   the parent PR check.
   All 32 test groups pass, including independent expected payload/format checks,
   variable-length occupancy repair, extent successor and new-split adjacency
   checks, and inconsistent-delta versus corrupt-base error classification.
   Ordinary host/target and parent kernel builds pass; the same suite passes under
   ASan/UBSan. No publication/failure-simulator or guest validation is claimed.
   [Writer stack usage](../technical-debt.md#writable-filesystem-kernel-stack-prerequisite)
   must be resolved before native writable integration; its refactor is deferred.
3. [x] **Implement publication and reopening.** Add exact writes/flushes through
   the platform adapter, ordered two-slot publication, checkpointing and reopening
   of supported committed states. Stop ordinary access on uncertain outcomes. Provide
   only the small host command surface needed to exercise shared-core operations.
   Integrate the shared test failure adapter and gate publication/recovery behavior.
4. [x] **Implement safe reclamation and enforce admission.** Protect retained
   roots and live operations, durably publish freed ranges before reuse, and
   enforce volume/pool/workspace limits. Demonstrate repeated reuse, not just
   monotonically growing allocations. Add admission, funded-drain and independently
   expected retained-payload tests with this behavior, including pre-file-API cases.
   Tasks 3/4 are delivered together in [pyxis-fs #12](https://git.internal/PyxisOS/pyxis-fs/pulls/12),
   pinned at `efe2453`. The 71-group maintained suite covers real private-editor
   publication, user/advance/free write and flush failures, both retained payloads
   before each maintenance slot write, historical-retirement reuse, generation
   boundaries, authority/reentry, ordinary refusals and funded drains at computed
   resource minima. Planning/maintenance read-error cases require pool-wide
   `ACCESS_STOPPED` while preserving confirmed progress; in-place allocation-delta
   heapsort has ordering/content-preservation coverage. Its predecessor's quadratic
   cost is a calculated concern, not a measured benchmark. ASan/UBSan, host tools
   and the freestanding archive pass.
   See [implemented writer](../../fs/docs/core.md#admitted-writer-and-publication),
   [healthy host command](../../fs/docs/host-tools.md#healthy-writer-sessions) and
   [coverage/measurements](../../fs/docs/testing.md#tasks-34-validation-observations).
   This does not qualify real-host post-error recovery, large populated workload
   capacity or native writable operation. The task-3/4 implementation refused nonempty
   orphan indexes before writes; task 6 replaces that temporary restriction with
   funded startup cleanup. Whole-map
   rebuilding and the kernel-stack prerequisite remain recorded limitations.
5. [x] **Implement file mutation.** Create, write and resize with authority
   checks, parent-controlled ownership, sparse/fragmented data, coherent live
   reads and explicit partial-progress/error semantics. Exercise durable reopen
   and byte-for-byte extraction against independent expected contents, with
   maintained operation, authority and failure tests in the same PR.
   Delivered in [pyxis-fs #13](https://git.internal/PyxisOS/pyxis-fs/pulls/13),
   pinned at `531c137`. The 86-group quick suite includes independent expected
   sparse/partial/EOF payloads, multi-batch results, every discovered creation
   allocation failure, 120 long-name creations, authority, live tokens and retained
   payloads through public overwrite/shrink maintenance. All pass natively and
   under ASan/UBSan; the separate populated file workload also passes both.
   Ordinary host tools and the freestanding archive build. Healthy host create,
   write, resize and extraction comparisons are recorded alongside explicit input
   and authority refusals. The parent kernel remains read-only.
   [File mutation](../../fs/docs/core.md#live-file-mutation) documents the implemented
   interfaces. [Task-5 measurements](../../fs/docs/testing.md#task-5-file-mutation-validation)
   record three runs on a 4 GiB image with 12 actual source files and sequential,
   independent small-write, overwrite and shrink/regrow history. The measured
   metadata amplification and planning costs are limitations, not performance
   targets or full-profile capacity evidence. Larger combined campaigns remain
   task 7; no QEMU defaults or kernel-stack implementation changed.
6. [x] **Implement namespace changes and orphan lifetime.** Add directories,
   removal and same-volume regular-file rename/replacement. Preserve identities,
   enforce source/destination rights, retain unlinked objects and recover their
   abandoned storage after restart. Rename publishes the old or new namespace,
   never a half-applied move. Deliver retained-handle, deletion-reservation and
   final-orphan-release/recovery tests with these operations.
   Delivered in [pyxis-fs #14](https://git.internal/PyxisOS/pyxis-fs/pulls/14),
   pinned at `ce88062`. All 107 quick groups and the separate populated file workload
   pass natively and under ASan/UBSan; host tools, the freestanding archive and
   parent read-only kernel build pass.
   Implemented interfaces and reference ownership are documented in
   [namespace changes and orphan lifetime](../../fs/docs/core.md#namespace-changes-and-orphan-lifetime).
   Persisted-accounting coverage requires cleanup retirements to use recovery
   workspace, while user mutations (including retained-orphan writes) use ordinary
   workspace. Last release and startup cleanup use reserved storage without further allocation;
   consumed-close and confirmed startup progress remain reportable on failure.
   Tests cover quota/profile/pool/generation boundaries, 120 retained maximum-name
   victims under quota pressure, detached/recreated directories, explicit replacement,
   local continuation invalidation and cleanup interrupted after confirmed progress.
   [Task-6 coverage](../../fs/docs/testing.md#task-6-namespace-and-orphan-validation)
   records the bounded scenarios and healthy host extraction checks. Larger combined
   campaigns remain task 7; real-host interrupted-session recovery remains unqualified.
   The parent updates read-only close-result consumers and defensively handles
   the new writer-only status; the writer is not kernel-linked and the stack
   prerequisite remains deferred.
7. [ ] **Validate the combined writer and close the milestone.** Exercise
   repeated edits, quota/workspace pressure, retained handles, orphan recovery,
   fragmented/sparse files and the agreed interruption cases. Reopen, extract,
   compare contents and check both retained states. Run the extended and populated
   workload commands; retain the already-required per-PR suite. Record coverage and limits;
   convert this document to an implemented reference and carry remaining work
   forward without claiming native mounting, FUSE or production-data safety.

   **Step 1:** [pyxis-fs #15](https://git.internal/PyxisOS/pyxis-fs/pulls/15)
   adds `make check-extended` and
   `--suite workload --profile recovery --seed 1 --source PATH`. The quick
   107-group `make check` remains the per-PR gate. The extended suite covers
   variable-length names, a valid depth-eight namespace, retained replacements
   and 432 selected publication/cleanup fault cuts. The recovery workload imports
   the agreed 922-object source census and requests the full 1 GiB sequential plus
   2000 separately committed 4 KiB append history, followed by bounded churn.
   [Commands, coverage and observed limits](../../fs/docs/testing.md#extended-campaigns-and-recovery-workload)
   distinguish editor pressure, full-volume admission and independent payload
   expectations. The first recovery run refuses sequential call 1258 at
   314.25 MiB confirmed, with 8187 extents against E=8192; the refused call confirms
   no bytes and leaves the writer healthy. Durable reopen independently verifies
   the full confirmed prefix and source census, with both retained states passing
   structural checks. This is a failed full-workload validation, not an accepted
   capacity guarantee: append/churn phases are not reached. Allocation fragmentation
   and profile sizing need focused review before changing the workload or limits.
   The 64 GiB/256 GiB runs, remaining coverage review and document
   conversion are not delivered in this step; task 7 remains unchecked.
