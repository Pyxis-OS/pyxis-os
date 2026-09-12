# Assignment: empty repository to a kernel-owned memory subsystem

Implement this milestone in one pass. This is an implementation assignment, not a request for another plan. Make ordinary local implementation decisions yourself and stop at the boundary below.

## Target and stopping point

Build a small, freestanding C kernel for Linux-hosted development using the existing `x86_64-elf` GCC/binutils cross-toolchain and GNU Make. Run it on a single x86_64 QEMU CPU, booted through OVMF/UEFI and Limine v12.x. BIOS and physical hardware support are out of scope.

At completion, the kernel must have polling serial diagnostics, its own GDT/IDT and exception handling, a bitmap physical allocator, a completely kernel-owned four-level paging hierarchy using only 4 KiB pages, recursive page-table access, kernel virtual-range allocation, and TLSF-backed `kmalloc`/`kfree`.

The kernel remains higher-half. It must actually switch CR3 to its new root. This is NOT a request for a low-address identity-mapped kernel. Do not retain Limine's root, share its child tables, or retain its HHDM as a permanent dependency.

Stop after architecture-independent kernel initialization prints useful initialization/memory information and enters a halt loop. Do not add processes, scheduling, syscalls, userland, or a shell.

## Repository layout

Use this as the initial layout. Local helper headers may be added where needed; do not manufacture empty future subsystems.

```text
.
├── AGENTS.md
├── BOOTSTRAP.md
├── README.md
├── Makefile
├── .clang-format
├── .gitignore
├── boot/
│   └── limine/
│       ├── entry.c
│       └── limine.conf
├── arch/
│   └── x86_64/
│       ├── entry.S
│       ├── init.c
│       ├── serial.c
│       ├── gdt.c
│       ├── idt.c
│       ├── isr.S
│       ├── paging.c
│       ├── linker.ld
│       └── include/
│           └── arch/
│               ├── init.h
│               ├── console.h
│               ├── cpu.h
│               ├── layout.h
│               └── paging.h
├── include/
│   └── kernel/
│       ├── boot.h
│       ├── init.h
│       ├── log.h
│       ├── panic.h
│       └── mm/
│           ├── pmm.h
│           ├── vm.h
│           └── heap.h
├── kernel/
│   ├── init.c
│   ├── log.c
│   ├── panic.c
│   └── mm/
│       ├── pmm.c
│       ├── vm.c
│       └── heap.c
├── lib/
│   ├── memory.c
│   └── format.c
├── third_party/
│   ├── limine/
│   └── tlsf/
├── scripts/
│   ├── make-image.sh
│   └── run-qemu.sh
└── build/                     # ignored generated output
```

`boot/limine` owns the Limine requests, response validation, and conversion into internal boot information. `arch/x86_64` owns machine instructions, descriptors, interrupt entry, UART port I/O, the virtual layout, and hardware page tables. `kernel/mm` owns physical allocation policy, virtual-range bookkeeping, and heap integration. `lib` contains only the small freestanding support routines actually needed.

Generic code must not depend on Limine structures, PML4 entries, CR3, x86 descriptor layouts, or raw x86 page-permission bits. Use a small compile-time architecture interface, not an operations-table/plugin framework. No speculative RISC-V implementation or empty architecture directories.

## Initialization flow

Preserve this conceptual order:

```text
x86_64 entry stub
  -> early_init()
       polling serial and early diagnostics
  -> Limine adapter
       validate responses and capture internal boot information
  -> arch_init(boot_info)
       install GDT/IDT and required exception machinery
       bootstrap the generic physical allocator
       construct and activate the kernel-owned address space
  -> kernel_init(boot_info)
       initialize kernel virtual-range allocation
       initialize the TLSF-backed heap
       report readiness and halt
```

The Limine C entry can orchestrate this flow. The assembly stub should establish a known, ABI-correct environment and a kernel-owned bootstrap stack before entering C; a statically allocated, aligned stack in the kernel image is sufficient.

`arch_init` may call generic PMM code because constructing page tables needs physical frames. That does not make the bitmap algorithm architecture-specific. Keep the algorithm in `kernel/mm/pmm.c` rather than distorting initialization order to avoid this dependency.

