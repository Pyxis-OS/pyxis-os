# Native filesystem format proposal

Task 1 of the [native filesystem milestone](native-filesystem.md): a format
proposal, not an implemented format or authorization to begin task 2. Owner
decisions are listed below. The remaining record layouts, encodings and policies
are **agent proposals**, not implicitly accepted requirements. The old filesystem
and its native read-only mounts remain unchanged.

## Decision status

Accepted in the owner's task-1 discussion on 2026-10-03: 4 KiB blocks, little-endian
64-bit addresses, 64 volume slots with growable inode files, block-pointer mapping,
one pool-wide metadata journal, 256-byte inodes, simple directory lists, persistent
cleanup, two checked headers, committed-journal recovery and bounded reclamation.
The inode budget of 120 pointer bytes, 72 current-field bytes and a separate
**64-byte future-feature reserve** was explicitly accepted. Journal capacity is
chosen per pool by the installer/formatter, with at least 128 MiB for the 256 GB
target. Shrink cleanup serializes writes/resizes to the affected inode.

The [PR review follow-up](https://git.internal/PyxisOS/pyxis-os/pulls/338) relays
additional owner decisions: compatible/read-only-compatible/required feature masks,
creation and modification timestamps, an internal parent number in directory
inodes, an in-memory free-inode list and sync completion at durable COMMITTED.
The owner confirmed signed 64-bit Unix-nanosecond timestamps, saturation to the
representable limits on write, and 12 remaining unassigned inode bytes. Mutations
proceed with an explicitly unknown affected timestamp if the clock is unavailable.

Agent details below include the 80-byte volume reserve, 16-byte directory-entry
reserve, exact record widths/numbering, absence of home-metadata checksums and
refusal of a dirty read-only mount. These remain proposed choices; the explicit
64-byte inode reserve is an accepted exception. Task 2 must preserve this split
and settle remaining policy choices before implementing them.

## Geometry and extension

Blocks are 4 KiB. Integers are little-endian and block addresses are 64-bit,
relative to the pool partition. A distinct magic identifies this format beside
the old core; this is not a new version of the old format. Each pool and volume
has a nonzero 16-byte identity. Identity grants no authority: capabilities remain
the access-control boundary, with no on-disk users or permissions.

The owner accepted three feature classes; the proposed encoding uses three 64-bit
masks. Unknown compatible bits are ignored. Unknown read-only-compatible bits
allow read-only opening but refuse writable opening. Unknown required
(incompatible) bits refuse all opening. Recovery writes are also prohibited when
an unknown read-only-compatible feature prevents writable use. A feature is
compatible only if older readers and writers can safely ignore it; classification
must account for updates to its fields, not merely successful decoding.
V1 defines block-pointer mapping only;
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
block. The header records magic, pool identity, block size/count, all three
feature masks, start/count pairs for bitmap, table and journal, and a CRC32C checksum
of the complete header block with its checksum field zeroed. These fields fit
within 128 bytes. The two copies are identical and immutable while mounted in v1.
One valid copy suffices; two valid copies that disagree require repair. Checksums
detect accidental damage, not malicious media, and do not replace bounds checks.
The proposed initial layout has checksums on headers and journal records/payload,
not on metadata at its home location. Once the journal is cleared, it cannot
detect every later home-metadata corruption. This is an agent-proposed integrity
limit, not an owner decision to omit future metadata checksums.

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
| Creation and modification times (signed 64-bit nanoseconds each) | 16 |
| Directory parent inode (64 bits) | 8 |
| Unassigned bytes within the 72-byte current-field area | 12 |
| Twelve direct and three indirect pointers (64 bits each) | 120 |
| Separate future-feature reserve | **64** |
| Total | 256 |

The initial implementation cannot consume the separate 64-byte reserve. Regular
files and directories are the only ordinary inode kinds. The mapping has twelve
direct pointers and one each for single, double and triple indirection; an indirect
block holds 512 pointers. Zero pointers mean absent mappings. Regular-file holes
read as zero. The mapping addresses 550,831,702,016 bytes, roughly 513 GiB, per
file; a 64-bit size field does not override that mapping limit. Creation and
modification timestamps are accepted for v1; access/change times are excluded.
Creation time is set on inode allocation. Modification time changes with file
contents/size or directory entries, not with journal checkpointing. Wall time is
not a monotonic change counter and cannot alone guarantee distinct values for
successive edits. The clock's [WALL_NOW](../kernel/wall-clock.md#native-interface)
returns signed 64-bit Unix seconds plus a normalized nanosecond fraction, not
a single signed count of nanoseconds. The owner chose **signed 64-bit nanoseconds
since the Unix epoch, UTC**, eight bytes per timestamp. When writing, compute
`seconds * 1000000000 + nanoseconds` with checked/wider arithmetic and clamp to
`INT64_MIN` or `INT64_MAX` if outside the representable range. Do not overflow
an intermediate or clamp seconds before adding the fraction. This is a conversion
from the clock API, not its existing ABI layout. The owner accepted that an
unavailable clock does not fail a mutation: the affected time is marked unknown.
Proposed representation uses one validity bit per time in the existing inode
flags, with unknown time payload zeroed. A valid epoch-zero time is distinct from
unknown; clamped known times remain valid, and clamp limits are not unknown markers.
Allocation without a clock leaves creation time unknown; later mutations
update modification validity/time without inventing a creation time.

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

The owner accepted a 64-bit parent inode number in the directory inode's
current-field area. Root inode one names itself as parent. The slot has no meaning
for regular files and is proposed to be zero there. This is internal metadata:
it creates neither a `..` entry nor parent authority for a directory capability.
A directory move updates it in the same transaction as both directory-entry edits.
It supports ancestor walking for cycle checks, checker/repair work and internal
path reconstruction; it does not add a move or path-disclosure API. Proposed
detachment encoding is parent zero; linked nonroot directories require exactly
one matching entry in their recorded parent. Detached cleanup entries and the
self-parented root are exceptions to that backlink check.

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
   Only now may those metadata images be written to their home locations. The
   owner accepted returning `fsync`/`sync` at this durability point for the work
   covered by that commit, after all required data is durable.
3. Checkpoint all images and flush. Publish EMPTY with the next sequence into the
   older slot and flush before reusing journal space or releasing freed blocks.

Accepted task-3 policy: step 3 runs in the background after sync completion, before
the next commit. The transaction still costs four flushes; the first two establish
durability and the last two finish checkpoint/reuse. Keep committed images stable
through checkpointing. A sync covering several bounded transactions waits for all
required commits and data; one early batch is not completion of the whole request.
Measure foreground latency and the remaining throughput cost separately.

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

**Proposed read-only policy:** opening requires a selected EMPTY; replay requires
write authority and understood writable features. Pending inode cleanup may remain
unreclaimed for read-only access. Recovery into RAM is not included in this proposal.
A 4 KiB write is not assumed atomic; control copies/checksums
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

Accepted task-3 policy: the writer builds at least an in-memory free-inode list
at mount, after journal recovery, and maintains it as slots are allocated and
actually reclaimed. Creation must not rescan the whole inode file. Slots awaiting
cleanup/live-reference release are not free. This adds no on-disk free-inode index;
measure mount-time scan/memory cost separately from mutation latency.

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
large shrink stalls and journal batching. The [design limits](../technical-debt.md#native-filesystem-design-limits)
distinguish accepted constraints from agent proposals. Task 1 delivers this
proposal; task 2 requires assignment and answers to the
[open owner questions](#open-owner-questions) that precede it.

## Open owner questions

Each question has a proposed default, and "defer" is a valid answer. They are
grouped so that no round asks more than three.

**Before task 2 (format library and host tools):**

1. **Checksums on home metadata.** Today only headers and journal records carry
   CRC32C ([pool layout](#pool-layout)). Proposed default: none in v1, matching
   ext2, with `fsck` structural checks as the safeguard. Note that bitmap,
   indirect and directory blocks are raw arrays with no spare space. Adding
   checksums later therefore means a separate checksum region or a reinterpretation
   behind a required or read-only-compatible feature, not a field to fill in.
2. **Read-only opening with a committed journal.** Proposed default: refuse, as
   in [journal and recovery](#journal-and-recovery). Replay needs write authority,
   for example host `fsck` with the image writable. The alternative is replaying
   into an in-memory overlay for read-only access, at the cost of more reader code.

**Before task 3 (native writer),** the milestone's
[proposed writeback details](native-filesystem.md#proposed-writeback-details):

1. **Periodic flush:** one task every T = 30 s, as a nominal interval, not a
   crash-loss bound.
2. **Whole-transaction `fsync`:** a file's size, mapping and directory entry
   become durable together; the commit satisfies every ordered-data dependency in
   the transaction.
3. **Delayed allocation:** blocks are assigned at writeback. This can be deferred
   if it complicates the first writer.
