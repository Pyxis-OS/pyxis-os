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

Boot installs serial and a kernel stack, copies boot information, installs
GDT/IDT/TSS, initializes the bitmap PMM, and switches to kernel-owned paging.
The current memory takeover then reports frame accounting and halts.

Bootstrap limits: 256 memory regions and usable RAM below 64 GiB; exhaustion
fails explicitly. Bootloader-reclaimable and firmware memory stay reserved.
The image remains at `0xffffffff80000000`; allocations use the 64 GiB range at
`0xffff800000000000`. PML4 slot 509 holds PMM metadata and two temporary frame
slots; slot 510 recursively exposes only the new tables. After switching CR3,
no HHDM or lower-half mapping remains. All leaves are 4 KiB with W^X permissions.
Empty page tables are retained. PMM frees require the caller's exact owned extent.
Maskable interrupts stay disabled; allocator/fault paths are single-CPU only.
BIOS and physical hardware are out of scope.
