# First process and capability ABI: worklist

Working sequence for the console-and-blob milestone described in
[the process draft](../processes.md). Tasks may be taken by either the project
owner or an assistant in separate turns. No task is assigned or started by
this document; implementation begins when that task is explicitly requested.

Keep the task numbers stable. When picking up a task, record who is working on
it and any agreed scope change. On completion, check it off and add the commit
or PR, validation performed, and any remaining decision needed by the next task.
The process draft holds the agreed design; this file tracks the work.

## Tasks

- [x] **1. Agree on the first ABI slice.** Documentation only. Choose handle
  representation and reuse rules, startup-record layout and entry delivery,
  `call` request/result encoding, initial operations and rights, errors, and
  partial-transfer behavior. Include blob size alongside console write and
  blob offset reads. Record decisions in the process draft; keep later IPC,
  creation and namespace operations open. Complete when the contract needed
  by the following tasks is explicit, without adding placeholder headers.

- [x] **2. Introduce process ownership.** A process owns its address space and
  belongs to a space. The existing user task refers to the process, initially
  one task per process. Preserve the current hello program and syscalls.
  Complete when launch, normal exit and fatal-user-fault cleanup follow the new
  ownership model while allocation and final reclamation remain on the BSP.

- [x] **3. Add process-local capability tables.** Support installing an object
  reference with rights, resolving a handle with required rights, and closing
  it. Define the release path when a close originates on an AP, and release all
  remaining references during process cleanup. Complete when these kernel
  operations obey task 1's handle contract. No userspace operations yet.

- [x] **4. Implement safe user-buffer access.** Add the range, overflow and
  permission checks and copying needed by syscall requests and replies.
  Invalid buffers return recoverable errors. Complete when the access path
  works on the process's executing CPU without moving allocator ownership or
  allowing concurrent mutation of an active address space.

- [x] **5. Deliver the startup record.** Pass the agreed versioned record to
  the userspace entry point and expose it to the freestanding program. Support
  the output role first and fill it when the console capability lands in task
  6; until then use the agreed invalid-handle value. The content role remains
  absent until task 8. Complete when the entry convention, record bounds and
  lifetime match task 1, with the existing hello path still usable.

- [x] **6. Add capability-based console output.** Implement the console
  object's `WRITE` operation through `call`, provide a small userspace wrapper,
  and grant the output capability at launch. Convert hello to use that handle.
  Complete when output reaches the owning space's TTY and invalid handles,
  missing rights and invalid buffers are handled through the agreed errors.

- [x] **7. Expose handle close.** Add the userspace `close` operation and its
  wrapper. Complete when closing the output handle removes that process's
  access while the space retains its console, stale handles follow the agreed
  reuse rules, and exit releases handles left open.

- [x] **8. Add the immutable blob object.** Wrap an existing boot-archive file,
  expose its size and offset reads through `call`, and grant its `READ`
  capability through the content startup role. Replace `call_object()`'s
  console-only dispatch and unconditional WRITE requirement with handle lookup
  and a small switch on object type. Use focused console/blob handlers to check
  supported operations, required rights and request layouts, keeping the
  userspace `call` ABI unchanged. Do not introduce a generic dispatch framework.
  Complete when reads return the expected bytes and EOF behavior, and releasing
  process references leaves the archive's shared backing and mapping intact.

- [x] **9. Complete the userspace example.** Package a small text asset with
  the program. Print a greeting, query the supplied blob's size, read it in
  chunks, write its contents, close the handles and exit. Handle errors and
  partial progress in the wrappers. Complete when this runs through the native
  capability ABI and normal process cleanup.

## Scope and validation

The first resources are a space console with `WRITE` and a boot-archive blob
with `READ`. The launcher resolves the archive name. This milestone adds no
filesystem lookup, VFS, general IPC, driver framework or broad libc. Future
libc wrappers can use the native ABI; their familiar names do not prescribe
kernel syscalls or POSIX semantics.

Use focused commits and reviewable PRs. For code changes, use ordinary builds,
QEMU boots and debugger inspection where useful. Record what was actually
observed and what was reviewed only. Do not add tests, fault injection, boot
automation, or temporary demonstration syscalls. Documentation-only changes
need document/link review and a clean diff, not another boot.

