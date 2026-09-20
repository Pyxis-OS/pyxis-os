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

- [ ] **1. Agree on the first ABI slice.** Documentation only. Choose handle
  representation and reuse rules, startup-record layout and entry delivery,
  `call` request/result encoding, initial operations and rights, errors, and
  partial-transfer behavior. Include blob size alongside console write and
  blob offset reads. Record decisions in the process draft; keep later IPC,
  creation and namespace operations open. Complete when the contract needed
  by the following tasks is explicit, without adding placeholder headers.

- [ ] **2. Introduce process ownership.** A process owns its address space and
  belongs to a space. The existing user task refers to the process, initially
  one task per process. Preserve the current hello program and syscalls.
  Complete when launch, normal exit and fatal-user-fault cleanup follow the new
  ownership model while allocation and final reclamation remain on the BSP.

- [ ] **3. Add process-local capability tables.** Support installing an object
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

All tasks are pending. Task 1 is the next discussion; the existing process
draft has not frozen a binary ABI. No code implementation is underway.