Do not assume that early initialization, logging, or boot-information capture can use `kmalloc`.

## Build and boot

Pin a released Limine v12.x version; use v12.9.0 unless there is a documented reason to select another 12.x release. Pin the compatible protocol header separately and record the exact revision. Bootloader version and protocol base revision are different numbers: use the documented supported protocol revision, not `LIMINE_BASE_REVISION(12)`.

Use current request delimiters, linker retention, configuration syntax, and response structures from the pinned dependencies. Request the memory map, HHDM, executable load addresses, and exactly four-level paging. Reject missing required responses or unsupported required features with a serial diagnostic. Do not add framebuffer, SMP, or module requests that this milestone does not need.

Use freestanding compiler/linker settings, including no red zone, no compiler-generated FP/SIMD, an appropriate higher-half kernel code model, and no host startup files or host libc. Retain debug symbols and useful warnings. Compile vendored allocator code under compatible kernel settings too. Compiler runtime helpers are acceptable only from the target cross-toolchain and only where actually needed.

Provide straightforward targets:

```text
make          build the kernel
make image    create the UEFI boot image
make run      boot it normally in QEMU with serial on the terminal
make debug    the same target, paused for GDB
make clean    remove generated build output
```

Choose one uncomplicated, unprivileged image-creation method. Do not add a kernel filesystem driver to read the boot image. Allow overrides for the cross-toolchain prefix, QEMU executable, memory size, and OVMF paths. Use a matching OVMF code/variables pair, with firmware code read-only and a build-local writable variables copy. Never modify the host firmware files. Do not require KVM; an optional acceleration override is fine.

Keep the Makefile readable. Small shell scripts for image assembly and QEMU invocation are enough. No Python build wrapper, container setup, toolchain builder, or automatic host-package installation. Missing dependencies should produce actionable errors. Dependency acquisition must use explicit pins rather than following a moving branch on every build.

## Boot-information boundary

Expose an internal `boot_info` representation with your own memory-region types, kernel physical/virtual placement, and the bootstrap mapping information actually needed. Do not expose Limine types outside the adapter.

Copy the information that must survive into kernel-owned storage. In particular, copying a pointer to a Limine response is not copying the response or its referenced arrays. Keep retained pointers valid across the CR3 switch. Distinguish physical addresses from usable virtual pointers in APIs and naming.

Bounded bootstrap storage is acceptable. Document capacity and fail explicitly on exhaustion; never silently truncate the memory map. Prefer this to building a general early-allocation framework.

Leave bootloader-reclaimable memory reserved in this milestone. Reclaiming it can come later. Nonetheless, there must be no continuing dependency on its stack, GDT, page tables, or response pointers after takeover. Also keep firmware, ACPI, reserved, bad, and kernel/module regions unavailable to normal PMM allocation.

## Serial and exceptions

Implement polling COM1 output and simple polling input. Generic logging and panic must remain usable before the heap exists and after replacing the page tables. Support only the formatting needed for readable messages and addresses; do not port a full stdio library.

Install a kernel-owned GDT and IDT. Include a minimal TSS and dedicated double-fault IST stack for useful catastrophic-fault handling; no ring-3 descriptors or process machinery are required yet.

Exception entry must correctly handle vectors with and without hardware error codes, save the state consumed by C, and satisfy the C calling convention. Fatal faults should print the vector, error code, instruction address, and relevant register state. Page faults should additionally report CR2 and decode the useful error bits. Panic and exception reporting must not allocate memory or try to repair arbitrary faults.

Keep maskable interrupts disabled. Do not add interrupt-controller initialization, timer interrupts, scheduling, or blanket `sti` calls. Any unavoidable defensive hardware handling should remain local and small.

## Physical memory allocation

Implement a bitmap allocator with allocation/free of individual 4 KiB frames and contiguous runs of frames. Return physical addresses, not pointers presumed dereferenceable by C. Contiguous-run allocation needs page alignment; a more elaborate DMA/alignment policy is not required.

Derive allocatable ranges from the internal memory map, respecting full-page boundaries, holes, overflow, and reservations. Reserve physical frame zero and all bootstrap/allocator metadata before exposing pages as free. A run must not cross an unavailable frame just because its bitmap indexes are consecutive.