## Current handoff

Task 1 is complete: the project owner accepted the proposed first ABI slice.
Commit `e13deb3` records its encodings, buffer and partial-transfer rules in the
process draft. Documentation and links reviewed; these assignments do not change
the running ABI or add placeholder headers.

Task 2 is complete (assistant), commit `666195c`. A process owns its private
address space and borrows its containing space. Submission transfers the process
to one user task and requires the target CPU to host that space. Process creation
and reclamation remain on the BSP; the existing hello and syscalls are unchanged.

Validation: ordinary `make image` completed without warnings; a four-CPU KVM
`make run` boot ran hello on CPU 1 and reported normal exit and reclamation.
A single-CPU KVM `make debug` boot ran hello on the BSP, with output visible in
the framebuffer. GDB inspection confirmed process membership, destruction on
the permanent scheduler stack with the kernel root active and IF=0, and PMM
allocated-frame and heap live-allocation counts returning to pre-launch values.
The fatal-user-fault path still reaches the same completion and reaping path;
it and allocation-failure unwinding were code-reviewed, not fault-injected.

Task 3 is complete (assistant), commits `111b16d` and `7d2c8f7`: growable
process-local slot tables, checked handle lookup and rights, and allocation-free
retirement to the BSP on last object release. Installation stays on the BSP
before process submission; resolution and close use the process's existing
exclusive execution ownership. Process cleanup releases all remaining entries.
No userspace operations or concrete console/blob objects were added.

Validation: ordinary `make image` completed without warnings and a four-CPU KVM
`make run` boot ran hello through normal exit and BSP cleanup. Manual GDB calls
on a four-CPU TCG boot exercised installation, rights denial, invalid and stale
handles, double close, slot reuse with a new generation, growth from eight to
sixteen slots, and preservation of an earlier handle across growth. A last
close on CPU 1 queued an object without heap activity; its destruction callback
ran on the BSP with IF=0 and the retirement lock released. Process cleanup
released nine remaining entries while preserving a separate kernel reference.
After releasing that reference, heap live allocations and allocated physical
frames returned to their pre-launch counts. These were debugger-created object
headers using `kfree` as the destruction target, not console or blob objects.
No test code, fault injection or boot automation was added. Generation/count
overflow, allocation failure, and the busy-BSP preemption condition were
code-reviewed rather than forced at runtime.

Task 4 is complete (assistant), commits `90cea0e` and `51b9cf6`. Current-process
buffer checks walk the active root's recursive mappings with IF=0 on the
executing CPU. Copy helpers validate the whole range before touching either
destination. They allocate nothing and do not use shared scratch mappings or
VM range metadata. Existing syscalls and mapping ownership remain unchanged.

Validation: ordinary `make image` completed without warnings; a normal four-CPU
KVM boot ran hello through exit and BSP cleanup. Manual GDB calls on CPU 1 under
four-CPU TCG verified a read across two mapped pages, a writable-buffer round
trip, read-only write rejection, missing ancestors/leaves, null-page and kernel
addresses, noncanonical and overflowing ranges, and zero-length copies with
otherwise invalid pointers. Copies spanning a mapped page and an unmapped page
returned false with their kernel/user destinations unchanged. Calls before task
submission rejected the inactive root and absent current process. Heap/frame
counts and CR3 were unchanged across AP calls, and hello resumed to normal exit.
Ancestor permission checks and unsupported large entries were code-reviewed;
no page tables were altered to force those cases. No tests, fault injection,
temporary syscalls or boot automation were added.

Task 5 is complete (assistant), commits `43db18f` and `f4715ea`. Kernel and
userspace share the startup and opaque-handle headers. Before submission, the
BSP prepares a record in a separately allocated read-only, non-executable user
page, filled through a temporary kernel alias. Entry delivers its address in
RDI and the freestanding stub preserves it for C main. Hello checks version
and size, then uses the existing character syscall. Both roles remain invalid.

