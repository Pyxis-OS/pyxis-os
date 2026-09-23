# Directory capabilities

The launcher retains two trees for the kernel lifetime: read-only `app`, built
from the boot initrd, and initially empty `home`, backed by RAM. The shell receives
both roots and explicitly passes grants to its children. RAM contents survive
process exit and disappear on reboot.

Both use the existing tagged CALL interface. There is no userspace filesystem
server, mount table, block device or kernel path parser.

## Tree and lifetime

The [archive iterator](../include/kernel/initrd.h) returns borrowed entry views.
The [tree builder](../include/kernel/fs/initrd_tree.h) allocates directory names,
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

The initrd is immutable; RAM directories are append-only. A per-directory lock
protects entry links, count and generation. Lookup acquires a child reference
under that lock, then releases the lock before handle installation or BSP table
growth. Destruction runs only after the last owner releases the directory.
Names and child pointers never change once published. Removal and rename remain
later work and must revisit enumeration's borrowed-name lifetime.

## Lookup

[Directory requests](../include/abi/directory.h) distinguish file and directory
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
| CREATE | Add a directory or empty file to RAM backing |

Returned directory rights must be a subset of the parent's granted directory
rights. Returned file READ requires READ_FILES; WRITE requires WRITE_FILES.
Neither implies the other. Zero-rights grants are allowed. Enumeration is not
required to read a known name. There is no removal operation. CREATE is separate
from LOOKUP and ENUMERATE; granting WRITE cannot make initrd backing mutable.

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

Generation checks and selection of one entry occur under the directory lock.
The handler then copies the selected immutable name with the caller's directory
reference keeping it alive. A successful creation increments the generation;
mutation after selection affects the next enumeration call. Even an old END
cursor reports CHANGED after a new entry is published. A short-buffer reply
keeps the original cursor, including zero when no generation has been acquired.

Generation never wraps: exhausted generation or entry-count space rejects
creation with LIMIT. Failed creation does not advance it. This contract provides
no snapshot, and callers should not retry forever if another process keeps
changing a directory.

## Exclusive creation

CREATE takes the same counted single component, kind and requested rights as
LOOKUP, and returns a newly owned handle. It requires CREATE on the parent.
Returned directory rights are still a subset of the parent; file READ still
requires READ_FILES, and WRITE requires WRITE_FILES. CREATE alone does not imply
permission to read or modify the child.
Existing names return ALREADY_EXISTS, regardless of kind; nothing is opened,
replaced or truncated. There is no recursive parent creation.

The handler checks all user buffers and authority before staging an entry. An
initrd directory rejects mutation with READ_ONLY even if its grant includes
CREATE; a grant without CREATE fails the authority check with DENIED first.
A new RAM file has no data allocation, size zero and immediate EOF through the
file protocol. A WRITE grant permits subsequent writes and resizing; see the
[file contract](processes.md#implemented-file-calls).

[RAM entry preparation](../include/kernel/fs/ramfs.h) runs on the BSP through a
focused request recorded in task metadata. The requester waits with no directory
lock held. The BSP allocates an unpublished entry and child; it never dereferences
a remote private stack or user address. The resumed caller copies its validated
name into that allocation and installs the provisional child handle, using the
existing BSP table-growth request if needed.

Under the directory lock, CREATE rechecks the name and generation capacity, then
links the complete entry and increments the generation. That is the publication
point. Nothing fallible remains afterward: the caller's sole task owns stable,
already-validated reply mappings. A competing creator returns ALREADY_EXISTS,
closes its provisional handle and asks the BSP to discard its unpublished entry.
Failed staging also discards partial ownership. Existing handles and names remain
valid; a failed request may have grown its own capability table.

There is no allocator call or wait under a directory lock. The BSP request queue
participates in scheduler wake/preemption decisions, including single-CPU use.
Disposal releases the child through normal object retirement and frees the entry
on the BSP. Closing a successful creation handle never deletes the name: its
parent keeps an independent child reference.

## Userspace example

[Libpyxis helpers](../userspace/include/directory.h) wrap lookup and enumeration
without allocation or path parsing. The optional Hello example gets
`startup_root("app")`, enumerates it, looks up `share` as a directory, then looks up
`hello.txt` with file READ. It reads through the existing file protocol and closes
both lookup handles and its startup grants. Its fixed name buffer reports an
oversized entry as an error rather than silently truncating it.

The sample text is packaged at `share/hello.txt`; the source remains beside hello.
The endpoint client retains a directly supplied file capability for its existing
transfer example. [Path helpers](paths.md) now compose these component operations
for explicit schemes and relative paths, retaining working-directory handles
without an ambient fallback root.

Hello also creates a directory and file under `home`, then looks both up through
independent grants and lists them. It writes and truncates through the creation
handle, which has WRITE only, then reads through a READ-only lookup handle.
All creation and lookup handles are closed; the retained home tree owns the
entries and file contents after the program exits.
