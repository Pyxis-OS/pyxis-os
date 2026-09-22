# Processes, capabilities and the first userspace ABI

Status: the first console-and-blob ABI slice below is agreed for implementation.
The broader process and resource model remains a working draft alongside the
[spaces draft](spaces.md). Startup delivery, console and blob CALL operations,
and handle close are implemented, including the complete userspace example.
[Request/reply endpoints](endpoints.md) now connect separate client and server
processes through the same tagged call ABI. Programs use console capabilities
for TTY output; the separate kernel-log syscall remains available for diagnostics.
The [worklist](wip/process-capability-abi.md) tracks the focused tasks and
handoffs for this first milestone.

## First program

Use one small program to work through the userspace interface and kernel
ownership together: print a greeting, read the contents of a supplied file,
write those contents to the console, then exit.

The launcher resolves the file through the existing boot-archive reader and
grants a capability to an immutable blob containing its bytes. The program
receives these two resources:

| Startup role | Object | Rights |
| --- | --- | --- |
| Output | The space's console | `WRITE` |
| Content | An immutable blob backed by a boot-archive file | `READ` |

This example needs no filesystem root, path lookup operation or VFS semantics.
The [filesystem draft](vfs.md) remains separate. The boot archive is a source of
bytes, not a decision about how applications will eventually discover files.

The hello program:

1. Look up its named output and content resources through libpyxis.
2. Write a greeting, handling errors and any partial write.
3. Query the blob size and read at explicit byte offsets into a stack buffer.
4. Write the returned bytes, advance the offset, and repeat until EOF, checking
   that the counts agree with the immutable blob's size.
5. Attempt to close both handles even after an I/O error, then exit with a
   success or failure status. The kernel releases any handles left open.

An explicit read offset avoids introducing a shared seek position when handles
are later duplicated or passed between processes.

## Objects, capabilities and handles

An object is the underlying resource: a console, blob, memory allocation or
endpoint. A capability grants particular operations on that object. A handle
is an opaque, process-local value naming an entry in the process's capability
table. The kernel entry holds the object reference and rights; userspace cannot
gain rights by changing the handle value.

Different processes may hold different handles and rights to the same object.
Rights are defined by the object's protocol: `CONSOLE_RIGHT_WRITE` authorizes
console output; `BLOB_RIGHT_READ` authorizes blob reads and size queries. Both
currently use bit 0, with different meanings. The capability table stores the
mask without imposing global READ/WRITE meanings; installation rejects bits
unsupported by the target object type. The message never supplies authority.
A holder may copy a capability through an endpoint request with equal or
reduced rights; no separate transfer permission is required. Multiple operations
may require the same right; split rights when one operation needs to be
grantable without the others.

For this slice, `handle_t` is an opaque `uint64_t`; zero is invalid. Internally,
the low 32 bits identify a table slot and the high 32 bits hold its generation.
Generations start at one. Closing an entry invalidates its handle; reuse gets
a new generation. Retire a slot rather than wrap its generation and make a
stale handle valid again. Userspace must not decode handles or use them as
global object identifiers. The table implementation, console call and userspace
close use this encoding.

Each space would have a capability table describing resources available to its
environment. A process would receive an explicit subset with equal or reduced
rights, selected by the authorized launcher or supervisor. Membership in the
space would not automatically grant every capability in its table.

The working assumption is that closing a granting handle does not revoke
references already granted to processes. Revocation and space teardown need
their own rules. Whether a space also imposes an authority ceiling on incoming
grants, and how cross-space admission works, remain open.

Resource charges are distinct from access grants. Giving another process access
to a blob should not silently transfer the memory charge to that process or
its space. This matters to the containment goals in the spaces draft.

## Startup record

Entry receives a pointer in `RDI` to a process-owned startup region. The
[shared header](../include/abi/startup.h) defines its layout; all embedded
addresses refer to that child's virtual memory. The total budget is 64 KiB,
including page padding. Only the required pages are allocated. Version remains
1, with the old fixed-role layout replaced outright; rebuild in-tree programs
together with the kernel.

The read-only portion contains the header, named resource bindings, scheme-root
bindings, working-directory context and environment. The writable portion holds
`argv`, its final NULL and argument strings. Both portions are non-executable
and survive until address-space destruction. Read-only metadata records the
argument count and writable-area address; no userspace heap is needed for entry.

Resource names are nonempty, case-sensitive and unique within their table.
Each binding names a handle already installed in the child's capability table.
No new reference is acquired when recording or looking up a binding. Different
names may alias a handle; close each owned handle once. Missing resources are
omitted, and lookup returns HANDLE_INVALID. Closing a handle does not update the
immutable snapshot, so a later lookup can return its stale value.