Validation: ordinary `make image` completed without warnings and a normal
four-CPU KVM boot ran hello on CPU 1 through successful exit and BSP cleanup.
GDB inspection under four-CPU TCG confirmed version/size, invalid role handles,
alignment, read-only/NX page permissions, RDI at user entry and main, and zeroed
remaining general registers apart from the user stack pointer. User-buffer
checks accepted reads and rejected writes. Following exit, heap live allocations
and allocated-frame counts returned to their pre-launch values. Failure
unwinding was code-reviewed; no tests, fault injection, temporary syscalls or
boot automation were added.

Task 6 is complete (assistant), commits `2558ac7`, `67f61a5` and `338c6a7`.
Each space owns a console object; the launcher grants its process WRITE access
through the output startup role. CALL checks the handle, rights, operation and
all request/reply/data ranges before output, then stages up to 256 bytes and
reports partial progress. Hello uses the console wrapper. Legacy character
syscalls remain. Status 6 reports unavailable console output without progress.

Validation: ordinary `make image` completed without warnings. Normal one- and
four-CPU KVM boots ran hello through successful exit and cleanup; framebuffer
inspection confirmed its output in CPU 1's tab. Manual GDB calls on CPU 1 under
four-CPU TCG verified invalid/stale handles, missing WRITE rights, unsupported
operations, malformed requests and invalid buffers. Errors left the reply and
TTY cursor unchanged. Zero-length output succeeded without reading its source;
a 300-byte payload reported successive writes of 256 and 44 bytes. Unknown
legacy calls preserved RDX. After hello resumed and exited, heap/frame counts
returned to baseline and the console retained only its space-owned reference.
Allocation failures, unavailable output, overlapping buffers and the userspace
wrapper's long-string retry loop were code-reviewed rather than forced at
runtime. No tests, fault injection or boot automation were added.

Task 7 is complete (assistant), commits `0081560` and `744ca64`. Syscall 3 closes
a handle through the existing capability release path and returns status with
zero reply bytes. No access rights are required to close an owned handle.
The userspace wrapper accounts for RDX being overwritten; hello closes its
output after printing. The space retains its console independently.

Validation: ordinary `make image` completed without warnings. Normal one- and
four-CPU KVM boots ran hello through close, exit 0 and cleanup. GDB inspection under
four-CPU TCG confirmed rejection of absent-process, invalid, stale and already
closed handles with no reference change. Closing a handle with no access rights
on CPU 1 succeeded; reuse of its slot had already given it a new generation.
After hello's actual close returned to userspace, RAX and RDX were both zero.
Subsequent calls through that handle failed while another console grant remained
valid. Exit released that deliberately open grant, leaving the space's console
reference and initialized TTY alive. Heap and frame counts returned to their
pre-launch values. Last-reference retirement is unchanged from task 3 and was
code-reviewed here; no tests, fault injection or boot automation were added.

Task 8 is complete (assistant), commits `f58ca17` and `c9f5863`. CALL looks up
the object and granted rights once, then selects a focused console/blob handler
through a type switch. Handlers check supported operations before rights and
request layouts. Capability resolution gained an optional granted-rights output;
the userspace call convention and existing encodings are unchanged.

The blob borrows an immutable archive view and frees only its wrapper. Size and
offset reads require READ, validate destinations before copying, and report EOF
without overflowing offset arithmetic. The launcher grants hello.pxe itself as
the content role for now; hello leaves that grant for normal exit cleanup.

Validation: ordinary `make image` completed without warnings and normal one-
and four-CPU KVM boots ran hello through exit 0. Manual GDB calls on CPU 1 under
four-CPU TCG verified startup delivery, blob size, matching archive bytes, a
read clipped at EOF, EOF at the exact size and UINT64_MAX, zero capacity with
an invalid destination address, and overlapping request/data/reply storage.
Invalid/stale handles, denied rights, wrong-object operations, malformed sizes,
read-only and unmapped buffers, and overflowing destination ranges returned
the expected errors. Checked destination/reply sentinels survived errors.
The last close on CPU 1 queued the wrapper; its destructor ran on the BSP with
IF=0. Archive data, physical translation and read-only mapping survived wrapper
destruction. After hello exited, heap/frame counts returned to baseline.
Allocation-failure unwinding and empty-file reads were code-reviewed; no tests,
fault injection or boot automation were added.

