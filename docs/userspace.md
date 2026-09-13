# Building userspace images

`make tools` builds the hosted `elf2pxe` converter (`HOSTCC`, default `cc`).
`make userspace` builds all userspace programs; `make -C userspace hello` builds
only `hello`. Pass `CROSS_COMPILE` as for the kernel. Outputs live under
`build/tools` and `build/userspace`, so the root `make clean` removes them too.

Each program has its own directory and an explicit target in
`userspace/Makefile`. The initial program starts at a small assembly `_start`
which calls `int main(void)`. The kernel supplies a 16-byte-aligned writable
user stack with interrupts disabled; the call places a return address on that
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
one-page user stack at `0x800000`, and calls `user_run`. This activates the
loaded space and enters its entry address with SYSRETQ. The architecture helper
preserves the kernel caller's stack and callee-saved registers. TSS.RSP0 and
SYSCALL use a separate entry stack, leaving that suspended call intact.

Exit resumes the kernel call instead of returning through SYSRETQ. `user_run`
switches back to the kernel address space and returns the status. Its caller
then destroys the user space, releasing the image and stack frames, private
page tables and VM metadata. The loader's original module frames remain
boot-reserved; they are not returned to the PMM. The static kernel entry stack
is reused, not freed.

Expected output is the greeting, `userspace: exited with status 0; address space
released`, and the normal kernel initialization halt message. There is one
active run at a time, with interrupts disabled and no scheduler. Exit has no
cleanup callbacks or stream flushing; the caller owns the loaded space until
it is destroyed. Exceptions remain fatal rather than terminating only the
user program.
