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
changes may be included. Close is not a substitute for checkpointing. Task 1
defines the exact progress/error interface, including uncertain outcomes.

Live object handles retain identity and granted authority while observing the
latest committed state. They do not permanently pin the generation at open.
Each operation protects the state it uses. Existing immutable diagnostic views
must have an explicit coexistence policy; do not silently turn them into live
views or permit independent read-only opens of changing media.

Removal leaves retained object handles usable. Storage remains charged while an
object is unlinked but retained. Persistent orphan bookkeeping must let restart
reclaim objects whose runtime owners no longer exist, without treating a named
or otherwise protected object as garbage. The directory behavior below is agreed;
task 1 still specifies orphan representation and the remaining retained-handle
rules in detail.

Writable opening requires fully understood, validated media. Refuse writable
access to degraded or unsupported retained states; keep read-only inspection
available under its existing contract. A full validation pass at writable open
is acceptable initially. It is not a requirement to scan the entire pool for
every mutation or reclaim batch. Refusal is not permission to repair or discard
a damaged state automatically.

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

This resolves the directory-lifetime policy only. Task 1 remains open for the
operation-rights table, exact error/progress interfaces, diagnostic-view and
directory-continuation behavior, persistent orphan representation, bounded
allocation/reclamation algorithms and admission costs, recovery validation
mechanism, and implementation PR boundaries. The existing format requires a
read-required feature and explicit orphan-root semantics before unlinked objects
can persist; these directory rules do not by themselves define that encoding.

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
- Live-view identity, orphan representation, directory continuation invalidation,
  operation references and retained-state advancement when no user write follows.
- Failure outcomes before publication, during uncertain publication and during
  cleanup, including what remains readable and what requires close/reopen.
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
