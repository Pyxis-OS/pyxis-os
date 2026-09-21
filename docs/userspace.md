# Userspace

## Building an image

`make userspace` builds the freestanding programs and the hosted `elf2pxe`
converter. Each program has a directory and an explicit target in the
[userspace Makefile](../userspace/Makefile); for example,
`make -C userspace hello` builds just that program. Pass `CROSS_COMPILE` as for
kernel builds and `HOSTCC` for the converter. Outputs live under `build/`.

To convert an already linked executable:

```sh
build/tools/elf2pxe --format p1f -o hello.pxe hello.elf
```

The input must be a fixed-address, little-endian x86_64 ELF executable with no
interpreter, dynamic linking, runtime relocations or TLS. The
[linker script](../userspace/linker.ld) separates segment permissions on page
boundaries; writable executable mappings are rejected. The
[P1F header](../include/pxe/p1f.h) defines the image layout and constraints.
Keep the ELF for debugging; the converted image does not replace its symbols.
See [gdb.md](gdb.md) for kernel debugger usage.

## Entry and loading

`make initrd` packages hello and its text asset, plus the client and server
programs, into `build/initrd.cpio`; `make image`
includes that uncompressed `newc` archive as the sole Limine module. The kernel
keeps the archive mapped read-only and uses `initrd_lookup()` to find the program
and content by exact archive names. The text source lives beside hello in
[message.txt](../userspace/hello/message.txt). See [the initrd interface](../include/kernel/initrd.h)
for the supported records and borrowed-view lifetime. The archive provides raw
file bytes, without extraction, a block device or VFS semantics.

The image loader creates an inactive private address space, copies the program
into owned backing and applies its permissions. Failure releases partial
allocations; loading does not change the caller's active address space.

The kernel supplies an executable entry point and a writable, 16-byte-aligned
user stack. Entry receives a pointer in `RDI` to the read-only
[startup record](../include/abi/startup.h), which remains mapped until process
exit. Its address is chosen by VM allocation; programs must use the pointer.
The startup record supplies output, content and endpoint roles; absent resources
have invalid handles. Programs check the version and size before using them.
The shared assembly entry preserves the pointer as the argument to
`main(const struct startup_info *startup)`, then passes main's return value to
`exit`. Hello checks the record's version and size, prints a greeting, queries
its content blob's size, and reads and prints the content in chunks. It attempts
to close both handles even after an I/O error, then returns success or failure.
The content capability grants READ access to the packaged text asset. There is no
libc or argument vector. The [syscall header](../userspace/include/syscall.h) defines syscall
numbers and the register convention. Normal output targets the owning space's
TTY; diagnostic output targets the kernel log, which goes to the Caelum TTY and
serial.

The [console wrapper](../userspace/include/console.h) uses the native CALL ABI
and reports actual bytes written. Its byte-count and string helpers finish partial writes;
each kernel call renders a bounded chunk. Requests and replies have shared
layouts, and CALL returns both status and reply byte count. All programs use
console capabilities for TTY output.

CALL takes a handle, a tagged message and its size, then a reply buffer and
capacity. Shared protocol headers define the tag and payload union. Rights are
checked against the handle's object type, so the same bit may mean console
WRITE or blob READ. Kernel and userspace are rebuilt together against the
shared ABI headers. Older layouts are not supported.

The [blob wrappers](../userspace/include/blob.h) query size and read at explicit
offsets. They check reply lengths and counts; a short read is allowed and zero
bytes with nonzero capacity means EOF. Hello uses a fixed stack buffer, so it
does not allocate storage proportional to the blob size or require NUL-terminated
content. It checks that EOF and transferred counts agree with the queried size.

The [handle wrapper](../userspace/include/handle.h) releases the calling process's
reference. A closed handle is immediately stale; other owners, including the
space that owns the console, retain their references. Process exit releases
handles left open. The read-only startup record is not updated after close.

The client and server use the [endpoint wrappers](../userspace/include/endpoint.h)
to exchange a structured request with an attached capability. The client receives
a content blob at startup and copies a READ grant to the server. The server
reads and prints its contents, closes its received handle, then replies with
the byte count. The client prints the result and closes its own content grant,
which remained valid. Closing its endpoint then lets the server exit.
Both have output handles; the server has no startup content grant. Their
endpoint rights are CALL for the client and RECEIVE | REPLY for the server.
The [endpoint contract](endpoints.md) describes ownership, growth and errors.

The [boot launcher](../kernel/user/launch.c) creates a process that owns the
loaded address space and belongs to the target CPU's space. Before submission, it calls
`process_prepare_startup()` with the initial resource handles. This allocates
a zeroed user page and fills the record through a temporary kernel alias,
leaving the user mapping read-only and non-executable. Task submission transfers
sole ownership of this process on success; on failure it remains with the caller
for cleanup. There is initially one user task per process. Each task also owns
a private kernel-entry stack. Allocation, submission and reclamation stay on the BSP,
while tasks can run on their assigned AP. See the
[task interface](../include/kernel/user.h) for the ownership contract and
[smp.md](smp.md) for CPU selection and cross-CPU handoff rules.

## Switching and cleanup

Each CPU's scheduler runs on a permanent stack separate from task stacks. Before
entering a user task, it activates the process's address space and selects its
TSS and syscall-entry stack. First entry uses IRETQ; a preempted task resumes
through its saved interrupt frame. The local APIC timer selects runnable tasks
round-robin.

Userspace runs with interrupts enabled. Interrupt gates and SYSCALL disable
them on kernel entry, so syscall execution is not preempted on its own CPU and
a long syscall delays scheduling there. Endpoint CALL and RECEIVE explicitly
park the task while waiting; the scheduler resumes its private kernel stack and
address space before the handler accesses user memory again. Separate BSP
kernel tasks run with interrupts enabled and can be preempted. Other CPUs
continue running. SYSCALL needs an explicit kernel-stack switch; unlike an interrupt from userspace, it
does not load the stack from the TSS. Kernel GS identifies the current CPU,
with SWAPGS separating it from the user GS base on entry and return.

Task switches preserve general registers, x87/SSE state, segment selectors and
FS/GS bases. AVX and user FS/GS-base instructions are disabled; userspace must
stay within that supported feature set. Kernel code must not use FP/SIMD.

Exit and ordinary fatal user exceptions abandon the task's entry stack and
resume its scheduler. Only after switching to the permanent stack and kernel
root can the CPU return ownership to the BSP for cleanup. This prevents freeing
an executing stack or active page tables. The BSP then destroys the process
and its private address space, followed by the task's kernel stack and metadata.
The owning space and its TTY survive; original boot-module frames remain
reserved. Fatal kernel exceptions still panic, and exit runs no cleanup
callbacks or stream flushing.
