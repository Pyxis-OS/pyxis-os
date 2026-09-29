# Pinned tasks on multiple CPUs

`make run CPUS=4` boots one QEMU socket with four cores and one thread per core.
`CPUS` defaults to one and also applies to `make debug`. With multiple CPUs,
CPU 0 (the BSP) services allocation and cleanup. Each other CPU runs its
selected [init](init.md); defaults start shells on CPUs 1 and 2, and an idle
init on further CPUs. Children stay on their parent's CPU. A single-CPU boot
runs the primary init on the BSP.
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

Batch launch splits submission into BSP-only `user_task_prepare_on()` and
`user_task_publish_group()`. Preparation allocates and initializes each task and
kernel stack without enqueueing it; its process remains inactive and owned by
the preparer. `user_task_discard_prepared()` releases only that task and stack,
leaving process destruction to the preparer. Publication enqueues the complete
group under the queue lock and transfers every process and task together. It
allocates nothing; after unlocking, the BSP retains only the destination CPU
index for notification. All observer handles and result slots exist before this
transfer. The blocked caller lends its capability table to BSP preparation;
cross-CPU requests and results live in task metadata, never remote stack pointers.

`process_create(owner, address_space, &process)` takes ownership of an inactive
private address space only on success. `process_destroy(process)` releases an
unsubmitted process and its address space if subsequent setup fails. Neither
call owns or destroys the containing space or its TTY. Before submission, call
`process_prepare_startup()` with named bindings to installed handles, arguments
and environment; omit absent resources. The startup region belongs to the
process's address space, with read-only metadata and writable argument pages.
Entry receives its address in RDI. See the
[startup interface](../include/kernel/user/startup.h) and
[process interface](../include/kernel/process.h) for ownership contracts.

A short lock protects ready-list and completion-list links. It is never held
across allocation, logging, a context switch or waiting for another CPU. The
current task and saved scheduler stack are local to their CPU. After scheduler
startup, remote task creation and resource wakeups of parked tasks send a fixed
xAPIC rescheduling IPI after releasing the queue lock. The sender retains only
the destination CPU index after publication, not a borrowed task/wait pointer.
The target checks its ready queue without waiting for its next timer tick.

Vector 37 is reserved for rescheduling. Interrupt handlers release their locks
and acknowledge EOI before checking for a context switch. Device interrupts also
make this check, so an awakened BSP worker can preempt an eligible running task.
The existing rules still apply: AP kernel execution and userspace syscall paths
are non-preemptible; kernel tasks run preemptibly on the BSP. An interrupt arriving
while the scheduler is idle returns to its ready-queue loop. IF=0 and STI/HLT's
interrupt shadow cover publication between the empty-queue check and halt.

Private-memory allocation/release requests also notify the BSP after publication.
The requester has already left its private root and task stack before linking
its parked task into the memory queue. Notification happens after unlocking and
never dereferences that task again. The existing rescheduling handler can wake
an idle BSP or preempt an eligible BSP task because it checks the memory queue.
A BSP caller sends no self-IPI: its scheduler loop services the request directly.
There is no remote allocation, change to VM ownership, or new interrupt handler.

Initial HOST request publication also notifies the BSP after releasing the queue
lock, so a request arriving after its queue sweep can wake an idle BSP. The
caller saves its own wait pointer before publication and uses the existing
early-wakeup handshake; it does not inspect the borrowed request until completion.
The BSP forwards the record to the transport worker as before. CPU 0 callers send
no self-IPI and reach forwarding through their scheduler when they block.

The periodic timer remains 120 Hz. Timed-wait expiry, sleeping tasks, other
BSP-only request queues and exit cleanup retain their existing scheduler/timer
service paths. Resource wakeups on the same CPU do not send a self-IPI; interrupt
return, the current task's yield/block/return or timer preemption reaches the
scheduler. This change adds no migration, priorities or tickless timers.

Blocking userspace syscalls and BSP kernel tasks use a wait record embedded in
task metadata. The resource publishes it under its own lock and removes it before
waking the task. The scheduler queue lock protects notification and parking.
An early wake is remembered; a task is only enqueued after execution has returned
to the permanent scheduler stack.
This prevents lost wakeups or resuming a stack still in use. The resource lock
may nest the queue lock; the reverse order is forbidden. Neither is held across
a context switch.

