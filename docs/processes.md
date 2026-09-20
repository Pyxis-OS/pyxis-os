# Processes, capabilities and the first userspace ABI

Status: working draft for discussion, not an approved specification or an
implementation plan. Names, layouts and policies remain provisional. This
develops the process and resource model alongside the [spaces draft](spaces.md).

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
Rights can be object-specific, such as `READ`, `WRITE`, `MAP` or `LOOKUP`.
Permission to duplicate or transfer a capability may need separate rights;
their representation and numeric assignments are not chosen here.

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

Propose a small startup record in user-readable memory, containing:

| Field | Meaning |
| --- | --- |
| Version | Identifies the startup-record contract |
| Byte size | Bounds the supplied record |
| Output handle | Capability to the process's initial output resource |
| Content handle | Capability to the blob used by this example |

These are named roles, not assumptions that handles zero and one always mean
particular resources. The content field is specific to the first example; it
does not settle a general argument or resource-discovery scheme.

Exact field widths, alignment, invalid-handle representation, record lifetime
and delivery to the entry point still need agreement. Passing a user pointer
in an entry register is a candidate, not an assigned register convention.
The existing program entry and syscall ABI remain unchanged by this draft.

## First operation shapes

These describe logical inputs and results, not C structure layouts or syscall
numbers. Userspace wrappers could present `write`, `read_at`, `close` and `exit`.

| Operation | Authority | Request | Result |
| --- | --- | --- | --- |
| Console write | Output handle with `WRITE` | Source user address, byte length | Status, bytes written |
| Blob read at offset | Content handle with `READ` | Byte offset, destination user address, capacity | Status, bytes read |
| Close | A valid handle in the calling process | Handle | Status |
| Exit | The calling process | Exit status | Does not return |

For a nonzero read capacity, successful zero-byte reads at or beyond the blob's
end indicate EOF. Reads can return fewer bytes than requested. Writes report
actual progress; a wrapper must handle partial writes and avoid retrying forever
if a nonempty write succeeds with no progress. Zero-length requests transfer
zero bytes. Exact rules for errors after partial progress remain to be settled.

Failures need to distinguish invalid handles, insufficient rights, unsupported
operations and invalid user buffers. Counts must stay within the supplied
length or capacity. Error encodings and limits are still open.

A small `call` entry accepting a handle, operation and request/reply buffers
could carry the object operations above. Whether to use that form or dedicated
syscalls is undecided. A kernel operation on a passive object does not itself
require a message queue or a userspace server.

User addresses refer to the calling process. The kernel must check range
overflow, user accessibility and read/write permissions, and copy through a
defined user-memory access path. Invalid buffers must produce an error rather
than a fatal kernel exception. Request metadata must be captured before use;
reply buffers must also be validated. The design must respect the kernel's
current BSP-only VM operations and allocation/reclamation constraints when a
syscall runs on an AP.

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

Before implementing the first example, settle the startup record and syscall
encoding, handle reuse/stale-handle behavior, buffer-access rules, partial-result
semantics, and the ownership path across process exit and BSP cleanup. Table
capacity, growth and failure behavior also remain open.

This document authorizes no implementation, placeholder APIs, object-manager
framework or changes to the scheduler, memory subsystem or existing ABI.