Include useful total/free/allocated accounting. Expected exhaustion must return failure; invalid frees should be diagnosed rather than silently corrupting state. Define the free contract clearly: callers own the allocation and must supply its correct extent. Do not turn this bitmap allocator into a general object-ownership tracker.

The bitmap and other PMM state need valid mappings after CR3 changes. The PMM must not require the heap or the virtual-range allocator to allocate a frame.

## Kernel-owned virtual memory

Create a fresh hierarchy with only 4 KiB leaves, separate from Limine's hierarchy. The restriction applies to the kernel's new tables; do not assume the bootloader's temporary mappings use that page size.

Preserve the higher-half kernel virtual addresses. Map the kernel text read/execute, read-only data read/nonexecute, and writable data/BSS/stacks read/write/nonexecute, with appropriate page-aligned linker boundaries. All mappings in this milestone are supervisor-only. Ensure write protection and NX are configured and supported on the chosen QEMU CPU.

Include every object needed across the switch: executing code, current stack, GDT/IDT/TSS and exception stacks, retained boot information, and allocator metadata. Do not map physical holes or MMIO indiscriminately. Leave virtual page zero and the unused lower half unmapped.

Reserve a recursive PML4 slot, a disjoint kernel allocation area, and a small temporary mapping area. Keep layout constants in one architecture-local place and document the chosen ranges. The recursive slot must point to the new root itself, not Limine's root.

Use Limine's HHDM only as a bootstrap access mechanism while constructing the replacement. After activation, use recursive mappings for the active page tables and pre-established temporary mapping slots for arbitrary physical frames. A newly allocated table must be zeroed before publishing it as present. The temporary slots must be usable without allocating more tables or consulting the heap. Invalidate stale translations when reusing them.

The final address space must not depend on Limine's HHDM, identity mappings, or page-table frames. Persistent objects that were initially accessed through the HHDM need their own mappings or kernel-owned copies before the switch. Temporary boot-only mappings must not become an accidental permanent direct map.

Provide low-level operations for mapping a supplied physical frame, unmapping, changing leaf permissions, and querying a translation. Validate alignment, canonicality, address-width limits, and collisions. Do not silently replace a present mapping. Keep physical-address extraction separate from flag extraction, and preserve effective permission constraints throughout the walk.

Mapping does not allocate the supplied data frame. Unmapping does not implicitly free it. Allocate missing table frames through the PMM, without depending on the heap. Handle allocation failure without corrupting existing mappings or leaking newly allocated data frames.

Perform appropriate local TLB invalidation for all affected translations, including recursive aliases when changing hierarchy entries. Keep PCID/global-page optimizations and SMP shootdowns out of scope. Empty table frames may be retained and reused for this milestone; document that policy rather than claiming full table reclamation.

Only the active kernel address space needs an implementation now. Do not add future process-address-space creation/cloning/switching APIs without an actual caller.

## Virtual ranges and heap

Keep three responsibilities distinct:

```text
PMM                 chooses and owns physical frames
VM range allocator  reserves virtual ranges and manages their backing
Heap                subdivides VM-backed pools into byte-sized allocations
```

The VM layer must find/reserve page-aligned ranges in the designated kernel allocation area, release reservations, allocate backing frames and map a range, and unmap/free ranges whose backing it owns. Reserving a range must not imply allocating physical memory. A backed allocation should be virtually contiguous without requiring physically contiguous frames. Use eager, zero-initialized backing; demand paging is out of scope.

Use simple free-range bookkeeping with splitting and coalescing. Define errors and ownership clearly, check size/alignment arithmetic for overflow, and unwind partially completed allocations. Never free caller-owned frames merely because a low-level mapping was removed.

Resolve metadata bootstrap explicitly. VM metadata cannot depend on `kmalloc` while `kmalloc` needs VM allocations. A bounded, documented bootstrap metadata pool with explicit exhaustion is acceptable for this milestone; no elaborate allocator stack or hidden recursive allocation path is wanted.

