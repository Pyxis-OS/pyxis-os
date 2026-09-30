# Writable filesystem core and recovery

Status: agreed milestone scope, 2026-09-30. No writable implementation is
claimed. Task 1 settles the remaining mechanisms and numerical bounds before
implementation. Work proceeds one explicitly assigned task at a time.

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
committed partial progress. Do not promise whole-write atomicity or acknowledge
an uncertain publication as successful.

Publication remains pool-wide and serialized. A volume checkpoint covers all
accepted changes in that volume before its ordering point; sibling-volume
changes may be included. Close is not a substitute for checkpointing. The
progress and failure contract below separates operation outcomes from pool
health; task 1 still defines its concrete interface.

Live object handles retain identity and granted authority while observing the
latest committed state. They do not permanently pin the generation at open.
Each operation protects the state it uses. The enumeration and diagnostic-view
contract below keeps ordinary operations live and immutable diagnostics exclusive
to read-only instances; neither permits independent opens of changing media.

Removal leaves retained object handles usable. Storage remains charged while an
object is unlinked but retained. Persistent orphan bookkeeping must let restart
reclaim objects whose runtime owners no longer exist, without treating a named
or otherwise protected object as garbage. The retained-file and directory
behavior and orphan-index lifecycle below are agreed; task 1 still specifies the
on-disk encoding, concrete reference accounting and workspace bounds.

Writable opening requires fully understood, validated media. Refuse writable
access to degraded or unsupported retained states; keep read-only inspection
available under its existing contract. A full validation pass at writable open
is acceptable initially. It is not a requirement to scan the entire pool for
every mutation or reclaim batch. Refusal is not permission to repair or discard
a damaged state automatically.

## Agreed mutation rights and retained files

Agreed during task 1, 2026-09-30; these are core behavior requirements, not
implemented interfaces or the later native ABI mapping.

| Operation | Required held authority |
| --- | --- |
| Write within the existing file length | `file.write` on that file |
| Write that extends the file | Both `file.write` and `file.resize` on that file |
| Explicitly grow or shrink a file | `file.resize` on that file |
| Create a file or directory | `dir.create` on the parent |
| Remove a file or empty directory | `dir.remove` on the parent |
| Rename a regular file within its volume | Source-parent `dir.remove` and destination-parent `dir.create` |
| Replace an existing destination during rename | Rename authority above, plus destination-parent `dir.replace` |

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

## Agreed checkpoint authority

Agreed during task 1, 2026-09-30; the directory right and checkpoint operations
are not implemented yet. Require `file.checkpoint` through a file handle and an
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

Agreed during task 1, 2026-09-30; implementation belongs to task 6.

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

Agreed during task 1, 2026-09-30; implementation belongs to task 6. Use a
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

Writable reopening first validates both retained states, then drains abandoned
orphans from the selected state in bounded transactions before exposing the
writable instance. Old runtime handles do not survive the quiesced close/reopen
boundary; retained on-disk states still protect storage under the usual rules.
An interruption resumes from the last committed state. Insufficient recovery
workspace causes refusal, not unsafe deletion, and degraded or unsupported
retained states do not authorize cleanup. Read-only inspection reports orphans
without cleaning them up.

Opening may take substantial time after many removals or a large abandoned file.
This first writer completes abandoned-orphan cleanup before applications gain
access, avoiding concurrent startup cleanup. Exact record encoding, per-batch
work bounds and admission costs remain task-1 work; this agreement does not
claim that existing reserve defaults suffice or change the orphan-format gate.

## Agreed live enumeration and diagnostic views

Agreed during task 1, 2026-09-30; this specifies behavior, not implemented APIs.
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

Task 1 still defines the continuation representation, directory-change tracking
and operation-reference mechanism. The existing immutable tree-position token
cannot be used against a changing tree without that binding and freshness check.
These decisions do not themselves change the core or native ABI.

## Agreed progress, failures and read availability

