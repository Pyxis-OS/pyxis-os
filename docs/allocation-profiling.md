# Allocation benchmarks and memory-request profiling

`allocbench` is a native userspace program, built and installed with the ordinary
userland tools. It uses the clock and private-memory capabilities. `--profile`
also needs the optional `profile` startup resource; init, session and the shell
forward that caller-scoped authority to their children. Collection starts off in
every new process.

## Running the benchmark

```text
allocbench heap
allocbench heap --profile
allocbench heap --mixed --size 4096 --live 256 --profile
allocbench growth
allocbench growth --profile
allocbench pages
allocbench pages --profile
```

Run each command several times in a fresh process. Keep CPU count, memory,
accelerator, other guest workloads and host conditions comparable. Record whether
the host is itself a VM; nested KVM results are not bare-metal allocator numbers.

- `heap` preallocates the live set before timing, then replaces each allocation
  while the other slots remain live. Defaults: 64-byte blocks, 128 live slots,
  4096 rounds. `--mixed` rotates through eight sizes between one byte and
  `--size`, creating different holes and reuse patterns. Profiled backing
  allocation counts reveal any unexpected growth during the warmed run.
- `growth` skips payload warm-up, allocates the live set, then frees alternating
  slots followed by the remainder. Defaults: 128 blocks of 64 KiB, one round.
  Use a fresh invocation for each sample: libc retains pools until process exit.
  Runtime, slot storage and output setup already use the heap, so this measures
  expansion of an initialized allocator, not its very first allocation.
- `pages` allocates private regions directly, holds `--live` regions, then
  releases them before the next round. Defaults: 64 KiB, one live region,
  64 rounds. Sizes must be page multiples; `--mixed` is unsupported here.

All modes accept `--size`, `--live` and `--rounds` as positive decimal values.
Size is capped at 1 MiB, live slots at 65536, rounds at 1000000, and maximum live
payload at 64 MiB. Growth requires one round. These are application workload
bounds, not allocator limits. They do not guarantee sufficient memory.

The timed workload includes loop bookkeeping and volatile first/last-byte writes
on each allocation, but no printing. It does not sweep whole buffers. Slot
storage, heap warm-up, clock calibration, profiling control calls and final heap
cleanup are outside the interval. Page release and growth-mode frees are inside.
Peak live payload excludes slot storage, allocator overhead and pool slack.
Counts distinguish allocation attempts, releases and failures; throughput counts
both allocation and release operations, not pairs or bytes copied. A failed run
prints partial results, cleans up and returns failure.

A batch of 1000 clock calls reports their mean elapsed cost, including call and
loop overhead. It is not subtracted. Current clock reads use HPET MMIO; neither
nanosecond units nor this calibration imply nanosecond accuracy. Compare matching
profiled and unprofiled runs rather than subtracting a presumed constant cost.

## Profiling capability

