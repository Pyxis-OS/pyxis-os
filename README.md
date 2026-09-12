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
Generic initialization then creates the VM allocator and TLSF heap, reports
memory accounting and halts. There are no processes, devices beyond COM1, or shell.

Bootstrap limits: 256 memory regions, usable RAM below 64 GiB, 256 VM range
records and 16 KiB TLSF control storage. Capacity failures are
explicit. Bootloader-reclaimable and firmware memory stay reserved.
The image remains at `0xffffffff80000000`; allocations use the 64 GiB range at
`0xffff800000000000`. PMM metadata lives at `0xfffffe8000000000`; two scratch
pages at `0xfffffe8040000000` zero physical frames without allocating tables.
Slot 510 recursively exposes the new tables. After switching CR3 there is no
HHDM or lower-half mapping. All leaves are 4 KiB with W^X permissions.

PMM returns physical addresses (zero on failure); frees require the exact owned
extent. VM reservations allocate no frames; backed ranges own zeroed, individually
allocated frames and unwind partial failure. Reserve before mapping caller-owned
frames, remove those mappings before releasing, and release ranges whole. Low-level
unmap never frees frames. VM errors use `mm_result`; invalid PMM frees panic.
`kmalloc(0)` and exhaustion return NULL, allocations have 16-byte alignment, and
`kfree(NULL)` is harmless. Requests above 2 GiB are rejected. Pools start at
256 KiB and grow on demand; freed blocks are reused, while pools and empty page
tables remain allocated. Interface headers document the full ownership contracts.

Maskable interrupts stay disabled; allocators run on one CPU and never in fault
handlers. BIOS, physical hardware, concurrency and bootloader-memory reclamation
are outside this milestone.
