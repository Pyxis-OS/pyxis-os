# Native filesystem format proposal

Task 1 of the [native filesystem milestone](native-filesystem.md). This is a
design in progress, not an implemented format or permission to begin task 2.
Only the explicitly accepted choices below are settled. The old filesystem and
its native read-only mounts remain unchanged.

## Accepted foundation

Owner accepted these three choices on 2026-10-03:

1. **Geometry and encoding.** Fixed 4 KiB blocks, little-endian fields and
   64-bit block addresses relative to the pool partition. Headers carry feature
   flags and reserved bytes. One block size serves allocation, caching and journal
   records.
2. **Volumes.** A fixed table holds up to 64 volumes. Each has a stable ID,
   name, root inode and growable inode file, allocating from the pool-wide bitmap.
   The volume record embeds the inode file's initial mapping so opening it does
   not require reading that same file first. The volume count is fixed; file and
   inode capacity grow as needed within available space and mapping limits.
3. **Journal.** One pool-wide metadata journal holds complete replacement
   metadata blocks. One transaction is committed and checkpointed at a time:
   required file data becomes durable before commit, then committed metadata is
   copied to its final locations before the journal space is reused. This
   serializes commits. Journal configuration is recorded below; large-operation
   splitting remains open.

These supplement the milestone's accepted capability-only authority, block-pointer
mapping and explicit `fsync`/`sync` durability. They do not accept its proposed
30-second flush policy, whole-transaction `fsync` semantics or delayed allocation.

### Accepted journal capacity

On 2026-10-03 the owner chose a configurable journal size and **at least 128 MiB
for the 256 GB target**, replacing the proposed 16 MiB default for that target.
Use 128 MiB as the starting target value. This reserves about 0.052% of a 256 GB
disk and provides 32,768 filesystem blocks before control/descriptor overhead.
The journal holds metadata images, not file contents, and a commit writes only
its used records. Capacity does not require every transaction to fill the journal.

The installer/formatter chooses the size per pool; this is not a build setting.
This decision does not settle defaults for small images or authorize journal
resizing on an existing pool. The recovery and cleanup proposals below remain open.

## Structures to specify

| Structure | Contents and purpose |
| --- | --- |
| Pool header | Format identification, geometry, pool identity, feature flags, bitmap/table/journal locations and reserved bytes. Copy and validation rules remain open. |
| Allocation bitmap | One bit per pool block; shared by all volumes. Fixed metadata and journal blocks must be reserved from file allocation. |
| Volume table | 64 records containing identity, name, root inode and the inode-file mapping. Record encoding and unused-slot representation remain open. |
| Inode file | Growable array of fixed-size inode records; accepted mapping and free-slot rules are below. |
| Directory file | Maps a component name to an inode within the same volume; accepted record rules are below. |
| Journal | Replacement metadata images, their destination blocks and transaction commit information. Recovery record encoding remains open. |

New structures will reserve bytes for later extensions. Exact offsets and flag
values belong to the completed proposal and format library; this draft does not
establish an ABI by omission.

## Accepted inodes, directories and cleanup

Owner accepted these choices, including the explicit inode reserve, on 2026-10-03.

1. **256-byte inodes with direct and indirect block pointers.** Use twelve direct
   pointers and one each for single, double and triple indirection. An indirect
   block contains 512 little-endian 64-bit pointers; zero means no block. Missing
   file-data mappings read as zero. This addresses roughly 513 GiB per file.
   Inode records carry kind, mapping type, byte length, flags and reserved bytes;
   kind zero marks a free slot. Inode numbers index this file and cannot be reused
   while an old object is still held. A volume record embeds the same mapping
   for its inode file. The fixed inode size trades some space for simple indexing
   and room for future fields. Of its 256 bytes, 120 hold pointers, 72 form the
   current-field area and **64 are reserved for future format features**. The
   initial implementation cannot consume that separate reserve.
2. **Directories are unsorted lists of variable-length entries.** Each entry has
   an inode number, record length, name length and name bytes. Records stay within
   one block, and freed entries can be reused. Names are case-sensitive UTF-8,
   1–255 bytes, with no NUL, slash, `.` or `..`; comparison uses exact bytes without
   normalization. Inode kind is authoritative. V1 has regular files and directories,
   with one directory entry per linked inode (except the unnamed root); hard links
   and symbolic links are deferred. This preserves the existing native name rules
   and keeps lookup linear in directory size. Namespace bindings and path resolution
   remain outside the disk format.
3. **A persistent cleanup list preserves open-after-removal behavior.** Each
   volume records a list head, and each inode has a next-cleanup field and a cleanup
   state. Removing a name or replacing its destination records the detached inode
   in the same transaction as the directory edit. Existing handles retain access;
   reclamation starts after the last handle closes. After journal replay at boot,
   there are no surviving handles, so recovery resumes that cleanup. Freeing blocks
   proceeds in journal-sized batches that update both bitmap and pointers together;
   the inode stays listed until its storage is reclaimed. Large shrinking truncates
   can use the same list with a recorded target size, reclaiming only beyond that
   target. Admission, concurrent-operation rules and transaction bounds must be
   specified before this mechanism is ready for implementation.

The cleanup choice preserves the [existing file-handle lifetime contract](../interfaces/filesystem-mutations.md)
without requiring every large deletion to fit one journal transaction. It does
not promise atomic whole-file writes or durable unsynced data.

