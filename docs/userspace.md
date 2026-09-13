# Building userspace images

`make tools` builds the hosted `elf2pxe` converter (`HOSTCC`, default `cc`).
`make userspace` builds all userspace programs; `make -C userspace hello` builds
only `hello`. Pass `CROSS_COMPILE` as for the kernel. Outputs live under
`build/tools` and `build/userspace`, so the root `make clean` removes them too.

Each program has its own directory and an explicit target in
`userspace/Makefile`. The initial program starts at a small assembly `_start`
which calls `int main(void)`. The kernel supplies a 16-byte-aligned writable
user stack with interrupts disabled; the call places a return address on that
stack as required by the C ABI. There is no libc, argument vector or exit
syscall. Returning from `main` reaches HLT and the expected ring-3 #GP(0).

`hello/main.c` prints `Hello from C!` using the unbuffered helpers in
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
one-page user stack at `0x800000`, activates the loaded space, and enters its
entry address with SYSRETQ. Module frames remain boot-reserved; they are not
returned to the PMM. The current kernel syscall logs each character separately:
expect one `syscall0:` line per character of the greeting, followed by the
deliberate HLT exception after `main` returns. There is no scheduler or process
lifetime management.
