# BSP service requests

Caelum separates task execution from BSP-owned subsystem operations through a
closed service catalog and one FIFO executor. Allocation, VM mutation and final
object destruction remain BSP-owned. The request mechanism stays internal to the
kernel; each service defines its own public operation contract.

## Ownership boundaries

| Component | Responsibility |
| --- | --- |
| [Task/scheduler](../../kernel/task.c) | Ready queues, waits, parking, deadlines, retirement and post-switch handoff |
| [Request mechanism](../../kernel/service/request.c) | Storage sizing, reservation, publication, dispatch and completion |
| Subsystem | Typed inputs/results, authority checks, resource loans, actual operation and rollback |
| BSP executor | Nonblocking operations under the existing allocation and VM contracts |
| HOST worker | Blocking filesystem transport and final completion of forwarded requests |
| Native filesystem worker | Policy/view operations, block waits and final completion of forwarded requests |
| Network worker | TCP/mixed readiness observation and final completion of forwarded waits |
| Readiness worker | Waits without TCP interests, independent of network-device availability |
| ACPI worker | Power-off and restart, completing forwarded requests only on failure |

The catalog and dispatch are explicit. Adding a service changes its subsystem
and the catalog rather than adding scheduler payloads, service sweeps or pending
checks. BSP-local service helpers call each other directly; kernel workers cannot
submit synchronous client requests to the executor.

## Storage and lifetime

User-task preparation eagerly allocates one shared request area and a separate
persistent profiling block before publication. The catalog computes the largest
record size/alignment and validates each header offset and heap alignment
requirement. Preparation checks the selected layout, reserves the area and zeroes
that typed range. There is no fixed page-size limit or scheduler-owned payload
union. Submission itself cannot fail to allocate storage, including required
cleanup requests.

One synchronous request may occupy the area at a time. Its reservation extends
through result consumption, including HOST/native user-buffer copying and transfer of
returned objects or executable captures. A second preparation requires the first
reservation to be released. Independent launch captures/prepared batches and returned heap
objects retain their own lifetimes across operations.

Kernel workers allocate neither user-request nor profiling storage. Failed task
preparation, unpublished-task discard and normal retirement free each owned
allocation once on BSP with IF=0. Failure leaves the inactive process owned by
the preparer. Retirement asserts no request reservation, deferred publication,
timed wait or resource queue link remains; request destruction requires FREE
state. There is no external cancellation of an outstanding synchronous request.

File, console, process and pipe queues share one task-owned resource link,
separate from the request area and scheduler wait record. Each resource detaches
its link under its lock before waking or reusing it. A timed console waiter
removes any remaining queue link after resumption. No spinlock spans a wait or
service operation. Logical FILE busy ownership can survive a detached resource
wait and a later backing-replacement request.

## Publication and completion

The [internal interface](../../include/kernel/service/request.h) separates completion
from waiting. Ordinary requests follow:

```text
FREE -> PREPARED -> QUEUED -> SERVICING -> COMPLETE -> consume -> FREE
```

The caller captures validated inputs and prepares its task wait before publishing
under the FIFO lock. Publication transfers the request and specified resource
loans to BSP. The caller retains its saved wait pointer and must not inspect the
request until notification returns ownership. The synchronous wrapper handles
notification before parking: an early wake records completion without making a
still-executing task runnable on another CPU.

Service writes results, clears subsystem/queue references, detaches the wait and
publishes COMPLETE before notification. Neither service nor completion accesses
the request afterward; the caller may immediately consume, reuse or retire it.
COMPLETE is not an asynchronous polling interface. The caller consumes results,
updates any caller-local profile and explicitly releases the reservation.

Every DISPLAY operation adds DEFERRED before QUEUED. Its catalog entry requires
the scheduler to leave the task stack, activate the kernel root, clear
entry/current-task state and establish parking before publication. This lends an
inactive private address space to BSP. Resumption reloads the process root before
accessing the saved task stack. The same ordering applies to BSP userspace. See
[SMP handoffs](smp.md). Private memory used the same deferred path until SMP task
7a; it now runs in the caller's syscall ([memory](memory.md#execution)).

