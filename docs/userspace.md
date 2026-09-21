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

`make initrd` packages the initial program into `build/initrd.cpio`; `make image`
includes that uncompressed `newc` archive as the sole Limine module. The kernel
keeps the archive mapped read-only and uses `initrd_lookup()` to find the program
by its exact archive name. See [the initrd interface](../include/kernel/initrd.h)
for the supported records and borrowed-view lifetime. The archive provides raw
file bytes, without extraction, a block device or VFS semantics.

The image loader creates an inactive private address space, copies the program
into owned backing and applies its permissions. Failure releases partial
allocations; loading does not change the caller's active address space.

The kernel supplies an executable entry point and a writable, 16-byte-aligned
user stack. Entry receives a pointer in `RDI` to the read-only
[startup record](../include/abi/startup.h), which remains mapped until process
exit. Its address is chosen by VM allocation; programs must use the pointer.
The assembly entry preserves it as the argument to
`main(const struct startup_info *startup)`, then passes main's return value to
`exit`. Hello checks the record's version and size, then prints using its output
capability. Content is still `HANDLE_INVALID`. There is no libc or argument
vector. The [syscall header](../userspace/include/syscall.h) defines syscall
numbers and the register convention. Normal output targets the owning space's
TTY; diagnostic output targets the kernel log, which goes to the Caelum TTY and
serial.

The [console wrapper](../userspace/include/console.h) uses the native CALL ABI
and reports actual bytes written. Its print helper handles partial progress;
each kernel call renders a bounded chunk. Requests and replies have shared
layouts, and CALL returns both status and reply byte count. Legacy character
helpers and syscalls remain available during migration.

The launcher creates a process that owns the loaded address space and belongs
to the target CPU's space. Before submission, it calls
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
a long syscall delays scheduling there. Separate BSP kernel tasks run with
interrupts enabled and can be preempted. Other CPUs continue running. SYSCALL
needs an explicit kernel-stack switch; unlike an interrupt from userspace, it
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
