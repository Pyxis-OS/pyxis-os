# Native filesystem format proposal

Task 1 of the [native filesystem milestone](native-filesystem.md), completed as
an owner-approved design on 2026-10-03. The choices below are accepted; byte
budgets and record encodings make them concrete for task 2. This is not an
implemented format or authorization to begin the format library or writer.
The old filesystem and its native read-only mounts remain unchanged.

## Geometry and extension

Blocks are 4 KiB. Integers are little-endian and block addresses are 64-bit,
relative to the pool partition. A distinct magic identifies this format beside
the old core; this is not a new version of the old format. Each pool and volume
has a nonzero 16-byte identity. Identity grants no authority: capabilities remain
the access-control boundary, with no on-disk users or permissions.

The pool header carries a 64-bit required-feature mask. Unknown bits reject
opening, including read-only opening. V1 defines block-pointer mapping only;
a later mapping can reinterpret its pointer area behind a feature bit. Fixed
records reserve bytes as shown below; unused bytes are initially zero and cannot
acquire meaning without a feature flag. V1 flags are zero unless defined here.
Bitmap and indirect blocks are raw arrays governed by the pool format, without
separate headers; changing their interpretation likewise requires a feature bit.
Exact magic/flag constants and encoding helpers belong to the format-library task.

## Pool layout

| Structure | Layout and reserve |
| --- | --- |
| Primary and backup headers | One block each: 128-byte field area and 3,968 reserved bytes. |
| Allocation bitmap | One bit per pool block, rounded up to whole blocks. |
| Volume table | 64 fixed 512-byte records: eight blocks total. |
| Journal | Two control blocks, followed by reusable descriptor and metadata-image space. |
| Volume storage | Inode-file, indirect, directory and regular-file blocks allocated from the shared bitmap. |

The primary header is block zero; the backup is the last complete partition
block. The header records magic, pool identity, block size/count, required
features, start/count pairs for bitmap, table and journal, and a CRC32C checksum
of the complete header block with its checksum field zeroed. These fields fit
within 128 bytes. The two copies are identical and immutable while mounted in v1.
One valid copy suffices; two valid copies that disagree require repair. Checksums
detect accidental damage, not malicious media, and do not replace bounds checks.

Validate geometry against the partition, nonoverlap of all fixed regions and
bitmap capacity. Both headers and all bitmap/table/journal blocks are permanently
allocated. Bitmap bit one means allocated; bit order is least-significant first
within each byte. Padding bits beyond the pool are set. Block zero is therefore
available as the null-pointer encoding, never as allocated file storage.
A trailing partial partition block is unused. Fixed-region locations are chosen
when formatting; no mounted pool expansion or journal relocation is defined.

## Volumes and inodes

Volumes own no predetermined range; their files grow from the shared pool.
The fixed table limits the pool to 64 volumes. Each live record has a unique ID
and name within the pool, a root inode, cleanup-list head, and embedded inode-file
length/mapping. Embedding the mapping avoids having to read the inode file to
find that same file. An unused volume record is all zero. The volume record's
field budget is:

| Area | Bytes |
| --- | ---: |
| ID | 16 |
| State, flags, mapping type (32 bits each), name length (16 bits) | 14 |
| Root inode, cleanup head, inode-file length (64 bits each) | 24 |
| Field-area padding | 2 |
| Name storage, counted UTF-8 bytes followed by zeros | 256 |
| Twelve direct and three indirect pointers (64 bits each) | 120 |
| Separate future-feature reserve | 80 |
| Total | 512 |

Names use the component rules below, up to 255 bytes. There are no volume quotas
or guaranteed reservations in v1; one volume can exhaust pool free space. The
formatter creates its selected volumes. This format does not by itself add a
runtime volume-management API.

An inode file grows as a dense array of 256-byte records. Inode numbers index
that array; slot zero is reserved and inode one is the root directory. Zero inode
numbers also terminate cleanup lists and mark unused directory records. Kind zero
marks a free inode; reuse requires completed cleanup and no remaining live
references. The inode-file mapping has the same pointer scheme as ordinary
inodes, and its blocks are metadata.

