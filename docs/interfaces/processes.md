# Processes, capabilities and the first userspace ABI

Status: console, initrd/RAM file, directory, private-memory, process-completion,
caller-scoped/group-bound launchers and [mapped display](graphics.md) capabilities are implemented.
The broader process and resource model remains a working draft alongside the
[spaces draft](../wip/spaces.md). Startup delivery, console and file CALL operations,
and handle close are implemented, including the complete userspace example.
[Request/reply endpoints](endpoints.md) now connect separate client and server
processes through the same tagged call ABI. Programs use console capabilities
for TTY output; the separate kernel-log syscall remains available for diagnostics.
The [worklist](../wip/process-capability-abi.md) tracks the focused tasks and
handoffs for this first milestone.

## Current programs

Normal boot starts the [shell](../userland/shell.md) at home:// with terminal, launcher,
memory, display, clock, keyboard, optional [space-title authority](../userland/init.md#space-titles)
and app/home root grants. It launches foreground utilities
with explicit resources, waits for completion and returns to its prompt. Its space and TTY
survive shell exit; no supervisor restarts it.

Hello and the client/server examples remain explicit build targets, outside the
normal initrd. Their demonstration flows require the grants described below.

Hello receives output through a named resource and an application directory
through the `app` startup scheme binding, plus a shared RAM root under `home`. It lists that directory, looks up and
lists `share`, then opens `hello.txt` with READ and prints it through the console.
The [path helpers](../userland/paths.md) compose lookups and retain working-directory handles.
Hello reads through an explicit scheme path and again after changing directory.
It also creates a directory under `home`, then uses libc stdio to create a file,
write formatted text, seek back and print the contents. Its memory
capability supplies temporary file-read buffers and a process-lifetime path
workspace; see [private memory](../kernel/memory.md) for allocation and release.
Lookup returns owned handles, which hello closes after use. File reads use
explicit offsets and never introduce a shared seek position. See
[the directory contract](directories.md) for rights, cursor behavior and lifetime.

The endpoint example retains its directly supplied read-only content file. Its
client copies that capability to the server, which prints the file and returns
the byte count. These examples exercise directory discovery and explicit grant
transfer independently. The endpoint exchange still uses directly supplied grants.

## Objects, capabilities and handles

An object is the underlying resource: a console, file, directory, memory
service, display, clock, keyboard, launcher, process-control object or endpoint. A capability
grants particular operations on that object. A handle is an opaque, process-local
value naming an entry in the process's capability
table. The kernel entry holds the object reference and rights; userspace cannot
gain rights by changing the handle value.

Different processes may hold different handles and rights to the same object.
Rights are defined by the object's protocol: `CONSOLE_RIGHT_WRITE` authorizes
console output and `CONSOLE_RIGHT_READ` authorizes terminal input; file READ authorizes reads, WRITE authorizes writes/resizing,
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
[shared header](../../include/abi/startup.h) defines its layout; all embedded
addresses refer to that child's virtual memory. The total budget is 64 KiB,
including page padding. Only the required pages are allocated. Version remains
1, with the old fixed-role layout replaced outright; rebuild in-tree programs
together with the kernel.

The read-only portion contains the header with three standard-stream bindings,
named resource bindings, scheme-root
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

Standard streams are fixed stdin/stdout/stderr slots with explicit console, file
or [pipe](pipes.md) protocol tags. Absent slots use `STARTUP_STREAM_NONE` and an
invalid handle.
Each present slot owns a distinct handle, disjoint from all named resources,
roots and working-directory bindings, with exactly its protocol's READ right
for stdin or WRITE right for output. Libc adopts these handles directly before
heap initialization; startup lookup borrows them and creates no extra reference.
Closing a FILE releases that handle, leaving its startup entry stale. The kernel
still reclaims remaining handles on exit or fault. See [stdio](../userland/stdio.md).

Scheme roots have their own name/handle table. Preparation checks that each root
is an installed directory capability and that root names are nonempty, unique
and contain neither ':' nor '/'.
The shell receives the read-only application root under `app` and a shared RAM
root under `home`. No lookup right is added by the startup binding.
Startup also copies a launcher-ordered chain of installed directory handles,
boundary first and current last, plus an optional descriptive path. The kernel
validates their types without inferring ancestry or granting parent access. The
shell starts at home; libpyxis retains an independently owned context.

Startup also carries an optional explicit [service namespace](namespaces.md)
handle. It is borrowed by `startup_namespace()` and has no ambient association
with the process's space. Launch selects a LOOKUP-authorized namespace through
`namespace_grant` (zero absent, otherwise grant index plus one). A namespace entry
cannot also supply a standard stream. Known directory-root/name conflicts reject
launch; runtime resolution checks again because bindings can change.

The launcher supplies argument strings, including argv[0] when present; neither
the kernel nor startup parses a command line. Zero arguments are valid and still
provide argv[0] == NULL. Environment names are nonempty, case-sensitive, unique
and cannot contain '='; values may be empty. The initial environment is a copied,
read-only snapshot with allocation-free lookup. Process-local mutation and libc
getenv support remain later work. Environment strings confer no authority.

[Startup preparation](../../include/kernel/user/startup.h) accepts stable, borrowed
kernel inputs under exclusive ownership of an inactive process on the BSP.
It validates names, installed handles and the total size, copies data through a
borrowed kernel mapping, and publishes the startup address only after completion.
Failure releases partial backing without changing capability ownership. Inputs
are never retained; source storage may be released after the call.

The shared assembly entry calls the native C startup routine, which checks the
record's bounds and initializes accessors before invoking `main(argc, argv)`.
Its return value goes to exit. Programs use
[the startup helpers](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/startup.h) instead of decoding the
record. The boot launcher supplies three dedicated console stream handles in
addition to the shell's named terminal input/output, memory,
launcher, [display](graphics.md), [clock](../kernel/timekeeping.md) and
[keyboard](../devices/keyboard.md) resources, app/home roots, a home directory chain, argv[0] and an OS_NAME environment entry.

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
[message header](../../include/abi/message.h), followed by its operation's payload.
The handle selects the actual object type; a mismatched protocol is
rejected. Operation numbers are local to each protocol and may overlap.

| Operation | Authority | Payload fields, in order | Reply field |
| --- | --- | --- | --- |
| Console write | `CONSOLE_RIGHT_WRITE` | Source user address, byte length | Bytes written |
| Console read | `CONSOLE_RIGHT_READ` | Destination user address, capacity | Bytes read |
| Console size | Console READ or WRITE | Unused | Columns, rows |
| File read at offset | `FILE_RIGHT_READ` | Byte offset, capacity | Byte count, copied bytes |
| File size | File READ or WRITE | None | File byte size |
| File write at offset | `FILE_RIGHT_WRITE` | Byte offset, length, copied bytes | Bytes written |
| File resize | `FILE_RIGHT_WRITE` | New byte size | None |
| File sync | `FILE_RIGHT_WRITE` | None | None |

Send the complete 32-byte `console_message`, including unused union storage;
the wrappers initialize it to zero. FILE uses exact operation extents after the
16-byte tag: READ has 16 payload bytes, WRITE has 16 plus its inline byte count,
RESIZE has eight, and SIZE/SYNC have none. FILE request and reply payloads are
bounded to 4 KiB, excluding the tag. READ capacity is at most 4,088 bytes and
WRITE length at most 4,080; larger native payloads are rejected. Libpyxis caps
larger application requests to one transfer and returns its actual short count.
READ requires reply capacity for its eight-byte count plus the requested bytes,
but copies only the returned bytes. Its successful reply is exactly eight bytes
plus that count. Console SIZE needs a 16-byte reply; the other count/size replies
contain one eight-byte field. `RDX` reports reply bytes, including FILE READ data;
the transferred data count is a field in the reply, not `RDX`. RESIZE and SYNC
return zero reply bytes and ignore the reply buffer. Errors return no reply bytes.
Close removes a valid handle from the caller's table and releases its reference.
It requires no access rights on that handle. Invalid and already-closed handles
return the invalid-handle status; both success and failure return zero in RDX.
[Local handle copying](../userland/paths.md#local-handle-copies) retains another reference
with the same or reduced rights, without changing the source.
`handle_query()` returns kernel-authenticated resource/transport masks, protocol
and native/exported kind, including for zero-authority grants. It exposes no
object identity, allocates nothing and changes no ownership. `SYSCALL_HANDLE_INFO`
takes a handle and a `struct handle_info` output address in RDI/RSI; RAX returns
status and RDX is 32 on success, zero on failure. Interface and kind are immutable:
a withdrawn export can still be queried, but invocation reports closure. The
`handle_rights()` convenience helper selects the two authority masks from this
query. FILE helpers use the interface/kind to choose native calls or exported
endpoint invocation; they never infer identity or routing from handle bits.

Other references to the object remain valid. A last release uses the existing
BSP retirement path, so closing on an AP does not allocate or destroy objects.

For a nonzero read capacity, successful zero-byte reads at or beyond the file's
end indicate EOF. Reads can return fewer bytes than requested. Console writes
report actual progress; a nonempty write cannot succeed with zero progress.
Native file writes also permit short progress: a nonempty success reports
1..requested bytes. Advance the source and offset by that count, then submit
only the remaining suffix. RAM-file writes still complete in full or fail
unchanged; this is a property of RAM backing, not the general file contract.
A zero-length write validates authority, offset arithmetic and reply storage,
then succeeds with zero without dereferencing its data address, extending the
file or contacting the backend. A missing WRITE grant still fails.

Errors carry no count for that call. OUTCOME_UNKNOWN means a submitted mutation
has no trustworthy completion: it may already have changed the file. Do not
infer zero effect, automatically replay it or roll back unrelated changes.
Previously confirmed calls keep their progress. Libpyxis rejects impossible
write counts (oversized, overflowing, or zero for nonempty input) and malformed
write/resize replies as OUTCOME_UNKNOWN. This status does not claim that ordinary
host errors undo earlier effects. NO_SPACE, QUOTA and FILE_TOO_LARGE distinguish
storage limits from NO_MEMORY (allocation failure) and LIMIT (representation or
implementation bounds).

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
with equal or reduced rights. BSP must exclusively own both tables, either
during preparation or through a blocked caller's launch loan.
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
errors without consuming the endpoint request. Launch similarly lends only its
caller's table for grant copying and installing the completion observer; no
arbitrary access to another submitted process's table is permitted. The kernel
result enum is separate from syscall status values.

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
[capability interface](../../include/kernel/object/capability.h) and
[object lifetime interface](../../include/kernel/object/object.h) define the contracts.
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

The WRITE handler requires WRITE and captures the fixed request into kernel
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

The shell receives a separate READ-only grant named `input`, referring to the
same space console. READ validates its entire destination and count reply before
consuming bytes, then sleeps if input is empty. It returns up to 256 available
bytes per call, without echo, editing or EOF. Zero capacity returns immediately,
even without a keyboard, and does not acknowledge input loss. Nonempty reads
return UNAVAILABLE if keyboard initialization failed. INPUT_LOST reports and
acknowledges discarded input; no data or count reply is written on that result.
See [keyboard input](../devices/keyboard.md) for mapping and overflow behavior.
[Independent terminal sessions](../userland/terminal-sessions.md) implement the
same application protocol with separate queues, input EOF and hangup; they do
not require keyboard/framebuffer authority.

Readers share a stream and acquire read ownership FIFO. Ownership survives
sleeping for input; later readers cannot steal a wakeup or overtake the owner.
The short input lock protects the queue and wait links, with interrupts disabled;
no user copy or sleep occurs under it. Wait records live in shared task metadata.
The BSP input producer detaches a wait record before waking it. Read ownership
is independent of the output lock. Foreground ownership is cooperative: a parent
must stop reading while its child uses the same console.

SIZE requires either READ or WRITE and returns current character columns/rows,
excluding the session tab bar. Dimensions are fixed by the space's TTY at boot;
there is no resize event. Libpyxis `console_read()` and `console_size()` preserve
native call statuses. No ABI/schema version bump is needed.

The [shared syscall header](../../include/abi/syscall.h) and
[console layouts](../../include/abi/console.h) define the active slice. RDX carries
reply length for CALL; the diagnostic log call preserves its previous value.
No endpoint queues or general object-operation table are involved.

Hello closes its startup grants after printing through the
[handle wrapper](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/handle.h). The native close wrapper accounts
for the RDX result; the one-argument syscall wrapper cannot be used for
close because it assumes RDX is preserved. Closing leaves the startup record
unchanged, so its output binding then contains a stale handle. Exit releases any
entries still open, while the space keeps its own console reference.

### Implemented file calls

The [file layouts](../../include/abi/file.h) define SIZE, READ, WRITE, RESIZE and SYNC.
Reads and writes use explicit offsets; neither objects nor handles carry a seek
position. READ requires READ authority; WRITE, RESIZE and SYNC require WRITE without
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

RAM WRITE checks offset/length overflow, the entire source and reply storage before
mutation. Success reports the full requested count; failure leaves contents and
logical size unchanged. A nonempty write beyond EOF zero-fills the gap. A
zero-length write ignores the data address and does not extend the file, even
at an offset beyond EOF. It still checks authority, backing and reply storage.
RESIZE returns no reply bytes. Growth exposes zero-filled bytes; shrink discards
the tail permanently, including when later growth reuses retained capacity.

SYNC returns no reply bytes. RAM files accept it as a no-op; host files request
full backing-storage synchronization. Parent directories require separate sync
requests, and neither close nor `fflush` implicitly syncs storage. See the
[host synchronization contract](../devices/virtio-fs.md#synchronization) for ordering and
durability limits.

A RAM file uses one heap buffer, with geometric capacity growth and an exact-size
retry if spare capacity cannot be allocated. Nonzero shrink retains capacity;
resize to zero releases it. There is no protocol file-size ceiling beyond checked
arithmetic and allocator limits. See [technical debt](../technical-debt.md) for the
memory-cost tradeoff.

A short per-file spinlock protects operation ownership and a FIFO of waiters.
Only the owner accesses bytes, size and capacity. It can lend that ownership to
the BSP while blocked for backing replacement; `busy` remains set and no spinlock
spans the wait. Other operations sleep and receive ownership directly in FIFO
order. The previous owner detaches the resource waiter before waking it; only
after that wait finishes can the new owner prepare a BSP service wait.

The typed FILE replacement request uses the common BSP FIFO and its ordinary
executor notification, with no VM handoff. Waiters and requests live in task
metadata, never private syscall stacks. The BSP's local replacement helper
allocates, copies the old live prefix and frees the old buffer; failure keeps the
old allocation and capacity. Capacity zero releases backing without allocation.
Submission also allocates nothing. The BSP clears its file loan before completion
and never changes logical size. The requester consumes the result, releases its
request, then zeroes/copies bytes and publishes size before handing operation
ownership on. Readers cannot observe an intermediate state.

The current single-task/private-mapping contract keeps checked user sources and
replies stable across waits. Only the resumed caller accesses them. Request
metadata is captured before any data/reply write, including overlapping buffers.
All fallible work precedes mutation. Shared user memory, task cancellation or
multiple tasks per process would require revisiting these assumptions.

[Libpyxis wrappers](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/file.h) return native status for all four
operations and validate reply sizes/counts; failure clears output values.
[Libc stdio](../userland/stdio.md) composes these operations into independently positioned
FILE streams without a kernel descriptor table.

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
[user-memory interface](../../include/kernel/user_memory.h). The current character
syscalls remain unchanged and do not yet use these helpers.

## Implemented process completion

A process-control capability exposes WAIT through a tagged native CALL. Its
[protocol](../../include/abi/process.h) has one right, WAIT, and a header-only request.
The [libpyxis wrapper](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/process.h) preserves native errors and
returns EXITED with a signed exit status, FAULTED, or TERMINATED after an execution
group stop. Fault and termination results carry zero exit_status; architecture fault
diagnostics remain in the kernel log. Group completion additionally observes deferred
cleanup; see [execution groups](execution-groups.md).

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
preparation succeeds. There is no termination operation. The launcher below returns this same
completion capability.

Wait records live in permanent task metadata, never remote task stacks. The
completion lock serializes registration and publication, with the lock order
completion then scheduler queues. Wake-before-sleep is remembered. Publication
detaches each waiter before waking it, and no lock spans a context switch.
These rules depend on the existing one-task-per-process contract; external task
cancellation would need to detach an outstanding wait before teardown.

Client receives a process-control handle from launching server. It finishes its endpoint exchange,
closes the endpoint so server can exit, then waits and reports the result. A
wait while retaining the open endpoint would leave server waiting for another
request or closure.

## Implemented userspace launch

A launcher capability authorizes LAUNCH in the caller's space on its assigned
CPU. The ordinary launcher is a caller-scoped service, like private-memory allocation;
there is no target-space or CPU argument. Holding a launcher does not grant
implicit access to files or other resources. A launcher can itself be delegated
through an explicit grant, authorizing the recipient to launch in its own space.

The [request](../../include/abi/launcher.h) supplies a READ file handle for the P1F
image, source handles with resource/transport masks, arguments, environment,
standard streams, named resources, an optional namespace, scheme roots and
working-directory context. Source grants retain or reduce each authority mask. Bindings and working-directory entries refer to grant-list
indices; repeated references share one child handle, while separate grant-list
entries produce separate handles. No resources, roots, environment or launcher
are inherited implicitly. The image handle is not passed unless listed.

Each present standard stream references an exclusive grant-list index. It cannot
share that index with another stream, a named resource, a root, a working
directory or the namespace. The existing grant installation creates exactly one child handle for
that entry, and startup records it directly; there is no second retained copy.
Separate entries may deliberately copy the same source object, as for console
stdout/stderr and explicit terminal resources. No terminal grants are synthesized
from stream metadata.

Unknown protocols, nonzero indices for absent streams, out-of-range/aliased
indices and masks other than the exact direction right fail with BAD_REQUEST.
Missing source authority reports BAD_HANDLE or DENIED; an object/protocol
mismatch reports WRONG_TYPE. Validation precedes child submission. An absent
stream is valid and does not cause implicit inheritance.

All nested addresses belong to the caller. Arrays, copied strings and alignment
have a combined 64 KiB capture budget; the final startup region separately has
the existing 64 KiB budget including page padding. Array counts and arithmetic
are checked before copying. Names, duplicate bindings, directory types and
startup layout use the existing startup validation. Empty arrays are ignored;
working_path is optional and requires a nonempty directory chain. argv contains
argc string addresses; the kernel adds the child's final NULL.

For RAM/archive images, the caller obtains staging storage from BSP, then
captures all metadata on its own CPU/root. Source handles remain alive in its
exclusively owned table while blocked. It acquires the file's existing
operation ownership before lending the table and file to BSP. The image remains
stable throughout validation and load: other reads/writes/resizes queue until
loading releases ownership. No spinlock is held during loading, and no second
whole-image snapshot is allocated. Later file changes cannot alter the child's
copied image.

A caller still needs its existing LAUNCH-authorized launcher capability. A READ
file handle is sufficient as the source for a host-backed binary or interpreter;
the launcher creates the process in the caller's space on its assigned CPU and
applies the caller's explicit grants. This uses the existing launcher request,
with no new syscall, rights or image-format version. The host executable file
limit is 16 MiB and reports CALL_LIMIT; it is not a process runtime memory
limit. The host worker reads into an owned heap copy, then the existing BSP
loader validates and maps that image. It accepts positive short reads. An empty
or invalid image reports BAD_REQUEST. Premature EOF or a detected difference
between file sizes before and after capture reports CALL_IO. Transport and
other backend errors retain their status, and failed capture is not retried.
The staging copy is freed on both success and failure. The process never runs
from live host mappings.

Capture retains the same host file node and open handle across its reads, so an
atomic pathname replacement does not redirect an in-progress load. A caller
must not modify the image in place while it is loading. Before/after size
checks cannot prove that captured contents form a coherent snapshot. Script
files retain their existing live READ-handle behavior; this capture path applies
only to binary executables and interpreters.

BSP creates an inactive process and initial stack using the same helper as boot
setup, installs explicit grants and prepares startup. It installs a WAIT observer
in the parent's table before task submission; failure after installation closes
that unpublished observer and destroys the child. Only successful submission
makes the child runnable. The image operation is released before submission,
and staging storage is freed before waking the caller. BSP never dereferences
caller addresses or a remote task-stack request. Caller mappings are unchanged.

Launch capture allocation/disposal, single-child preparation/publication and
batch group operations use typed requests on the common BSP FIFO. The request
captures the parent process and assigned CPU before publication. Loading and
capability installation use BSP-local helpers directly, without executor
self-waits. The caller consumes returned handles or owned allocation results
before releasing the request; cleanup submission itself needs no allocation.
Capture and group allocations outlive individual requests. Each stage consumes
its capture, while the group retains prepared children and provisional observers
until a later publish or discard request. No user address or remote stack pointer
is part of the service record.

Success returns one owned process-control handle. The child may run or exit
before the parent resumes; the handle retains completion independently. Failure
leaves no runnable child and preserves parent source grants. Handle-table growth
may remain for reuse after failure. Invalid image/startup data uses BAD_REQUEST;
invalid handles, denied rights, wrong image object type, bad user buffers and
allocation exhaustion retain their native statuses.

[Libpyxis](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/launcher.h) exposes launcher_launch without heap
allocation. The shell receives a launcher from boot setup and explicitly
constructs its children's grants, arguments, environment and directory context.
The optional client/server example requires a readable server image and endpoint
grants in addition to its launcher. Endpoint creation from userspace is separate
work.

## Batch launch

`LAUNCHER_LAUNCH_BATCH` accepts an array of one through eight ordinary launch
requests under the same LAUNCH authority (ordinary or group-bound). It prepares every image,
child grant table, startup region, kernel task stack and WAIT observer before
publishing any child. Each request retains the existing image and startup limits;
there is no implicit inheritance or new target-space/CPU selection.

The full reply range is validated before side effects. The kernel installs all
observers and prepares their ordered result array before publication. Caller
mappings remain stable throughout the operation, so delivery of that prepared
result cannot introduce an error after children become runnable. For an [execution group](execution-groups.md), publication rechecks admission under
the group lock: sealing can reject the entire prepared batch with ENDPOINT_CLOSED.
After admission succeeds, publication contains no allocation, validation or other
fallible work. It makes every child
runnable, not simultaneous: a child may already have finished before return.
Single-child launching shares the same preparation and publication machinery.

Image operation ownership ends after each stage's preparation, before acquiring
the next image. Repeating the same executable in a batch is therefore valid.
There are no public prepared-process handles and no partially runnable batch.

On any preparation failure, all prepared children and their task stacks are
released, all provisional observer handles are removed, and no child executes.
Source handles remain valid, including caller-owned pipe ends. Installed child
endpoint references are released through normal deferred destruction, without
retaining unused copies in staging. As with single launch, grown capability-table
storage may remain available for reuse.

The fixed `launch_batch_reply` holds `failed_index` and eight child-handle slots.
Success fills the first requested slots in request order, zeroes unused slots,
and sets the index to `LAUNCH_NO_STAGE`. Failure zeroes all child slots. The index
is the zero-based request responsible for an image, grant, startup, task-setup or
observer-installation failure; batch-wide failures use `LAUNCH_NO_STAGE`.

This operation has a narrow exception to ordinary CALL error replies: after
validating output storage it can return the complete reply alongside a nonzero
native error status. Dispatch failures and unusable reply storage return no
reply bytes. The userspace helper initializes outputs to invalid handles and
`LAUNCH_NO_STAGE`, so a failure without metadata cannot leave a stale stage index.

`launcher_launch_batch` submits native PXE requests. `program_launch_batch`
prepares each request's optional single-level shebang interpreter first, then
submits the whole batch. Script-prefix, interpreter-resolution and preparation
errors identify the corresponding input index without starting any children.
Temporary interpreter grants, arrays and strings are released on every path;
appending the script grant preserves dedicated standard-stream indices. Mutable
images retain the existing per-image capture limits, not a cross-image snapshot.

After success, each child has its ordinary process lifetime and retains any
[execution-group membership](execution-groups.md). Observers only
wait; their closure, launcher exit or a sibling fault does not terminate another
child. Normal exit and faults reclaim that child's resources and pipe ends.
The caller must close its own unused pipe copies before waiting for EOF-dependent
children. Previously created or truncated files are not rolled back on failure.
The [shell pipeline guide](../userland/shell.md#foreground-pipelines) specifies stream
selection, redirection precedence and child completion policy. The
[stream reference](../userland/shell-streams.md) connects these launch and I/O contracts.

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

## Execution-group launch containment

[Execution groups](execution-groups.md) add separately authorized creation, bound
launchers, inherited membership and sealed admission. The ordinary local launcher
remains caller-scoped. Membership survives observer/launcher closure; supervision
closure currently seals new launches without terminating existing members. Group
termination and completion require the separate [cleanup work](../wip/execution-group-termination.md).
