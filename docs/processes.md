# Processes, capabilities and the first userspace ABI

Status: console, initrd/RAM file, directory, private-memory and process-completion
capabilities are implemented.
The broader process and resource model remains a working draft alongside the
[spaces draft](spaces.md). Startup delivery, console and file CALL operations,
and handle close are implemented, including the complete userspace example.
[Request/reply endpoints](endpoints.md) now connect separate client and server
processes through the same tagged call ABI. Programs use console capabilities
for TTY output; the separate kernel-log syscall remains available for diagnostics.
The [worklist](wip/process-capability-abi.md) tracks the focused tasks and
handoffs for this first milestone.

## Current programs

Hello receives output through a named resource and an application directory
through the `app` startup scheme binding, plus a shared RAM root under `home`. It lists that directory, looks up and
lists `share`, then opens `hello.txt` with READ and prints it through the console.
The [path helpers](paths.md) compose lookups and retain working-directory handles.
Hello reads through an explicit scheme path and again after changing directory.
It also creates and rediscovers a directory and file under `home`, writes and
truncates the file, then reads it through an independent grant. Its memory
capability supplies temporary file-read buffers and a process-lifetime path
workspace; see [private memory](memory.md) for allocation and release.
Lookup returns owned handles, which hello closes after use. File reads use
explicit offsets and never introduce a shared seek position. See
[the directory contract](directories.md) for rights, cursor behavior and lifetime.

The endpoint example retains its directly supplied read-only content file. Its
client copies that capability to the server, which prints the file and returns
the byte count. These examples exercise directory discovery and explicit grant
transfer independently. The endpoint exchange still uses directly supplied grants.

## Objects, capabilities and handles

An object is the underlying resource: a console, file, directory, memory
service, process-control object or endpoint. A capability grants particular operations on that object. A handle
is an opaque, process-local value naming an entry in the process's capability
table. The kernel entry holds the object reference and rights; userspace cannot
gain rights by changing the handle value.

Different processes may hold different handles and rights to the same object.
Rights are defined by the object's protocol: `CONSOLE_RIGHT_WRITE` authorizes
console output; file READ authorizes reads, WRITE authorizes writes/resizing,
and either permits size queries. Both
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
to a file should not silently transfer the memory charge to that process or
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

Scheme roots have their own name/handle table. Preparation checks that each root
is an installed directory capability and that root names are nonempty, unique
and contain neither ':' nor '/'.
Hello receives the read-only application root under `app`. All three boot
programs receive explicit grants to the shared RAM root under `home`. No lookup right is added by the startup binding.
Startup also copies a launcher-ordered chain of installed directory handles,
boundary first and current last, plus an optional descriptive path. The kernel
validates their types without inferring ancestry or granting parent access. Hello
starts at its application root; libpyxis retains an independently owned context.

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
its arguments and environment in its greeting, then discovers its content through
its application directory.

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
| File read at offset | `FILE_RIGHT_READ` | Byte offset, destination user address, capacity | Bytes read |
| File size | File READ or WRITE | Unused | File byte size |
| File write at offset | `FILE_RIGHT_WRITE` | Byte offset, source user address, length | Bytes written |
| File resize | `FILE_RIGHT_WRITE` | New byte size | None |