Parking a user task saves user CPU state just like timer preemption. Resume
restores the same CPU, process root and private entry stack, with interrupts
still disabled. The process remains alive while blocked. User mappings stay
stable except during an explicit private-memory loan after the task has left
its address space.
Only an explicit table-growth loan allows the BSP to modify its capability table.
A wake that precedes parking only records notification; it sends no IPI and
does not make a still-running task available to another context.

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
IF=0. A last object release normally queues its embedded retirement link without
touching the heap. Endpoint receipts instead release logical delivery ownership
synchronously under the endpoint lock; their reusable embedded objects never
enter the retirement list. The BSP scheduler drains this separate list after
task cleanup and runs destruction callbacks outside its lock. Pending objects
also cause a busy BSP task to return to the scheduler on its next timer
interrupt. No table grows on an AP; final releases never require an AP allocator
call.

Namespace creation uses a blocked caller's exclusive table loan for its fixed
binding storage and initial grant. Namespace lookup captures a reference and both
authority masks under its own lock, releases the lock, then installs the grant or
lends the table for growth. Replacement cannot alter a captured lookup. Namespace
locks never span allocation, endpoint locking or parking.

Endpoint creation lends the blocked task's table to the BSP, which allocates
its bounded delivery storage and installs both initial handles. Export creation
uses the same table loan for backing allocation and client installation. Export
control holds storage separately from client references, so it cannot prevent
natural retirement. Accepted deliveries retain their target until caller and
receipt ownership both end. A delivery slot becomes reusable as soon as the
caller has collected its outcome, if any, and the final receipt reference is
released. Reuse does not depend on BSP scheduling. Queued cancellation releases
the unpublished receipt under the already-held endpoint lock; delivered work
keeps its receipt until the provider finishes it or exits.

Endpoint objects, exports and live receipts each retain endpoint backing
storage. Its final owner queues a separate backing object after dropping the
endpoint lock, so a concurrent BSP reaper cannot free the lock before unlock.
Final client/backing destruction runs on the BSP. A caller reserves four free
slots before admitting a request so collecting reply grants needs no growth.
RECEIVE needs one slot for the receipt and one per request
attachment. If those slots are unavailable, the task queues a capability-growth
request and blocks outside the endpoint lock. Its metadata contains the queue
link, completion result and wait record, so submitting work never allocates on
an AP. Publication lends exclusive table ownership to the BSP; the submitting
task must not access the table again until completion. The BSP may complete
before the task finishes parking, using the same early-wake handling as endpoint
calls. After waking, RECEIVE rechecks the endpoint queue before installing
handles atomically and consuming a delivery.

Endpoint CALL deadlines use timed waits. On resumption the caller checks the
delivery under the endpoint lock and detaches its published waiter before
returning. RECEIVE and REPLY also check expiry under that lock, so late work
cannot win merely because the caller has not run yet. A delivered timeout keeps
its receipt storage while releasing the caller; its pending cancellation notice
uses that same record and existing recipient handle, without table growth or
additional queue capacity.

The BSP scheduler detaches a batch of requests under the queue lock, grows each
table with IF=0 outside the lock, and wakes its owner with the result. It does
not touch that task again after wake. Pending growth also makes a busy BSP task
return to its scheduler on the next timer interrupt. BSP userspace uses the
same path; there is no special AP allocator or generic work-item framework.

Kernel tasks share the BSP ready queue with any BSP userspace. Create one with
`kernel_task_create(entry, argument)` after scheduler initialization, on the BSP
with interrupts disabled. Its entry runs on a private stack in the kernel
address space with interrupts enabled. The timer can preempt it; returning from
entry retires its stack and metadata through the same scheduler cleanup path.
The argument is borrowed, so its owner must keep it alive until entry returns.

Kernel tasks can also use `task_wait_prepare`, `task_wait_sleep` and
`task_wait_sleep_until`. Save and disable interrupts before entering this
sequence; prepare/sleep are task-context calls, never interrupt-handler calls.
Under the resource lock, check for work and publish the prepared wait record if
there is none. Release every lock before sleeping. A wake before sleep returns
immediately; a wake during the context switch is remembered until the scheduler
has saved the stack. A parked task goes back onto its owning CPU's normal ready
queue. Wake never switches into the task directly and can run from interrupt
entry or another CPU with IF=0.

