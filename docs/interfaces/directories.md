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
mount authority. Native create/remove/link/rename, writes, resizing and sync run
on the sole kernel filesystem worker. Native FILE_SIZE accepts READ or WRITE.
The tree internals below describe the archive/RAM backends.

`DIRECTORY_INFO` samples the held object's [identity and modification time](file-metadata.md)
without additional content rights; it is separate from filesystem-capacity information.

The kernel-native [ACPI table root](../kernel/acpi.md#table-export) uses the
same protocol with a read-only backing: ENUMERATE and LOOKUP are answered by
the ACPI worker, LOOKUP returns an immutable copied FILE, and one extra
operation, `ACPI_TABLES_INFO`, describes a listed name. Other backings return
BAD_OPERATION for it.

## Scoped filesystem information

`DIRECTORY_FILESYSTEM_INFO` accepts a complete zeroed directory message and
returns a copied 320-byte `directory_filesystem_info` record. NPFS and RAM
directories require only `DIRECTORY_RIGHT_FILESYSTEM_INFO`; no LOOKUP, ENUMERATE or READ_FILES
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

On NPFS, all listed fields are available on success. Capacity includes shared metadata
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

RAM directories with FILESYSTEM_INFO authority return FILESYSTEM_TYPE_RAM and
zero flags. NPFS-specific IDs, generation, capacity and volume name are
inapplicable and zero; they are not zero-capacity/identity measurements. Other
backends still refuse this query. Boot init's private RAM ancestor carries INFO
for private staging validation; public tmp:// authority is unchanged.

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
length limit. Native (npfs) and host directories refuse a name over 255 bytes
with NAME_TOO_LONG, in lookup, creation, removal and rename; RAM directories
accept longer names. Empty names, embedded NUL, `/`, `.` and `..` are invalid. The
kernel compares bounded chunks without allocating on an AP.

Lookup never follows a symbolic link. Only host directories contain them, and
a host symlink fails with LINK_NOT_FOLLOWED; other host special files fail with
BAD_OPERATION. Enumeration still reports symlinks with their own kind.

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

For native CREATE, the handler captures the name and reserves the result slot in
the caller's table, which stays usable by sibling tasks. The worker stages the
returned handle before publishing creation; failed creation removes that staged
handle. No table-entry pointer crosses the wait. The entry survives closing the
returned handle. Successful native
namespace mutations join the pool's running transaction and become durable at the
next commit (see [filesystem mutations](filesystem-mutations.md)); file data may
remain dirty until synchronization.

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
its validated name into it and prepares an owned grant for the reserved slot, which
is installed only at publication.

Under the directory lock, CREATE rechecks detached state, the name and generation
capacity, then links the complete entry and increments the generation. That is the publication
point, and nothing fallible remains in the directory itself. The reply copy still
follows: if a sibling unmapped the reply buffer during the wait, the call returns
BAD_BUFFER, closes only the exact returned handle generation and leaves the
created name in place. A competing creator returns ALREADY_EXISTS, releases its
grant and reserved slot and asks the BSP to discard its unpublished entry.
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
HOST also accepts exact SYMLINK: it unlinks that entry without following it.
Other backings reject that kind with BAD_OPERATION; no link handle, read,
creation or traversal is added. ANY retains its existing FILE/DIRECTORY scope.
ANY accepts either kind in the same atomic operation, without a userspace
lookup/remove race. Only REMOVE on the parent is required; file READ/WRITE and
parent LOOKUP/ENUMERATE are not implied or needed. It returns no reply bytes.
HOST symlink removal observes its kind without following it and releases the
lookup before UNLINK. An external replacement can still change which name is
affected; a replacement directory is never recursively entered or sent RMDIR
by this exact SYMLINK operation. Other special kinds remain refused.

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

Native removal decrements a regular file's name count with the namespace edit;
only its last name records detachment. Directories retain unique ancestry. Existing
handles retain the inode; persistent cleanup reclaims it in bounded batches after
references drain. An open detached directory rejects child creation. Reboot resumes
cleanup because pre-crash handles no longer survive. This does not revoke other
capabilities. Roots have no removable parent entry and removal is nonrecursive.

## Native regular-file links

`DIRECTORY_LINK` takes a source parent, counted source name, destination DIRECTORY
handle and counted destination name. Its 40-byte payload fits the existing
72-byte directory message; success has no reply or returned handle. Both names
are single UTF-8 components. This operation is implemented only for writable
npfs regular files in one volume. Directories return WRONG_TYPE, mixed backends
or volumes BAD_OPERATION, and an existing destination ALREADY_EXISTS.

Source LOOKUP, READ_FILES and WRITE_FILES are all required; destination CREATE
suffices. No persistent rights ceiling follows a name, so source read-only or
write-only authority cannot be amplified through an alias. The destination's
retained grant and both lineages survive worker delivery. Successful namespace
capture is not undone by later CLOSE or CUT. There is no held-FILE/orphan link
form. The handler copies both names before waiting; the worker rechecks node
ceilings and same-volume membership.

Aliases share cached inode identity, contents and size. Adding/removing names
updates the count atomically in the running transaction. Last-name removal marks
DETACHED; reclamation also waits for held inode references. Count overflow returns
LIMIT before activation or namespace effects. LINK does not flush dirty file data
or imply namespace durability. The first eligible LINK on a legacy pool does
permanently gain its format feature through two ordered header writes/flushes
before staging the edit; see the [adapter](../devices/filesystem-native-adapter.md).
`directory_link` exposes this component operation. `path_link` resolves both
parents with disjoint caller scratch; libc `link` takes one owned cwd snapshot
for both paths. Provider paths do not OPEN or fall back to copying. `ln` supports
exact targets, directory destinations and multiple sources, with exclusive
creation and no symbolic-link/force flags. Optional [metadata](file-metadata.md)
reports the real count for NPFS regular files.

## Atomic rename and directory moves

RENAME takes the source parent, counted source name, a caller-owned destination
parent capability, counted destination name, REPLACE/NO_REPLACE and source kind
ANY/FILE/DIRECTORY. Kind is checked inside the edit; it carries trailing-slash
requirements without a racy child lookup. The payload is 56 bytes (72 including
the header); every consumer, including reused independent bundles, must be
rebuilt against this ABI. Success has no reply.

Source REMOVE and destination CREATE are required; REPLACE requires destination
REMOVE even for an absent destination. File READ/WRITE and parent ENUMERATE/LOOKUP
are unnecessary for the direct operation. RAM and writable npfs support files
and directories within one store/mount instance. Different volumes or instances
and mixed stores return BAD_OPERATION, without copying/deleting. FAT32/host
retain file rename; directory moves there return BAD_OPERATION. Read-only backing
returns READ_ONLY after capability checks. Providers have no directory rename.

Replacement requires matching kinds and an empty directory victim (NOT_EMPTY
otherwise). Its retained empty-directory handle stays detached and refuses new
creation; retained file victims keep their contents. Missing sources/detached
parents return NOT_FOUND. NO_REPLACE refuses a distinct existing destination;
the same existing entry succeeds under either policy without changing generations.
For npfs, two distinct names of the same regular inode also succeed as a no-op
under either policy, preserving both names and their count.
Host files retain the documented external name/type races and same-name
NO_REPLACE refusal. Type constraints do not turn host preflight into a snapshot.

A directory move keeps the same object/inode and every descendant. The cycle
check refuses a destination equal to or below the moved directory with BAD_REQUEST,
under the same topology owner as the edit. RAM walks borrowed parent links under
the mutation lock; creation, removal, rename and final parent retirement share
that boundary. Destruction clears immediate children's borrowed parent links
before releasing references/freeing the parent. No parent capability is exposed.
The allocation-free walk is bounded to 1024 links; incomplete checks return LIMIT.
npfs validates copied parent records with cycle detection under its worker, bounded
to 1024 iterations; corrupt ancestry returns IO and retires the failed pool.

RAM prepares name storage outside locks, then rechecks names, kind, ancestry,
victim emptiness, detach state and generation/count capacity under the topology
and parent locks. Publication transfers the original reference, replaces the
victim and installs the destination before unlocking. No fallible work remains.
Disposal happens on BSP after unlocking. Each changed parent generation advances
once; the moved directory's child generation is unchanged. Lookup/enumeration
keep their directory locks. No allocation, BSP wait or scheduler lock occurs
under the mutation locks.

npfs stages both parent entries, a cross-parent moved directory's backlink and
victim detachment as one operation of the running transaction, never split across
commits.
Known rollback restores the cached records. Directory moves do not flush dirty
descendant files; existing file-replacement ordered flushing remains. IO and
OUTCOME_UNKNOWN preserve the storage failure contract, not a rollback/retry promise.

Held directories/files and delegated roots retain identity and rights. Existing
cwd chains retain their granted ancestors; `..` pops that chain, without finding
a new physical parent or reopening paths. A delegated subtree root cannot escape
upward. Descriptive `getcwd` may become stale; later explicit traversal can make
a new chain using existing authority. See [working paths](../userland/process-state.md).

## Userspace example

[Libpyxis helpers](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/directory.h) wrap lookup and enumeration
without allocation or path parsing. The optional Hello example gets
`startup_root("app")`, enumerates it, looks up `share` as a directory, then looks up
`hello.txt` with file READ. It reads through the existing file protocol and closes
both lookup handles and its startup grants. Its fixed name buffer reports an
oversized entry as an error rather than silently truncating it.

The sample text is packaged at `share/hello.txt` in `DISTRIBUTION=full`
development images; the default core archive omits it. The source remains beside hello.
The endpoint client retains a directly supplied file capability for its existing
transfer example. [Path helpers](../userland/paths.md) now compose these component operations
for explicit schemes and relative paths, retaining working-directory handles
without an ambient fallback root.

Hello also creates a directory and file under `home`, then looks both up through
independent grants and lists them. It writes and truncates through the creation
handle, which has WRITE only, then reads through a READ-only lookup handle.
All creation and lookup handles are closed; the retained home tree owns the
entries and file contents after the program exits.

## Typed child lookup

`DIRECTORY_LOOKUP_TYPED` resolves one ordinary component once and returns an
owned handle plus kernel-attested FILE or DIRECTORY protocol. Its request has
separate FILE and DIRECTORY masks: both must contain known bits, but authority
is checked only for the actual kind. Parent LOOKUP remains required. FILE masks
need parent READ_FILES/WRITE_FILES; DIRECTORY masks must fit the parent's mask.
Native archive/RAM, host, npfs and FAT32 use their existing retained lookup and
reservation path, preserving source lineage. Symbolic links and unknown/special
kinds remain refused. There is no kind-probe/re-lookup fallback; exact-kind
LOOKUP and exclusive CREATE retain their current contracts.
