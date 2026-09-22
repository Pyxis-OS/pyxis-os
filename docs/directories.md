# Directory capabilities

The initial filesystem view is a read-only tree built from the boot initrd on
the BSP before userspace starts. The launcher retains its root for the kernel
lifetime and grants hello that directory as the startup scheme root `app`.
The directory protocol uses the existing tagged CALL interface; it needs no
userspace filesystem server, mount table or kernel path parser.

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

The current tree is immutable after publication. Lookup can therefore retain a
borrowed child pointer while its task blocks for capability-table growth: its
live directory reference keeps the entry and child alive. Writable backings
must revisit that synchronization before allowing concurrent entry changes.

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
| READ_FILES | Grant READ on a file found through LOOKUP |

Returned directory rights must be a subset of the parent's granted directory
rights. Returned file READ requires READ_FILES. Zero-rights grants are allowed.
Enumeration is not required to read a known name. The initial protocol exposes
no creation, removal or write rights.

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

The cursor includes a generation checked by the handler. The initrd never
mutates; a mismatched generation reports CHANGED. A later writable backing must
advance its generation without reuse when entries change and synchronize the
check with enumeration. This contract provides no snapshot, and callers should
not retry forever if another process keeps changing a directory.

## Userspace example

[Libpyxis helpers](../userspace/include/directory.h) wrap lookup and enumeration
without allocation or path parsing. Hello gets `startup_root("app")`, enumerates
it, looks up `share` as a directory, enumerates that directory, then looks up
`hello.txt` with file READ. It reads through the existing file protocol and closes
both lookup handles and its startup grants. Its fixed name buffer reports an
oversized entry as an error rather than silently truncating it.

The sample text is packaged at `share/hello.txt`; the source remains beside hello.
The endpoint client retains a directly supplied file capability for its existing
transfer example. General URI/relative-path helpers and working-directory
navigation are task 5; no ambient fallback root exists.
