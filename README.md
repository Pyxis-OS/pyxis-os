# Pyxis OS

Caelum is the freestanding x86_64 kernel of Pyxis OS. Development targets one
QEMU CPU booted through OVMF/UEFI and the vendored Limine v12.9.0.

Requires GNU Make, an `x86_64-elf-` GCC/binutils toolchain supporting GNU C23,
QEMU, xorriso, and a matching raw OVMF code/variables pair.

```
make
make image
make run
make debug  # paused; GDB: file build/caelum.elf, target remote :1234
make clean
```

Override `CROSS_COMPILE`, `QEMU`, `MEMORY` (default `256M`), `ACCEL` (default
`tcg`), `OVMF_CODE` and `OVMF_VARS` on the Make command line. Firmware defaults
to `/usr/share/OVMF/{OVMF_CODE,OVMF_VARS}.fd`; variables are copied into build
on each run. Serial uses the terminal; exit QEMU with Ctrl-a x.

The current boot foundation installs serial, a kernel stack, GDT/IDT/TSS and
fatal exception reporting, copies at most 256 memory regions, then halts.
Maskable interrupts stay disabled. BIOS and physical hardware are out of scope.
