# Building userspace images

`make tools` builds the hosted `elf2pxe` converter (`HOSTCC`, default `cc`).
`make userspace` builds all userspace programs; `make -C userspace hello` builds
only `hello`. Pass `CROSS_COMPILE` as for the kernel. Outputs live under
`build/tools` and `build/userspace`, so the root `make clean` removes them too.

Each program has its own directory and an explicit target in
`userspace/Makefile`. The initial assembly program starts at `_start`, invokes
syscall 0 with `'A'`, then executes HLT to produce the expected ring-3 #GP(0).
It has no runtime, libc, argument vector or return path. Its caller supplies a
16-byte-aligned writable user stack with interrupts disabled.

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
