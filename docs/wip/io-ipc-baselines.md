# I/O and IPC performance baselines

Status: agreed next milestone after [userspace services](../userspace-services.md).
This is a measurement milestone, not an optimization assignment. Start with
userspace workloads and existing clocks; add kernel instrumentation only after
a concrete unanswered question is identified and its scope is discussed.

## Outcome and scope

Establish repeatable measurements for file reads, writes, copies, pipes,
endpoint delivery and exported files. Separate HTTP fetching from consuming an
already-open snapshot. Record what each number includes so later changes can be
compared against useful baselines rather than an unexplained throughput figure.

Small manually invoked userspace applications are the primary deliverable:
`iobench` for byte I/O and a focused IPC benchmark with a companion process.
The read-mode contract below is implemented; remaining CLI spelling, default
sizes and workload bounds are settled before their implementation tasks.
Keep workloads bounded and build/package them normally;
do not add a generic benchmark framework, boot automation or CI performance gates.

Reuse [allocbench and its memory profile](../allocation-profiling.md) and
[ttcp](../tcp.md#transmit-only-ttcp) for context. Do not duplicate them or expand
their scope merely to collect everything through one command. A TCP transmit
result is not directly comparable to HTTP download throughput.

No scheduler, allocation, buffering, queue, payload-limit or network-algorithm
changes belong in this milestone. Identified bottlenecks become separate bounded
investigations. Kernel tracing, sampling, per-process CPU accounting, contention
between spaces and mixed-workload fairness are deferred. Correctness failures
must be reported rather than hidden behind a performance result.

## Measurement contract

Use the existing monotonic clock and describe results as elapsed time, not CPU
time. Time batches to avoid a clock syscall around every small operation. Report
clock-read overhead separately; do not subtract a presumed constant or imply
that nanosecond units provide nanosecond accuracy. Per-operation means computed
from batches are not individual-call latency percentiles.

Each result identifies the workload, backend/URI, application buffer size,
requested and completed bytes/operations, elapsed interval and failures. Print
throughput only for a completed successful workload. A failed run may report
confirmed partial progress, but must remain visibly failed and return nonzero.
Counts distinguish application I/O calls from lower-level native transfers;
do not label the former as syscall counts without observing that boundary.

Allocate buffers, prepare fixtures and warm up outside the interval unless those
costs are explicitly the subject of the measurement. No logging, progress output
or debugger stops inside a timed workload. Payload-producing modes keep reports
on stderr so stdout remains usable in pipelines. Handle short transfers normally
and measure actual bytes, not requested lengths multiplied by call count.

Use deterministic bounded fixtures and verify byte counts/content outside timed
regions where possible. State any validation work that necessarily remains inside
an interval. Consumer completion acknowledgments must reflect consumed bytes,
not merely successful producer writes. Verification is part of the manually
invoked benchmark, not a separate test or fault-injection infrastructure.

## Workloads and boundaries

| Path | Measure | Keep distinct |
| --- | --- | --- |
| Native files | Sequential reads from archive, RAM and optional `host://`, using conventional libc descriptor I/O | Open/setup versus transfer; backend and host cache state |
| Writes | RAM and writable host files, reporting confirmed short writes | Write acceptance versus separately requested sync; creation/growth versus prepared storage |
| File copies | Read/write loop through completion, followed by content verification | Source/destination pair, application buffer size and optional sync |
| Pipes | Producer-to-consumer batches using existing standard-stream grants | Producer acceptance versus consumer completion; launch/setup outside steady transfer timing |
| Endpoint CALL | Sequential request/reply batches with small and near-limit payloads | Round trips versus application payload bytes; zero-byte control cost |
| Endpoint SEND | Bounded batches with an explicit receiver acknowledgment | Admission time versus delivery/processing completion; queue-full results |
| Exported files | Reads from an already-open immutable snapshot | Native FILE versus exported FILE transport; OPEN outside read-only timing |
| HTTP | OPEN/fetch, snapshot reads, and the combined consumer path | Network/parser/staging cost versus retained-body IPC; each OPEN performs a new fetch |

Exercise several application buffer sizes, including small requests, the native
transfer boundaries and larger requests. Descriptor `read()` returns one native
transfer; its caller loops over short results. Other helpers may perform multiple transfers.
The current FILE payload carries at most 4,088 read bytes or 4,080 write bytes;
endpoint application payloads allow 4,096 bytes. Use the actual interface limits,
not an assumed common 4 KiB application transfer. A larger caller buffer does
not establish that a larger native transfer took place.

The IPC companion is a small explicit producer/consumer or echo service, not a
generic provider framework. Use existing launcher, endpoint and pipe authority.
Stay within existing placement rules and report where each process runs; adding
cross-space launch/placement control is outside scope. Keep requests bounded and
account for queue-full behavior without treating rejected messages as completed
work or spinning indefinitely for capacity. Exact payload sets, acknowledgment
protocol and whether attachment-copy cost is included need discussion in task 3.

File-writing runs require explicitly selected output paths and an agreed policy
for existing files before implementation. Do not silently truncate user data.
Document cleanup and distinguish file contents from filesystem durability.
For host files, sync measures the exposed backend operation; it does not isolate
the physical disk or promise more than the [host filesystem contract](../virtio-fs.md).

## HTTP and comparison fixtures

Use a manually operated local HTTP server serving a known bounded file as the
primary network reference. Read the same bytes through native storage and an
exported snapshot when comparing those paths. The small text provider is useful
for control/read overhead, but comparing its short guide against a large native
file is not a matched bulk-throughput comparison.

Record HTTP OPEN separately: it includes name resolution where used, connection,
request, response parsing and complete body staging before the file is returned.
Reading the retained snapshot performs no further network fetch. Measure a
complete consumer path as well, including a pipe where useful, with launch and
terminal output costs stated explicitly. No application-specific changes to cat,
tee or cksum are needed merely to make the provider consumable.

Public sites can demonstrate functionality, but are not the repeatable benchmark
reference. Repeated host reads may use host page caches; label observed cache
conditions without claiming cold-cache storage measurements. Do not add cache
eviction or HTTP caching to manufacture a benchmark configuration.

## Environment and reporting

Run one selected workload at a time and collect repeated samples with identical
parameters. Record warm-up policy, repetition count, individual results and a
simple summary such as median and range. Do not claim statistical confidence or
tail-latency distributions from a handful of batch timings.

Record kernel/userland/ports revisions, CPU count and placement, guest RAM,
accelerator, host CPU/OS and whether the host itself is a VM. Identify network
backend, server and fixture, and any virtio-fs setup. Keep nested-VM, owner's host
KVM and eventual physical-hardware results separate. Existing presenter and
provider tasks remain part of the documented environment, not invisible noise.

Use ordinary builds and manual QEMU execution. Inspect existing CI for submitted
revisions, but do not interpret CI wall times as guest benchmark results. Clean
up benchmark processes, handles and temporary resources after each run. No
performance target or minimum throughput is required to complete the milestone.

## Implemented task 1: file reads

The ordinary userland build packages `iobench.pxe` and `share/iobench.bin`.
The latter is a deterministic 1 MiB fixture: byte at zero-based offset `i` is
`(i ^ (i >> 8) ^ (i >> 16) ^ 0xa5) & 255`. Host Lua 5.4 generates it; no binary
fixture is checked into Git. This first mode accepts exactly that file or a copy,
not arbitrary inputs. See the [userland tool reference](../../userspace/iobench/README.md).

```text
iobench read app://share/iobench.bin
iobench read app://share/iobench.bin --buffer 64 --rounds 5
iobench read app://share/iobench.bin --buffer 512
iobench read app://share/iobench.bin --buffer 4096
iobench read app://share/iobench.bin --buffer 65536
```

`--buffer` is the application request size, 1..65536 bytes (default 4088), and
`--rounds` is 1..100 (default 5); both accept positive decimal values. Each
invocation performs one untimed, verified warm-up followed by the selected
number of measured passes. Buffer allocation, clearing and a fresh read-only
open happen before timing. The timed loop fills successive positions of the
retained 1 MiB buffer and accounts for actual progress. The last request is
clamped to the remaining bytes. A separate EOF probe, full content verification,
close and all reporting happen afterward. Reopening resets the descriptor
position without introducing a seek interface.

Reports go to stderr. Each pass identifies the requested payload goal, completed
bytes, payload `read()` attempts and positive short reads. The goal is not the
sum of request capacities. Call counts exclude the extra EOF probe and are not
instrumented syscall counts. `failed_pass` is a Boolean covering the whole pass,
including setup and validation; diagnostics identify the failure. Failure stops
the invocation with nonzero status, retaining partial counts and earlier samples
but suppressing the failed pass's throughput and the successful-run summary.

A 1000-call clock loop reports its mean elapsed overhead separately, without
subtraction. Results include individual elapsed nanoseconds and MiB/s, followed
by median and min/max elapsed time and throughput at the median elapsed time.
For even sample counts, median elapsed is the average of the middle two values.
The fixed fixture bounds memory use and each pass; there is no timed checksum
loop, cache eviction or automatic runtime calibration.

### Matched RAM and host fixtures

In the guest, choose a new disposable RAM destination first: shell `>` truncates
an existing file. Preparation and removal are outside the benchmark:

```text
cat app://share/iobench.bin > home://iobench.bin
iobench read home://iobench.bin
iobench read home://iobench.bin --buffer 65536
rm home://iobench.bin
```

For host reads, create a dedicated export containing
`build/userspace-root/share/iobench.bin` as `iobench.bin`, using the
[virtio-fs setup](../virtio-fs.md#start-the-host-service). A read-only daemon is sufficient.
Then run `iobench read host://iobench.bin`, optionally adding `--buffer 65536`.
Remove the copied fixture and stop the daemon after quitting QEMU. Do not modify
the source during a run; successful verification establishes the returned bytes,
not an atomic snapshot guarantee for mutable native files.

### Initial observations

Measured on 2026-09-28 with kernel/source `2010228`, userland `4acf4b2`, ports
`6ec1290` and lwIP `a1aadb9`, using Pyxis GCC 16.2.0. An ordinary `make -j16 image`
built the complete inputs. The boot command was:

```sh
make run CPUS=4 ACCEL=kvm MEMORY=256M QEMU_DISPLAY=gtk \
  OVMF_CODE=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  OVMF_VARS=/usr/share/edk2/ovmf/OVMF_VARS.fd \
  VIRTIO_FS_SOCKET=/tmp/pyxis-iobench-socket/fs.sock
```

The development host was itself a KVM VM: Fedora 44, Linux 6.19.10, exposing an
i9-12900K. QEMU 10.2.2 used Q35, `-cpu max`, four CPUs and 256 MiB; benchmarks ran
sequentially on workload CPU 1. The normal presenter, development/read-only
shells, and per-space text/HTTP providers remained present. VirtIO entropy was
enabled; VirtIO networking was disabled. Host reads used virtiofsd 1.14.0 with
`--readonly`, `--sandbox namespace`, `--inode-file-handles=never`,
`--no-announce-submounts` and `--rlimit-nofile=0`, launched through `unshare -Ur`.
The fixture was copied before boot and each command warmed it once; no host
cache eviction was attempted. These are warmed nested-VM observations, not
owner's-host, physical-disk or cold-cache measurements.

Every sample below read and verified 1048576 bytes, with no failed passes.
Each row is one invocation with five measured passes after its warm-up.
Sample order is preserved; elapsed values are shown in milliseconds to three
decimal places here.

| Backend | Request bytes | Individual elapsed samples, ms | Median, ms | Range, ms | MiB/s at median elapsed | Clock loop, us/read |
| --- | ---: | --- | ---: | --- | ---: | ---: |
| Archive | 64 | 3.238, 3.444, 3.630, 3.414, 3.648 | 3.444 | 3.238–3.648 | 290.370 | 40.576 |
| Archive | 512 | 0.657, 0.588, 0.707, 0.739, 0.582 | 0.657 | 0.582–0.739 | 1522.673 | 38.780 |
| Archive | 4088 | 0.261, 0.687, 0.266, 0.264, 0.250 | 0.264 | 0.250–0.687 | 3791.901 | 37.989 |
| Archive | 4096 | 0.300, 0.265, 0.272, 0.254, 0.269 | 0.269 | 0.254–0.300 | 3723.840 | 38.492 |
| Archive | 65536 | 0.566, 0.256, 0.252, 0.248, 0.310 | 0.256 | 0.248–0.566 | 3899.396 | 38.551 |
| RAM | 4088 | 0.260, 0.252, 0.255, 0.280, 0.258 | 0.258 | 0.252–0.280 | 3879.427 | 40.934 |
| RAM | 65536 | 0.257, 0.330, 0.267, 0.265, 0.289 | 0.267 | 0.257–0.330 | 3746.020 | 37.183 |
| Host | 4088 | 128.371, 118.724, 115.105, 117.740, 122.077 | 118.724 | 115.105–128.371 | 8.423 | 40.864 |
| Host | 65536 | 109.658, 119.869, 118.068, 118.922, 136.087 | 118.922 | 109.658–136.087 | 8.409 | 36.381 |

Archive requests of 64 and 512 bytes required 16384 and 2048 payload reads,
respectively, with no short reads. Every 4088/4096/65536-byte row required 257
payload reads; 4096 and 65536 produced 256 positive short reads, while 4088 had
none. Large requests therefore did not enlarge native transfers. The host path
was much slower here, but these elapsed batches do not distinguish BSP queuing,
VirtIO/FUSE transport, daemon scheduling or host filesystem service time.

Memory-backed passes near 0.25 ms are short relative to the roughly 36–41 us
clock-call loop means. Boundary overhead and run-to-run variation are material;
no constant was subtracted, and the occasional larger sample has no measured
attribution. The observations do not establish confidence intervals, per-call
latency distributions or small performance differences between large requests.
Larger fixtures/batches or scoped instrumentation would require their own
measurement decision before drawing finer conclusions.

Manual validation also exercised an even sample count, missing input, a short
ordinary file and an out-of-range request size. Failures returned status 1
without throughput or a successful summary. After all measurements, GDB showed
four idle CPU contexts, zero retained host lookup references/open handles,
idle request/hiprio queues and empty completed/file/memory work queues. No
debugger stops occurred during samples. QEMU, GDB and virtiofsd were stopped and
the disposable RAM/host fixtures removed. Owner's-host results remain for task 5.

## Focused PR tasks

- [x] **1. Measurement contract and file reads.** Settle command syntax, default
  sizes, bounded fixtures, timing and repetition rules. Add `iobench` read mode
  for archive, RAM and optional host-backed files, with explicit short-transfer
  accounting. Document reproducible manual commands and initial observations.
- [ ] **2. Writes and copies.** Agree output-file and sync policies, then add
  bounded write/copy workloads. Report acceptance and requested sync separately,
  verify resulting data, and distinguish allocation/growth from steady I/O.
- [ ] **3. Pipes and IPC.** Settle the small companion/acknowledgment protocol
  and payload sets. Measure pipe consumer completion, CALL round trips and SEND
  admission versus acknowledged completion with existing capabilities. Keep
  startup separate from transfer timing; do not introduce new scheduling APIs.
- [ ] **4. Exported files and HTTP.** Reuse the measurement tools for matched
  native/snapshot reads and a known local HTTP fixture. Report OPEN, retained
  reads and complete consumer paths separately, using existing ttcp measurements
  only with their different direction and completion boundary clearly stated.
- [ ] **5. Baseline report and handoff.** Collect repeatable representative
  results, including host results when available. Explain measured boundaries,
  instrumentation overhead and unresolved attribution. Record concrete follow-up
  candidates without implementing optimizations. Rewrite this document as the
  usage/baseline reference under docs, remove the completed checklist and update
  the milestone index.

Userland owns the tools; Pyxis owns their integration pin and documentation.
No new public ABI or compiler-container rebuild is anticipated. If a task needs
kernel instrumentation or an unresolved authority/lifetime decision, stop and
discuss that specific scope before implementing it.
