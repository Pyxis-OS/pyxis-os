# Scheduling on multiple CPUs

`make run CPUS=4` boots one QEMU socket with four cores and one thread per core.
`CPUS` is the total logical CPU count and defaults to one. `THREADS` defaults
to one and must divide `CPUS`; `make run CPUS=8 THREADS=2` creates four cores
with two threads each. Both settings also apply to debug and USB launches. With multiple CPUs,
CPU 0 (the BSP) services allocation and cleanup. Boot init creates the configured
[spaces](../userland/init.md) independently of the CPU count. User tasks run on
any CPU their space allows, including the BSP, which also runs the kernel
workers. A single-CPU boot runs everything on the BSP. Placement and balancing
are described below; on four CPUs the default space inits start on CPU 1, since
each earlier init is blocked when the next space is created, and balancing
spreads runnable work.
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

PIT calibration is serialized by this startup order. APs do not print startup
messages. The normal log has one line with the online CPU count; with
`LOG_LEVEL=trace`, the BSP also reports each AP's APIC ID, stack top and timer
count.
After all handoffs, the adapter removes its temporary mappings and drops its
startup references. No CPU continues to depend on Limine's stacks, page tables
or response pointers. The original bootloader frames remain reserved.

### Core topology

Each CPU samples CPUID after its local APIC is enabled, before publishing online
state. With `LOG_LEVEL=trace`, the BSP logs its index, APIC ID and core key, and
logs AP records after acquiring their online state. These records are immutable
before scheduling.

A valid SMT level in CPUID leaf `0xB`, subleaf zero, supplies the low APIC thread
bit width. Shifting the full APIC ID by that width retains package identity in
the core key. The implementation follows the [Intel SDM CPUID definition](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).

