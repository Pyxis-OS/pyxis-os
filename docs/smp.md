# Pinned userspace on multiple CPUs

`make run CPUS=4` boots one QEMU socket with four cores and one thread per core.
`CPUS` defaults to one and also applies to `make debug`. With multiple CPUs the
initial userspace image runs on CPU 1, leaving CPU 0 (the BSP) to service cleanup.
A single-CPU boot runs the image on the BSP. Other CPUs idle until given work.
CPU indices are dense, stable for the boot, and distinct from hardware APIC IDs.

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

## Scheduling and ownership

Each CPU has its own GDT, TSS, double-fault stack, active-space bookkeeping,
syscall stack storage, timer calibration and atomic interrupt counter. The IDT
is initialized once and shared without subsequent changes. Kernel GS points
to the CPU-local record; user entry and return use SWAPGS. Fatal NMI reporting
does not depend on GS because an NMI can interrupt the entry/exit window.

After bringing CPUs online, the BSP initializes the console and calls
`user_init()` to allocate one scheduler per CPU. APs wait until the BSP enters
`user_schedule()`, which publishes the initialized state with release ordering.
Each CPU then runs its own ready queue round-robin with local timer preemption.
Its permanent boot stack becomes its scheduler stack. Tasks never migrate.

The BSP loads a private image and user stack, then submits it using:

```c
enum mm_result result = user_task_create_on(cpu_index, space, entry, stack_top);
```

`cpu_index` must be below `arch_cpu_count()`. `user_task_create()` remains a
shorthand for CPU zero. Submission may occur before or after scheduling starts,
but only on the BSP with IF=0 and outside interrupt/fault entry. Success transfers
sole ownership of the space to the task; failure leaves it with the caller.
Do not inspect or mutate the space after transfer. One task still owns one space.

A short lock protects ready-list and completion-list links. It is never held
across allocation, logging, a context switch or waiting for another CPU. The
current task and saved scheduler stack are local to their CPU. Local timer
interrupts wake idle CPUs to check for submissions and wake the BSP to collect
completions, so these handoffs need no IPI and may wait about one timer period.

Exit and ordinary user faults return to the local scheduler. After switching
to its permanent stack and reloading the kernel root, the CPU clears its task
entry-stack pointer and publishes completion. It must not touch the task afterward.
The BSP detaches completed tasks under the lock and then frees their image,
stacks, tables and metadata outside it. If the BSP itself runs userspace, a
pending completion makes its next user timer interrupt return to the scheduler
even when there is no second runnable BSP task.

## Memory and output boundaries

Allocators, VM metadata, page-table mutation and the two scratch mappings remain
BSP-only. AP syscalls currently print or exit; they cannot allocate memory.
Private spaces are built before publication and reclaimed only after retirement.
Kernel code, CPU records, scheduler stacks, heap pools and framebuffer mappings
remain mapped throughout AP execution. A shared kernel range must not be
unmapped, remapped or protected while another CPU can use it.

Task activation reloads CR3 before accessing a newly published task stack. Task
retirement reloads it after leaving that stack, before the BSP can reclaim the
backing. With PCID and global pages disabled, these reloads invalidate the local
translations, including those of reused kernel-stack ranges. Other CPUs never
access that task's stack or private mappings. This is a restricted ownership
protocol, not general cross-CPU TLB invalidation; mutable shared mappings and
concurrent allocator calls still require additional synchronization.

Normal log calls serialize serial and framebuffer output per format invocation.
Single-byte syscalls from different programs can still interleave their text.
Fatal kernel diagnostics switch to unlocked serial-only output so an exception
in the lock owner cannot deadlock reporting. Such output may interleave, and a
kernel panic still halts only the faulting CPU. Direct TTY manipulation remains
limited to BSP initialization before releasing AP schedulers.

There are no spaces objects, migration, kernel threads, cross-CPU address-space
sharing or new userspace syscalls in this milestone.

## Debugger inspection

After a normal four-core boot, QEMU exposes each CPU as a GDB thread. These are
read-only inspections, suitable even when the CPUs are halted in idle:

```gdb
info threads
thread apply all info registers rip rsp cr3 gs_base
p 'arch/x86_64/smp.c'::cpu_count
p 'arch/x86_64/smp.c'::cpus[1]->online
p 'arch/x86_64/smp.c'::cpus[1]->timer_interrupts
p 'kernel/user.c'::schedulers[1]
```

CPUs have distinct stack pointers and GS bases. An idle CPU uses the kernel
root; a CPU running a task uses that task's private root. Resume, interrupt
execution again, and inspect the counters to observe timer delivery. Use the
BSP and the stopping conditions in [gdb.md](gdb.md) for debugger-invoked allocator
and submission calls. Never call them on an AP. Once a task completes, its
pointer may already have been freed by the BSP.
