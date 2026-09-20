# Processes, capabilities and the first userspace ABI

Status: the first console-and-blob ABI slice below is agreed for implementation.
The broader process and resource model remains a working draft alongside the
[spaces draft](spaces.md). The planned ABI is not yet the running kernel ABI;
the existing hello program and syscalls remain usable during the migration.
The [worklist](wip/process-capability-abi.md) tracks the focused tasks and
handoffs for this first milestone.

## First program

Use one small program to work through the userspace interface and kernel
ownership together: print a greeting, read the contents of a supplied file,
write those contents to the console, then exit.

The launcher would resolve the file through the existing boot-archive reader
and grant a capability to an immutable blob containing its bytes. The program
would receive these two resources:

| Startup role | Object | Rights |
| --- | --- | --- |
| Output | The space's console | `WRITE` |
| Content | An immutable blob backed by a boot-archive file | `READ` |

This example needs no filesystem root, path lookup operation or VFS semantics.
The [filesystem draft](vfs.md) remains separate. The boot archive is a source of
bytes, not a decision about how applications will eventually discover files.

Conceptually, the program would:

1. Read its startup record and obtain the output and content handles.
2. Write a greeting, handling errors and any partial write.
3. Read a chunk at an explicit byte offset into a userspace buffer.
4. Write the returned bytes, advance the offset, and repeat until EOF.
5. Exit with a success or failure status; the kernel releases remaining handles.

An explicit read offset avoids introducing a shared seek position when handles
are later duplicated or passed between processes.

## Objects, capabilities and handles

An object is the underlying resource: a console, blob, memory allocation or
endpoint. A capability grants particular operations on that object. A handle
is an opaque, process-local value naming an entry in the process's capability
table. The kernel entry holds the object reference and rights; userspace cannot
gain rights by changing the handle value.

Different processes may hold different handles and rights to the same object.
The first rights are `READ` (bit 0) and `WRITE` (bit 1), stored in the kernel
table. Other object-specific rights, such as `MAP` or `LOOKUP`, and permission
to duplicate or transfer a capability remain later decisions.

For this slice, `handle_t` is an opaque `uint64_t`; zero is invalid. Internally,
the low 32 bits identify a table slot and the high 32 bits hold its generation.
Generations start at one. Closing an entry invalidates its handle; reuse gets
a new generation. Retire a slot rather than wrap its generation and make a
stale handle valid again. Table capacity and growth are task 3 decisions, not
a fixed slot-count limit implied by this encoding. Userspace must not decode
handles or use them as global object identifiers.

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

The entry point receives a pointer in `RDI` to this record in user-readable,
read-only memory, valid until process exit:

```c
struct startup_info {
  uint32_t version;
  uint32_t size;
  handle_t output;
  handle_t content;
};
```

Version 1 has size 24 bytes and alignment 8, with fields at offsets 0, 4, 8
and 16. Fields use the current x86_64 little-endian representation. `size`
bounds the supplied record; userspace checks version and size before reading
the handles. An absent resource has the invalid handle, zero.

These are named roles, not assumptions that handles zero and one always mean
particular resources. The content field is specific to the first example; it
does not settle a general argument or resource-discovery scheme.

Task 5 introduces this entry convention. Until then, the existing program
entry remains unchanged; no startup-record headers are needed in advance.

## First operation shapes

The initial object operations use one synchronous kernel call:

```c
call(handle, operation, request, request_size, reply, reply_capacity)
```

Reserve syscall 2 for `call` and 3 for `close(handle)`. Existing character-output
syscalls 0 and 1 remain during migration; exit keeps its current encoding.
The x86_64 convention uses `RAX` for the syscall number and `RDI`, `RSI`, `RDX`,
`R10`, `R8`, `R9` for arguments. Return `RAX` holds status and `RDX` the number
of reply bytes written (zero on failure, and always zero for close). Exit does
not return. These assignments take effect only as their worklist tasks land.

Request and reply fields below are consecutive `uint64_t` values, including
user addresses, offsets and byte counts. They do not embed C pointers, enums
or `size_t`. Operation numbers are distinct across object types; using an
operation on the wrong object returns an unsupported-operation error.

| Operation | Authority | Request fields, in order | Reply field |
| --- | --- | --- | --- |
| 1: Console write | Output handle with `WRITE` | Source user address, byte length | Bytes written |
| 2: Blob read at offset | Content handle with `READ` | Byte offset, destination user address, capacity | Bytes read |
| 3: Blob size | Content handle with `READ` | Empty | Blob byte size |

Request sizes must match exactly: 16, 24 and 0 bytes respectively. Each reply
needs at least 8 bytes of capacity; success writes one 8-byte field and returns
8 in `RDX`. The transferred data count is in that field, not in `RDX`.
Close removes a valid handle from the caller's table and releases its reference.

For a nonzero read capacity, successful zero-byte reads at or beyond the blob's
end indicate EOF. Reads can return fewer bytes than requested. Writes report
actual progress; a nonempty write cannot succeed with zero progress. A transfer
with zero data length succeeds with a zero count and does not dereference its
data address. Request and reply validation still apply. Partial progress is
success with the actual count; errors report no transferred bytes.

Status values for this slice are 0 for success, 1 for an invalid or stale handle,
2 for denied rights, 3 for an unsupported operation, 4 for a malformed request
or insufficient reply capacity, and 5 for an invalid user buffer. Counts must
stay within the supplied length or capacity. Operations added later can define
additional errors when needed.

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
How reference releases and handle-table changes on an AP reach the BSP reaper
needs to be resolved before implementation; this draft does not relax the
current concurrency restrictions.

## Later operations and open decisions

The broader ABI vocabulary under discussion is `create`, `call`, `send`, `recv`,
`wait` and `close`. This first program does not require endpoint creation or IPC.
Creation must be tied to authority and resource budgets; the ability to request
an object type cannot alone authorize privileged resources.

Endpoints would be objects receiving opaque message bytes and explicitly
attached capabilities. A raw handle value or pointer embedded in those bytes
has no meaning in another process. Syscall request layouts and transported
messages therefore need separate contracts.

Sharing a capability would create an additional reference with equal or reduced
rights. Moving would remove the sender's reference only when the transfer
commits; failure must not lose it. Neither operation automatically transfers
ownership or accounting for the underlying object.

Future IPC work must define synchronous reply association, blocking receive,
queue-full results, waitable conditions and peer-closure wakeups. The namespace
and named-endpoint ideas in the spaces draft do not choose those mechanisms.

Table capacity, growth and release scheduling remain task 3 decisions. The
safe-buffer implementation belongs to task 4. Neither should relax the existing
allocator and VM ownership rules. Convert hello when console calls land; retire
legacy character syscalls separately once their callers have migrated.

Only an explicitly selected worklist task is an implementation assignment.
The later ideas here call for no placeholder APIs or object-manager framework.
