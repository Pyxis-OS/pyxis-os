# Private root epochs and concurrent flush initiators

Task 3 foundation, 2026-10-26. Implements part of the
[accepted invalidation design](../../../wip/vm-invalidation.md); shared kernel
VM ownership and its caller conversions are still pending. This foundation
keeps the general kernel VM's BSP assertion and private whole-process drainage.
It adds no ABI, grant, boot option or production diagnostic.

Every process receives a non-reused root incarnation, with a local generation
advanced only under drained mutation ownership. Dispatch captures the pair and
reserves its CPU in the residency bitmap. Departure clears the actual captured
pair only after switching to kernel CR3 on the permanent scheduler stack.
Incarnation/generation exhaustion refuses before mapping effects.

Shared invalidation uses permanent per-CPU requested/acknowledged epochs. The
global acquire/release issue chain orders each initiator's prior closure;
monotonic request mailboxes and ACKs can therefore cover an older request even
if its publisher is late. A larger integer alone would not prove this. Each
handler reloads its current CR3 and acknowledges without allocation, locks,
logging, transient pointers or a drain loop. Initiators retry outstanding
doorbells and sleep in 1 ms polls against one absolute caller-supplied deadline.
PCID and global pages remain disabled.

## Ordinary nested QEMU checks

Before code: accepted docs head `1d82c4c3`, kernel equivalent to main `5c96f016`.
After foundation: `2d4dd5a1`. Clang 23 builder
`pyxis-llvm23.1.3-3d69e83`, debug ceiling, q35/KVM, host CPU with invariant TSC,
4 GiB RAM, one and four CPUs, VirtIO graphics/network/RNG. No disk is mounted;
`nvme.write=0`. The same existing native packet archive/probes were used;
the one-CPU configuration confines Remote children to CPU 0, the four-CPU
configuration to CPU 1. All command lines, configuration/archive/kernel hashes,
probe outputs and build logs are retained at
`/shared/present/bsp-removal/task3/`.

One completed run per configuration before and after: eager allocation/release,
zeroing, sparse/self-unmap, context pins and unwind, sibling quiescence, parked
address/pipe operations, Go parallel/retiring native threads, stopped children,
and a VirtIO resize from 1280x800 to 1024x640 all pass. Resize confirms the
ordinary remote-flush path; its cell-copy diagnostic does not measure IPI cost.

| Nested run | Small 4 KiB | Large 1 MiB | Long control |
| --- | ---: | ---: | ---: |
| Before, 1 CPU | 3.057 ms | 4.877 ms | 64.476 ms |
| After, 1 CPU | 3.635 ms | 14.915 ms | 63.543 ms |
| Before, 4 CPUs | 3.268 ms | 4.959 ms | 62.517 ms |
| After, 4 CPUs | 2.759 ms | 15.865 ms | 61.891 ms |

These are single runs on a shared nested host, with no controlled performance
or native-speed claim. Earlier listener-startup preparation failures contain no
benchmark samples and are not counted as completed runs.

The first after-runs' short large-allocation case was slower inside allocation
service, while the long control and release service did not show that increase.
An additional matched four-CPU pair used `-mem-prealloc` on both kernels to move
host guest-RAM provisioning outside the measured operations:

| Preallocated RAM, 4 CPUs | Small | Large | Long control |
| --- | ---: | ---: | ---: |
| Before | 2.529 ms | 4.426 ms | 60.786 ms |
| After | 2.548 ms | 4.259 ms | 60.112 ms |

Both functional collectors and resize checks pass. The large-case increase was
not reproduced in this pair; this does not isolate host paging from every other
shared-host effect or establish a speedup. The original results remain above,
not replaced with the later pair.

## Concurrent initiators and root inspection

A separate private QEMU fixture starts independent processes on CPUs 1 and 2.
Each runs 32 ordinary all-online-CPU invalidations; the recorded maximum active
initiators is two and both complete. The fixture changes no PTE, drops no IPI
and suppresses/delays no acknowledgement. It is not merged, added to the ABI,
or placed in a native packet. Production sources were restored and rebuilt.
The patch, source, ELF hash, configuration, scripts and final passing record are
under `task3/concurrent/ordinary-two-initiators-gdb-final/` and its parent.
Earlier collector/GDB setup failures are recorded separately in `evidence.json`.

Read-only GDB hardware breakpoints establish on CPU 1:

- Before departure accounting: actual CR3 equals kernel root `0x62000`, captured
  pair is incarnation 2/generation 1, root count 1, residency mask `0x2`.
- Before the next private dispatch: roots 0, mask 0, same incarnation 2 and
  generation 2. Entry then reserves residency and reloads the private root.

These addresses are disposable guest values. Stopping under GDB is functional
inspection, not a latency measurement. Both concurrent actors, the ordinary
functional collector and the resize pass after detachment. All owned QEMU/GDB
processes were joined and stopped.

## Remaining qualification

Shared structural ownership, stable concurrent queries, safe scheduler
admission, retained uncertainty, actual shared range reuse/permissions and
consumer exclusion are not delivered by this foundation. The separately
authorized delayed-ACK check below covers the existing retained-resize consumer;
it does not qualify the pending structural-owner integration. Native task 1/2
results do not qualify task 3.