Without that leaf, AMD Zen-family CPUs (`AuthenticAMD`, family >= `0x17`) with
TopologyExtensions use leaf `0x8000001E`'s full APIC ID and threads-per-core count.
The fallback accepts power-of-two counts to derive an integral SMT bit width.
Older AMD fields describe compute units instead; see the
[Zen OSRR](https://www.amd.com/content/dam/amd/en/documents/processor-tech-docs/programmer-references/56255_OSRR.pdf)
(pp. 36, 74–75) and [Family 15h BKDG](https://www.amd.com/content/dam/amd/en/documents/archived-tech-docs/programmer-references/42301_15h_Mod_00h-0Fh_BKDG.pdf).

Two CPUs share a core only when both records are known and their width/key pairs
match. Missing, unsupported or mismatched topology is logged as unknown and
isolated to that CPU. Core keys may have gaps and are distinct from dense Pyxis
CPU indices. No ACPI or public ABI is involved.

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
Its permanent boot stack becomes its scheduler stack.

The BSP loads a private image and user stack, wraps that address space in a
process belonging to its space, then submits the process using:

```c
enum mm_result result = user_task_create(process, entry, stack_top);
```

Submission may occur before or after scheduling starts, but only on the BSP
with IF=0 and outside interrupt/fault entry. Success transfers sole ownership of
the process to the task; failure leaves it with the caller. Do not inspect or
mutate the process or its address space after transfer. There is one task per
process.

### Placement and migration

Each CPU's load is its queued tasks plus one while a task occupies it, kept under
the queue lock. A user task may run on any CPU in its space's effective set,
the BSP included. Kernel workers queued on the BSP count toward its load. There
are no priorities or other scoring.

Each space keeps two CPU sets:

- **Ceiling:** fixed when boot init creates the space, from its configured
  `cpus`.
- **Effective set:** starts as the ceiling, and only trusted init's
  [affinity setup](../userland/init.md#affinity-setup) narrows it, before the
  space's first launch.

The effective set and the setup flag change only under the queue lock. Once
setup closes, which happens before any task other than init exists in the space,
the set is fixed. If an affinity request excludes the caller's CPU, the caller
switches out in `task_syscall_leave()` after saving its user state. Requeueing
then always moves a task off a CPU its space no longer allows, and the syscall
return completes on the new CPU from the task's own kernel stack.

- **Publication** places each new task on the least-loaded CPU it may use. Ties
  first prefer a CPU whose other SMT siblings all have zero load, then the
  launching parent's CPU, the lowest AP index, and the BSP. Sibling loads include
  CPUs outside the space's allowed set; the selected CPU still must be allowed.
  Members of a batch
  are placed one at a time, each seeing those already queued, so a pipeline
  spreads across idle CPUs. The parent's CPU is a tie-break only; the parent is
  blocked in its launch syscall and cannot move meanwhile.
- **Preemption** of a user task in user mode requeues it on another CPU when
  that CPU's load is at least two below the local one. The threshold stops equal
  neighbours trading a task every tick. The destination's reschedule IPI is its
  only notification.
- **An idle CPU**, the BSP included, pulls the first movable task it may run
  from the busiest other queue, provided that queue's CPU keeps a task. The BSP
  pulls only while none of its kernel workers is runnable. Idle CPUs keep their
  120 Hz timer, so they retry every tick without extra IPIs.

Topology breaks ties only; it does not override logical CPU load, the push
threshold or pulling. Initial publication can spread four compute tasks over
four cores, then a transient queued task can make the existing push consolidate
compute onto siblings. Equal logical loads do not trigger a corrective move.
The [core-placement measurements](../development/experiments/core-placement/README.md)
record both initial placement and this observed limit; native ThinkPad timing
confirmation remains owner-run.

Only a user task outside a syscall moves. It is either new or was preempted in
user mode, so its whole continuation is on its own kernel stack. Dispatch on the
destination reloads its CR3, TSS stack, syscall stack, FP state and both GS
bases. A task that blocks in a syscall, including a deferred BSP request, keeps
its CPU and resumes there; it becomes movable again after its next user-mode
preemption. Parked, executing and retired tasks are never moved, and kernel tasks
stay on the BSP. `cpu_index` changes only under the queue lock, which also orders
wake and stop notification.

Batch launch splits submission into BSP-only `user_task_prepare()` and
`user_task_publish_group()`. Preparation allocates and initializes each task,
kernel stack, reusable request area and separate profiling storage without
enqueueing it; its process remains inactive and owned by the preparer.
`user_task_discard_prepared()` releases those task allocations, leaving process
destruction to the preparer. After execution-group admission succeeds, publication enqueues the complete
batch under the queue lock and transfers every process and task together.
Members may target different CPUs' queues. Publication allocates nothing; after
unlocking, the BSP retains only the distinct destination CPU indices and notifies
each remote one once. All observer handles and result slots exist before this
transfer. The blocked caller lends its capability table to BSP preparation;
cross-CPU requests and results live in the shared request allocation, never remote
stack pointers.

Task metadata occupies 784 bytes. Each user task eagerly owns one 4,928-byte
request allocation, sized for the explicit service catalog's largest typed
record, HOST, with an 8-byte alignment requirement, and a separate zeroed
720-byte profiling allocation. Their combined size is 6,432 bytes, excluding heap
overhead and the unchanged 16 KiB kernel stack. Kernel workers use only the 784-byte task metadata
and allocate neither user area. Preparation failure, prepared-task discard and
retirement release each owned allocation exactly once on the BSP with IF=0.

`process_create(owner, address_space, &process)` takes ownership of an inactive
private address space only on success. `process_destroy(process)` releases an
unsubmitted process and its address space if subsequent setup fails. Neither
call owns or destroys the containing space or its TTY. Before submission, call
`process_prepare_startup()` with named bindings to installed handles, arguments
and environment; omit absent resources. The startup region belongs to the
process's address space, with read-only metadata and writable argument pages.
Entry receives its address in RDI. See the
[startup interface](../../include/kernel/user/startup.h) and
[process interface](../../include/kernel/process.h) for ownership contracts.

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

Display requests publish to the common BSP request FIFO only once their caller
has left its private root and task stack, cleared entry/current state and become
parked. Private-memory operations no longer use the FIFO; they run in the
caller's syscall ([memory](memory.md#execution)). Publication detaches an idle executor's waiter under the
request lock and wakes it after unlocking. The ordinary ready queue and existing
rescheduling IPI make the worker runnable on the BSP. A BSP caller sends no
self-IPI and uses the same deferred handoff. Scheduler/preemption code has no
display queue checks. No remote allocation or new interrupt handler is added.

Capability growth, namespace creation, endpoint creation/export, RAMFS entry/name
allocation and discard, and RAM FILE backing replacement publish to that same
FIFO before the caller sleeps. Their typed records use the caller's reusable
shared request allocation.
Capability/endpoint operations lend the capability table exclusively until
completion; endpoint creation also lends the process's receiver owner list. FILE
replacement lends exclusive operation ownership while `busy` remains set; RAMFS
discard transfers the detached entry. The caller stops accessing the loan and
request at publication and waits through its saved wait pointer. Completion may
arrive before parking, but only records notification and cannot enqueue a
still-running caller. These operations change no private mappings and require no
VM handoff. They use the executor's ordinary wake and ready-queue path, including
for BSP userspace; scheduling and preemption inspect none of their request queues.

HOST requests use the same FIFO and synchronized idle-executor notification.
The caller saves its wait pointer before publication and does not inspect the
borrowed record until final completion. The executor forwards it to the existing
transport worker, which completes it through the common wakeup path. BSP callers
use the same executor path and send no self-IPI.

Nominal preemption remains 120 Hz; each local timer also targets its sleeping
tasks' deadlines. Exit cleanup retains its scheduler service path. Resource wakeups on
the same CPU do not send a self-IPI; interrupt return, the current task's
yield/block/return or timer preemption reaches the scheduler. This change adds no
migration or priorities; preemption remains active when idle. See
[deadline timer ownership](timekeeping.md#scheduler-timing).

Blocking userspace syscalls and BSP kernel tasks use a wait record embedded in
task metadata. The resource publishes it under its own lock and removes it before
waking the task. File, process, console and pipe resources share a separate
16-byte link in task metadata; they detach it before reuse, including after a
timed wait. It never overlaps service request storage. The scheduler queue lock
protects notification and parking.
An early wake is remembered; a task is only enqueued after execution has returned
to the permanent scheduler stack.
This prevents lost wakeups or resuming a stack still in use. The resource lock
may nest the queue lock; the reverse order is forbidden. Neither is held across
a context switch.

Execution-group stop requests mark task metadata under the group and scheduler
locks, remove timed membership for an interruptible wait, and notify the assigned
CPU. They leave subsystem registration detachment to the resumed continuation.
The scheduler retires marked runnable userspace at its next safe point. Syscall
continuations stay runnable until their own unwind returns all loans; their final
syscall boundary prevents user return. Published BSP/HOST requests remain
uninterruptible. See [execution groups](../interfaces/execution-groups.md).

Parking a user task saves user CPU state just like timer preemption. A parked
syscall resumes on the same CPU, restoring its process root and private entry
stack, with interrupts still disabled. The process remains alive while blocked. User mappings stay
stable except during an explicit private-memory loan after the task has left
its address space.
Only an explicit capability-table loan allows the BSP to modify its table.
A wake that precedes parking only records notification; it sends no IPI and
does not make a still-running task available to another context.

Exit and ordinary user faults return to the local scheduler. After switching
to its permanent stack and reloading the kernel root, the CPU clears its task
entry-stack pointer and publishes completion. It must not touch the task afterward.
The BSP detaches completed tasks under the lock, destroys each user process and
its private address space, then frees the task's kernel stack, request and profile
allocations and metadata outside the lock. Kernel tasks have no process or user
request/profile allocations. If the BSP itself runs userspace,
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

Namespace creation uses the caller's exclusive table loan for its fixed
binding storage and initial grant. Namespace lookup captures a reference and both
authority masks under its own lock, releases the lock, then installs the grant or
lends the table for growth. Replacement cannot alter a captured lookup. Namespace
locks never span allocation, endpoint locking or parking.

Endpoint creation lends the calling task's table to the BSP, which allocates
its bounded delivery storage and installs both initial handles. Export creation
uses the same table loan for backing allocation and client installation. Its owned
receiver and authority remain live throughout the loan. The BSP helpers grow the
loaned table directly when needed; they never submit a nested request or wait on
the executor. Export control holds storage separately from client references,
so it cannot prevent natural retirement. Accepted deliveries retain their target until caller and
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
attachment. If those slots are unavailable, the task submits a capability-growth
request to the common FIFO outside the endpoint lock. Submission never allocates
on an AP. After completion, RECEIVE rechecks the endpoint queue before installing
handles atomically and consuming a delivery; growth failure leaves it queued.
COPY's source slot and the invoking syscall's handle keep their objects alive
across growth, without retaining pointers into replaceable table storage. Namespace
lookup separately retains its captured client and authority masks through growth.

Endpoint CALL deadlines use timed waits. On resumption the caller checks the
delivery under the endpoint lock and detaches its published waiter before
returning. RECEIVE and REPLY also check expiry under that lock, so late work
cannot win merely because the caller has not run yet. A delivered timeout keeps
its receipt storage while releasing the caller; its pending cancellation notice
uses that same record and existing recipient handle, without table growth or
additional queue capacity.

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
local deadline expiry path, including timer interrupts of a busy task. The private
memory, capability-growth and other process-service helpers remain user-only.

`kernel_task_sleep_until(deadline)` suspends the current kernel task until an
absolute monotonic nanosecond deadline. A past deadline yields to ready tasks.
Sleeping tasks are checked both by the BSP scheduler and by local deadline interrupts,
so a busy task cannot prevent a sleeper from becoming runnable. Sleep requires
interrupts enabled and no held locks. The [task header](../../include/kernel/task.h)
defines the calling contracts.

`kernel_task_yield_if_runnable()` requires the same IF=1/no-lock calling context.
It checks the BSP ready queue under the scheduler lock, then yields if another
task is runnable; otherwise it returns. It does not make sleeping tasks runnable
or replace timer handling of deadlines and task retirement. Empty timed-wait and
kernel-sleeper lists skip expiry clock reads; local timer programming still reads
the clock to preserve the nominal preemption phase.

The [BSP request executor](bsp-service-requests.md) is created immediately
after `task_init()`, before user tasks are published. Creation failure is fatal.
It currently services pipe creation, private memory, display, screen capture,
capability growth, namespace creation, endpoint creation/export, RAMFS entry/name
allocation and discard, RAM FILE backing replacement, launch preparation/publication and HOST
forwarding. Preparation zeroes the selected typed record in the caller's reusable
request area. One reservation spans preparation, publication, completion and
result consumption; it does not allocate. Task adapters expose reservation,
current request/profile storage and deferred handoff. Subsystems own their
operation capture, service helpers and profiling controls; persistent aggregates
remain caller-only in the separate profile allocation.

The executor runs one FIFO operation with IF=0, enables interrupts,
and conditionally yields between operations. An individual operation
remains non-preemptible. Only the scheduler inspects ready queues.

When its queue is empty, the executor publishes an untimed wait under the request
lock. The first subsequent publisher detaches that wait and wakes it after
unlocking; later publishers need no additional wake while it is already notified
or servicing work. An early wake is remembered through the normal parking
handshake. A parked worker becomes runnable, with an IPI only for a remote
publisher. There is no polling, self-IPI or scheduler sweep of this request queue.
Local operations write their result and publish completion before waking the
caller. HOST forwarding instead transfers the request to its existing transport
worker in FORWARDED state. The executor makes no further request access and
continues its normal scheduling boundary; only HOST completion paths publish the
final result, including immediate unavailability and initialization failure.
Completion makes no further access to the request or loaned state. The caller
consumes the result before releasing its reservation, including staged HOST user
copies and ownership transfer of returned objects or image captures. Only then
may another service reuse the storage; retirement asserts no reservation remains.
The executor always finishes a published wait before reusing its wait record.
Synchronous BSP request clients remain user-only, so the executor cannot submit
to itself and wait. Its subsystem operations use local helpers; RAMFS allocation
and disposal and FILE buffer replacement helpers are static to their subsystems.
Launch preparation calls BSP-local loading, capability installation and task
preparation directly; it does not submit nested service requests.

Framebuffer presentation runs as a BSP kernel task. It copies the active
space on a roughly 60 Hz monotonic deadline schedule, skipping missed frames.
Rendering stays out of interrupt entry; APIC interrupts still bound wakeup
latency. The scheduler does not know about display timing. See
[timekeeping](timekeeping.md) for the clock and deadline contracts.

[Screen capture](../interfaces/screen-capture.md) uses deferred publication after
the caller leaves its stack and private root, lending its capability table until
completion. The executor forwards admission to the presenter without waiting for
a frame. The presenter copies with IF=1, while pending/active ownership changes,
allocation and READ-only FILE installation remain BSP/IF=0 work outside the output
lock. It clears all presenter references and the loan before waking the caller.

## Memory and output boundaries

VM metadata and page-table mutation remain BSP-only and require interrupts
disabled, with one exception: a process's private memory operations change its
own active address space in its syscall, on any CPU
([memory](memory.md#execution)). The general kernel VM area asserts BSP
ownership: its ranges are reused, and changing a mapping another CPU may hold
requires explicit quiescence and acknowledged TLB invalidation. Ordinary VM
mutation invalidates only the caller's translations. A kernel task must
save/disable interrupts around these calls and restore them afterward; being pinned to the BSP alone does
not prevent same-CPU reentry. Other heap allocation by syscalls, such as
capability growth or RAM-file backing, still goes through BSP requests.

Three lower layers are safe on any CPU with interrupts disabled,
outside interrupt and fault entry:

- **Kernel heap.** `kmalloc()` and `kfree()` hold a short heap lock around
  TLSF and its counters. Pools live in a 256 GiB arena after the general kernel
  VM area, in the same shared PML4 slot, and are never removed.
  - **Growth.** A miss drops the heap lock and takes a separate growth lock.
    It then retries, because another CPU may have grown the heap meanwhile.
    Only then does it map zeroed frames at arena addresses that were never
    mapped before. It publishes the pool and allocates the request in one
    heap-lock section.
  - **No shootdown.** No other CPU can hold a translation for an address that
    was never mapped, so publication needs none.
  - **Failure.** Growth first compares the pool's pages, plus an allowance
    for new page tables, with the free frames. A growth that cannot fit is
    refused before mapping anything; it retires nothing and sends a
    memory-pressure notice. A growth that still fails, because another CPU
    took frames meanwhile, unmaps locally and retires only the pages it
    mapped; the never-mapped remainder stays usable. A used-up arena is
    NO_MEMORY. `heap_get_stats()` reports arena use and retired bytes.

- **Physical allocator.** `pmm_alloc()`, `pmm_free()` and `pmm_get_stats()`
  serialize the frame bitmap and its counters with one short lock. The search
  is first fit over 64-bit bitmap words, skipping fully allocated words and
  starting from a hint below which every frame is unavailable. Frames come back
  unzeroed; the caller zeroes them outside the lock. Memory-pressure
  notification can also come from any CPU.
- **Scratch mappings.** Each CPU index owns two slots in the 2 MiB window at
  `TEMP_MAP_BASE`, enough for all 256 xAPIC IDs. Every slot's page-table
  ancestors exist before any private root copies the kernel slots. Only the
  owning CPU maps its slots, with a local `invlpg`; IF=0 keeps it on that CPU
  until it unmaps. Frame zeroing and page-table walks use the calling CPU's
  slots.

Spinlocks (`include/kernel/spinlock.h`) are held with IF=0 and never across
logging, a context switch or waiting for another CPU. The heap and PMM locks
are leaves; allocation notifies memory pressure only after releasing the PMM
lock. Only the heap growth lock is held across allocation, so it comes first
in the full order:

1. Heap growth lock.
2. Either the PMM lock or the heap lock, each released before the next is
   taken; growth takes the PMM lock while mapping and the heap lock to publish.
3. The memory-pressure lock, reached from growth through a failed or low
   frame allocation, or a refused growth.
4. The queue lock, taken by the pressure lock's wake and by resource and group
   locks.

No caller of `kmalloc()` or `pmm_alloc()` may hold the queue or pressure lock.

User-buffer checks are a narrow exception to BSP-only queries: the executing
CPU can inspect its active private root through recursive mappings, with IF=0
and stable user mappings. They use no scratch slots or VM metadata.
General `vm_query()` and page-table mutation remain BSP-only.

Private spaces are built before publication and reclaimed only after retirement.
The [display](../interfaces/graphics.md) service can borrow a parked task's
inactive space: its scheduler publishes the request only after leaving the task
stack and reloading the kernel root and clearing entry/current state. The BSP executor
changes private mappings before waking the owner; normal resumption reloads CR3 before any task access. No other CPU uses
that private root during the loan, and the borrowing task keeps its CPU because it
is inside its syscall.
Kernel code, CPU records, scheduler stacks, heap pools and direct boot/Bochs
framebuffer mappings remain mapped throughout AP execution. Resizable TTY backing follows
the retirement protocol below. A shared kernel range must not be
unmapped, remapped or protected while another CPU can use it.

Task activation reloads CR3 before accessing a newly published task stack. Task
retirement reloads it after leaving that stack, before the BSP can reclaim the
backing. With PCID and global pages disabled, these reloads invalidate the local
translations, including those of reused kernel-stack ranges. A task executes on
one CPU at a time; after a move, the previous CPU has already left the task's
stack and dropped its private root, and the destination reloads CR3 before using
them. Endpoint messages and wait records
use heap storage, whose mappings remain backed even after freeing the object.
This task-stack protocol does not permit arbitrary shared mapping changes.

`vm_kernel_flush_remote()` permits a separate quiescent retirement. The caller
must first end every remote access and prevent refills, including through cached
pointers, until unmapping finishes. It runs in a BSP kernel task with IF=1,
the kernel root active, and no output, queue or allocator locks held. The helper
keeps page tables, frames and VM ownership unchanged while requesting a new
flush generation from every online CPU. Each CPU reloads its current CR3,
including when running a private root, before acknowledging that generation.
With PCID and global pages disabled, this removes shared kernel translations.
The IPI handler takes no locks, allocates nothing, logs nothing and performs no
device waits. CPU membership stays fixed after boot.

The implementation uses one one-second deadline for IPI dispatch and all
acknowledgements; this timeout is not a public ABI promise. Success permits
ordinary BSP `vm_free()` with IF=0 while refills remain prohibited. Failure
requires keeping the original mappings and backing until reboot; a late
acknowledgement does not authorize reclamation. This is not concurrent shared
unmapping or permission mutation: the flush itself changes no mappings.

Live display resize uses the output lock to finish old AP TTY writes and replace
all pixel pointers and dimensions. No AP may keep an old pixel pointer beyond
that lock. The sole presenter starts resize between frame leases, so it also
relinquishes old TTY backing before flushing. Old buffers remain mapped through
the acknowledgement wait. A timeout retains one old TTY/navigation/cursor batch
and disables further resize while leaving the committed geometry usable. GPU
targets and control buffers remain BSP-only and additionally need confirmed
device ownership release before reclamation.

Normal log calls save/disable interrupts while serializing serial and framebuffer
output per format invocation, then restore the caller's interrupt state.
Single-byte syscalls from different programs can still interleave their text.
Fatal kernel diagnostics switch to unlocked serial-only output so an exception
in the lock owner cannot deadlock reporting. Before the presenter's first frame,
the first panicking CPU also draws on the
[early console](early-console.md#panic-ownership). Such output may interleave, and a
kernel panic still halts only the faulting CPU. Shared TTY mutation still needs
serialization; the low-level log lock interface requires interrupts disabled.

Power-off and restart hold all user execution ([ACPI](acpi.md#power-off-and-restart)).
The hold is set and released under the queue lock. A user task parks instead of
running user code at its next syscall return, user-mode timer preemption or
dispatch from a ready queue; parked tasks sit on a held list outside every queue.
Release requeues them through the normal placement path.

Kernel tasks stay on the BSP. Shared user address spaces and kernel-task fault
recovery are not supported; a kernel-task fault is fatal.

## Spaces and fairness

Spaces exist independently of the CPU count: boot init creates the configured
spaces in registry order, after Caelum ([init](../userland/init.md)). Creation
runs as a BSP request, appends to the registry and never removes a space. A space's
ceiling and effective CPU set constrain where its tasks may run; they reserve no
CPU and are not budgets. Each CPU runs its queue round-robin, so a space with
more runnable tasks receives more CPU time. There are no priorities, per-space
quotas or memory limits, and CPU sets do not protect against exhaustion of
shared services.

## Remaining serial services and limits

These stay on the BSP:

- the request executor and every service in its catalog: pipe and terminal
  creation, capability growth, namespace creation, endpoint creation and export,
  RAMFS entries, RAM-file replacement, launch preparation, display, HOST and
  native filesystem admission, readiness waits and system-info memory;
- the network, native filesystem, HOST transport, virtio-blk, USB, ACPI and
  presentation workers;
- task reaping, object retirement and the general kernel VM;
- device interrupt routing.

Private memory, the heap, the PMM and the scratch mappings work on any CPU.
Moving a worker off the BSP is a later step; see the
[follow-ups](../wip/scheduling-and-threads.md#serial-services-off-the-bsp).

Accepted limits are recorded in [technical debt](../technical-debt.md):

- [BSP userspace and kernel workers](../technical-debt.md#bsp-userspace-and-kernel-workers)
- [scratch-slot false sharing](../technical-debt.md#scratch-slot-false-sharing)
- [PMM first-fit search](../technical-debt.md#pmm-first-fit-search-under-its-lock)
- [the never-reused heap arena](../technical-debt.md#never-reused-kernel-heap-arena)
- [BSP-only allocation and VM mutation](../technical-debt.md#bsp-only-allocation-and-vm-mutation)

Placement is not topology-aware: SMT siblings count as separate CPUs.

## Measurements

The runtime SMP milestone recorded matched results at each step. All are
nested-VM runs unless marked native:

| Record | Covers |
| --- | --- |
| [Task-1 baseline](../development/experiments/smp-task1-baseline/README.md) | Pre-milestone baseline, heap growth |
| [Task 4a](../development/experiments/smp-task4a/README.md) | Placement, balancing, migration; native ThinkPad check |
| [Task 5](../development/experiments/smp-task5/README.md) | PMM lock, per-CPU scratch slots, stress |
| [Task 6](../development/experiments/smp-task6/README.md) | Concurrent heap growth, arena, stress |
| [Task 7a](../development/experiments/smp-task7a/README.md) | Local private memory |
| [Task-7 PMM](../development/experiments/smp-task7-pmm/README.md) | PMM word search, scratch false sharing |
| [Task 7b](../development/experiments/smp-task7b/README.md) | Userspace on the BSP |
| [Task 8](../development/experiments/smp-task8/README.md) | Final matched set, native ThinkPad check, lifetime scenarios |

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
BSP and the stopping conditions in [gdb.md](../development/gdb.md) for debugger-invoked allocator
and submission calls. Never call them on an AP. Once a task completes, its
pointer may already have been freed by the BSP.

Execution-group sealing serializes with batch enrollment/publication under a group
lock before the scheduler queue lock. BSP reaping detaches the stop-request list
link before freeing task metadata; its member count stays positive until process
and task reclamation finish. Group completion additionally waits for admitted
launches and attributed deferred cleanup. See [execution groups](../interfaces/execution-groups.md).
