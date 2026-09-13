# Building userspace images

`make tools` builds the hosted `elf2pxe` converter (`HOSTCC`, default `cc`).
`make userspace` builds all userspace programs; `make -C userspace hello` builds
only `hello`. Pass `CROSS_COMPILE` as for the kernel. Outputs live under
`build/tools` and `build/userspace`, so the root `make clean` removes them too.

Each program has its own directory and an explicit target in
`userspace/Makefile`. The initial program starts at a small assembly `_start`
which calls `int main(void)`. The kernel supplies a 16-byte-aligned writable
user stack with interrupts enabled; the call places a return address on that
stack as required by the C ABI. Returning from `main` passes its result to
`exit`, syscall 1. The status is a signed 32-bit value, carried in the low
32 bits of RDI. There is no libc or argument vector.

`hello/main.c` prints `Hello from C!` and a newline using the unbuffered helpers in
`userspace/lib/io.c`. `putchar` invokes syscall 0 for one byte, and `print`
walks a NUL-terminated string without adding a newline. The small x86_64
`syscall1` wrapper declares RAX as the number/result, RDI as the argument,
and RCX, R11, flags and memory as clobbered. These are local userspace helpers,
not a libc implementation. The program and helpers use the same freestanding
cross-compiler flags, and link without host libraries or startup objects.

The converter accepts fixed-address, little-endian x86_64 ELF executables:

```sh
build/tools/elf2pxe --format p1f -o hello.pxe hello.elf
```

It converts `PT_LOAD` segments, omitting empty ones. The input must already be
fully linked: no interpreter, dynamic linking, relocations requiring runtime
processing, or TLS. Keep the original `.elf` for symbols; for example, after
connecting GDB to the kernel, `add-symbol-file build/userspace/hello.elf` adds
the program's fixed-address symbols.

P1F's on-disk layout and segment invariants are defined in
`include/pxe/p1f.h`. Payloads follow the descriptor table consecutively; zeroed
memory tails consume no file bytes. The userspace linker script puts permission
boundaries on separate pages. Read-only, read/execute and read/write segments
are supported; write/execute segments are rejected.

`make image` builds `hello` and includes its `.pxe` as the single Limine module.
The boot adapter retains its physical placement; after VM initialization the
kernel maps those reserved frames read-only and passes the bytes to
`image_load()`. The loader creates an inactive address space, copies through a
temporary kernel mapping, and applies final segment permissions. It leaves the
active address space unchanged and destroys partial allocations on failure.

Kernel initialization releases the module's temporary mapping, allocates a
one-page user stack at `0x800000`, and calls `user_task_create_on`. It chooses
CPU 1 when available and CPU 0 otherwise. Success transfers ownership of the
loaded space to the task and places it on that CPU's ready queue.
Failure leaves the space owned by the caller. Each task owns one private space
and a 16 KiB kernel stack; there is no shared-space ownership scheme yet.
After `user_init()`, the BSP can queue additional loaded images through the same
interface, including while APs run. `user_task_create()` selects CPU zero.
There is no public task-creation syscall.

Each CPU's scheduler uses its permanent boot stack, separate from every task stack.
It selects tasks round-robin and activates each task's address space, TSS.RSP0
and syscall-entry stack. First entry uses IRETQ with zeroed general registers;
subsequent timer returns restore the interrupted registers and instruction.
The local APIC timer runs periodically at approximately 100 Hz, calibrated
against PIT channel 2 without enabling the speaker or legacy PIC interrupts.

Userspace runs with IF=1. Interrupt gates and SYSCALL clear IF on kernel entry;
the kernel, including allocators and logging, remains non-preemptible. A long
syscall therefore delays preemption. Timer handling acknowledges the interrupt
before switching contexts and does not allocate, reclaim memory or print.
Syscalls still return through SYSRETQ, explicitly enabling interrupts again.

Each task preserves x87/SSE state eagerly with FXSAVE64/FXRSTOR64, along with
segment selectors and FS/GS bases. Fresh tasks receive empty x87 state, zeroed
XMM registers and the default floating-point controls. AVX/XSAVE and user
FS/GS-base instructions are disabled. Kernel builds continue to prohibit FP/SIMD.
Kernel GS identifies the current CPU; SWAPGS separates it from the saved user
GS base on entry and return.

Exit abandons the current kernel-entry stack and resumes the scheduler. Only
after returning to its own stack and activating the kernel space does the
scheduler publish completion to the BSP. The BSP frees the task's image,
user/kernel stacks, private page tables and VM metadata. Ordinary fatal user
exceptions print diagnostics and terminate that task; kernel exceptions, NMI,
double fault and machine check still panic. An invalid syscall return address
also terminates the current task. No cleanup
callbacks or stream flushing run on exit. Original boot-module frames remain
reserved and are not returned to the PMM.

The normal image still runs `hello`. Expected output includes its greeting,
`userspace: CPU 1 exited with status 0; address space released` on a multicore
boot, plus per-CPU idle messages. On a single CPU the task runs on CPU 0.
Cleanup and AP idle messages may arrive in either order. With no ready task the
scheduler waits using STI/HLT and returns to IF=0 after each interrupt. There are
no kernel threads, blocking syscalls, priorities or process relationships. Userspace
tasks are pinned to their selected CPU; allocation, submission and reclamation
remain BSP-only as described in [smp.md](smp.md).
