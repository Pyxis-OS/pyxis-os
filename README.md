# Pyxis OS

Caelum is the freestanding x86_64 kernel of Pyxis OS. Development targets
QEMU booted through OVMF/UEFI and the vendored Limine v12.9.0.

Requires GNU Make, the [Pyxis GCC/binutils toolchain](toolchain/README.md),
QEMU, GNU cpio, xorriso, host Lua 5.4, and a matching raw OVMF code/variables pair.
Userspace and ports are [pinned submodules](docs/sdk-and-repositories.md).
CI publishes [independent build bundles](docs/build-bundles.md) for local reuse.

```
git submodule update --init userspace ports
make
make image
make sdk  # export the userspace SDK to build/sdk; see docs/sdk.md
make image INIT=/tmp/init.sh  # optional native PXE or shebang init
make run
make run CPUS=4  # one socket, four cores, no SMT
make run LOG_LEVEL=trace  # include scheduler idle diagnostics (default: info)
make debug  # paused; GDB: file build/caelum.elf, target remote :1234
make clean
```

Override `CROSS_COMPILE`, `QEMU`, `CPUS` (default `1`), `MEMORY` (default `256M`), `ACCEL` (default
`tcg`), `OVMF_CODE` and `OVMF_VARS` on the Make command line. Firmware defaults
to `/usr/share/OVMF/{OVMF_CODE,OVMF_VARS}.fd`; variables are copied into build
on each run. Serial uses the terminal; exit QEMU with Ctrl-a x. Graphics use
GTK by default; `QEMU_DISPLAY=none` keeps a run headless.

Boot installs serial and a kernel stack, copies boot information, installs
GDT/IDT/TSS, initializes the bitmap PMM, and switches to kernel-owned paging.
Generic initialization creates the VM allocator and TLSF heap, brings APs onto
owned stacks and paging, initializes framebuffer text output, and schedules
[init](docs/init.md) on CPU 1 when available, or the BSP on a single-CPU boot.
The default init script hands off to the interactive shell.
Use Super+Right to select CPU 1; the shell starts at `home://`. See
[the shell walkthrough](docs/shell.md).
Local APIC timers preempt each CPU's pinned tasks and wake idle CPUs. The BSP
owns allocation and completed-task cleanup; see `docs/smp.md`.

Bootstrap limits: 256 memory regions, usable RAM below 64 GiB, 256 kernel VM range
records and 16 KiB TLSF control storage. Capacity failures are
explicit. Bootloader-reclaimable and firmware memory stay reserved.
The image remains at `0xffffffff80000000`; allocations use the 64 GiB range at
`0xffff800000000000`. PMM metadata lives at `0xfffffe8000000000`; two scratch
pages at `0xfffffe8040000000` access physical frames and inactive page tables.
Slot 510 recursively exposes the new tables. After switching CR3 there is no
HHDM; the initial kernel root has no lower-half mappings. All leaves are 4 KiB
with W^X permissions. New VM spaces share the kernel mappings and own their user
ranges; see `include/kernel/mm/vm.h` for creation, allocation and destruction.

PMM returns physical addresses (zero on failure); frees require the exact owned
extent. VM reservations allocate no frames; backed ranges own zeroed, individually
allocated frames and unwind partial failure. Reserve before mapping caller-owned
frames, remove those mappings before releasing, and release ranges whole. Low-level
unmap never frees frames. VM errors use `mm_result`; invalid PMM frees panic.
`kmalloc(0)` and exhaustion return NULL, allocations have 16-byte alignment, and
`kfree(NULL)` is harmless. Requests above 2 GiB are rejected. Pools start at
256 KiB and grow on demand; freed blocks are reused, while pools remain allocated.
Empty private page tables are reclaimed when their space is destroyed. Interface
headers document the full ownership contracts.

Kernel execution stays non-preemptible; only userspace and idle enable maskable
interrupts. Allocation and task submission remain BSP-only, outside interrupt/
fault handlers. Task migration, shared-space execution, cross-CPU TLB shootdowns,
kernel threads, AVX, physical hardware and bootloader-memory reclamation remain
unsupported. See `docs/userspace.md`.