Agreed during task 1, 2026-09-30; this specifies behavior, not implemented APIs.
An operation result preserves three independent facts:

- Confirmed progress: for a write, the contiguous byte prefix whose transactions
  completed both required flushes. No byte in an uncertain transaction contributes
  to that count. Namespace operations report whether their transaction committed.
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
| Ordinary permission, quota, workspace or memory admission failure | Current transaction not committed; retain any earlier confirmed prefix | Reads and later mutations remain available. |
| Replacement write or first flush fails before slot publication | Current transaction not committed; retain any earlier confirmed prefix | Stop mutation until recovery. Reads, metadata, listing and lookup may use the last confirmed state while its integrity remains established. |
| Slot write or final flush has an uncertain outcome | Current transaction unknown; retain any earlier confirmed prefix | Stop all ordinary access until recovery, including reads, metadata, listing, lookup, new acquisition and derivation through existing handles. |
| Cleanup after a confirmed user commit fails before its own slot publication | User commit remains confirmed; report cleanup failure separately | A cleanup write/flush failure stops mutation until recovery. Reads may use the last confirmed state, including the confirmed user commit, while its integrity remains established. |
| Cleanup's own slot publication becomes uncertain | User commit remains confirmed; cleanup outcome is unknown | Stop all ordinary access until recovery, even if cleanup was intended to change only allocation bookkeeping. |

Deferring cleanup because a bounded batch cannot be admitted is not itself an
I/O failure or uncertain publication. Keep its storage charged and protected;
reads remain available and later mutations still require full admission. The
reclamation design must separately prove how cleanup eventually makes progress.

These access restrictions are pool-wide, including sibling volumes. In the
readable but mutation-stopped state, ordinary rights still apply, no read uses
the unpublished candidate, and no operation advances retained roots or frees or
reuses blocks. A checkpoint fails while mutation is stopped; it cannot clear the
failure or promise renewed storage health. Reads can still fail with their own
I/O or validation errors. If integrity of the confirmed state is no longer
established, stop ordinary access rather than serving an unproved state or
switching generations under existing handles.

After uncertain publication, even cached object data is unavailable through
ordinary operations. In-memory outcome/health reporting and handle closure remain
available. Closing releases runtime resources without performing recovery writes
or retrying failed cleanup; persistent orphan bookkeeping must survive that
release. Recovery requires quiescing outstanding I/O, closing the instance and
reopening through validation. No independent inspector may open changing media.
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

Task 1 remains open for concrete result/status interfaces,
directory-continuation and operation-reference mechanisms, persistent
orphan encoding, bounded allocation/reclamation algorithms and admission
costs, the recovery validation mechanism, and implementation PR boundaries.

## Publication, reclamation and admission gates