| Inode area, in order | Bytes |
| --- | ---: |
| Kind and mapping type (16 bits each), flags (32 bits) | 8 |
| Byte length, next cleanup inode, shrink target (64 bits each) | 24 |
| Cleanup state (32 bits) | 4 |
| Unassigned bytes within the 72-byte current-field area | 36 |
| Twelve direct and three indirect pointers (64 bits each) | 120 |
| Separate future-feature reserve | **64** |
| Total | 256 |

The initial implementation cannot consume the separate 64-byte reserve. Regular
files and directories are the only ordinary inode kinds. The mapping has twelve
direct pointers and one each for single, double and triple indirection; an indirect
block holds 512 pointers. Zero pointers mean absent mappings. Regular-file holes
read as zero. The mapping addresses 550,831,702,016 bytes, roughly 513 GiB, per
file; a 64-bit size field does not override that mapping limit. No timestamps,
link counts or permission fields are added merely to occupy unused space.

## Directories

A directory is a dense, block-aligned file containing an unsorted list of entries.
Each has a 32-byte header: 64-bit inode number, 16-bit record length, 16-bit name
length, 32-bit flags and **16 reserved bytes**, followed by the name and zero
padding. Record lengths are multiples of eight, at least 32, and cannot cross a
block boundary. Records, including free entries, cover the entire block. An inode
number of zero marks a free entry, with zero name length; its space can be reused
or merged with adjacent free records. Inode kind is authoritative.

Names are case-sensitive UTF-8, 1–255 bytes, excluding NUL, slash, `.` and `..`;
comparison uses exact bytes without normalization. Names are unique within each
directory. Each linked inode has one directory entry, except the unnamed root.
Hard links and symbolic links are deferred. Lookup is linear in directory size;
indexing can arrive through a feature flag. Namespace bindings and path resolution
remain outside the format. Removing directories requires them to be empty.

## Journal and recovery

The installer/formatter chooses journal capacity **per pool**, not through a build
setting. For the 256 GB target the owner requires **at least 128 MiB**, starting
at 128 MiB: 32,768 blocks, about 0.052% of that disk. Smaller-image defaults are
not established; those format invocations must select a suitable size explicitly.
The chosen region stays fixed for the pool's lifetime in v1. Capacity is not a
requirement to fill or write the entire region on each commit.

Only one transaction is committed and checkpointed at a time. It journals whole
replacement metadata blocks, not regular-file contents. Journal records have:

| Record | Contents and reserve |
| --- | --- |
| Control block | Magic, pool ID, 64-bit sequence, EMPTY/COMMITTED state, image/descriptor-block counts, payload CRC32C and full-block CRC32C in a 128-byte field area; **3,968 reserved bytes**. |
| Descriptor | 64-bit home-block address, 32-bit metadata kind, 32-bit flags and **16 reserved bytes**: 32 bytes total. |
| Image | One complete 4 KiB metadata block per descriptor, in descriptor order. |

Descriptor blocks precede images; unused descriptor space is zero. The payload
checksum covers pool ID, sequence and counts followed by descriptor blocks and
images, binding the payload to its control record. Descriptor kinds distinguish
bitmap, volume-table, inode-file, directory and indirect blocks. With N images,
capacity requires `2 + ceil(N / 128) + N <= journal_blocks`; a 128 MiB region
holds up to 32,512 images. Admission reserves the complete next batch before
metadata changes. An indivisible namespace operation must fit or fail before it
changes the namespace. The writer must establish its worst-case admission bound;
a formatter must not advertise writable support with insufficient capacity.

Formatting initializes both control blocks as EMPTY, with sequences zero and
one, and zero payload counts/checksum. Starting from the highest valid EMPTY:

