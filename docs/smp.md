# Pinned tasks on multiple CPUs

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
`task_init()` to allocate one scheduler per CPU. APs wait until the BSP enters
`task_schedule()`, which publishes the initialized state with release ordering.
Each CPU then runs its own ready queue round-robin with local timer preemption.
Its permanent boot stack becomes its scheduler stack. Tasks never migrate.

The BSP loads a private image and user stack, wraps that address space in a
process belonging to the target CPU's space, then submits the process using:

```c
enum mm_result result = user_task_create_on(cpu_index, process, entry, stack_top);
```

`cpu_index` must be below `arch_cpu_count()`. `user_task_create()` remains a
shorthand for CPU zero. Submission may occur before or after scheduling starts,
but only on the BSP with IF=0 and outside interrupt/fault entry. Success transfers
sole ownership of the process to the task; failure leaves it with the caller.
The target CPU must host the process's owning space. Do not inspect or mutate
the process or its address space after transfer. There is one task per process.

`process_create(owner, address_space, &process)` takes ownership of an inactive
private address space only on success. `process_destroy(process)` releases an
unsubmitted process and its address space if subsequent setup fails. Neither
call owns or destroys the containing space or its TTY. See the
[process interface](../include/kernel/process.h) for the full lifetime contract.

A short lock protects ready-list and completion-list links. It is never held
across allocation, logging, a context switch or waiting for another CPU. The
current task and saved scheduler stack are local to their CPU. Local timer
interrupts wake idle CPUs to check for submissions and wake the BSP to collect
completions, so these handoffs need no IPI and may wait about one timer period.

Exit and ordinary user faults return to the local scheduler. After switching
to its permanent stack and reloading the kernel root, the CPU clears its task
entry-stack pointer and publishes completion. It must not touch the task afterward.
The BSP detaches completed tasks under the lock, destroys each user process and
its private address space, then frees the task's kernel stack and metadata
outside the lock. Kernel tasks have no process. If the BSP itself runs userspace,
a pending completion makes its next user timer interrupt return to the scheduler
even when there is no second runnable BSP task.

Capability tables follow that same exclusive process ownership. The BSP installs
entries before submission; the executing CPU can resolve or close them with
IF=0. A last object release queues its embedded retirement link without touching
the heap. The BSP scheduler drains this separate list after task cleanup and
runs destruction callbacks outside its lock. Pending objects also cause a busy
BSP task to return to the scheduler on its next timer interrupt. No table grows
on an AP; final releases never require an AP allocator call.

Kernel tasks share the BSP ready queue with any BSP userspace. Create one with
`kernel_task_create(entry, argument)` after scheduler initialization, on the BSP
with interrupts disabled. Its entry runs on a private stack in the kernel
address space with interrupts enabled. The timer can preempt it; returning from
entry retires its stack and metadata through the same scheduler cleanup path.
The argument is borrowed, so its owner must keep it alive until entry returns.

`kernel_task_sleep(ticks)` suspends the current kernel task until that many BSP
timer interrupts have been delivered. Zero ticks yields to other ready tasks.
Sleeping tasks are checked both by the BSP scheduler and by timer preemption,
so a busy task cannot prevent a sleeper from becoming runnable. Sleep requires
interrupts enabled and no held locks. The [task header](../include/kernel/task.h)
defines the calling contracts.

Framebuffer presentation is the first BSP kernel task. It copies the active
space and sleeps for two local timer ticks between copies. Rendering stays out
of interrupt entry and the scheduler does not know about display timing.

## Memory and output boundaries

Allocators, VM metadata, page-table mutation and the two scratch mappings remain
BSP-only and require interrupts disabled. A kernel task must save/disable
interrupts around these calls and restore them afterward; being pinned to the
BSP alone does not prevent same-CPU reentry. AP syscalls currently print or exit;
they cannot allocate memory.

User-buffer checks are a narrow exception to BSP-only queries: the executing
CPU can inspect its active private root through recursive mappings, with IF=0
and stable user mappings. They do not use shared scratch slots or VM metadata.
General `vm_query()` and page-table mutation remain BSP-only.

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

Normal log calls save/disable interrupts while serializing serial and framebuffer
output per format invocation, then restore the caller's interrupt state.
Single-byte syscalls from different programs can still interleave their text.
Fatal kernel diagnostics switch to unlocked serial-only output so an exception
in the lock owner cannot deadlock reporting. Such output may interleave, and a
kernel panic still halts only the faulting CPU. Shared TTY mutation still needs
serialization; the low-level log lock interface requires interrupts disabled.

Kernel tasks stay on the BSP. Task migration, shared user address spaces and
kernel-task fault recovery are not supported; a kernel-task fault is fatal.

## Debugger inspection

After a normal four-core boot, QEMU exposes each CPU as a GDB thread. These are
read-only inspections, suitable even when the CPUs are halted in idle:

```gdb
info threads
thread apply all info registers rip rsp cr3 gs_base
p 'arch/x86_64/smp.c'::cpu_count
p 'arch/x86_64/smp.c'::cpus[1]->online
p 'arch/x86_64/smp.c'::cpus[1]->timer_interrupts
p 'kernel/task.c'::schedulers[1]
```

CPUs have distinct stack pointers and GS bases. An idle CPU uses the kernel
root; a CPU running a task uses that task's private root. Resume, interrupt
execution again, and inspect the counters to observe timer delivery. Use the
BSP and the stopping conditions in [gdb.md](gdb.md) for debugger-invoked allocator
and submission calls. Never call them on an AP. Once a task completes, its
pointer may already have been freed by the BSP.