Scheme roots have their own name/handle table. The working-directory chain runs
from the permitted navigation boundary to the current directory, optionally
accompanied by a descriptive display path. Directory objects are not implemented
yet: preparation currently rejects nonempty roots or working-directory context.
The native accessors therefore return no directory resources for current programs.
Actual directory type validation and traversal belong to the filesystem tasks.

The launcher supplies argument strings, including argv[0] when present; neither
the kernel nor startup parses a command line. Zero arguments are valid and still
provide argv[0] == NULL. Environment names are nonempty, case-sensitive, unique
and cannot contain '='; values may be empty. The initial environment is a copied,
read-only snapshot with allocation-free lookup. Process-local mutation and libc
getenv support remain later work. Environment strings confer no authority.

[Startup preparation](../include/kernel/user/startup.h) accepts stable, borrowed
kernel inputs under exclusive ownership of an inactive process on the BSP.
It validates names, installed handles and the total size, copies data through a
borrowed kernel mapping, and publishes the startup address only after completion.
Failure releases partial backing without changing capability ownership. Inputs
are never retained; source storage may be released after the call.

The shared assembly entry calls the native C startup routine, which checks the
record's bounds and initializes accessors before invoking `main(argc, argv)`.
Its return value goes to exit. Programs use
[the startup helpers](../userspace/include/startup.h) instead of decoding the
record. The boot launcher supplies named output/content/endpoint grants as needed,
each program's filename as argv[0], and an OS_NAME environment entry. Hello uses
its arguments and environment in its greeting, then reads its content capability.

## First operation shapes

The initial object operations use one synchronous kernel call:

```c
call(handle, message, message_size, reply, reply_capacity)
```

Syscall 2 implements `call`; 3 implements `close(handle)`. The diagnostic
character-output syscall targets the kernel log. TTY output uses console calls.
The x86_64 convention uses `RAX` for the syscall number and `RDI`, `RSI`, `RDX`,
`R10`, `R8` for the five arguments. Return `RAX` holds status and `RDX` the number
of reply bytes written (zero on failure, and always zero for close). Exit does
not return. Kernel and userspace are rebuilt together against the shared headers;
there is no support for older CALL layouts.

Request and reply fields below are consecutive `uint64_t` values, including
user addresses, offsets and byte counts. They do not embed C pointers, enums
or `size_t`. Each message starts with a protocol/operation pair from the
[message header](../include/abi/message.h), followed by its protocol's payload
union. The handle selects the actual object type; a mismatched protocol is
rejected. Operation numbers are local to each protocol and may overlap.

| Operation | Authority | Payload fields, in order | Reply field |
| --- | --- | --- | --- |
| Console write | `CONSOLE_RIGHT_WRITE` | Source user address, byte length | Bytes written |
| Blob read at offset | `BLOB_RIGHT_READ` | Byte offset, destination user address, capacity | Bytes read |
| Blob size | `BLOB_RIGHT_READ` | Unused | Blob byte size |

Send the complete protocol message structure, including unused union storage:
`console_message` is 32 bytes and `blob_message` is 40 bytes. Both start with the
16-byte tag. Sizes must match exactly. The blob size operation ignores payload
fields, but the complete message must be readable. Initialize unused storage to
zero; the wrappers do this. The shared headers assert sizes and payload offsets.
Each reply needs at least 8 bytes of capacity; success writes one 8-byte field and returns
8 in `RDX`. The transferred data count is in that field, not in `RDX`.
Close removes a valid handle from the caller's table and releases its reference.
It requires no access rights on that handle. Invalid and already-closed handles
return the invalid-handle status; both success and failure return zero in RDX.
Other references to the object remain valid. A last release uses the existing
BSP retirement path, so closing on an AP does not allocate or destroy objects.

For a nonzero read capacity, successful zero-byte reads at or beyond the blob's
end indicate EOF. Reads can return fewer bytes than requested. Writes report
actual progress; a nonempty write cannot succeed with zero progress. A transfer
with zero data length succeeds with a zero count and does not dereference its
data address. Request and reply validation still apply. Partial progress is
success with the actual count; errors report no transferred bytes.

Status values for this slice are 0 for success, 1 for an invalid or stale handle,
2 for denied rights, 3 for an unsupported protocol or operation, 4 for a malformed
request or insufficient reply capacity, and 5 for an invalid user buffer. Counts must
stay within the supplied length or capacity. Operations added later can define
additional errors when needed. Console output adds status 6, unavailable, when
the TTY cannot be used (including panic mode); no output occurs on that error.

These operations complete synchronously in the kernel. They require neither
endpoint queues nor userspace servers. Userspace wrappers can expose convenient
write, read-at-offset, size and close functions over the native ABI.

User addresses refer to the calling process. The kernel must check range
overflow, user accessibility and read/write permissions, and copy through a
defined user-memory access path. Invalid buffers must produce an error rather
than a fatal kernel exception. Request metadata must be captured before use;
request, reply and data buffers must all be validated before side effects.
The implementation must respect the kernel's current BSP-only VM operations
and allocation/reclamation constraints when a syscall runs on an AP.