## Delayed acknowledgement: production-kernel QEMU pass

**Authorized and checked (chronium, 2026-10-26):** one disposable, RAM-only,
four-CPU QEMU run of production foundation head `adb8b87e`. No kernel delay
hook, guest-memory/PTE write, general-register edit, native operation or merged
test mechanism. The exact tested ELF SHA-256 is
`fcd295b78a69954a90ac6f50702f320cc51bf27ae6333810cb329acd2c0473f3`.
The diskless configuration, old live archive, invariant-TSC/KVM clock and
`nvme.write=0` match the earlier foundation check. This is failure-path
correctness, not a native latency or performance measurement.

GDB hardware breakpoints stop CPU 3 at entry to `arch_tlb_flush_interrupt`,
before its acknowledgement. QEMU 10.2.2's GDB `vCont` resumes only CPUs 0–2;
CPU 3 stays stopped while the initiator's existing absolute one-second deadline
expires. QMP stops that running subset for read-only inspection. Then all CPUs
resume, including the delayed handler. See the pinned
[partial-resume implementation](https://github.com/qemu/qemu/blob/v10.2.2/gdbstub/system.c#L558)
and [QEMU GDB CPU/thread model](https://www.qemu.org/docs/master/system/gdb.html#debugging-multicore-machines).

| Observation | Requested epochs | Acknowledgements, CPUs 0–3 | Resize disabled |
| --- | --- | --- | --- |
| Before request | 0, 0, 0, 0 | 0, 0, 0, 0 | No |
| Deadline expired, CPU 3 still paused | 1, 1, 1, 1 | 1, 1, 1, 0 | Yes |
| CPU 3 resumed: late ACK | 1, 1, 1, 1 | 1, 1, 1, 1 | Yes |
| Another resize requested | 1, 1, 1, 1 | 1, 1, 1, 1 | Yes |

The existing resize transaction had switched software to 1024x640 before
retiring its old buffers; it had not unmapped them. On timeout it emits the
existing retention diagnostic and transfers the old batch to permanent
`retained_resize`. Inspection confirms **five VM-owned ranges, 2,940 pages
(12,042,240 bytes)** stay backed. Every page's physical frame and PTE permissions
match the pre-request snapshot, excluding only hardware accessed/dirty status.
The PMM marks every frame allocated and allocatable. Range states and backing
counts match too. These checks pass at timeout, after the late ACK, and after
the additional resize request. The retained record's bytes remain identical
after timeout; a hardware breakpoint on its cleanup function is never hit.

The later 1280x720 request does not start another resize: the layout stays
1024x640, the resize-disable flag stays set, and no second epoch is issued.
Private MEMORY completes 16 allocations/releases without failures while that
consumer is fenced. Remote remains usable, returns the kernel log, and exits
with confirmed normal completion. No routine panic or late-ACK reclamation.

The observed KVM clock floor after timeout exceeds the captured absolute
deadline. Host/GDB stop intervals are not treated as target performance data.
The source/ELF, host commands, QEMU command line, GDB script/transcript, full
page/bitmap checks and snapshots are retained under
`/shared/present/bsp-removal/task3/delayed-ack/gdb-paused-vcpu/` and its parent,
with a hash inventory. Only this prose is committed; no raw log, image or
test code. All owned QEMU/GDB/Remote helpers are stopped and joined.

This confirms the existing display consumer's fencing and retention policy
with the new engine. Shared-owner admission/cleanup, range reuse and permission
transitions still need their own implementation and qualification. This single
approval is not a blanket authorization for other fault scenarios or native
fault testing. Chronium clarified that the fault-injection rule concerns
manufactured appearances/screenshots, not approved disposable failure-path
checks; no screenshot was produced here.

## Shared query guard checkpoint

Follow-up branch `mm/shared-kernel-vm`, source `7d5ee540`, separates normal
general-kernel mapping snapshots from structural mutator admission. New ancestor
frames are allocated/zeroed before short guarded publication; leaf publication,
unmap/protect and scalar query snapshots share an allocation-free guard. Retained
ancestors stay alive. The never-reused heap arena keeps its separate owner and
disjoint ancestor proof. Fatal/debugger recursive inspection keeps its existing
separate path. All general kernel mutations still require the BSP; the long-lived
structural owner and caller conversion are not implemented at this checkpoint.

Production-kernel single runs with one and four CPUs pass the same memory/Go/
stop/resize collector. A separate private fixture on CPUs 1/2 also completes 32
queries of each actor's retained kernel stack and 32 concurrent invalidations
per CPU, with overlap 2. The scalar snapshot is not a lease; the actor retains
its stack throughout. The same GDB departure/re-entry checks pass. Evidence is
under `/shared/present/bsp-removal/task3/shared-query-fixture/` and baseline
labels `query-guard-1cpu` / `query-guard-preallocated-4cpu`. No failure injected,
no production diagnostic/test operation, all sources restored and guests stopped.