### Inode space budget

The required fields fit without consuming the separate reserve. This field-width
budget is a layout proposal within the accepted allocation, not extra behavior:

| Area | Bytes |
| --- | ---: |
| Kind and mapping type, 16 bits each; flags, 32 bits | 8 |
| Byte length, next cleanup inode and cleanup target size, 64 bits each | 24 |
| Cleanup state, 32 bits | 4 |
| Unassigned bytes within the 72-byte current-field area | 36 |
| Twelve direct pointers and three indirect pointers, 64 bits each | 120 |
| Separate future-feature reserve | 64 |
| Total | 256 |

Unassigned and reserved bytes are initially zero; future meanings require feature
flags. The mapping type also allows a future mapping to reinterpret the pointer
area. No timestamps, permissions or other new behavior are implied by free space.

## Next owner decisions

Proposals only. Each can be changed or deferred independently.

1. **Two checked pool headers, with strict feature handling.** Put the primary
   header at pool block zero and its backup at the last complete block. They
   describe fixed geometry, identity and bitmap/table/journal extents; mutable
   roots and cleanup state live in journaled volume records instead. A CRC32C
   checksum covers each header. Validate bounds, nonoverlap, bitmap capacity and
   permanent reservation of fixed metadata. One valid copy suffices, but two valid copies
   that disagree are an error, not an invitation to guess. V1 does not rewrite
   the headers while mounted. Unknown required-feature bits reject a mount,
   including read-only mounts; reserve space for finer compatibility rules later.
   Other record types also retain explicit reserved bytes. Checksums detect
   accidental damage, not malicious media, and do not replace bounds checks.
2. **One reusable journal area, with the accepted target capacity above.** The
   chosen capacity is stored when the pool is formatted and stays fixed in v1. Two
   alternating, checksummed control blocks record a sequence number and EMPTY or
   COMMITTED state; formatting initializes both. A checksum-valid unknown state
   is rejected, never treated as a torn copy. Descriptors name destination blocks;
   the control records the image count and payload checksum. The complete descriptor
   set and metadata images are bound to the transaction sequence and checksummed.
   Write and flush the ordered file data and journal payload before publishing
   COMMITTED, then flush that control record. Checkpoint the metadata to its home
   blocks and flush before publishing and flushing a higher-sequence EMPTY record.
   Only then reuse payload space. Recovery selects the highest valid control
   sequence; a committed transaction is fully validated before any replay write.
   Invalid committed payload is an error, not an uncommitted transaction to skip.
   Home destinations must be unique, valid metadata blocks outside the headers
   and journal. Control sequence exhaustion fails instead of wrapping. No valid
   control, or conflicting controls at the same sequence, requires repair.
   Read-only opening requires an empty journal; recovery requires write authority.
   Pending cleanup can remain unreclaimed during read-only access. Writable
   recovery completes replay and the EMPTY flush before admitting new transactions.
3. **Bounded cleanup, with serialization on the affected inode.** Removing a
   name or committing a smaller size records its cleanup state atomically. Each
   reclamation batch journals pointer removal, allocation-bit clearing and progress
   together; it never frees a block still reachable in the committed mapping.
   Every freed block remains unavailable for reuse, even within the freeing
   transaction, until home writes and the newer EMPTY control are durable. This
   includes former directory and indirect blocks whose old contents recovery may
   still need. Shrink cleanup blocks subsequent writes and resizes
   of that inode until done, while reads respect the smaller size. Before later
   growth exposes bytes, they must be zeroed or supplied by the write, including
   the retained partial block; those data writes are ordered dependencies of the
   size-growth commit. A detached open file remains usable until final
   close; its inode cannot be reclaimed early. Before modifying metadata, reserve
   journal capacity for the complete next batch. An indivisible namespace operation
   must fit or fail before changing the namespace; large data writes and cleanup
   may span transactions and do not gain whole-operation crash atomicity.
   Cleanup itself needs no new disk blocks. List insertion/removal and cleanup
   state changes share the transaction that requires them. Unlink during shrink
   changes the existing entry to detached-file cleanup, never adding a duplicate.

A metadata journal must not assume that a 4 KiB write is atomic: Caelum's
[block interface](../devices/block-storage.md#ordering-persistence-and-failure)
requires successful flushes for persistence and does not promise that atomicity.
The proposed control copies and checksums handle incomplete writes under that
contract. Any uncertain write/flush failure stops further mutation; no rollback
or continued writable use is promised. Corruption and failed storage can still
require repair; successful recovery is not guaranteed for arbitrary damage.

## Remaining design work

After this round, finish record sizes/reserves, journal validation fields and
cleanup progress details within the accepted design. Any material behavior choice
still unresolved comes back to the owner, at most three at a time. The final
proposal must cover directory and volume reserves as explicitly as the inode.

Writeback scheduling and `fsync` policy remain the separate proposals in the
milestone. They need resolution before the writer task, without turning cache
policy into an on-disk format requirement.

## Task handoff

Branch: `docs/native-filesystem-format`, based on Pyxis `d9b88e4`. This work changes
only Pyxis design documents and no dependency revisions. Rounds 1 and 2 are
accepted, as are per-pool installer/formatter configuration and the 128 MiB minimum
for the 256 GB target. The remaining round 3 proposals await the owner. Task 1
remains unchecked. No build, boot or filesystem implementation has been attempted;
no local validation processes are running.
