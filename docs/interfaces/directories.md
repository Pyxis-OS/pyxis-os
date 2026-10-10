# Directory capabilities

The launcher retains two trees for the kernel lifetime: read-only `boot`, built
from the boot initrd, and initially empty `tmp`, backed by RAM. The shell receives
both roots and explicitly passes grants to its children. RAM contents survive
process exit and disappear on reboot.

Both use the existing tagged CALL interface. There is no kernel path parser.
[Native filesystem objects](../devices/filesystem-native-adapter.md) use the same
protocol with capability authority, 255-byte UTF-8 components and shared directory
generations. They retain inode references independently of parent objects. Trusted
init acquires roots through the [mount protocol](../../include/abi/mount.h) for its
configured disk; no on-disk principal is supplied. Roots require LOOKUP. Mutation
rights additionally require mount WRITE; observation independently requires OBSERVE.
Installer disk objects can also acquire read-only roots through the same mount
path after releasing raw claims; see [installer authority](../devices/installer-authority.md).
Returned directory/file grants can be attenuated and delegated independently of
mount authority. Native create/remove/file rename, writes, resizing and sync run
on the sole kernel filesystem worker. Native FILE_SIZE accepts READ or WRITE.
The tree internals below describe the archive/RAM backends.

`DIRECTORY_INFO` samples the held object's [identity and modification time](file-metadata.md)
without additional content rights; it is separate from filesystem-capacity information.

## Scoped filesystem information

`DIRECTORY_FILESYSTEM_INFO` accepts a complete zeroed directory message and
returns a copied 320-byte `directory_filesystem_info` record. Native directories
require only `DIRECTORY_RIGHT_FILESYSTEM_INFO`; no LOOKUP, ENUMERATE or READ_FILES
is implied or required. Other backends return BAD_OPERATION and retain their
existing grants. The `directory_filesystem_info()` library wrapper publishes a
record only after a successful, well-formed reply.

Observation is a separate kernel grant. Acquiring it on a native root requires
`MOUNT_RIGHT_OBSERVE` in addition to OPEN_ROOT; it implies no mutation authority.
Copies may attenuate a retained root to observation alone. Directory lookup may explicitly request observation
only when its parent grant holds it. A child never reacquires withheld mount
or parent authority. `DIRECTORY_RIGHTS`
is the complete recognized mask; `DIRECTORY_CONTENT_RIGHTS` is the existing six
content-operation bits, used for non-native grants.

| Field | Meaning and evidence |
| --- | --- |
| `type` | `FILESYSTEM_TYPE_NPFS` |
| `flags` | READ_ONLY, plus independent GPT_DEGRADED and filesystem DEGRADED opening flags |
| `pool_id`, `volume_id` | Opaque 16-byte identities from retained selected metadata |
| `generation` | Current retained pool journal sequence |
| `pool_allocatable_bytes` | `(pool blocks - 2) * 4096`; verified geometry excluding the two header slots |
| `volume_name` | Retained filesystem volume name, NUL-terminated and zero-padded |

All listed fields are available on success. Capacity includes shared metadata
and reserves; it is neither per-volume capacity nor writable allowance. Multiple
bindings or volumes with the same pool ID share this capacity and must not be
summed. Namespace binding names come from the caller's own selected roots, not
this record. The worker owns mounted state; external changes to an attached image
are unsupported.

Used/free bytes, volume charged bytes, guarantees, quotas and percentages are
unavailable and have no fields in this record; absence never means zero. Ordinary
opening validates geometry and selected root envelopes, not global allocation
accounting. The query copies retained metadata on the filesystem worker, without
traversal, a whole-image check, new handles or block I/O. Failure publishes no reply.

## Tree and lifetime

The [archive iterator](../../include/kernel/initrd.h) returns borrowed entry views.
The [tree builder](../../include/kernel/fs/initrd_tree.h) allocates directory names,
entries and child objects while file contents continue borrowing the archive's
read-only mapping. It infers parent directories and accepts explicit empty
directories. Repeated directory declarations merge with the existing directory.
Duplicate files, file/directory conflicts and invalid paths reject the tree.