Send the complete protocol message structure, including unused union storage:
`console_message` is 32 bytes and `file_message` is 40 bytes. Both start with the
16-byte tag. Sizes must match exactly. The file size operation ignores payload
fields, but the complete message must be readable. Initialize unused storage to
zero; the wrappers do this. The shared headers assert sizes and payload offsets.
Operations with a reply need at least 8 bytes of capacity; success writes one
8-byte field and returns 8 in `RDX`. The transferred data count is in that field,
not in `RDX`. RESIZE returns zero reply bytes and ignores the reply buffer.
Close removes a valid handle from the caller's table and releases its reference.
It requires no access rights on that handle. Invalid and already-closed handles
return the invalid-handle status; both success and failure return zero in RDX.
[Local handle copying](paths.md#local-handle-copies) retains another reference
with the same or reduced rights, without changing the source.
Other references to the object remain valid. A last release uses the existing
BSP retirement path, so closing on an AP does not allocate or destroy objects.

For a nonzero read capacity, successful zero-byte reads at or beyond the file's
end indicate EOF. Reads can return fewer bytes than requested. Console writes
report actual progress; a nonempty write cannot succeed with zero progress.
RAM-file writes complete in full or leave the file unchanged. A transfer
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
2. It installs references to the space's console and the selected directory,
   file or endpoint, restricted to the requested rights, and fills startup data.
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
Console, file, directory, memory-service and endpoint objects use this lifetime
model. Memory-service lifetime is independent of process-owned regions.

CALL resolves the handle once, obtains its object and rights, then captures the
message tag from userspace. A small switch on object type checks the protocol
and selects the console, file, directory, memory or endpoint handler. Each checks
the operation, required rights, exact payload size and user buffers before acting. Tag reads
can fail before operation/rights checks. A mismatched protocol or unsupported
operation returns BAD_OPERATION; a supported operation with insufficient rights
returns DENIED. No operation callback table or registration framework is involved.

Object lifetime, capability tables and the concrete handlers live in
`kernel/object/`, with corresponding headers under `include/kernel/object/`.
Protocols stay in shared `include/abi/` headers. Console and file operations
complete within the kernel; endpoint calls may suspend the task until a userspace
peer replies. Directory lookup can block for capability-table growth. Dynamic
protocol discovery and framebuffer protocols remain later work. Directory CREATE
also waits for BSP entry allocation or disposal. File calls may wait behind
another operation on the same file or for BSP backing allocation/release.

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

Hello closes its startup grants after printing through the
[handle wrapper](../userspace/include/handle.h). The native close wrapper accounts
for the RDX result; the one-argument syscall wrapper cannot be used for
close because it assumes RDX is preserved. Closing leaves the startup record
unchanged, so its output binding then contains a stale handle. Exit releases any
entries still open, while the space keeps its own console reference.

### Implemented file calls

The [file layouts](../include/abi/file.h) define SIZE, READ, WRITE and RESIZE.
Reads and writes use explicit offsets; neither objects nor handles carry a seek
position. READ requires READ authority; WRITE and RESIZE require WRITE without
requiring READ. SIZE accepts either right. No lookup or append operation is
implicit in a file call. Directory grants control which file rights can be
obtained through LOOKUP/CREATE.

Initrd files borrow immutable archive bytes and their kernel-lifetime mapping.
Last-reference retirement frees only their wrapper on the BSP. Mutation returns
READ_ONLY even when a handle carries WRITE; lacking WRITE returns DENIED first.
RAM files start empty and own their backing, which is freed with the wrapper on
last-reference retirement. Directory entries and handles retain independent
references, so closing all handles need not destroy a named RAM file.

READ checks the full destination capacity and reply storage even at EOF. Zero
capacity ignores the data address. Reads clip to the remaining bytes; offsets at
or beyond EOF return zero without forming a data pointer. The reply is written
last and wins over overlapping data. SIZE returns one serialized size observation,
not a snapshot covering subsequent reads.

WRITE checks offset/length overflow, the entire source and reply storage before
mutation. Success reports the full requested count; failure leaves contents and
logical size unchanged. A nonempty write beyond EOF zero-fills the gap. A
zero-length write ignores the data address and does not extend the file, even
at an offset beyond EOF. It still checks authority, backing and reply storage.
RESIZE returns no reply bytes. Growth exposes zero-filled bytes; shrink discards
the tail permanently, including when later growth reuses retained capacity.

A RAM file uses one heap buffer, with geometric capacity growth and an exact-size
retry if spare capacity cannot be allocated. Nonzero shrink retains capacity;
resize to zero releases it. There is no protocol file-size ceiling beyond checked
arithmetic and allocator limits. See [technical debt](technical-debt.md) for the
memory-cost tradeoff.

A short per-file spinlock protects operation ownership and a FIFO of waiters.
Only the owner accesses bytes, size and capacity. It can lend that ownership to
the BSP while blocked for backing replacement; no spinlock spans the wait.
Other operations sleep and receive ownership directly in FIFO order. Waiters
and allocation requests live in task metadata, never private syscall stacks.
The BSP allocates, copies the old live prefix and frees the old buffer; failure
keeps the old allocation. The requester then zeroes/copies bytes and publishes
size before handing ownership on. Readers cannot observe an intermediate state.

The current single-task/private-mapping contract keeps checked user sources and
replies stable across waits. Only the resumed caller accesses them. Request
metadata is captured before any data/reply write, including overlapping buffers.
All fallible work precedes mutation. Shared user memory, task cancellation or
multiple tasks per process would require revisiting these assumptions.

[Libpyxis wrappers](../userspace/include/file.h) return native status for all four
operations and validate reply sizes/counts; failure clears output values. Hello
uses a WRITE-only creation grant and independently looks up a READ grant, writes
text, appends then truncates a tail, and prints the retained contents.

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

## Implemented process completion

A process-control capability exposes WAIT through a tagged native CALL. Its
[protocol](../include/abi/process.h) has one right, WAIT, and a header-only request.
The [libpyxis wrapper](../userspace/include/process.h) preserves native errors and
returns either EXITED with a signed exit status or FAULTED. Fault results carry
no exit status; detailed architecture diagnostics remain in the kernel log.

WAIT blocks until completion. The result is immutable and is not consumed:
multiple observers and repeated waits receive the same result, including waits
started after completion. Rights and writable reply storage are checked before
sleeping. A short reply buffer or extra request payload is rejected. Only the
resumed observer accesses its own user reply mapping.

The execution owner retains one reference to the control object. After exit or
an ordinary fatal user fault, the scheduler leaves the private root and task
stack before handing the task to the BSP. The BSP releases the process address
space, allocation records, capability table, kernel stack and task metadata
before publishing completion and dropping that reference. Capability object
retirement still follows its normal deferred destruction path.

The control object contains no process pointer, so retaining a handle preserves
only completion state, not execution memory. Closing the last observer before
exit leaves execution alone. Unsubmitted preparation failures release their
control object without a result; launch must not expose a live observer until
preparation succeeds. There is no termination operation or userspace launcher
in this task.

Wait records live in permanent task metadata, never remote task stacks. The
completion lock serializes registration and publication, with the lock order
completion then scheduler queues. Wake-before-sleep is remembered. Publication
detaches each waiter before waking it, and no lock spans a context switch.
These rules depend on the existing one-task-per-process contract; external task
cancellation would need to detach an outstanding wait before teardown.

Client receives a named `server_process` grant. It finishes its endpoint exchange,
closes the endpoint so server can exit, then waits and reports the result. A
wait while retaining the open endpoint would leave server waiting for another
request or closure.

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
