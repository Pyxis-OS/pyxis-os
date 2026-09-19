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

The initial program is built into the boot image as a Limine module. The kernel
loader creates an inactive private address space, copies the image into owned
backing and applies its permissions. Failure releases partial allocations;
loading does not change the caller's active address space.

The kernel supplies an executable entry point and a writable, 16-byte-aligned
user stack. The program's assembly entry calls C `main`, then passes its return
value to `exit`. There is no libc or argument vector. The
[syscall header](../userspace/include/syscall.h) defines syscall numbers and the
register convention. Normal output targets the TTY; diagnostic output targets
the kernel log, which also goes to serial. Both currently share the global TTY.

Task submission transfers sole ownership of the loaded address space on
success; on failure it remains with the caller. Each task also owns a private
kernel-entry stack. Allocation, submission and reclamation stay on the BSP,
while tasks can run on their assigned AP. See the
[task interface](../include/kernel/user.h) for the ownership contract and
[smp.md](smp.md) for CPU selection and cross-CPU handoff rules.

## Switching and cleanup

Each CPU's scheduler runs on a permanent stack separate from task stacks. Before
entering a task, it activates that task's address space and selects its TSS and
syscall-entry stack. First entry uses IRETQ; a preempted task resumes through its
saved interrupt frame. The local APIC timer selects runnable tasks round-robin.

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
an executing stack or active page tables. The BSP then releases task-owned
memory; original boot-module frames remain reserved. Fatal kernel exceptions
still panic, and exit runs no cleanup callbacks or stream flushing.