Archive paths are relative. Leading `./` is ignored, `.` can name the archive
root, and a directory record may end with `/`. Interior empty components, `.`
and `..`, and absolute paths are rejected. These are archive-import rules, not
userspace path-resolution semantics.

Each directory entry owns a child reference; children do not retain parents.
Looking up a child acquires another reference. Closing the parent handle does
not close child handles. Last-reference retirement frees directory entries on
the BSP and queues their children for retirement without recursive C calls.
Failure during tree construction releases the unpublished root through this
same path. The archive's bytes survive file-object destruction.

The initrd is immutable; RAM directories support creation, removal and file
rename. A per-directory lock protects entry links, count, generation and detached state.
Lookup acquires a child reference under that lock, then releases the lock before
handle installation or BSP table growth. Destruction runs only after the last
owner releases the directory.
Names and child pointers never change within an entry. Removed entries are
detached under the lock and disposed on the BSP after borrowed readers finish.

## Lookup

[Directory requests](../../include/abi/directory.h) distinguish file and directory
kinds explicitly. LOOKUP takes one case-sensitive name, an expected kind and
requested rights. The name is counted bytes, excluding NUL, with no fixed ABI
length limit. Empty names, embedded NUL, `/`, `.` and `..` are invalid. The
kernel compares bounded chunks without allocating on an AP.

| Authority on the parent | Permits |
| --- | --- |
| LOOKUP | Resolve a child name |
| ENUMERATE | List names and kinds, without acquiring child handles |
| READ_FILES | Grant READ on a file found through LOOKUP or CREATE |
| WRITE_FILES | Grant WRITE on a file found through LOOKUP or CREATE |
| CREATE | Add a directory or empty file to writable backing |
| REMOVE | Remove a file or empty directory name from writable backing |

Returned directory rights must be a subset of the parent's granted directory
rights. Returned file READ requires READ_FILES; WRITE requires WRITE_FILES.
Neither implies the other. Zero-rights grants are allowed. Enumeration is not
required to read a known name. CREATE and REMOVE are independent of LOOKUP,
ENUMERATE and file rights; granting WRITE cannot make initrd backing mutable.

Successful lookup installs a new owned handle in the caller's table. Missing
names return NOT_FOUND, an unexpected kind returns WRONG_TYPE, and excessive
rights return DENIED. Invalid request/reply buffers cause no insertion. If the
table is full, the task uses the existing BSP growth request, holding no locks,
then retries insertion. Allocation/reference-limit failures return an error
without publishing a handle. Reply storage is validated before installation and
remains writable under the current single-task/private-address-space contract.

## Enumeration

An opaque cursor belongs to one directory object. Start with the zero cursor;
after each entry, pass the returned cursor to that same directory. Handle copies
do not share iteration state. Order is unspecified, and names may be longer than
an application's local buffer.

Each successful CALL returns one of these outcomes:

- ENTRY: a whole NUL-terminated name, its file/directory kind, and the next cursor.
- END: no name or handle; repeating the returned cursor remains at the end.
- BUFFER_TOO_SMALL: required bytes including NUL, with the name buffer untouched
  and the input cursor unchanged. Retry with more storage or report the error.
- CHANGED: no entry; restart explicitly from the zero cursor if desired.

Ordinary syscall failures still return no reply bytes. Enumeration outcomes live
inside a successful reply so a short buffer can carry its required size without
changing the CALL error convention. Name and reply destinations must be disjoint;
request storage may overlap outputs because the kernel captures the request first.

For native directories, the worker checks the shared inode generation, selects
and stages a name before replying. Mutations through any handle invalidate acquired
cursors; restart CHANGED from zero. Positions are opaque and not wrapper identities.

For archive/RAM directories, generation checks, selection and copying the name
occur under the directory lock. User mappings are validated beforehand and remain
private/stable, so the copy cannot allocate or sleep. Removal cannot reclaim the name during copying.
Successful creation, removal or a rename that changes entries increments the
generation; a later mutation affects the next enumeration call. Even an old END cursor reports CHANGED after
a mutation. A short-buffer reply keeps the original cursor, including zero when
no generation has been acquired.