## Kernel ownership and lifetime

A process would own its address space and capability table and belong to a
space. A task would represent execution and refer to its process. Begin with
one task per process; shared address-space execution and multiple tasks within
a process are later decisions.

The ownership walkthrough for the example is:

1. The launcher creates a process in a space and loads its program and stack.
2. It installs references to the space's console and the selected archive blob,
   restricted to the proposed rights, and fills the startup record.
3. Once setup succeeds, it makes the initial task runnable. Earlier failures
   unwind the new address space, table entries and references.
4. Calls resolve handles through the current process, check object type and
   rights, and operate on that object.
5. Closing a handle removes its table entry and releases that reference.
6. On exit or a fatal userspace fault, execution leaves the process's stack and
   address space before final cleanup. The BSP reaper releases remaining
   capabilities and process-owned memory, preserving the existing cleanup order.

The space retains its console, and the boot archive retains its reserved backing
and read-only kernel mapping. Closing these process handles does not free that
backing. Objects remain alive while handles or other kernel owners retain them.
The table and reference implementation preserves the current concurrency
restrictions as described below.

### Implemented capability-table ownership

Each process starts with an empty table. The BSP installs capabilities before
task submission, adding a reference to each supplied object while the caller
keeps its original reference. The first install allocates eight entries; later
installs reuse vacant slots or double capacity, bounded by allocation success
and the handle's index range. Growth preserves slot indices and generations.
Allocation failure leaves existing handles and reference ownership intact.

`capability_grant()` copies a source grant into another exclusively owned table
with equal or reduced rights. Both tables must still belong to the BSP launcher.
The destination gains a reference; the source remains unchanged. Failure clears
the output handle and leaves existing entries and references intact. This is a
kernel setup operation, not a userspace transfer syscall.

After submission, the process's single executing task owns the table. Resolve
and close require interrupts disabled, do not allocate, and need no table lock.
Resolve checks generation and every requested right, then returns a borrowed
object valid until the entry closes. Close invalidates the handle immediately.
`capability_insert()` adds a received reference into an available slot on the
owning CPU without allocating. A full table returns CAP_FULL. The executing
task then lends its table to a BSP growth request and blocks; no other code
may access it during that loan. The BSP grows it outside all queue/endpoint
locks and returns ownership on wake. Existing handles, generations and object
references survive growth. Allocation and handle-space exhaustion return
errors without consuming the endpoint request. There is no general launcher
access to submitted tables. The kernel result enum is separate from syscall
status values.

Objects have an atomic reference count and a destruction callback, with no
global object registry or operation dispatch. The last release links the object
itself into a locked retirement list; it needs no queue allocation. The BSP
scheduler detaches that list and invokes callbacks outside the lock with IF=0.
A pending release also prompts timer preemption on a busy BSP. An object's
payload still needs its own synchronization, and callbacks must not depend on
a process or table entry that has already been reclaimed.

Process destruction releases every remaining table reference and the table's
storage. Object destruction may follow in the next reaping pass; independent
kernel or process references keep shared resources alive. The
[capability interface](../include/kernel/object/capability.h) and
[object lifetime interface](../include/kernel/object/object.h) define the contracts.
Both concrete console and blob objects use this lifetime model.

CALL resolves the handle once, obtains its object and rights, then captures the
message tag from userspace. A small switch on object type checks the protocol
and selects the console, blob or endpoint handler. Each handler checks the operation,
required rights, exact payload size and user buffers before acting. Tag reads
can fail before operation/rights checks. A mismatched protocol or unsupported
operation returns BAD_OPERATION; a supported operation with insufficient rights
returns DENIED. No operation callback table or registration framework is involved.

Object lifetime, capability tables and the concrete handlers live in
`kernel/object/`, with corresponding headers under `include/kernel/object/`.
Protocols stay in shared `include/abi/` headers. Console and blob operations
complete within the kernel; endpoint calls may suspend the task until a userspace
peer replies. Dynamic protocol discovery, file protocols and framebuffer
protocols remain separate future work.

### Implemented console calls

Each space retains a console wrapper around its existing TTY. The initial
process receives an additional reference with WRITE rights, named by the output
startup role. Process cleanup releases its grant while the space retains the
console, TTY and framebuffer. The object header's immutable type tag keeps an
operation from treating another object kind as a console.

The console handler requires WRITE and captures the fixed request into kernel
storage. It validates the entire source range and the eight reply bytes actually
written before any output.
Extra reply capacity is unused. A zero-length source is not dereferenced, but
still requires a valid capability, request and reply. Errors leave the reply
and TTY untouched and return zero reply bytes.