1. Freeze the transaction's metadata images and ordered-data dependencies. Keep
   uncommitted metadata out of home locations, including newly allocated metadata.
   Write and flush the required file data and journal payload.
2. Publish COMMITTED with the next sequence into the older control slot and flush.
   Only now may those metadata images be written to their home locations.
3. Checkpoint all images and flush. Publish EMPTY with the next sequence into the
   older slot and flush before reusing journal space or releasing freed blocks.

Sequence exhaustion stops rather than wrapping. Recovery selects the highest
valid control; one torn copy can be ignored, but no valid control, conflicting
equal sequences, or a checksum-valid unknown state requires repair/rejection.
A selected EMPTY needs no replay. For COMMITTED, validate the entire payload before
writing any home block: checked counts/ranges, checksums, kinds and unique home
addresses. Exclude headers/journal as destinations and enforce fixed-region kinds.
Do not require traversing the partially checkpointed home tree to validate the log;
whole-pool ownership reconciliation belongs to `fsck`. Invalid committed payload
is an error, not an uncommitted transaction to discard. Replay images, flush homes,
then publish/flush the next EMPTY before admitting new writes. Replay is repeatable
if recovery itself is interrupted.

Read-only opening requires a selected EMPTY: recovery requires write authority.
Pending inode cleanup may remain unreclaimed for read-only access. No recovery
into RAM is defined. A 4 KiB write is not assumed atomic; control copies/checksums
work with Caelum's [ordered flush contract](../devices/block-storage.md#ordering-persistence-and-failure).
An uncertain write/flush failure stops mutation, without promising rollback or
continued writable use. Arbitrary media corruption can still require repair.

## Cleanup and live handles

Each volume has one persistent cleanup list; an inode appears at most once.
Its cleanup state can represent detached-file cleanup, shrink cleanup or both.
Unlink/replacement records detachment in the same transaction as the directory
edit. A shrinking truncate records its smaller length, target and list membership
atomically. Unlink during shrink adds detached state to the existing entry;
shrink cleanup can finish while its open handles survive. Afterwards detached
state keeps the inode listed until its final references and operations drain.
This preserves the [existing handle lifetime contract](../interfaces/filesystem-mutations.md).

Cleanup walks mapped blocks beyond the target, removing highest mappings first
and freeing empty indirect blocks. Pointer removal, allocation-bit clearing and
list/state changes commit together. Cleared pointers are durable progress, so
recovery resumes from the remaining mapping without a second progress structure.
Cleanup needs no new disk blocks. A detached inode's remaining storage is reclaimed
only after its references drain; after reboot no old handles survive. Only fully
reclaimed detached inodes become free slots. Completed shrink-only cleanup removes
its list entry without freeing the live inode.

Every freed block stays unavailable, even within the freeing transaction, until
home writes and the newer EMPTY are durable. This includes former directory and
indirect blocks that recovery might still need. During shrink cleanup, subsequent
writes/resizes of that inode wait; reads respect the new size. Growth must zero or
supply every newly exposed byte, including retained-block tails, before exposure;
those data writes are ordered dependencies of the size-growth commit. Unmapped
holes remain zero without allocating storage. In-place data writes and large
writes/cleanup spanning transactions do not promise whole-operation crash atomicity.

## Scope and next task

Task 2 defines the format-only library and host formatter/checker/inspector in
pyxis-fs beside the old core. Caelum owns caching, allocation policy and the writer;
these do not migrate into the shared library. No new persistent permission model,
public capability API, compiler-container rebuild or dependency-pin change is part
of this proposal. The native installer is a later consumer of the same format.

`fsync`/`sync` remain durability points and close releases a handle without such a
promise. The milestone's proposed flush interval, whole-transaction `fsync` policy
and delayed allocation remain unaccepted; resolve them before the writer task.
Implementation must measure latency and bytes written from the start, including
large shrink stalls and journal batching. Accepted [design limits](../technical-debt.md#native-filesystem-design-limits)
have explicit revisit points. Task 1 is complete; task 2 requires assignment.