Generation never wraps: exhausted generation rejects mutation with LIMIT.
RAM creation also checks entry-count capacity. Failed mutation does not advance
it. This contract provides no snapshot, and callers should not retry forever if another process keeps
changing a directory.

## Exclusive creation

CREATE takes the same counted single component, kind and requested rights as
LOOKUP, and returns a newly owned handle. It requires CREATE on the parent.
Returned directory rights are still a subset of the parent; file READ still
requires READ_FILES, and WRITE requires WRITE_FILES. CREATE alone does not imply
permission to read or modify the child.
Existing names return ALREADY_EXISTS, regardless of kind; nothing is opened,
replaced or truncated. There is no recursive parent creation.

For native CREATE, the handler captures the name and lends its capability table
exclusively to the worker. The worker stages the returned handle before publishing
creation; failed creation removes that staged handle. No table-entry pointer
crosses the wait. The entry survives closing the returned handle. Successful native
namespace mutations reach durable COMMITTED before returning; file data may remain
dirty until synchronization.

The archive/RAM handler checks all user buffers and authority before staging an
entry. An initrd directory rejects mutation with READ_ONLY even if its grant includes
CREATE; a grant without CREATE fails the authority check with DENIED first.
A new RAM file has no pages, size zero and immediate EOF through the
file protocol. A WRITE grant permits subsequent writes and resizing; see the
[file contract](processes.md#implemented-file-calls).

[RAM entry preparation](../../include/kernel/fs/ramfs.h) runs on the BSP through a
typed RAMFS request in the common BSP FIFO. Entry allocation, name-only allocation
and discard use task-owned records, with no directory lock held and no VM handoff.
The BSP allocates an unpublished entry and child; it never dereferences a remote
private stack or user address. The resumed caller owns the returned entry, copies
its validated name into it and installs the provisional child handle, using the
existing BSP table-growth request if needed.

Under the directory lock, CREATE rechecks detached state, the name and generation
capacity, then links the complete entry and increments the generation. That is the publication
point. Nothing fallible remains afterward: the caller's sole task owns stable,
already-validated reply mappings. A competing creator returns ALREADY_EXISTS,
closes its provisional handle and asks the BSP to discard its unpublished entry.
A directory removed while staging waited rejects publication with NOT_FOUND.
Failed staging also discards partial ownership. Existing handles and names remain
valid; a failed request may have grown its own capability table.

There is no allocator call or wait under a directory lock. Publication promptly
notifies an idle executor through its ordinary wait/ready-queue path, including
single-CPU use; the scheduler does not inspect RAMFS requests. Discard transfers
an unpublished or removed entry with no list links or borrowed readers. Submission
allocates nothing, so cleanup remains available after allocation failure. The
BSP's local helpers release the child through normal object retirement and free
the entry. Closing a successful creation handle never deletes the name: its
parent keeps an independent child reference.

## Removal

REMOVE takes one counted name and FILE, DIRECTORY or ANY as its expected kind.
ANY accepts either kind in the same atomic operation, without a userspace
lookup/remove race. Only REMOVE on the parent is required; file READ/WRITE and
parent LOOKUP/ENUMERATE are not implied or needed. It returns no reply bytes.
Names have the same component rules as LOOKUP. A missing name returns NOT_FOUND,
a kind mismatch WRONG_TYPE, and a nonempty directory NOT_EMPTY. Insufficient
rights returns DENIED before immutable backing can report READ_ONLY.

A successful removal unlinks the entry and advances the parent's generation.
For RAM backing, no fallible work remains after unlinking. The detached entry retains its child
reference until the existing BSP disposal service releases it. Open file handles
remain usable, and recreating the name creates a different object. The last
reference releases the old object's storage through normal retirement.

Removing a RAM directory takes the mutation lock, then its parent and child locks,
checks that the child is empty and marks it detached before unlinking. The
mutation lock serializes operations needing more than one directory lock,
including rename between arbitrary parents. CREATE checks detached state before
staging and again at publication. An open removed directory can still be
inspected or closed, but attempts to create children return NOT_FOUND. A new
directory created under the old name does not reactivate those handles.

Native removal atomically records detachment with the namespace edit. Existing
handles retain the inode; persistent cleanup reclaims it in bounded batches after
references drain. An open detached directory rejects child creation. Reboot resumes
cleanup because pre-crash handles no longer survive. This does not revoke other
capabilities. Roots have no removable parent entry and removal is nonrecursive.

## Atomic file rename

RENAME is sent to the source directory, with a source name, a destination
capability from the caller's own table, destination name and explicit REPLACE
or NO_REPLACE policy. Both names use the same counted-component rules as LOOKUP.
The directory payload is 48 bytes (64 bytes including the protocol/operation
header); all consumers are rebuilt together. Success returns no reply bytes.

Source REMOVE and destination CREATE are required. If a different destination
entry exists, NO_REPLACE returns ALREADY_EXISTS; REPLACE additionally needs
REMOVE on the destination. Neither file READ/WRITE nor parent ENUMERATE/LOOKUP
is needed for the direct operation. Sources must be files, and replacement
accepts only files. Directory moves/replacement return WRONG_TYPE. Missing
sources and detached parents return NOT_FOUND. Native parents must be in the same
volume, and mixed backends fail BAD_OPERATION. Native REPLACE requires destination
REMOVE even if the destination name is absent. Native replacement records the old
inode's detachment atomically and preserves its open handles. Initrd mutation
returns READ_ONLY after capability checks. There is no implicit
copy-and-delete fallback or cross-filesystem implementation.

For RAM backing, renaming an existing file to the same entry, including through
another handle to the same parent, succeeds without allocation or generation
changes under either policy. Ordinary source REMOVE/destination CREATE checks
still apply. Native same-entry rename also leaves the namespace generation
unchanged, after authority checks and writer preparation; REPLACE still requires
destination REMOVE. A missing source never becomes a successful no-op.

The native worker serializes the complete name operation and uses one metadata
transaction for both parent edits and any replacement cleanup membership. The
caller's two capability borrows survive the blocked call. There is no missing
replacement interval. Success reaches durable COMMITTED before returning.

RAM rename first checks the operation under both parent locks. If work is needed,
it releases all locks and submits a name-only RAMFS request for storage with a
`NULL` child. The caller's private mappings and directory capabilities remain
stable while waiting. After filling the name, it reacquires
both parents and rechecks entries, rights, detached state and generation/count
capacity. It never carries borrowed entry pointers across that wait.

Publication transfers the source entry's owned file reference into the prepared
entry, unlinks the source and any replaced entry, and publishes the destination
before releasing either parent lock. No fallible work remains at that point.
There is no observable missing-destination interval; failures make no namespace
changes themselves. Existing file handles keep their objects, including handles
to a replaced destination. Obsolete entries are disposed on BSP after unlocking.
Each changed parent advances its generation once; same-parent rename advances it
once total. Enumeration order remains unspecified.

For RAM backing, a short shared mutation lock precedes directory locks for REMOVE
and RENAME,
preventing cycles between parent/child removal and arbitrary-parent rename.
Lookup, enumeration and creation retain their per-directory locks. No allocation,
BSP wait, scheduler lock or file-data operation occurs under these locks.

## Userspace example

[Libpyxis helpers](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/directory.h) wrap lookup and enumeration
without allocation or path parsing. The optional Hello example gets
`startup_root("app")`, enumerates it, looks up `share` as a directory, then looks up
`hello.txt` with file READ. It reads through the existing file protocol and closes
both lookup handles and its startup grants. Its fixed name buffer reports an
oversized entry as an error rather than silently truncating it.

The sample text is packaged at `share/hello.txt`; the source remains beside hello.
The endpoint client retains a directly supplied file capability for its existing
transfer example. [Path helpers](../userland/paths.md) now compose these component operations
for explicit schemes and relative paths, retaining working-directory handles
without an ambient fallback root.

Hello also creates a directory and file under `home`, then looks both up through
independent grants and lists them. It writes and truncates through the creation
handle, which has WRITE only, then reads through a READ-only lookup handle.
All creation and lookup handles are closed; the retained home tree owns the
entries and file contents after the program exits.
