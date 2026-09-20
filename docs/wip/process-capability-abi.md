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

- [ ] **4. Implement safe user-buffer access.** Add the range, overflow and
  permission checks and copying needed by syscall requests and replies.
  Invalid buffers return recoverable errors. Complete when the access path
  works on the process's executing CPU without moving allocator ownership or
  allowing concurrent mutation of an active address space.

- [ ] **5. Deliver the startup record.** Pass the agreed versioned record to
  the userspace entry point and expose it to the freestanding program. Support
  the output role first and fill it when the console capability lands in task
  6; until then use the agreed invalid-handle value. The content role remains
  absent until task 8. Complete when the entry convention, record bounds and
  lifetime match task 1, with the existing hello path still usable.

- [ ] **6. Add capability-based console output.** Implement the console
  object's `WRITE` operation through `call`, provide a small userspace wrapper,
  and grant the output capability at launch. Convert hello to use that handle.
  Complete when output reaches the owning space's TTY and invalid handles,
  missing rights and invalid buffers are handled through the agreed errors.

- [ ] **7. Expose handle close.** Add the userspace `close` operation and its
  wrapper. Complete when closing the output handle removes that process's
  access while the space retains its console, stale handles follow the agreed
  reuse rules, and exit releases handles left open.

- [ ] **8. Add the immutable blob object.** Wrap an existing boot-archive file,
  expose its size and offset reads through `call`, and grant its `READ`
  capability through the content startup role. Complete when reads return the
  expected bytes and EOF behavior, and releasing process references leaves
  the archive's shared backing and mapping intact.

- [ ] **9. Complete the userspace example.** Package a small text asset with
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

Task 4 is next: safe user-buffer access on the executing CPU. The kernel
capability results are not syscall status values; map them explicitly when
userspace operations land. Startup-record delivery remains task 5.