Sleep resumes with IF=0. Recheck the resource condition under its lock: a wake
does not reserve work for the waiter. With a deadline, remove any remaining
resource wait pointer under that lock before reusing the record; expiry can race
with a resource wake. Restore the saved interrupt state after that detachment.
One task has one wait record, and all published references must be detached before
its entry returns. The resource owns the pending-work condition; the wait record
only remembers notifications for this wait and is not a persistent event counter.

Kernel waits save only the kernel context and resume in the shared kernel root;
they do not save or restore user FP/segment state. Timed event waits use the
existing BSP expiry path, including timer preemption of a busy task. The private
memory, capability-growth and other process-service helpers remain user-only.

`kernel_task_sleep_until(deadline)` suspends the current kernel task until an
absolute monotonic nanosecond deadline. A past deadline yields to ready tasks.
Sleeping tasks are checked both by the BSP scheduler and by timer preemption,
so a busy task cannot prevent a sleeper from becoming runnable. Sleep requires
interrupts enabled and no held locks. The [task header](../include/kernel/task.h)
defines the calling contracts.

`kernel_task_yield_if_runnable()` requires the same IF=1/no-lock calling context.
It checks the BSP ready queue under the scheduler lock, then yields if another
task is runnable; otherwise it returns. It does not make sleeping tasks runnable
or replace timer handling of deadlines and unmigrated service queues.

The [BSP request executor](wip/bsp-service-requests.md) is created immediately
after `task_init()`, before user tasks are published. Creation failure is fatal.
It currently services pipe creation; other subsystem queues retain their existing
scheduler paths. The executor runs one FIFO operation with IF=0, enables
interrupts, and conditionally yields between operations. An individual operation
remains non-preemptible. Only the scheduler inspects ready queues.

When its queue is empty, the executor publishes an untimed wait under the request
lock. The first subsequent publisher detaches that wait and wakes it after
unlocking; later publishers need no additional wake while it is already notified
or servicing work. An early wake is remembered through the normal parking
handshake. A parked worker becomes runnable, with an IPI only for a remote
publisher. There is no polling, self-IPI or scheduler sweep of this request queue.
The executor always finishes a published wait before reusing its wait record.
Synchronous BSP request clients remain user-only, so the executor cannot submit
to itself and wait. Its subsystem operations use local helpers.

Framebuffer presentation runs as a BSP kernel task. It copies the active
space on a roughly 60 Hz monotonic deadline schedule, skipping missed frames.
Rendering stays out of interrupt entry; APIC interrupts still bound wakeup
latency. The scheduler does not know about display timing. See
[timekeeping](timekeeping.md) for the clock and deadline contracts.

## Memory and output boundaries

Allocators, VM metadata, page-table mutation and the two scratch mappings remain
BSP-only and require interrupts disabled. A kernel task must save/disable
interrupts around these calls and restore them afterward; being pinned to the
BSP alone does not prevent same-CPU reentry. AP syscalls may access their
capabilities and block on endpoints, but cannot allocate memory.

User-buffer checks are a narrow exception to BSP-only queries: the executing
CPU can inspect its active private root through recursive mappings, with IF=0
and stable user mappings. They do not use shared scratch slots or VM metadata.
General `vm_query()` and page-table mutation remain BSP-only.

Private spaces are built before publication and reclaimed only after retirement.
The [memory service](memory.md) can also borrow a parked task's inactive space:
its scheduler publishes the request only after leaving the task stack and
reloading the kernel root. The BSP changes private mappings before waking the
owner; normal resumption reloads CR3 before any task access. No other CPU uses
that private root during the loan.
Kernel code, CPU records, scheduler stacks, heap pools and framebuffer mappings
remain mapped throughout AP execution. A shared kernel range must not be
unmapped, remapped or protected while another CPU can use it.

Task activation reloads CR3 before accessing a newly published task stack. Task
retirement reloads it after leaving that stack, before the BSP can reclaim the
backing. With PCID and global pages disabled, these reloads invalidate the local
translations, including those of reused kernel-stack ranges. Other CPUs never
access that task's stack or private mappings. Endpoint messages and wait records
use heap storage, whose mappings remain backed even after freeing the object.
This is a restricted ownership protocol, not general cross-CPU TLB invalidation;
mutable shared mappings and concurrent allocator calls still require additional
synchronization.

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
