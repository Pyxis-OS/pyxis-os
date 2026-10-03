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
   serializes commits. Journal sizing and large-operation splitting remain open.

These supplement the milestone's accepted capability-only authority, block-pointer
mapping and explicit `fsync`/`sync` durability. They do not accept its proposed
30-second flush policy, whole-transaction `fsync` semantics or delayed allocation.

## Structures to specify

| Structure | Contents and purpose |
| --- | --- |
| Pool header | Format identification, geometry, pool identity, feature flags, bitmap/table/journal locations and reserved bytes. Copy and validation rules remain open. |
| Allocation bitmap | One bit per pool block; shared by all volumes. Fixed metadata and journal blocks must be reserved from file allocation. |
| Volume table | 64 records containing identity, name, root inode and the inode-file mapping. Record encoding and unused-slot representation remain open. |
| Inode file | Growable array of fixed-size inode records; the proposed mapping and free-slot representation are below. |
| Directory file | Maps a component name to an inode within the same volume; proposed record rules are below. |
| Journal | Replacement metadata images, their destination blocks and transaction commit information. Recovery record encoding remains open. |

New structures will reserve bytes for later extensions. Exact offsets and flag
values belong to the completed proposal and format library; this draft does not
establish an ABI by omission.

## Next owner decisions

Proposals only. Each can be changed or deferred independently.

1. **256-byte inodes with direct and indirect block pointers.** Use twelve direct
   pointers and one each for single, double and triple indirection. An indirect
   block contains 512 little-endian 64-bit pointers; zero means no block. Missing
   file-data mappings read as zero. This addresses roughly 513 GiB per file.
   Inode records carry kind, mapping type, byte length, flags and reserved bytes;
   kind zero marks a free slot. Inode numbers index this file and cannot be reused
   while an old object is still held. A volume record embeds the same mapping
   for its inode file. The fixed inode size trades some space for simple indexing
   and room for future fields.
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

The third proposal preserves the [existing file-handle lifetime contract](../interfaces/filesystem-mutations.md)
without requiring every large deletion to fit one journal transaction. It does
not promise atomic whole-file writes or durable unsynced data.

## Remaining design work

After the next round: settle header copies and feature handling, journal sizing,
commit/replay validation, and cleanup progress rules. Keep the decisions in rounds
of at most three; no choice in this section is accepted implicitly. A metadata
journal must not assume that a 4 KiB write is atomic: Caelum's
[block interface](../devices/block-storage.md#ordering-persistence-and-failure)
requires successful flushes for persistence and does not promise that atomicity.

Writeback scheduling and `fsync` policy remain the separate proposals in the
milestone. They need resolution before the writer task, without turning cache
policy into an on-disk format requirement.

## Task handoff

Branch: `docs/native-filesystem-format`, based on Pyxis `d9b88e4`. This work changes
only Pyxis design documents and no dependency revisions. Round 1 is accepted;
round 2 above awaits the owner. Task 1 remains unchecked. No build, boot or
filesystem implementation has been attempted; no validation processes are running.