The [profile protocol](../include/abi/profile.h) uses ordinary tagged CALLs and
`PROFILE_RIGHT_MEMORY`. Independent RAM FILE replacement collection uses
`PROFILE_RIGHT_FILE` and `profile_file_begin/snapshot/end`; see the
[FILE attribution contract](io-reliability-attribution.md#ram-file-profiling).
Host READ/WRITE collection independently requires `PROFILE_RIGHT_HOST`; see the
[host attribution contract](io-reliability-attribution.md#host-profiling-and-attribution-limits).
The memory operations below keep their existing meaning. Libpyxis exposes `profile_begin`, `profile_snapshot` and
`profile_end` through `<profile.h>`.

BEGIN clears the caller's counters and enables collection; repeated BEGIN is
BUSY. SNAPSHOT returns the current or last counters without changing collection.
END stops collection and returns final counters; inactive END is BAD_REQUEST.
Invalid reply storage does not stop collection. Closing a handle does not end a
collection; explicit END or process exit does. Handle copies/delegation grant
permission to profile the recipient itself, not the sender. There is no remote
inspection, sampling, trace buffer, stack capture or kernel-address exposure.

Allocation and release have separate counts, failure counts, page-rounded
requested bytes and successfully completed bytes. Totals saturate and set a flag
rather than wrapping. A window's allocated/released bytes are events, not a live
memory gauge: releases may refer to allocations predating BEGIN. Heap growth
runs expose pool count/bytes acquired during the window; freeing libc blocks does
not release those pools. Existing pools and fragmentation are not inspected.

Each admitted private-memory request records five boundaries:

1. Request preparation before parking.
2. After leaving the task stack/private root and establishing the parked handoff,
   just before taking the request publication lock.
3. Immediately before BSP private-memory service.
4. Immediately after service, before wakeup.
5. Caller resumption after restoring its address space and task stack.

The four intervals and their end-to-end total have elapsed-time sums and maxima.
Queue time includes publication lock/link overhead and waiting for the BSP
executor; service includes private allocation bookkeeping, pages, zeroing, mappings
and any kernel heap growth it causes. Resume includes notification and scheduling. Totals exclude
initial syscall validation, reply copying and aggregate-counter updates. Invalid
requests rejected before scheduler admission are not counted. No additional clock
reads occur for unprofiled memory requests.

Transient timing samples live in the typed memory request in the caller's reusable
request allocation. Persistent memory, FILE and HOST aggregates share a separate
816-byte allocation, eagerly zeroed during user-task preparation; kernel workers
allocate neither area. Collection remains caller-scoped under the current
one-task-per-process model. The profiling subsystem owns controls and caller
accessors; task provides only the current profile storage adapter. The deferred
handoff gives the BSP executor exclusive access to the request while the caller
is parked; it records service timestamps before waking the caller. The resumed
caller alone updates the aggregates before releasing the request reservation.
No allocator lock or private-memory allocation policy is changed.

This measures userspace heap performance and kernel private-memory service.
Standalone `kmalloc` throughput, PMM/VM subphase timings and system-wide accounting
remain separate investigations. Measurements alone do not change scheduling or
allocator behavior.

## Initial observations before prompt BSP notification

An ordinary QEMU 10.2.2 boot used Q35, KVM, `-cpu max`, four CPUs and 256 MiB,
with the presenter and two shells running. The development host is itself a
Fedora KVM VM exposing an i9-12900K. Commands ran sequentially on workload CPU 1,
without debugger stops. These are individual manual observations, not statistical
confidence intervals or portable performance targets:

| Default workload | Unprofiled elapsed | Profiled elapsed | Profile observation |
| --- | ---: | ---: | --- |
| Heap: 524288 allocations + 524288 frees | 12.887 ms | 12.724 ms | No backing requests |
| Growth: 128 allocations/frees, 8 MiB payload | 708.116 ms | 705.017 ms | 128 backing allocations, 9961472 bytes, no backing releases |
| Pages: 64 allocations + 64 releases of 64 KiB | 715.401 ms | 715.372 ms | Counts and completed bytes match both directions |

The mixed-size default heap run, with profiling enabled, took 17.634 ms and
made no backing requests. All runs completed without allocation/release failures.

In the growth profile, 663.027 ms accumulated in the BSP queue, versus 24.053 ms
in private-memory service. The page profile similarly spent 343.940 ms waiting
for allocation service and 342.754 ms waiting for release service, versus
9.250 ms and 3.562 ms in the services themselves. This identifies queue delay as
the dominant measured backing-memory cost in this setup; it does not isolate
individual PMM, VM or kernel TLSF costs. No scheduling optimization was made.

The clock-call loop varied from about 35 to 64 microseconds per read. Profiled
runs being slightly faster than their unprofiled counterparts is run-to-run and
scheduling variation, not negative instrumentation overhead. Repeat on the target
host and across different live sets before drawing optimization conclusions.

A separate single-CPU boot completed the same default page workload in 15.306 ms
unprofiled and 38.900 ms profiled. Queue sums fell to 4.467 ms for allocation and
4.464 ms for release; all 64 requests in each direction succeeded. Clock-call
means were about 37 microseconds. Five timestamp reads per request across 128
requests are already of the same order as the observed profiling overhead, so
these elapsed intervals must not be presented as uninstrumented CPU costs.

## Prompt BSP notification comparison

The notification change measured below sent the existing rescheduling IPI after
leaving the caller's task stack/address space and publishing the parked request.
Private memory now uses the common BSP executor and its ordinary ready-queue
notification; see [SMP scheduling](smp.md) and
[BSP service requests](bsp-service-requests.md). Allocation policy is unchanged.

A before/after comparison used baseline `7383e20` and notification commit
`9d07838`, identical userspace, and the same four-CPU nested-KVM setup described
above, with VirtIO networking through QEMU's user backend and VirtIO entropy.
Commands ran sequentially in fresh processes on CPU 1. Each allocation entry
below is one observation, with no allocation or release failures:

| Command | Before | After |
| --- | ---: | ---: |
| `allocbench growth` | 697.017 ms | 36.983 ms |
| `allocbench growth --profile` | 714.698 ms | 60.362 ms |
| `allocbench pages` | 698.310 ms | 27.252 ms |
| `allocbench pages --profile` | 699.340 ms | 40.225 ms |

The growth profile accumulated 661.964 ms in the BSP queue before notification,
versus 15.492 ms afterward. Service time was essentially unchanged: 27.195 ms
before and 27.716 ms after. The page profile's allocation/release queue sums fell
from 338.238/328.084 ms to 5.774/5.731 ms; corresponding service sums were
9.976/3.972 ms before and 9.526/3.772 ms after. Counts and completed bytes matched:
128 growth backing allocations totaling 9961472 bytes, or 64 page allocations
and 64 releases totaling 4194304 bytes in each direction. These observations
support reduced scheduling delay, not an increase in allocator service speed.
Clock-call means were about 35–36 microseconds in this comparison.

Three sequential `ttcp -t 10.0.2.2` runs per kernel sent the default 2048 buffers
of 8192 bytes to a host `ttcp -r`. Every receiver confirmed 16777216 bytes:

| Guest throughput, MiB/s | Before | After |
| --- | ---: | ---: |
| Run 1 | 1.373 | 1.377 |
| Run 2 | 1.376 | 1.382 |
| Run 3 | 1.386 | 1.376 |
| Median | 1.376 | 1.377 |

There was no material standalone TCP throughput change in these samples. `ttcp`
allocates its buffer and establishes the connection before the timed transfer;
TCP completion already uses prompt task wakeups. Concurrent allocation and network
load was not measured. These nested-VM observations do not predict throughput on
the development host or replace repeated measurements there.

A separate single-CPU boot also completed `allocbench pages`: 64 allocations and
64 releases, no failures, 16.066 ms unprofiled. This exercises the BSP caller path,
which services its request directly without sending itself an IPI.
