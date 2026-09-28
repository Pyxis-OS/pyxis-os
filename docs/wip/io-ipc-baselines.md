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
Exact CLI spelling, default sizes and workload bounds are settled before their
implementation tasks. Keep workloads bounded and build/package them normally;
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
transfer boundaries and larger buffers whose helpers perform multiple transfers.
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

## Focused PR tasks

- [ ] **1. Measurement contract and file reads.** Settle command syntax, default
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