The [existing publication envelope](../../fs/docs/format.md#future-publication-and-reclamation-envelope)
is authoritative: replacement data and metadata, flush, older-slot publication,
then flush before durability acknowledgement. A superblock write is not assumed
atomic. An uncertain write/flush outcome stops mutation until recovery establishes
safe state; no blind retry or generation-counter shortcut.

Both retained roots and live operation/I/O references protect data, metadata,
allocation-map nodes and pool roots alike. Publish free-map changes before later
reuse, with the existing incarnation checks. Reclamation must make sustained
editing possible; an append-only writer that eventually exhausts workspace does
not complete this milestone.

Before implementation, task 1 must establish:

- Maximum transaction dirty bytes/nodes, tree edits, splits and allocation-map
  changes, including how allocator metadata allocation terminates within a bound.
- Admission costs for each operation and reclamation, including recovery progress
  when ordinary space is exhausted. Prototype percentage reserves are not proof
  of sufficient workspace. Specify refusal for images whose budgets cannot meet
  writable minimums; do not silently resize their persisted reservations.
- How validated ownership/reachability evidence is maintained across successive
  commits and reconstructed on reopening, without relying on obsolete readers.
- Live-view identity and orphan encoding, mechanisms for directory-local
  continuation invalidation and operation references, and retained-state
  advancement when no user write follows.
- Concrete interfaces implementing the agreed progress/failure outcomes and
  pool access states, including cleanup reporting and close/reopen requirements.
- Compatibility checks for writable access. Preserve supported extension semantics
  and bytes or refuse writes. Change format versions only for an actual
  incompatibility; do not bump them simply because a writer now exists.

These are implementation decisions to review, not permission to reopen settled
identity, COW, accounting or authority principles. Do not use successful fake
operations, an unbounded in-memory reconstruction, or a new generic transaction
framework as a substitute for the concrete bounded algorithms.

## Focused tasks

1. [ ] **Specify the writable contract.** Audit the pinned format/core, resolve
   the gates above and document operation rights, state transitions, admission
   bounds, failure semantics and the recovery validation model. Establish useful
   PR boundaries for tasks 2–4; they are closely related and need not expose
   artificial public intermediate APIs. Stop for unresolved policy decisions.
2. [ ] **Implement bounded COW tree and allocation updates.** Add the required
   index edits, path replacement/splitting and allocation-map accounting, including
   its own replacement blocks. Keep uncommitted changes private and unwind
   failures without modifying a published tree.
3. [ ] **Implement publication and reopening.** Add exact writes/flushes through
   the platform adapter, ordered two-slot publication, checkpointing and reopening
   of supported committed states. Stop mutation on uncertain outcomes. Provide
   only the small host command surface needed to exercise shared-core operations.
4. [ ] **Implement safe reclamation and enforce admission.** Protect retained
   roots and live operations, durably publish freed ranges before reuse, and
   enforce volume/pool/workspace limits. Demonstrate repeated reuse, not just
   monotonically growing allocations.
5. [ ] **Implement file mutation.** Create, write and resize with authority
   checks, parent-controlled ownership, sparse/fragmented data, coherent live
   reads and explicit partial-progress/error semantics. Exercise durable reopen
   and byte-for-byte extraction against expected contents.
6. [ ] **Implement namespace changes and orphan lifetime.** Add directories,
   removal and same-volume regular-file rename/replacement. Preserve identities,
   enforce source/destination rights, retain unlinked objects and recover their
   abandoned storage after restart. Rename publishes the old or new namespace,
   never a half-applied move.
7. [ ] **Validate the combined writer and close the milestone.** Exercise
   repeated edits, quota/workspace pressure, retained handles, orphan recovery,
   fragmented/sparse files and the agreed interruption cases. Reopen, extract,
   compare contents and check both retained states. Record coverage and limits;
   convert this document to an implemented reference and carry remaining work
   forward without claiming native mounting, FUSE or production-data safety.

## Validation authorization and limits

The owner explicitly agreed to bounded host-side crash/failure validation for
this milestone. This is a scoped exception to the normal prohibition on fault
injection, not permission for a general testing framework, new CI, kernel probes
or boot automation. Task 1 must propose the concrete small mechanism and agree
its coverage before implementation.

Use disposable images and the real shared core. Cover interruption around
replacement writes, both flush boundaries and slot publication, plus selected
write/flush failures and reclamation/orphan transitions. Distinguish failures
known to precede publication from outcomes that may already be durable.
Define which writes reach simulated durable storage, including torn publication
if claimed: merely killing a process while host caches survive does not simulate
power loss or prove flush ordering. State exactly what the model establishes.

Pair structural checks with extracted-content comparisons: the existing checker
does not checksum payloads. Keep ordinary host builds/manual operations and
freestanding target compilation. Record exact revisions, budgets and image
geometry. No physical-device power-loss or exhaustive failure coverage follows
from a bounded host model.

Dependency changes belong in published `pyxis-fs` PRs before Pyxis updates its
pin. Other repository PRs are needed only for actual changes. No compiler
container rebuild or new userspace port is implied by this milestone.
