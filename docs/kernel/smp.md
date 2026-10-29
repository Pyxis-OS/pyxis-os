# Scheduling on multiple CPUs

`make run CPUS=4` boots one QEMU socket with four cores and one thread per core.
`CPUS` is the total logical CPU count and defaults to one. `THREADS` defaults
to one and must divide `CPUS`; `make run CPUS=8 THREADS=2` creates four cores
with two threads each. Both settings also apply to debug and USB launches. With multiple CPUs,
Launch/image/startup and native thread-context preparation execute in retained
kernel-root continuations on the calling CPU, as do private MEMORY operations.
Each CPU has one fixed RAM reaper; device hooks and hardware workers retain
their existing fixed owners. Boot init creates the configured
[spaces](../userland/init.md) independently of the CPU count. User tasks run on
any CPU their space allows, including the BSP, which also runs the kernel
workers. A single-CPU boot runs everything on the BSP. Placement and balancing
are described below; on four CPUs the default space inits start on CPU 1, since
each earlier init is blocked when the next space is created, and balancing
spreads runnable work.
CPU indices are dense, stable for the boot, and distinct from hardware APIC IDs.

## Boot handoff

The Limine adapter always requests x2APIC when supported (owner decision,
2026-10-14); CPUs without x2APIC retain xAPIC MMIO. Before
replacing the BSP root it records the CPU count, BSP APIC ID and physical
location of Limine's CPU pointer array. The original direct-map offset is
retained only to decode pointers in that array, never for later dereferences.

After VM and heap initialization, the BSP temporarily maps the bootloader-owned
CPU records through the kernel VM allocator. It allocates each AP's CPU-local
record and two separate 16 KiB stack areas, for normal execution and double fault.
These allocations remain kernel-owned until shutdown. CPU records grow with the
reported count, bounded by the existing 256-CPU scratch window. This is a limit
on dense CPU indices, independent of the full 32-bit APIC IDs. Allocation failure,
excess CPUs and duplicate or reserved broadcast IDs fail boot explicitly.

APs start one at a time. The BSP publishes a handoff pointer and entry address
using the release ordering required by the pinned Limine protocol. A small
Limine assembly entry reads the handoff pointer before entering architecture
code. The handoff itself resides in the kernel image, visible under both roots.

The AP checks paging features and configures NX/WP before replacing CR3. It
switches directly from the boot root and stack to the owned kernel root and
its allocated stack, without a stack access between those changes. It then
installs its GDT/TSS and GS base, loads the shared IDT, initializes CPU features
and its local timer, applies its supported [CPU performance policy](cpu-power.md),
and publishes its online state with release ordering. The BSP applies the same
policy locally; no CPU writes another CPU's HWP request.
Only after acquiring that state may the BSP reuse the handoff for another AP.
The BSP's running APIC timer supplies a startup timeout of approximately five
seconds without requiring interrupts on the BSP.

The BSP sets the local APIC timer rate once, before AP startup, and APs reuse
it ([timekeeping](timekeeping.md)). APs do not print startup
messages. The console has one line with the online CPU count; debug records
from the BSP also report each AP's APIC ID, stack top and timer count.
After all handoffs, the adapter removes its temporary mappings and drops its
startup references. No CPU continues to depend on Limine's stacks, page tables
or response pointers. The original bootloader frames remain reserved.

### Local APIC and external destinations

The adapter passes the boot mode to architecture code before paging. The BSP
checks CPU support, the selected mode and IA32_APIC_BASE; every AP checks the
same hardware mode before initialization. A supported CPU whose bootloader
declines x2APIC fails explicitly, rather than silently weakening the accepted
preference. Caelum never switches APIC modes while running.

