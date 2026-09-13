# Application processor startup

`make run CPUS=4` boots one QEMU socket with four cores and one thread per core.
`CPUS` defaults to one and also applies to `make debug`. The BSP continues to
run the initial userspace image. APs enter kernel idle and receive local APIC
timer interrupts; they do not yet run workloads.

## Boot handoff

The Limine adapter requests multiprocessor information in xAPIC mode. Before
replacing the BSP root it records the CPU count, BSP APIC ID and physical
location of Limine's CPU pointer array. The original direct-map offset is
retained only to decode pointers in that array, never for later dereferences.

After VM and heap initialization, the BSP temporarily maps the bootloader-owned
CPU records through the kernel VM allocator. It allocates each AP's CPU-local
record and two separate 16 KiB stack areas, for normal execution and double fault.
These allocations remain kernel-owned until shutdown. There is no fixed CPU
array limit; allocation failure and unsupported xAPIC IDs fail boot explicitly.

APs start one at a time. The BSP publishes a handoff pointer and entry address
using the release ordering required by the pinned Limine protocol. A small
Limine assembly entry reads the handoff pointer before entering architecture
code. The handoff itself resides in the kernel image, visible under both roots.

The AP checks paging features and configures NX/WP before replacing CR3. It
switches directly from the boot root and stack to the owned kernel root and
its allocated stack, without a stack access between those changes. It then
installs its GDT/TSS and GS base, loads the shared IDT, initializes CPU features
and its local timer, and publishes its online state with release ordering.
Only after acquiring that state may the BSP reuse the handoff for another AP.
The BSP's running APIC timer supplies a startup timeout of approximately five
seconds without requiring interrupts on the BSP.

PIT calibration is serialized by this startup order. APs do not print normal
startup messages; the BSP reports their APIC IDs, stack tops and timer counts.
After all handoffs, the adapter removes its temporary mappings and drops its
startup references. No CPU continues to depend on Limine's stacks, page tables
or response pointers. The original bootloader frames remain reserved.

## CPU-local state and current limits

Each CPU has its own GDT, TSS, double-fault stack, active-space bookkeeping,
syscall stack storage, timer calibration and atomic interrupt counter. The IDT
is initialized once and shared without subsequent changes. Kernel GS points
to the CPU-local record; user entry and return use SWAPGS. Fatal NMI reporting
does not depend on GS because an NMI can interrupt the entry/exit window.

APs keep the kernel root active and only execute initialization, idle and their
timer handler. They do not call the scheduler, allocators, VM mutation routines
or normal logging paths. Scheduler queues, allocation metadata and scratch
mappings therefore remain BSP-owned. CPU-local bookkeeping alone does not make
those operations safe to run concurrently. Parallel workloads need synchronization
and an appropriate cross-CPU TLB invalidation policy before sharing mutable
kernel mappings or reclaiming memory other CPUs may still reference.

## Debugger inspection

After a normal four-core boot, QEMU exposes each CPU as a GDB thread. These are
read-only inspections, suitable even when the CPUs are halted in idle:

```gdb
info threads
thread apply all info registers rip rsp cr3 gs_base
p 'arch/x86_64/smp.c'::cpu_count
p 'arch/x86_64/smp.c'::cpus[1]->online
p 'arch/x86_64/smp.c'::cpus[1]->timer_interrupts
```

The APs should have distinct stack pointers and GS bases, with CR3 naming the
owned kernel root. Resume, interrupt execution again, and inspect the counters
to observe timer delivery. Use the BSP and the stopping conditions in
[gdb.md](gdb.md) for any debugger-invoked allocator calls.