Each call stages at most 256 source bytes on the syscall stack and writes them
under the existing output lock, then writes the count reply last. This limits
each rendering batch; validation still covers the full requested source range.
The payload is captured before any overlapping reply is written. Callers that
overlap reply and source storage must account for that overwrite before retrying.
The userspace console wrapper checks status, reply size and progress.
`console_write_all()` repeats partial writes for an explicit byte count;
`console_print()` uses it for strings.

The [shared syscall header](../include/abi/syscall.h) and
[console layouts](../include/abi/console.h) define the active slice. RDX carries
reply length for CALL; the diagnostic log call preserves its previous value.
No endpoint queues or general object-operation table are involved.

Hello closes both supplied handles after printing through the
[handle wrapper](../userspace/include/handle.h). The native close wrapper accounts
for the RDX result; the one-argument syscall wrapper cannot be used for
close because it assumes RDX is preserved. Closing leaves the startup record
unchanged, so its output binding then contains a stale handle. Exit releases any
entries still open, while the space keeps its own console reference.

### Implemented blob calls

The [blob object](../include/kernel/object/blob.h) copies an immutable archive-file
view into a reference-counted wrapper. It borrows the bytes and the archive's
kernel-lifetime mapping. The launcher installs a READ capability and releases
its temporary reference; close or process cleanup eventually frees the wrapper
on the BSP. Neither path frees archive frames or removes the archive mapping.

The [blob layouts](../include/abi/blob.h) define size and offset-read operations.
Both require the blob's READ right. Size ignores payload fields after capturing
the complete message. Reads validate the full destination capacity
and the actual reply bytes before writing anything, including at EOF. Zero
capacity ignores the destination address. Counts are clipped to the remaining
blob bytes; offsets at or beyond the end return zero without forming a source
pointer. The reply is written last, so it overwrites any overlapping data bytes.
Request storage may overlap destinations because its fields are captured first.

The blob is not mapped directly into userspace, has no shared seek position,
and supports no mutation or lookup operations. The
[userspace wrappers](../userspace/include/blob.h) check status, reply size and
read counts, clearing output values on failure. Hello uses these wrappers to
read the text asset in chunks and the console helper to finish partial writes.

### Implemented user-buffer access

`user_buffer_check()` validates a readable or writable range belonging to the
currently executing user process. `copy_from_user()` and `copy_to_user()` check
the whole range before copying; a false result leaves the destination untouched
and can become the ABI's invalid-buffer error. No byte of an empty range is
accessed, regardless of its address. A current process and active private root
are still required. Kernel source/destination buffers are trusted caller-owned
storage and must not overlap or alias the user range.

These helpers run with interrupts disabled on the process's CPU. The x86_64
check reads the active root through recursive mappings, checking each ancestor
before descending and requiring user access (and write access when requested)
at every level. It rejects null-page, higher-half, noncanonical, overflowing,
unmapped and insufficiently permitted ranges. It never uses shared scratch
slots, VM range metadata, allocation, or an address-space switch.

Validation remains valid through the copy because user backing is eager and
the process has one task with stable, private mappings. Copy helpers never
schedule. A blocking endpoint operation captures input before sleeping and
keeps its process alive; it resumes its original task and root before writing
to the checked user destination. This path does not recover from kernel mapping
corruption or invalid kernel buffers. Future shared user backing, concurrent
unmapping or external task cancellation would require revisiting that invariant.

Object calls must capture request metadata into kernel storage and
validate all reply/data buffers before producing side effects. A copy helper
only validates its own range; it does not validate an entire operation. See the
[user-memory interface](../include/kernel/user_memory.h). The current character
syscalls remain unchanged and do not yet use these helpers.

## Later operations and open decisions

The broader ABI vocabulary under discussion is `create`, `call`, `send`, `recv`,
`wait` and `close`. This first program does not require endpoint creation or IPC.
Creation must be tied to authority and resource budgets; the ability to request
an object type cannot alone authorize privileged resources.

Endpoints now transport bounded opaque message bytes with synchronous reply
association, blocking receive, queue-full results and peer-closure wakeups. See
[the endpoint contract](endpoints.md) for the implemented limits. A raw handle
value or pointer embedded in those bytes grants no authority in another process.
A request may explicitly copy one capability with equal or reduced rights;
RECEIVE returns a handle installed in the recipient's table.

Copying a capability creates an additional reference with equal or reduced
rights. Moving would remove the sender's reference only when the transfer
commits; failure must not lose it. Neither operation automatically transfers
ownership or accounting for the underlying object.

Future IPC work must define moves, waitable conditions, cancellation and
resource accounting. The namespace and named-endpoint ideas
in the spaces draft remain separate from the current transport.

Only an explicitly selected worklist task is an implementation assignment.
The later ideas here call for no placeholder APIs or object-manager framework.