Vendor a pinned Matthew Conte TLSF implementation under `third_party/tlsf`, preserving its license and recording the upstream commit and local changes. Keep its allocator logic recognizable. Provide the small freestanding adaptation needed for assertions, diagnostics, and memory routines; do not drag in host libc or silently compile away corruption checks.

Expose `kmalloc` and `kfree` through your own kernel interface. Supply TLSF pools from VM-backed kernel memory. It should be possible to grow the heap by adding another pool when needed. Do not require physical contiguity for pools. Handle pool/control overhead, oversized requests, overflow, and growth failure. Ensure returned pointers satisfy the supported C allocation alignment, including at least 16-byte alignment on this target.

`kmalloc` returns failure on exhaustion, with documented zero-size behavior; `kfree(NULL)` is harmless. Freed blocks return to TLSF for reuse. Returning entirely empty pools to the PMM is not required yet. TLSF control data and VM metadata must not recursively allocate through the heap being initialized or expanded.

This milestone is single-CPU, with maskable interrupts disabled and no allocator use in fault handlers. Document those assumptions. Do not add fake locks or placeholder concurrency infrastructure.

## Style and scope discipline

Use conventional C: snake_case, two-space indentation, K&R control-statement braces, function opening braces on their own line, and small explicit interfaces. GNU C11 is a suitable baseline. Use standard fixed-width and size types, plus a few meaningful address types if useful. Preserve Make recipe tabs and upstream formatting in vendored files.

Comments should explain invariants, ownership, boot-order constraints, and non-obvious hardware behavior, not narrate every statement. Use runtime checks for real invalid state and clear failure returns for ordinary exhaustion.

Do not create automated tests, hosted tests, QEMU test harnesses, CI workflows, self-tests, demo allocation loops, synthetic fault injection, output-matching scripts, or test-only abstractions. Ordinary builds, a normal QEMU boot, and debugger use are fine. Do not add tests under another name.

Also out of scope: a scheduler, process/task system, context switching, syscalls, ring 3, user executable loading, initramfs/archive handling, VFS, filesystem drivers, POSIX compatibility, IPC, object/capability frameworks, VirtIO/PCI/device frameworks, graphical interfaces, SMP, demand paging, swap, huge pages, and a second architecture port. The kernel ELF is just Limine's boot artifact, not a decision about a future user executable format.

## Delivery

Implement the milestone, build it, and attempt an ordinary interactive QEMU boot where the required tools/firmware are available. Do not create an automation or log-parsing harness. Report exactly what was built/run and distinguish actual observations from code review. If the environment lacks a dependency, state the missing item and the command needed to continue; do not claim a successful boot.

The README should explain dependencies and commands, boot order, the virtual layout, allocation ownership/failure semantics, bootstrap limits, retained bootloader memory, and the temporary-mapping/recursive-access design. Keep it practical, not a speculative OS design document. Put durable coding/scope rules into a short AGENTS.md without copying this entire assignment into it.

Finish with a short handoff: implemented pieces, commands used, observed boot status, and concrete remaining limitations. The endpoint is a kernel ready for the owner to start designing the interesting OS mechanisms—not a kernel into which you have already invented those mechanisms.

## Implementation references

Consult the pinned versions when implementing; moving upstream documentation is not a substitute for matching the selected dependency.

- Limine v12.9.0 release: https://github.com/Limine-Bootloader/Limine/releases/tag/v12.9.0
- Limine protocol and header: https://github.com/Limine-Bootloader/limine-protocol
- TLSF upstream: https://github.com/mattconte/tlsf
- GCC x86 target options: https://gcc.gnu.org/onlinedocs/gcc/x86-Options.html
- QEMU invocation reference: https://www.qemu.org/docs/master/system/invocation.html
- Intel architecture manuals: https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html

## Addendum

- The operating system is **Pyxis OS**. Its kernel is **Caelum**. Use these names consistently in boot messages, build artifacts, and documentation.
- Compile C code as **GNU C23** using `-std=gnu23`. Retain K&R style with two-space indentation.
- If creating or updating `README.md`, keep it short and focused: a brief project description, required tools, build/run commands, and any essential limitations. No directory trees, emojis, marketing language, generic feature lists, lengthy architecture explanations, or speculative roadmaps. Do not turn the assignment into a README essay.