Task 9 is complete (assistant), commits `fd06fc7` and `ac14c48`. The build
packages a text asset beside hello, and the launcher grants it as content.
Blob size/read wrappers check status, reply size and counts. The console's
byte-count helper completes partial writes. Hello queries size, reads through
a fixed stack buffer until EOF, checks counts against the immutable size,
prints the bytes, attempts both closes even after I/O failure, and exits.

Validation: ordinary `make image` completed without warnings; archive inspection
showed the program and text entries. Normal one- and four-CPU KVM boots exited
with status 0, and framebuffer inspection showed the greeting and full text in
CPU 1's tab. GDB inspection of the actual userspace calls under four-CPU TCG
observed reads of 512 bytes, 173 bytes, then EOF. The first output chunk required
two successful 256-byte writes; the last required 173 bytes. Both closes
returned success with zero reply bytes and cleared their table entries before
exit 0. Heap/frame counts returned to pre-launch values; the space's console
reference and archive bytes survived. Error unwinding, empty content and
malformed-reply rejection were code-reviewed; no fault injection, tests or boot
automation were added.

All tasks in this milestone are complete. The subsequent capability discussion
led to the explicitly requested protocol pass below; this worklist assigns no
further implementation.

Follow-up protocol pass (assistant), commits `f213204`, `1bfbc15` and `5b62610`:
object code and headers now live under `kernel/object/` and
`include/kernel/object/`. Console WRITE and blob READ are separate protocol
rights with overlapping bit values. Installation rejects unsupported bits.
CALL carries a tagged protocol message and payload union; the operation is no
longer a separate syscall argument. Userspace wrappers migrated together.
This changes the native CALL ABI and requires rebuilding old userspace images.
The updated process document describes the current contract; earlier task
records above describe their original implementations.

Validation: a clean `make image` completed without warnings, and normal one-
and four-CPU KVM boots ran hello through exit 0. Manual GDB calls under four-CPU TCG
confirmed unsupported grant-bit rejection, zero-rights grants with denied
operations, mismatched protocol tags despite overlapping operation numbers,
unknown operations, malformed message sizes, invalid tag buffers and invalid
reply buffers. Checked reply/data sentinels survived rejected calls. Valid
blob read, size and EOF messages succeeded. Hello resumed through its full
example and exit; heap/frame counts returned to baseline and the space's
console survived. Payload-boundary checks and unchanged allocation-failure
paths were code-reviewed. No tests, fault injection or boot automation were added.

Follow-up request/reply milestone (assistant), commits `7a64a4d`, `5b39d17`,
`0944042`, `e2a14b6` and `bd6de4c`: explicit attenuated grants between unsubmitted
process tables, blocked-syscall parking, and paired endpoints using tagged
CALL/RECEIVE/REPLY messages. The startup record adds an endpoint role and retains
version 1. A client and server run beside hello; the client requests a numeric transformation and
prints the reply, then the server observes peer closure. Boot program setup
now lives in `kernel/user/`. See [the endpoint contract](../endpoints.md).

Each direction has one outstanding request and bounded inline data. Shared
message and wait records use stable heap mappings, preserving the current
restriction against remote accesses to reclaimable task stacks. No userspace
creation, capability transfer, asynchronous send or timeout is introduced.

Validation: a clean `make image` completed without warnings. Normal one- and
four-CPU KVM boots ran all three programs through exit 0. Manual four-CPU TCG/GDB
inspection confirmed reduced-rights grants, rejected rights escalation,
queue-full and busy results, stale-reply rejection, and denied operations.
Closing the service handle through the syscall dispatcher while its client was
blocked produced ENDPOINT_CLOSED and made that client runnable. Heap and frame
counts returned to their pre-launch values after cleanup. The exact early-wake
race, counter exhaustion and allocation-failure unwinding were code-reviewed;
no tests, self-tests or boot automation were added.

The compatibility-policy review removed the unused TTY character syscall and
its putchar/print wrappers; all current programs use console capabilities.
The separate diagnostic kernel-log syscall remains. AGENTS.md now requires a
concrete reason for compatibility paths and version increases. A clean image
build and normal four-CPU KVM boot passed after the removal, with all three
programs exiting successfully.