Screen capture retains DEFERRED publication. Its caller claims a destination
slot before submission; the shared request owns that capacity through FILE
installation or cleanup, without an exclusive table loan. After
SERVICING, the executor transfers the request in FORWARDED state to the sole
presenter without waiting for a frame. Admission and pending/active transitions,
allocation, capability installation and completion use BSP/IF=0; composition and
device waits use the presenter's existing IF=1 frame lease. Refusal or a finished
frame clears all presenter references and unconsumed claims before common completion.
See [screen capture](../interfaces/screen-capture.md#frame-boundary-and-lifetime).

HOST adds FORWARDED after SERVICING. The executor transfers it to the existing
transport worker without waiting and makes no further request access. The HOST
completion paths clear worker links, input loans and the transport profile pointer
before common completion; unavailable forwarding and startup failure obey the
same ownership rule. FIFO admission does not promise completion order across
services. See [HOST ownership](../devices/virtio-fs.md#native-directory-and-file-objects).

Native filesystem requests also use FORWARDED. Publication reserves one of the
32 shared native slots and stamps the deadline before entering the executor
FIFO. The worker derives authority from held views plus captured caller rights,
detaches input nodes and releases admission before common completion. The
4,592-byte typed record, including the owned CAPTURE staging pointer, fits the
existing 4,928-byte request allocation. See
[native object ownership](../devices/filesystem-native-adapter.md#build-and-ownership).

Readiness waits also use FORWARDED. The executor hands their copied interests to
the appropriate worker: TCP/mixed waits use the network worker, while terminal-only
waits use a dedicated BSP readiness worker. Both check current readiness before
deadlines and detach all registrations/object references before completion.
Each scan uses one fresh, lazy clock snapshot for blocked requests. Stop, ready
and polling observations need no expiry read; both workers recheck the earliest
absolute deadline before parking so an aged snapshot cannot add a blocking wait.
The typed wait record fits
inside the existing request area; it adds no per-task allocation or global waiter
slot pool. A poll can still require this worker handoff. It never waits for an I/O
condition. See [TCP readiness](../devices/tcp.md#readiness-and-transfer-attempts).

## Service catalog

| Service | Submission and retained contract |
| --- | --- |
| System information | Ordinary; coherent allocator-counter snapshot, or a copy of the ACPI worker's latest battery and AC poll; no caller state loan or mutation |
| Pipe creation | Ordinary; two preclaimed slots and atomic owned endpoint publication |
| Terminal creation | Ordinary; four preclaimed slots, fixed queue allocation and atomic owned handle publication |
| Capability growth | Ordinary; live table, BSP backing allocation and guarded copy/publication |
| Namespace creation | Ordinary; preclaimed initial grant slot and cleanup on failure |
| Endpoint creation/export | Ordinary; preclaimed result slots, owned receiver snapshot and BSP owner-list mutation |
| RAM directory allocation/discard | Ordinary; returned unpublished entries or transferred detached entries |
| Group creation / launch preparation and publication | Ordinary; claimed result capacity, owned grant captures, private observers, atomic publication and unpublished-child rollback |
| Display acquire/present/release | Deferred; inactive process and display loans for every operation |
| Screen capture | Deferred admission, then forwarded; preclaimed result slot, one pending/in-flight presenter request, owned READ-only FILE publication or unpublished-backing rollback |
| HOST forwarding | Ordinary admission; existing HOST worker owns transport and final completion |
| Native filesystem forwarding | Ordinary admission; bounded native worker owns core views/I/O and final completion |
| Readiness wait | Ordinary admission; selected worker owns observations, transient object references and final completion |
| Power-off and restart | Ordinary admission; the ACPI worker owns the user hold, pool flush and S5 or reset, and completes the request only on failure ([ACPI](acpi.md#power-off-and-restart)) |

Authority checks remain in their owning subsystems; callers validate user buffers
and copy replies. Services clear loans before completion. Capability growth or
launch preparation following HOST/native result consumption begins only after
releasing that filesystem request reservation. Native executable capture runs on
the owning filesystem worker with the original publication deadline and actual
caller rights. It transfers complete independently owned launch staging on
success; errors transfer no bytes. The 16 MiB per-image staging limit is separate
from native wrapper accounting. Launcher discard/preparation frees that staging
on the BSP, including stop and launch-failure paths.

## Executor policy

The executor is created after task initialization and before user-task
publication; failure is fatal. Concurrent submissions are ordered by FIFO-lock
publication. Deferred requests enter only after their safe handoff, so preparation
time does not reserve queue position.

When empty, the worker prepares and publishes its waiter under the request lock.
The first publisher detaches that waiter and notifies after unlocking. Further
publishers need no extra wake while the executor is already notified or working.
The worker always finishes its published wait before reuse, including early
notification. It parks without polling. A remote wake sends the existing IPI;
a BSP caller sends no self-IPI.

Each operation runs with IF=0 under current BSP allocation/VM rules. Afterward,
the worker restores interrupts and yields if the BSP ready queue has another
runnable task. The scheduler owns that decision. Individual operations remain
non-preemptible; there are no priorities, elapsed-time budgets or batch quotas.
Retirement, deadlines and resource waits remain scheduler responsibilities.

## Profiling and scheduling costs

Transient timestamps travel in typed requests. Persistent MEMORY and HOST
aggregates live in a separate caller-only block (MEMORY's samples are now taken
in the caller's own syscall); the profiling subsystem owns
BEGIN/SNAPSHOT/END controls. Services never borrow the aggregates. Disabled
collection adds no timestamp reads or submission-time allocations. See
[allocation profiling](../development/allocation-profiling.md) and
[I/O attribution](../development/io-reliability-attribution.md).

Initial HOST forwarding exposed unnecessary HPET reads in added scheduler passes.
Empty deadline lists now skip clock reads, as does the HOST worker's untimed idle
wait. Finite deadlines, wake ordering and conditional yielding are unchanged.
Corrected unprofiled transfers returned to baseline. Subsequent matched controls
around storage consolidation measured read/write/copy medians of
86.098/87.048/85.038 ms versus 87.080/87.611/86.011 ms before consolidation, with
all 72 passes verified. The [HOST correction PR](https://git.internal/PyxisOS/pyxis-os/pulls/241)
and [storage PR](https://git.internal/PyxisOS/pyxis-os/pulls/242) retain off/on
controls and ranges. These are nested-VM regression observations, not owner-host
performance estimates. Profiling still materially changes timing/interleaving;
subtracting a constant clock overhead is not justified.

## Validation and sizes

Closure validation on 2026-09-29 used the merged implementation at `add067f`.
The ordinary kernel/image build passed using verified unchanged SDK, userspace
and ports bundles. Fresh GCC 16.2.0 builds with normal GNU C23, `-O2 -g3` and
freestanding flags compared the original milestone baseline `3cdb965` with the
completed implementation. Sizes below exclude allocator overhead/rounding and
the unchanged separate 16 KiB task stack.

| Storage | Original baseline | Completed implementation |
| --- | ---: | ---: |
| Task metadata, user or kernel | 7,088 B | 752 B |
| User request allocation | embedded records | 4,920 B |
| Persistent user profiling | embedded 816 B | 816 B |
| Combined user metadata/request/profiles | 7,088 B | 6,488 B |
| Combined kernel metadata/request/profiles | 7,088 B | 752 B |
| Scheduler wait record | 32 B | 32 B |
| Pipe pair, including 64 KiB buffer | 65,680 B | 65,680 B |

This saves 600 bytes per user task and 6,336 per kernel worker across the whole
milestone. User storage uses three allocations instead of one. The largest
request is HOST, requiring 8-byte alignment against the heap's 16-byte guarantee.

Group termination subsequently adds stop/cleanup tracking: task metadata is now
784 bytes, the request header grows the largest record to 4,928 bytes, and combined
user metadata/request/profiles total 6,528 bytes. SMP task 7a then shrinks the
profile block to 720 bytes, for 6,432 in total. The table above records the
historical storage-consolidation measurement.

Interactive combined checks used one and four CPUs in nested KVM, fixed QEMU
10.2.2, CPU `max`, 256 MiB, matching Fedora OVMF, entropy and a private virtiofsd
1.14.0 export with default cache policy. No network/block devices or cache
eviction were used. Both configurations passed namespace/provider lookup, RAM
checksum pipelines, invalid second-image launch rollback, private-memory
allocation/release, mapped graphics and normal exit. The failed launch left the
first child's output empty. Four-CPU early pipe closure returned the expected
32-byte checksum and upstream endpoint-closed errors, then resumed the shell.
HOST executable loading and directory/file cleanup also passed.

Growing HOST-to-RAM copies with HOST profiling verified one warmup and five
1 MiB samples on each CPU count, with 258 HOST READs and successful transport
completions per sample and no failures or short transfers.

GDB observed the idle executor's published parked wait and a parked caller at
completion under kernel CR3 with IF=0. Deferred MEMORY on one CPU and DISPLAY
on four CPUs published from permanent scheduler stacks with kernel roots active,
current tasks cleared, callers parked and reservations intact. DISPLAY result
consumption had no remaining wait, queue link or subsystem loan. Earlier
[storage validation](https://git.internal/PyxisOS/pyxis-os/pulls/242) also observed
HOST-to-FILE reuse and exact request/profile destruction during normal retirement
and unpublished-child discard. Closure reviewed early notification, allocation
failure, concurrent console timeout/handoff and fatal-fault retirement by code
inspection; it did not force those paths.

The existing `session boot://iobench.pxe pipe --buffer 4096` workload verified one
warmup and five 1 MiB samples per CPU count, each with 256 reads/writes and no
short transfers or errors. No debugger was attached during timing. Values are
median milliseconds (minimum–maximum):

| CPUs | Revision | Write acceptance | Consumer acknowledgment |
| --- | --- | ---: | ---: |
| 1 | Historical baseline `3cdb965` | 2.921 (2.842–3.847) | 3.370 (3.342–4.298) |
| 1 | Completed implementation | 1.798 (1.780–2.460) | 2.259 (2.193–2.880) |
| 4 | Historical baseline `3cdb965` | 0.734 (0.720–1.730) | 1.043 (1.030–2.668) |
| 4 | Completed implementation | 0.721 (0.711–0.723) | 1.030 (1.019–1.043) |

The baseline timings were recorded during task 1, not rerun at closure. That boot
had no HOST device; closure included HOST. Other listed VM settings and workload
parameters match. These are historical regression context, not a matched
improvement claim. Pipe creation and worker launch are outside the timed interval;
it does not isolate service latency.

No public ABI, dependency pin, compiler-container input, test infrastructure or
boot/output automation changed. All validation QEMU, GDB and daemon jobs were
stopped. Owner-host timing remains unavailable.

## Retained limits

BSP-only allocation and inactive-root VM ownership remain intentional. Long
operations can delay later FIFO requests; a separate policy decision and measured
mixed-workload evidence are needed before introducing service budgets or workers.
Eager request/profile allocation charges every user task even when it never uses
HOST or enables profiling. Lazy provisioning must preserve guaranteed cleanup
capacity and define BSP handoff and allocation-failure ownership. These costs and
revisit points are recorded in [technical debt](../technical-debt.md).

Public asynchronous I/O, completion queues, cancellation, multiple outstanding
requests, quotas, priorities, task migration and process threads are not provided.
Asynchronous submission would need independently owned storage, completion
observation and resource retention. Waitable completion receipts and process-exit
draining remain undecided. Continued caller execution cannot access exclusively
lent tables or VM state; borrowed buffers need stable contents and lifetime or
independent captures.

Shared-table delivery replaces exclusive table loans with owned inputs and
claims. Concurrent siblings still need process activity drain, VM quiescence and
the remaining device-operation audit before thread creation. Kernel clients likewise
need an explicit nesting/dependency contract before synchronous submission can be
allowed. See the [thread follow-ups](../wip/scheduling-and-threads.md#multiple-user-threads).
