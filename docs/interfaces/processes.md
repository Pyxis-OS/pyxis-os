# Processes, capabilities and the first userspace ABI

Status: console, initrd/RAM file, directory, private-memory, process-completion,
caller-scoped/group-bound launchers and [mapped display](graphics.md) capabilities are implemented.
The broader process and resource model remains a working draft alongside the
[spaces draft](../wip/spaces.md). Startup delivery, console and file CALL operations,
and handle close are implemented, including the complete userspace example.
[Request/reply endpoints](endpoints.md) now connect separate client and server
processes through the same tagged call ABI. Programs use console capabilities
for TTY output; the separate kernel-log syscall remains available for diagnostics.
The first milestone's
[worklist](https://git.internal/PyxisOS/pyxis-os/src/commit/93851aebce74c71ceea93774c4d97e01bc2a60e7/docs/wip/process-capability-abi.md)
remains in Git history.

## Current programs

[Boot init](../userland/init.md) creates each space and its init; the session
then starts the [shell](../userland/shell.md) in the space's start directory
(`home://` for stock interactive spaces) with terminal, launcher,
memory, display, clock, keyboard, pointer, optional [space-title authority](../userland/init.md#space-titles)
and boot/tmp root grants. It launches foreground utilities
with explicit resources, waits for completion and returns to its prompt. Its space and TTY
survive shell exit; no supervisor restarts it.

Hello and the client/server examples remain explicit build targets, outside the
normal initrd. Their demonstration flows require the grants described below.

Hello receives output through a named resource and the boot archive directory
through the `boot` environment entry, plus a shared RAM directory under `home`. It lists that directory, looks up and
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
service, display, clock, keyboard, pointer, launcher, process-control object or endpoint. A capability
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

Handle admission captures rights and transport plus an owned storage reference
under the table guard. CALL holds it through blocking continuations and cleanup;
secondary handles and readiness interests use the same admission. No entry
pointer survives table growth. These storage references confer no extra grant
authority or open-end ownership. CLOSE detaches the exact generation first,
then applies protocol close and grant release outside the guard; an admitted
operation cannot make that handle usable again. Process teardown applies the
same logical close effects to its detached entries.

Grant-producing capture (COPY, launch sources and endpoint attachments) retains
prospective logical authority outside that guard, then rechecks the exact source
generation, object and masks. A stale source fails and unwinds; admitted grants
survive later CLOSE. Ordinary CALL/readiness references remain storage-only.

A process starts with one initial user task. Only a holder of the optional
`threads` resource can add native siblings, which share this table; ordinary
application launches omit it. The selected [libc thread profile](../userland/threads.md)
provides joinable and detached C11/pthread siblings, per-thread `errno`, static
local-exec language TLS and synchronized shared runtime state. Completed detached
workers are collected during create/join and orderly exit, within the profile's
bounded runtime records. Cancellation and process-shared synchronization remain
unsupported; the
[SDK TLS contract](../development/sdk.md#static-tls-and-runtime-keys) excludes
dynamic modules and other TLS models.
Growth and installation use the shared-table
protocols below; teardown requires drained activity and claims.
See the [native threads](threads.md#shared-capability-and-vm-ownership).

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
[shared header](../../include/abi/startup.h) defines its layout; embedded addresses
refer to the child's virtual memory. The fixed header is 72 bytes. Version remains
1; the replaced layout requires rebuilding programs with the kernel.

The read-only portion holds the header, a caller-bound
[environment handle](environment.md), the independently owned cwd directory chain
and an optional descriptive path. The writable portion holds `argv`, its final
NULL and argument strings. Both mappings are NX and survive until address-space
destruction. Arguments require no userspace heap at entry. Zero arguments are
valid and still provide `argv[0] == NULL`; the kernel parses no command line.

The environment owns each named CAP or TEXT entry. Exact directory names such as
`boot`, `home` and `tmp` use the same table as services, native resources and text.
LOOKUP captures an owned grant or copies complete text; it never borrows a handle
from immutable startup bindings. There are no separate stream, root, resource,
string-environment or namespace arrays. Sibling tasks share the process table;
children receive only the explicitly staged selection.

Standard streams are `stdin`, `stdout` and `stderr` CAP entries. Libc selects their
kernel-attested protocol and direction authority, supporting native CONSOLE/PIPE,
native or exported FILE, and exported STREAM for stdin. No HTTP-specific
observation companion is required or inferred.
Libc owns its lookup grants and releases its initialization snapshot before
constructors or main. Closing/replacing an original standard descriptor
conditionally removes only its recorded publication identities, preserving later
native replacements. See [environment ownership](environment.md) and
[stdio](../userland/stdio.md).

Cwd is a launcher-ordered chain of installed directory handles, boundary first and
current last. Preparation validates directory types without inferring ancestry or
granting parent access. The optional path describes the initial chain and confers
no authority. URI prefixes resolve through the caller's live environment; relative
paths use the retained cwd chain. Libpyxis retains its own cwd context.

The kernel separately installs an owned `thread_fs` grant with
`THREAD_RIGHT_SET_FS` only. It applies to the caller, is distinct from selected
grants, and adds no thread-management authority. Libc closes it after once-only
FS bootstrap; the read-only field then becomes stale. Native entries may ignore
it. Early memory lookup uses allocation-free environment helpers before TLS and
heap initialization.

[Startup preparation](../../include/kernel/user/startup.h) accepts stable borrowed
kernel inputs under exclusive ownership of an inactive process on the BSP. It
validates names, grants, cwd and size, seeds the environment with owned CAP/TEXT
cells, and copies startup mappings before publishing the address. Initial named
source handles are closed once after seeding unless they also own cwd positions;
there is no hidden original named-handle copy. Failure keeps the process
unpublished for cleanup. Inputs may be released after preparation.

The combined startup admission budget is 64 KiB including mapping padding and
selected environment entry metadata/names/text, even though values reside in the
native table. Only the actual startup mapping pages are allocated. The shared
assembly entry calls native C startup, which validates the record and initializes
[accessors](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/startup.h)
before `main(argc, argv)`; its return value goes to exit. Boot preparation stages
explicit terminal, memory, launcher, display, clock, keyboard, directory and text
entries, plus its cwd chain and arguments.

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
not return. `SYSCALL_START_FAILED` also never returns for a valid packed
startup reason; it uses the same process retirement path. Unknown syscall
numbers retain the all-ones status, named `CALL_UNKNOWN_SYSTEM_CALL`, rather
than a protocol-operation error. Kernel and userspace are rebuilt together against the shared headers;
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
| Console size | Console READ or WRITE | Unused | Columns, rows, generation |
| File read at offset | `FILE_RIGHT_READ` | Byte offset, capacity | Byte count, copied bytes |
| File size | File READ or WRITE | None | File byte size |
| File write at offset | `FILE_RIGHT_WRITE` | Byte offset, length, copied bytes | Bytes written |
| File resize | `FILE_RIGHT_WRITE` | New byte size | None |
| File sync | `FILE_RIGHT_WRITE` | None | None |

Send the complete 40-byte `console_message`, including unused union storage;
the wrappers initialize it to zero. FILE uses exact operation extents after the
16-byte tag: READ has 16 payload bytes, WRITE has 16 plus its inline byte count,
RESIZE has eight, and SIZE/SYNC have none. FILE request and reply payloads are
bounded to 4 KiB, excluding the tag. READ capacity is at most 4,088 bytes and
WRITE length at most 4,080; larger native payloads are rejected. Libpyxis caps
larger application requests to one transfer and returns its actual short count.
READ requires reply capacity for its eight-byte count plus the requested bytes,
but copies only the returned bytes. Its successful reply is exactly eight bytes
plus that count. Console SIZE needs a 24-byte reply; the other count/size replies
contain one eight-byte field. `RDX` reports reply bytes, including FILE READ data;
the transferred data count is a field in the reply, not `RDX`. RESIZE and SYNC
return zero reply bytes and ignore the reply buffer. Errors return no reply bytes.
Close removes a valid handle from the caller's table and releases its reference.
It requires no access rights on that handle. Invalid and already-closed handles
return the invalid-handle status; both success and failure return zero in RDX.
[Local handle copying](../userland/paths.md#local-handle-copies) retains another reference
with the same or reduced rights and the source's restrictions, without changing
the source. Ordinary COPY, IPC attachment and launch preserve existing revocation
lineage; explicit [revocable copies](forwarders.md) add lineage and a separate
CUT controller.
Releasing the last logical controller grant cuts a revocable copy and its
children automatically. Service slots retain their last target after all
controllers close; takeover requires an explicitly retained controller copy.
`handle_query()` returns kernel-authenticated resource/transport masks, protocol,
native/exported kind and restrictions, including for zero-authority grants. It
exposes no object identity, allocates nothing and changes no ownership.
`SYSCALL_HANDLE_INFO` takes a handle, a `struct handle_info` output address and
that buffer's capacity in RDI/RSI/RDX; RAX returns status and RDX is 40 on
success, zero on failure. A capacity smaller than the structure fails
BUFFER_TOO_SMALL before anything is written. Syscall number 5, the earlier query
without a capacity, stays unassigned, so a program built for it gets the
unknown-call status rather than an overrun. DESCRIBE and FORWARDER_CREATE take
their reply capacity in RDX the same way
([syscall.h](../../include/abi/syscall.h)); how a reply grows is in
[ABI](abi.md#keeping-a-releases-abi). Forwarded
holders expose the target's attested interface and attenuated masks. Querying a cut grant or its
descendants returns DENIED; querying a live export whose receiver has closed
returns ENDPOINT_CLOSED. A successful query does not reserve a future operation.
The `handle_rights()` convenience helper selects the two authority masks from
this query. FILE helpers use the interface/kind to choose native calls or exported
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

### Handle restrictions

A grant's restrictions limit what its holder may observe, independently of its
rights. `HANDLE_RESTRICT_DETAIL` makes DESCRIBE return an empty free-text detail
and set `DESCRIBE_DETAIL_WITHHELD`; outcome, flags, length, media type and the
error body are unchanged. The property is set for every restricted description,
so it does not reveal whether a detail exists. Only the kernel adds a
restriction, today only [pipe handoff](pipes.md#value-segments), and nothing
removes one. Rights narrowing never drops it: COPY (same or reduced masks), IPC
attachment, launch grants and environment capabilities, command-list entries,
capture handoff and take, and forwarder creation all keep the source's
restrictions. DESCRIBE through a forwarder also applies the restrictions of
every target grant on its current chain, so retargeting a slot onto a
restricted value withholds detail even though the slot holder's own grant, which
`handle_query()` reports, is unrestricted. A new kernel object's first grant is
unrestricted; restrictions belong to grants, so other holders of the same object
still see its detail.

## Kernel ownership and lifetime

A process owns its address space, capability table, process observation and
execution-group membership independently of any one user task. Each task owns
its execution storage and borrows that process. Preparation admits exactly one
prepared/submitted initial task, including blocked tasks. Native sibling tasks
are created later in the same address space through the `threads` resource
([threads](threads.md), tasks 5 and 6); process retirement waits for the
whole task set.

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
   address space before final cleanup. The BSP detaches the task stop target
   and reclaims task execution storage. Process finalization then releases
   capabilities and process-owned memory, publishes the process result, and
   completes group membership. Deferred object cleanup can keep the group
   pending after process observation completes. See [SMP ownership](../kernel/smp.md#scheduling-and-ownership).

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

After submission, admitted user operations and BSP services share the table
through a short IF=0 guard. Source-handle grant capture owns logical authority
and rechecks generation/masks after prospective retain; it never keeps an entry
pointer. Growth allocates on BSP outside exclusion and copies/publishes current
entries, including slot claims, under it. Old backing frees after unlock.

Grant-returning operations reserve exact destination indices/generations before
commit or sleeping. Claims are invisible to lookup and unavailable to another
installer; they own capacity, not object authority. Native creators preflight
owned grants, and endpoint/launch captures own transferred grants. Validated
ownership moves into every claimed destination entry together, without another
retain or failing allocation. Abort releases all unpublished claims; successful
publication releases unused claims. Launch observers remain private through
preparation and publish together after successful task submission.

Requests carry owned inputs/claims instead of an exclusive table loan. Their
admitted caller and uninterruptible completion keep destination storage alive;
stop unwinds references and claims before retirement. Final table teardown still
requires all activity drained. VM leases and scoped device admission are
implemented, and native siblings enter through the
[thread ABI](../kernel/userspace.md#entry-and-loading). The kernel result enum remains separate from
public syscall statuses.

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

Each call stages at most 256 source bytes on the syscall stack and applies them
to the TTY's cells under its output lock, then writes the count reply last. This
limits each batch; validation still covers the full requested source range. The
presenter draws the cells later
([kernel terminal rendering](../kernel/display.md#kernel-terminal-rendering)).
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

SIZE requires either READ or WRITE and returns current character columns/rows
and geometry generation atomically in a 24-byte `console_size_reply`, excluding
the session tab bar. Generation starts at one; a committed local screen resize
updates every space's TTY, including inactive spaces. Independent remote
terminal dimensions remain fixed with generation one. There is no resize event
or idle wakeup yet. The fixed-size console request payload is ignored for SIZE.
Libpyxis `console_size()` returns the whole reply, including generation;
libterm's `term_size()` uses it as a dimensions-only helper. Both preserve native
call statuses. See [local resize behavior](graphics.md#live-destination-geometry).

The [shared syscall header](../../include/abi/syscall.h) and
[console layouts](../../include/abi/console.h) define the active slice. RDX carries
reply length for CALL; the diagnostic log call preserves its previous value.
No endpoint queues or general object-operation table are involved.

The native [handle wrapper](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/handle.h)
accounts for the CLOSE result; the one-argument syscall wrapper cannot be used for
close because it assumes RDX is preserved. Closing a looked-up grant releases that
local ownership without removing its environment publication. Exit releases the
process table and remaining handles, while the space retains its own console
reference.

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
RESIZE returns no reply bytes. Growth exposes zero bytes; shrink discards the
tail permanently.

SYNC returns no reply bytes. RAM files accept it as a no-op; host files request
full backing-storage synchronization. Parent directories require separate sync
requests, and neither close nor `fflush` implicitly syncs storage. See the
[host synchronization contract](../devices/virtio-fs.md#synchronization) for ordering and
durability limits.

A RAM file keeps one physical frame per 4 KiB page, indexed by a heap array that
grows geometrically. Unwritten pages are holes that read as zeros:
- **Growth.** RESIZE growth and the gap before a write past EOF allocate nothing,
  so RESIZE does not reserve memory. A later WRITE inside the size can fail with
  NO_MEMORY, still leaving the file unchanged.
- **Shrinking.** It frees whole pages past the new size and zeroes the rest of
  the last page. Resize to zero also frees the index.
- **Release.** Last-reference retirement frees every frame and the index.

There is no protocol file-size ceiling beyond checked arithmetic and allocator
limits.

A short per-file spinlock protects operation ownership and a FIFO of waiters.
The owner alone accesses the size, the page index and the frames, for the whole
operation; no spinlock spans the operation. Other operations sleep and receive
ownership directly in FIFO order; the previous owner detaches the resource
waiter before waking it.

**Allocation and copying.** These run on the calling CPU with interrupts
disabled, with no BSP request:
- A WRITE first extends the index and gives each page in its range a zeroed
  frame, freeing any frames it added if one allocation fails.
- Only then does it copy the payload, page by page, through the CPU's own
  scratch slot.
- READ copies the same way. A FILE payload is below one page, so one call
  touches at most two pages.
- Readers cannot observe an intermediate state.

**Executables.** BSP copies selected RAM file bytes into reclaimable whole pages,
under the same 128 MiB per-image ceiling as host and native files. Boot-archive
executables are read in place. See [image capture](../kernel/program-loading.md#file-capture-and-costs).

Request metadata and write payloads are captured before waiting or committing,
including overlapping buffers. Read output is validated and copied under a short
mapping lease after acquiring file ownership; no lease crosses that wait. Late
output failure is BAD_BUFFER and does not undo committed effects. A process has one
initial user task; siblings use the same operations, and the
[native threads](threads.md) refuse unsupported concurrency with BUSY
before effects.

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
unmapped and insufficiently permitted ranges. It never uses scratch slots,
VM range metadata, allocation, or an address-space switch.

Validation and copying hold a short process mapping lease. Copy helpers never
allocate or schedule; a mapping lease must end before any indefinite wait or
root departure. Inputs are captured into kernel-owned staging before blocking
and remain untrusted. User contents can change independently of mapping stability.
A precheck does not pin a range across a wait: revalidate final outputs under a
new short lease. Late failure returns BAD_BUFFER without rolling back consumed
input, rendered/transmitted bytes or committed file/device/launch effects.

Paired outputs validate both ranges together, then write data followed by reply.
An invalid range writes neither. Returned handles publish under the output lease
where possible; otherwise failed output closes only that operation's exact
returned generations, preserving reused slots and existing logical CLOSE effects.
Kernel buffer corruption is not recoverable. See the
[user-memory interface](../../include/kernel/user_memory.h) and
[VM transaction](../kernel/memory.md#execution). Character-only syscalls do not
pass user pointers and retain their existing behavior.

## Implemented process completion

A process-control capability exposes WAIT through a tagged native CALL. Its
[protocol](../../include/abi/process.h) has two rights, WAIT and TERMINATE.
TERMINATE also authorizes the separate cooperative INTERRUPT request; all
three operations use a message header without payload.
The [libpyxis wrapper](https://git.internal/PyxisOS/pyxis-userland/src/branch/main/include/process.h) preserves native errors and
returns EXITED with a signed exit status, START_FAILED with a packed startup
reason, FAULTED, or TERMINATED after an execution-group stop or a process
TERMINATE request. Fault and termination results carry zero exit_status; architecture fault
diagnostics remain in the kernel log. Group completion additionally observes deferred
cleanup; see [execution groups](execution-groups.md).


Runtime failures before `main` explicitly use `SYSCALL_START_FAILED`, including
invalid startup records, TLS preparation/binding, uncertain resource retention
and standard-stream setup. Unknown syscall results during optional resource
queries are not legitimate absence and report the resource-retention step.
Its positive 32-bit reason packs the startup step
in bits 16..30 and a failing native status in bits 0..15; `0xffff` preserves the
unknown-syscall status. The [header](../../include/abi/process.h) defines valid
steps and statuses. Invalid reasons return BAD_REQUEST without terminating.
The caller needs no stderr, heap, TLS, handle or extra authority to end itself.

The first committed process result still wins, siblings stop and WAIT becomes
ready only after normal retirement. The shell prints one line naming the
command, step and reason; command-completion observers receive `launch_failed`.
Direct native job APIs preserve the process kind and packed reason. A Remote
parent reports its own shell startup failure before the final frame; that frame
also retains the native kind and reason. Ordinary `_Exit(1)` remains EXITED/1,
not a guessed startup failure. Boot-init observes space-root failures on the
Caelum tab; installer-init and otherwise-unobserved bundle job clients print the
same cause. Foreground result owners suppress duplicate reaper reporting.
Old static binaries cannot gain this report by
updating only the kernel; package/launch ABI checks reject known mismatches.

WAIT blocks until completion. The result is immutable and is not consumed:
multiple observers and repeated waits receive the same result, including waits
started after completion. Rights and writable reply storage are checked before
sleeping. A short reply buffer or extra request payload is rejected. Only the
resumed observer accesses its own user reply mapping.

`wait_many()` accepts exactly WAIT_COMPLETE for a WAIT-authorized process observer,
alone or mixed with execution-group, terminal attachment and TCP interests. The
event reports the same immutable completion boundary as PROCESS_WAIT; it does not
consume the result or report program success. Retrieve the result with
`process_wait()` after readiness. Process-only waits require no network device.
The existing whole-list authority validation, current-readiness-before-timeout
ordering, polling and cancellation rules apply unchanged.

The execution owner retains one reference to the control object. After exit or
an ordinary fatal user fault, the scheduler leaves the private root and task
stack before handing the task to the BSP. The BSP releases the process address
space, allocation records, capability table, kernel stack and task metadata
before publishing completion and dropping that reference. Capability object
retirement still follows its normal deferred destruction path.

The control object borrows a process link until retirement clears it before
freeing execution storage. Retaining a handle therefore preserves completion
state, not execution memory. Closing the last observer before
exit leaves execution alone. Unsubmitted preparation failures release their
control object without a result; launch must not expose a live observer until
preparation succeeds. The launcher below returns this same completion
capability, with both WAIT and TERMINATE, to the launching process.

### Cooperative process interruption

`PROCESS_INTERRUPT` requires existing TERMINATE authority on a process-control
handle. It queues a cooperative event without stopping execution or changing
completion. It returns NOT_FOUND when the process has not opted in and
ENDPOINT_CLOSED after stopping or a terminal result. PROCESS_TERMINATE and
execution-group stop keep their forceful behavior.

`SYSCALL_PROCESS_INTERRUPT_CREATE` needs no grant and creates a receiver only for
the calling process. It takes reply address/capacity in RDI/RSI and returns one
RECEIVE handle; a second active receiver returns BUSY. Opt-in publishes only
after successful reply copying, and failed creation detaches its receiver even
if a sibling copied the provisional handle.

Only the creating process can TAKE or observe the receiver, including through
copies and forwarders. TAKE is a header-only native call with no reply: OK
consumes the pending event and WOULD_BLOCK means none. Repeated interrupt
requests coalesce until TAKE. WAIT_READABLE observes pending state through
`wait_many` or a [readiness set](readiness-sets.md) without consuming it.

Closing the last RECEIVE grant synchronously detaches and discards pending
state. An admitted call or readiness registration keeps only storage alive;
TAKE then returns ENDPOINT_CLOSED and observation reports WAIT_ERROR. A new
receiver starts empty. The receiver retains the independent process control,
whose weak process link is cleared before execution storage is reclaimed.

### Forced termination

TERMINATE requests that the observed process stop and returns without waiting
or a reply payload. It uses the same per-task safe stop as execution-group
termination, including blocked-operation unwind, whether or not the process
belongs to a group. It is idempotent and also succeeds after completion: an exit
or fault already committed keeps its result, otherwise the result is TERMINATED.
Only that process stops; processes it launched are unaffected, and its group's
admission is not sealed. A handle restricted to WAIT is denied. Today no
process forwards an observer. Any future transfer of an observer handle carries
TERMINATE unless the rights are reduced to WAIT at transfer.

For TERMINATE, the control object borrows a process link. Launch preparation sets it
after installing the observer, and the BSP clears it under the completion lock
before discarding an unpublished preparation or retiring the process.
Retirement follows the reaping of its last task: the scheduler queues the
process for the BSP process reaper, which publishes completion only after
releasing the process. A TERMINATE in between finds no process and does
nothing; WAIT keeps waiting. A request holds the completion lock while it calls the stop request,
so the process memory stays live; this follows the existing
completion-then-queues lock order. Libpyxis
`process_terminate()` wraps the request.

Wait records live in permanent task metadata, never remote task stacks. The
completion lock serializes registration and publication, with the lock order
completion then scheduler queues. Wake-before-sleep is remembered. Publication
detaches each waiter before waking it, and no lock spans a context switch.
These rules depend on the existing one-task-per-process contract. Execution-group
stop resumes the observer's syscall continuation to detach an outstanding wait
before teardown. Readiness waits retain the control object only for their call;
their worker releases every interest before completing a stopped observer.

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

The [88-byte request](../../include/abi/launcher.h) supplies a READ file handle for
the P1F image, source grants with resource/transport masks, exact named CAP/TEXT
entries, arguments and cwd. A CAP entry's value is a grant-list index and its
length is zero. A TEXT entry's value is a caller string address and its byte
length excludes NUL. Names form one unique table; flags select EXPORTED or an
explicitly staged private value. Source grants preserve or reduce each authority
mask. The image is not delegated unless selected explicitly.

Each named capability owns its logical grant in the child's environment. Cwd
indices select independently owned directory handles. Repeated indices can name
the same selected source; separate list entries can copy the same object. Named
source handles are retired after environment seeding, except handles retained by
the cwd chain. There is no implicit environment, directory, service or launcher
inheritance. Ordinary capture selects exported entries; trusted handoff stages
selected private controllers without broadening its native resource allowlist.

Standard streams are ordinary named entries. Protocol and authority remain kernel-attested; libc validates supported
stream facets when adopting them. Child redirection and pipeline selection modify
staging only. A supplied capability/text name overrides that child's selection
without temporarily mutating the parent's table. There are no protocol-tagged
stream slots or namespace/root selectors in the request.

All nested addresses belong to the caller. Arrays, strings and alignment share
a 64 KiB capture budget. Startup admission separately charges its 64 KiB budget,
including selected environment names/text and mapping padding. Counts and address
arithmetic are checked before copying; exact names, duplicate entries, CAP
indices, TEXT lengths and directory types are validated before submission.
Empty arrays are ignored; working_path is optional and requires a nonempty cwd
chain. `argv` contains `argc` string addresses; the kernel adds the final NULL.
Missing source authority reports BAD_HANDLE or DENIED; malformed metadata reports
BAD_REQUEST. No absent stream or omitted entry gains ambient authority.

The caller obtains launch metadata storage from BSP, then captures metadata on
its own CPU/root. Source grants gain prospective logical ownership and an exact
generation/authority recheck before image waits; captured authority survives
source CLOSE. No BSP source-table loan or later handle re-resolution remains.
For RAM images it acquires the file's operation ownership, then
lends the file to BSP for a selected-image copy; other reads/writes/resizes queue
until the copy finishes and BSP ends that operation exactly once. Boot-archive
images retain their immutable bytes without a new copy. No spinlock is held
during loading; later RAM file changes cannot alter captured or child image bytes.

A caller still needs its existing LAUNCH-authorized launcher capability. A READ
file handle is sufficient as the source for a host-backed binary or interpreter;
the launcher creates the process in the caller's space on its assigned CPU and
applies the caller's explicit grants. This uses the existing launcher request,
with no new syscall, rights or image-format version. The host executable file
limit is 128 MiB and reports CALL_LIMIT; it is not a process runtime memory
limit. The host worker reads into owned BSP-backed whole pages, then the existing
BSP loader validates and maps that image. It accepts positive short reads. An empty
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

Native filesystem and RAM binaries and interpreters use the same 128 MiB
selected-image limit (`LAUNCH_CAPTURED_IMAGE_MAX_SIZE`). The native worker reads
length and bytes through the held policy view, requiring the caller's READ grant and both
core read/metadata rights. It publishes a complete owned copy only on success;
errors or an unexpected short read publish no staging bytes. The immutable view
fixes identity and generation, provided the attached image is not modified by
an external writer. The native request reservation is released before launch
preparation uses the BSP executor. Staging remains owned by launch capture and
is freed on every success, failure or stop path, outside the native adapter's
wrapper budget. RAM copying also runs on BSP. Allocation failures return
CALL_NO_MEMORY and unwind any partially backed prefix and its virtual reservation.
Release after loading returns all capture data frames; shared kernel page-table
ancestors retain their ordinary lifetime. Only BSP accesses capture buffers;
APs transfer ownership descriptors without reading them. This adds no aggregate
staging cap across callers; physical-memory exhaustion still rejects.

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
resource accounting. Later space-level service discovery remains separate from
the process environment and current transport.

Only an explicitly selected worklist task is an implementation assignment.
The later ideas here call for no placeholder APIs or object-manager framework.

## Execution-group launch containment

[Execution groups](execution-groups.md) add separately authorized creation, bound
launchers, inherited membership and sealed admission. The ordinary local launcher
remains caller-scoped. Membership survives observer/launcher closure; supervision
closure seals admission and requests safe termination of remaining members. Group
completion additionally waits for attributed deferred cleanup. The
[ownership matrix](execution-group-termination.md) records the stopping and
reclamation boundaries.