x2APIC accesses local registers through MSRs `0x800 + register_offset / 16`;
there is no local-APIC MMIO mapping. Timer calibration/arming, EOI and priority
keep their existing semantics. xAPIC retains its uncached MMIO mapping and
bounded ICR delivery polling. x2APIC writes the 64-bit ICR at MSR `0x830`, with
the full destination in bits 63:32 and no delivery-status polling. `MFENCE;
LFENCE` before that write orders queue, TLB and debugger publications before
the IPI, following the [Intel SDM x2APIC ordering rules](https://cdrdv2-public.intel.com/812386/253668-sdm-vol-3a.pdf)
and the [pinned Limine sequence](https://github.com/Limine-Bootloader/Limine/blob/34fe53c3d27d229ea25c28fc3e04db362543715b/common/sys/lapic.c#L424).

CPU records, topology and local IPI destinations retain 32-bit identities. Early
panic/NMI entry reads the active x2APIC ID MSR without GS or memory mappings;
xAPIC uses CPUID's eight-bit initial ID. Debugger NMI lookup matches the full
ID against prepared snapshots, published with release/acquire ordering, rather
than indexing by the hardware ID.

External IO-APIC and MSI/MSI-X still use eight-bit physical destinations to the
BSP. A target above 255 panics clearly before programming a truncated route.
There is no interrupt remapping; QEMU compatibility-message delivery does not
establish support for every native platform. MADT processor types are unchanged:
Limine supplies the CPUs; ACPI supplies the IO-APIC and override routes. See the
[x2APIC qualification record](../development/experiments/x2apic/README.md).

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

The preparation actor loads a private image and user stack, wraps that inactive
address space in a process belonging to its space, then submits it using:

```c
enum mm_result result = user_task_create(process, entry, stack_top);
```

Boot preparation uses the explicit BSP owner; runtime preparation uses the
retained caller in the kernel root, with IF=0 outside interrupt/fault entry. Success transfers the prepared
process lifetime and task storage to finalization; failure leaves the process
with the caller. Do not inspect or mutate the process or its address space after
transfer. Initially there is one task; native CREATE adds siblings in the same
address space and capability table. Each task borrows its process; the process
owns its lifetime, task set, terminal result and execution-group member record
independently of task storage. Native stack/FS reservations remain pinned through
retirement. See [threads](../interfaces/threads.md#lifecycle).

### Placement and migration

Each CPU's load is its queued tasks plus one while a task occupies it, kept under
the queue lock. A user task may run on any CPU in its space's effective set,
the BSP included. Kernel workers queued on the BSP count toward its load. There
are no priorities. Initial process/thread placement discounts only its executing
user preparation caller's one running contribution, including sibling-idleness
scoring. This transient scoring view does not change actual running/queued
accounting or idle stealing. Other actors and already-queued batch children count
normally; boot and fixed-worker publication have no discounted caller.

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
remain on their fixed CPUs. Device workers stay on the BSP; only the audited
RAM actors have AP wait/yield support. `cpu_index` changes only under the queue lock, which also orders
wake and stop notification.

Batch launch separates caller-owned `user_task_prepare()` from atomic
`user_task_publish_group()`. Preparation owns each inactive process, task stack,
request/profile storage and observer reservation until publication. The small
shared stack reservation uses bounded structural-owner admission; private image
backing/copying does not hold that global owner. Fixed space setup, device grants
and final reporting remain BSP phases around caller-owned child preparation.
Publication keeps the same group and queue guards, child placement and exact
reserved slots. It transfers tasks/processes together, then uses only saved
CPU indices to notify destinations. A refused unpublished batch transfers its
complete owned rollback record, captures, reservations, parent cleanup pin and
group attribution to the preparation CPU's RAM actor. No remote stack pointer
is lent to a worker.

Private image/startup copies use existing CPU-local frame aliases. Every alias
is removed before yielding. A selected external image has one owned read phase;
its producer cannot refill it after handoff. AP-read captures, and unread captures
whose cleanup moves away from the producer CPU, require confirmed shared-range
retirement before VA/backing reuse. A failed acknowledgement retains the whole
range and joined cleanup until reboot; a late acknowledgement cannot reclaim it.

The current task metadata is 840 bytes; process metadata is 432 bytes. Each user
task owns a 6,592-byte request area and 776-byte profiling area, plus its 16 KiB
kernel stack, excluding heap overhead. These artifact-derived sizes are tuning
and change with the service catalog. A fixed RAM actor adds one 16 KiB stack and
840-byte task record per CPU, without user request/profile areas. Its RAM-process,
prepared-launch, captured-range and task queue storage adds 88 bytes per CPU,
excluding allocation headers and page tables. All actors are mandatory and
prepared before runtime user admission; creation failure follows core-worker
boot failure rather than enabling a partial mode.

After permanent-stack/root departure, the scheduler records the departure CPU
before publishing completion. The BSP detaches one stop target and transfers its
whole task record to that CPU's RAM actor. Actual stack/request/profile/FP release
and stack/TLS unpin precede thread completion. The last departing task records
the process's immutable RAM-cleanup destination independently of later queue
ordering; a delayed cleanup-pin release uses it too.

Process cleanup keeps its existing order: BSP session hooks, assigned-CPU private
VM release, BSP post-VM endpoint/device hooks, then assigned-CPU allocation records
and capability/environment table walks. Bounded steps yield with no scratch alias
or short guard held. Process completion follows actual storage/VM/record cleanup;
execution-group completion additionally joins attributed deferred object/device
work. Process WAIT does not newly promise all asynchronous device callbacks have
finished. No work stealing, reaper migration, CPU0 fallback or retained frame
cache is added.

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
local APIC rescheduling IPI after releasing the queue lock. The sender retains only
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
parked. Native thread and private-memory operations instead use retained caller
kernel-root continuations under the same process VM mutation transaction
([memory](memory.md#execution)). Publication detaches an idle executor's waiter under the
request lock and wakes it after unlocking. The ordinary ready queue and existing
rescheduling IPI make the worker runnable on the BSP. A BSP caller sends no
self-IPI and uses the same deferred handoff. Scheduler/preemption code has no
display queue checks. No remote allocation or new interrupt handler is added.

Capability growth, process-environment staging/commit/capture, endpoint
creation/export and RAMFS entry/name allocation and discard publish to that same FIFO before the caller
sleeps. Their typed records use the caller's reusable
shared request allocation.
Capability operations use synchronized table access and exact destination claims
until completion; endpoint owner-list mutation remains BSP-owned while the
requesting task waits. FILE replacement lends operation ownership while `busy`
remains set; RAMFS discard transfers the detached entry. The caller stops accessing the loan and
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

Blocking userspace syscalls, BSP kernel tasks and fixed RAM actors use a wait
record embedded in task metadata. The resource publishes it under its own lock and removes it before
waking the task. File, process, console and pipe resources share a separate
16-byte link in task metadata; they detach it before reuse, including after a
timed wait. It never overlaps service request storage. The scheduler queue lock
protects notification and parking.
An early wake is remembered; a task is only enqueued after execution has returned
to the permanent scheduler stack.
This prevents lost wakeups or resuming a stack still in use. The resource lock
may nest the queue lock; the reverse order is forbidden. Neither is held across
a context switch.

Execution-group membership and process-control objects borrow process links,
protected by their respective locks. Stop targeting takes the control or group
lock, then the process lifetime lock, then a parking bucket (when registered),
then the scheduler queue lock. The process
keeps every task stop target live until the BSP detaches it before that task
is reclaimed. A stop request closes sibling admission, marks all tasks, removes
timed membership for interruptible waits, and notifies their assigned CPUs.
Ordinary interruptible waits leave subsystem detachment to the resumed
continuation. Address parking instead detaches its registration under its bucket
before notification; it never borrows that ordinary notify-first stop path.
The scheduler retires marked runnable userspace at its next safe point. Syscall
continuations stay runnable until their own unwind returns all loans; their final
syscall boundary prevents user return. Published BSP/HOST requests remain
uninterruptible. See [execution groups](../interfaces/execution-groups.md).

Native address waits compare/enrol under a process/address/width/incarnation
bucket while a short mapping lease protects validation. The lease ends before
sleep; only the scalar key and task-owned record remain. Wake, stop and drained
VM withdrawal unlink and commit one terminal result before scheduler notification.
Timer interrupt entry cannot take a bucket: it removes due timer membership and
queues cancellation on the local permanent scheduler stack, which detaches before
making the waiter runnable. A simultaneous peer wake preserves its first result;
that local cancellation list drains before the task can reuse its record.
Buckets may nest scheduler queues; the reverse order is forbidden. No bucket is
held across scheduling, allocation or a mapping-guard acquisition. See the
[native thread ABI](../../include/abi/thread.h) and [qualification](../development/experiments/threads-task6/README.md).

Parking a user task saves user CPU state just like timer preemption. A parked
syscall resumes on the same CPU, restoring its process root and private entry
stack, with interrupts still disabled. Process lifetime survives blocking, while
staged I/O holds no mapping borrower. Root admission observes the process VM gate;
final outputs revalidate under short leases rather than assuming old mappings.
BSP allocation/growth and admitted user installation share the short table guard;
no exclusive capability-table loan remains.

Admitted calls own object storage and copied rights/transport through their
continuations; they borrow no table entry across growth or blocking. The short
table guard covers acquisition, CLOSE detachment, slot claims and structural
publication; growth prepares backing on BSP outside it. CLOSE invokes logical effects
and releases its detached grant outside that guard. These storage references
drain during syscall cleanup before `task_syscall_leave` and retirement; they do
not prolong controller authority or pipe open directions. Grant-producing capture
additionally retains logical authority and rechecks the source generation/masks.
For a forwarder, source acquisition also pins the selected leaf under the
table-to-forwarder guard order, using atomic storage retention only. The
forwarder guard never acquires a table or subsystem guard; CUT detaches targets
and queued-receipt notifications before invoking logical release or endpoint
cancellation outside it. Endpoint guards may nest the forwarder guard for
operation election and record teardown, never the reverse. Controller grants
carry management lifetime; admitted and caretaker storage references do not.
Native BSP workers borrow the parked caller's operation context, while queued
provider messages and asynchronous completions own a cloned context. Every
before-effects election competes with CUT; already-started work retains its
real outcome. Launch observer derivation can allocate on the retained preparation
actor before publication; other fixed producers keep their existing owners.
Private VM mutations retain the existing quiescence transaction. See
[forwarders](../interfaces/forwarders.md).
Shared-table delivery, VM admission and native sibling entry are implemented;
ordinary thread-safe libc remains a [later thread gate](../interfaces/threads.md).

A wake that precedes parking only records notification; it sends no IPI and
does not make a still-running task available to another context.

Process exit, termination and ordinary user faults atomically elect the first
process-wide terminal result, taking no lifetime lock in fault entry. Later
events preserve it. Fault entry does no peer stop fanout; task reaping in the BSP
scheduler closes admission and requests sibling stop from its safe permanent stack. Native thread
EXIT records only that thread's word and leaves siblings running. Last ordinary
thread retirement supplies process EXITED/zero only if no earlier terminal
event exists.

After switching to its permanent stack and reloading the kernel root, the CPU
clears its task entry-stack pointer and publishes the completed task. It must
not touch the task afterward. An AP that publishes a completed task notifies the
BSP; without that, an idle BSP retired the process only at its next timer tick,
which added up to 8.33 ms to process exit. The BSP takes completed tasks under the queue lock,
then detaches each process member outside it. Only the last linked member closes
VM admission. It moves out the owned context pins and observer, frees kernel
stack/request/profile/metadata, releases the pins, then publishes the immutable
thread result. Completion retains neither process nor task pointers.

`process_task_reclaimed()` decrements retiring-storage accounting only after
that sequence. Final process teardown requires no linked tasks, retained task
storage or VM activity. The scheduler only marks the process RETIRED, detaches
its control and group links and queues it for the BSP process reaper, without
allocating. From then on TERMINATE finds no process and WAIT keeps waiting; the
group still counts the member. The reaper is a BSP kernel task. Under group
cleanup attribution held in its own task record, it ends device sessions, then
releases the private address space in steps of at most 256 backed pages, range
records or page-table entries with interrupts off, enabling interrupts and
yielding to runnable BSP work between steps. Only after the remaining resources
are released does it publish process control completion and complete the group
member, so WAIT still means the memory is free. The budget is tuning, not a
contract. Endpoint, capability-table and object release after the address space
still run as one interrupts-off step, as does object reaping in the scheduler.
Deferred cleanup can still delay group completion. Every admitted reference and operation
must unwind before task retirement. Kernel tasks have no process or user
request/profile allocations. A pending completion makes the BSP's next user
timer interrupt return to the scheduler even with no second runnable BSP task.

[Audio exit](../interfaces/audio.md#handoff-and-output-limits) clears the session
owner and invalidates its generation without allocation or sleeping. Queue
storage stays with the sole audio worker until it drains the cleanup notice.
For a grouped producer, a cleanup token keeps completion pending until that
storage is freed; the worker retains no retired process pointer. Queue consumption
and direct DMA publication share a bounded IF=0 phase, so exit cannot interleave
with the mix commit. Already mixed DMA belongs to the engine and may outlive the
producer.

Capability tables use admitted process lifetime and short IF=0 exclusion. The
BSP installs entries before submission; admitted user/service activity captures,
claims, installs or detaches entries under the same guard. A last object release
normally queues its embedded retirement link without touching the heap. Endpoint receipts instead release logical delivery ownership
synchronously under the endpoint lock; their reusable embedded objects never
enter the retirement list. The BSP scheduler drains this separate list after
task cleanup and runs destruction callbacks outside its lock. Pending objects
also cause a busy BSP task to return to the scheduler on its next timer
interrupt. No table grows on an AP; final releases never require an AP allocator
call.

Environment SET stages its cells on the BSP, fills them in the caller (text
copied from user memory, grants acquired with the requested attenuation) outside
any table lock, and publishes the whole batch with one BSP commit. LOOKUP retains
the selected cell under the table guard, reserves handle capacity, then installs a
validated grant; a later SET, UNSET or snapshot close cannot alter that capture.
The guard covers only pointer/counter access and cell retains and never spans
allocation, grant callbacks, user copies, endpoint locking or parking. See the
[environment reference](../interfaces/environment.md).

Endpoint creation preclaims both handles before BSP backing allocation and
atomic publication. Export creation carries an owned receiver snapshot and one
claim; it rechecks endpoint state before publishing. Reply claims live in the
shared delivery record, and successful REPLY moves its already-owned grants
without later allocation or retain. Growth requests never require a borrowed
exclusive table or nested executor wait.
Export control holds storage separately from client references,
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
across growth, without retaining pointers into replaceable table storage.
Environment LOOKUP separately retains its captured cell through growth.

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
It currently services pipe creation, display, screen capture and audio admission,
capability growth, process-environment staging/commit/capture, endpoint
creation/export, RAMFS entry/name allocation and discard, launch preparation/publication and HOST
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
caller. Forwarded operations transfer requests to their owning HOST, native
filesystem, readiness, ACPI, audio or presentation worker in FORWARDED state.
The executor makes no further request access and continues its normal scheduling
boundary; the owning completion path publishes the final result, including
immediate unavailability and initialization failure.
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

[Audio requests](../interfaces/audio.md) stage bounded PCM in reusable shared
request storage before submission. Their publication needs no private-root or
capability-table loan. The caller's uninterruptible reservation keeps its process
identity and grant alive until completion. The audio worker drains exit notices,
owns its request FIFO and services one request between refill checks. It enters
the request's cleanup attribution while servicing, restores the previous context
and clears borrowed pointers before completion, then makes no further request
access. Notifications are remembered across work and wait publication.
A running stream uses IRQ wakeups and a 5 ms watchdog; an idle, absent or failed
engine parks untimed while retaining request and cleanup service.

Framebuffer presentation runs as a BSP kernel task. It copies the active
space on a roughly 60 Hz monotonic deadline schedule, skipping missed frames.
Rendering stays out of interrupt entry; APIC interrupts still bound wakeup
latency. The scheduler does not know about display timing. See
[timekeeping](timekeeping.md) for the clock and deadline contracts.

[Screen capture](../interfaces/screen-capture.md) uses deferred publication after
the caller leaves its stack and private root. It reserves the returned FILE
slot before submission; the shared request owns this claim until completion. The
executor forwards admission to the presenter without waiting for a frame. The presenter copies with IF=1, while pending/active ownership changes,
allocation and READ-only FILE installation remain BSP/IF=0 work outside the output
lock. It clears all presenter references and unconsumed claims before waking the
caller.

## Memory and output boundaries

Private VM metadata and page-table mutation have one process owner, with IF=0
per step. MEMORY runs in a kernel-root continuation on its caller CPU; DISPLAY
and thread-context changes remain BSP clients of the same gate. All use
[process quiescence](memory.md#execution): the caller leaves its root before the
transaction begins. The general kernel VM area still
requires separate quiescence and acknowledged invalidation for reused ranges.
A kernel-root actor retains the CPU-neutral structural owner and disables
interrupts for each mutation step; pinning alone does not prevent reentry. RAM-file backing and the layers below remain usable on the
writing CPU under their existing contracts.

Selected-image snapshots from npfs, FAT32 and host files acquire that owner in
their sleep-safe worker context, with the existing one-second admission
budget, and release it before storage reads. RAM capture allocation also waits
before shared mutation. Caller-CPU child/batch preparation retains its private
VM actor proof; only shared task-stack and fixed space/device mutations acquire
the structural owner. That owner must be released before waiting for a different
fixed actor. Ordinary contention with the yielding reclaimer therefore waits
rather than failing an immediate owner try; an exhausted admission budget still
returns `BUSY`. The producer retains its image operation and grants until
admission or normal failure cleanup. Caller-owned captures are discarded by the
caller, with confirmed AP retirement before reuse.

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
  notification can also come from any CPU. VM backing uses caller-owned vectors
  of at most 64 single frames under one acquisition; no frames stay in an idle
  per-CPU cache. Batched free checks every bit before clearing any, and a
  different CPU may free the vector. Zeroing/mapping/pressure follow unlock.
- **Scratch mappings.** Each CPU index owns two slots in the 2 MiB window at
  `TEMP_MAP_BASE`, enough for 256 dense CPU indices. Every slot's page-table
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

Process stop targeting separately nests the control or group lock, process
lifetime lock, then queue lock. Detaching control/group process links happens
after releasing the process lifetime lock. None of these locks spans allocation,
user copying or a context switch.

No caller of `kmalloc()` or `pmm_alloc()` may hold the queue or pressure lock.

Active-root user-buffer checks remain separate: the executing CPU can inspect its active private root through recursive mappings, with IF=0
and stable user mappings. They use no scratch slots or VM metadata.
Private queries/mutation require stable ownership; MEMORY uses its drained
process token on the calling CPU. Shared queries use short guarded snapshots,
which are not resource leases. General kernel mutation requires a retained
CPU-neutral logical owner; it never holds a spinlock across allocation or waits.
Unpublished preparation, retirement and DISPLAY/thread clients still run on the
BSP, enrolling in that same owner rather than gaining ownership from CPU identity.
See the [shared-VM contract](../wip/vm-invalidation.md#decision-6--shared-ownership-queries-and-safe-admission).

Private spaces are built before publication. Dispatch reserves root activity
under the process VM guard before CR3 activation. Scheduler departure reloads the
kernel root on its permanent stack before acknowledging the reservation. Short
copy leases cannot cross a wait/departure. MEMORY and DISPLAY mapping mutations
close admission, permit old syscall continuations to drain, and require zero
roots/borrowers before effects. Mutator-only reply reentry remains closed to
ordinary dispatch until its final copy/cleanup. MEMORY drainage notifies its
caller directly; BSP clients retain the pending-list worker. Neither blocks the
BSP executor. Retirement reclaims only drained activity. Kernel-root user
continuations retain their task, CPU and FP/TLS ownership; they reserve no
private-root activity while executing or parked in that mode.
Native siblings share the process address space under these rules; an ordinary
launch still has one task. The kernel TLB-flush IPI is a separate contract.

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
pointers, until unmapping finishes. It runs in the retained structural owner's
kernel-root task on any CPU, with IF=1 and no output, queue or allocator locks held. The helper
keeps page tables, frames and VM ownership unchanged while requesting a new
flush generation from every online CPU. Each CPU reloads its current CR3,
including when running a private root, before acknowledging that generation.
With PCID and global pages disabled, this removes shared kernel translations.
The IPI handler takes no locks, allocates nothing, logs nothing and performs no
device waits. CPU membership stays fixed after boot.

The implementation uses one one-second deadline for IPI dispatch and all
acknowledgements; this timeout is not a public ABI promise. Success permits
owner-held `vm_free()` with IF=0 while refills remain prohibited. Failure
requires keeping the original mappings and backing until reboot; a late
acknowledgement does not authorize reclamation. This is not concurrent shared
unmapping or permission mutation: the flush itself changes no mappings.

Live display resize takes each TTY's output lock in turn to finish old AP writes
to its cells and replace its cell storage, raster and dimensions. APs write only
cells, never pixels, and keep no cell pointer beyond that lock. The sole
presenter starts resize between frame leases, so it also relinquishes old TTY
backing before flushing. Old buffers remain mapped through
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

Kernel workers have fixed owner CPUs: they never migrate or get stolen by another
CPU (owner, 2026-10-13). Placing one on an AP needs its own accepted IRQ, request
and lifetime ownership; shared kernel VM now has a CPU-neutral owner. Device
callbacks, object mutation, GPE methods and firmware actions stay on the BSP.
These stay on the BSP:

- the request executor and every service in its catalog: pipe and terminal
  creation, capability growth, process-environment mutation and capture,
  endpoint creation and export,
  RAMFS entries, launch preparation, display, audio, HOST and
  native filesystem admission, readiness waits and system-info memory;
- the network, native filesystem, HOST transport, virtio-blk, USB, ACPI, audio
  and presentation workers;
- task reaping, the process reaper and object retirement;
- device interrupt routing.

Physical allocation covers addresses below 64 GiB. Boot intersects every usable
region with that span for metadata sizing, metadata placement and free-frame
initialization, and warns once if usable RAM is excluded. RAM above the span is
unused, including RAM displaced by MMIO holes on a 64 GB machine. The heap
arena's existing capacity bound is unchanged; see the
[coverage debt](https://git.internal/PyxisOS/pyxis-os/issues/1335).

The heap, PMM, scratch mappings and RAM-file backing work on any CPU; private
MEMORY mutations run on the calling CPU under the process transaction.
Remaining DISPLAY/thread-context mutations and final process destruction retain
their BSP ownership.
Moving a worker off the BSP is a later step; see the
[follow-ups](../wip/scheduling-and-threads.md#serial-services-off-the-bsp).

Accepted limits are recorded in [technical debt](https://git.internal/PyxisOS/pyxis-os/issues?type=all&state=all&labels=4):

- [BSP userspace and kernel workers](https://git.internal/PyxisOS/pyxis-os/issues/1318)
- [scratch-slot false sharing](https://git.internal/PyxisOS/pyxis-os/issues/1314)
- [PMM first-fit search](https://git.internal/PyxisOS/pyxis-os/issues/1311)
- [the never-reused heap arena](https://git.internal/PyxisOS/pyxis-os/issues/1316)
- [BSP-only allocation and VM mutation](https://git.internal/PyxisOS/pyxis-os/issues/1310)

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
| [Task 7a](../development/experiments/smp-task7a/README.md) | Local private memory (superseded: private memory again runs through the BSP VM transaction) |
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
lock. Publication takes the process lifetime lock before enqueueing; stop targeting
nests group, process lifetime and scheduler queue locks. BSP reaping first detaches
the process's task stop target and frees task storage, then detaches its process-owned
group link before process destruction. Its member count stays positive until
process and task reclamation finish. Group completion additionally waits for admitted
launches and attributed deferred cleanup. See [execution groups](../interfaces/execution-groups.md).
