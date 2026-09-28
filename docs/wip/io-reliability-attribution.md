# I/O reliability and bottleneck attribution

Status: task 1 is complete: logical receipt release fixes completed-work capacity
retention, and the agreed IPC/HTTP reruns passed. Task 2 is RAM file-growth
attribution; its instrumentation contract remains to be discussed. This follows
the completed [I/O and IPC baselines](../io-ipc-baselines.md).

## Outcome and scope

Make sequential IPC and exported-file consumption reliable under sustained use,
then explain the measured RAM file-growth and host FILE costs well enough to
choose justified follow-ups. Finish with comparable benchmark reruns and a
report of what changed, what was learned and what remains unknown.

The baseline exposed three concrete questions:

- Completed endpoint receipts retained delivery slots until BSP destruction.
  Sequential CALL and acknowledged SEND runs reached QUEUE_FULL, and ordinary
  1 MiB HTTP snapshot reads failed with EAGAIN after a successful body fetch.
- RAM writes growing from zero took a 55.654 ms median versus 0.314 ms for
  prepared overwrite in the matched no-sync runs. Allocation, copying, BSP
  service and task resumption have not been separated.
- Host reads took roughly 119 ms per MiB in nested KVM. Guest queueing,
  VirtIO/FUSE transport, host scheduling and backing service are combined;
  host write/sync fixtures were in tmpfs, not on durable storage.

These are recorded observations, not fixed failure thresholds or diagnoses of
allocator, scheduler or device performance. There is no arbitrary throughput
target and no requirement to produce an optimization for every investigation.
An attribution task can finish with evidence that the existing behavior is
appropriate, or with a concrete proposal requiring a separate implementation PR.

Preserve capability authority, subsystem boundaries, BSP allocation/destruction
and VM-mutation rules, and existing scheduling and delivery guarantees. Do not
hide failures by enlarging queues, retrying failed operations, adding sleeps or
shrinking fixtures. Genuine capacity exhaustion must still report QUEUE_FULL.
Public interfaces change only if a concrete need is discussed and agreed.

## Measurement rules

Reuse `iobench`, `ipcbench` and the baseline's deterministic fixtures and timing
boundaries. Keep successful short-batch results as controls alongside the failing
larger cases. Record exact kernel/userland/dependency revisions, commands,
individual samples, clock overhead, CPU placement, devices and backing storage.
Keep nested-VM, owner's-host and physical-hardware results separate.

Before adding instrumentation, agree the specific question, event boundaries,
collection method and lifetime/overhead contract. Prefer narrow counters or
timestamps around the affected path over a generic tracing/profiling framework.
No logging or debugger stops inside timed samples. Instrumentation must not
silently alter scheduling, pace requests or become an unexplained part of a
before/after comparison. State whether measurements include instrumentation.

Do not call an end-to-end interval kernel CPU time. If guest observations can
only bound transport plus host service together, report that limit; host-side
instrumentation needs its own explicit scope. Longer batches, additional fixture
sizes or new benchmark controls also need agreement before implementation.

Use ordinary builds, manual interactive QEMU runs and debugger inspection.
No new tests, fault-injection facilities, boot automation, CI performance gates
or benchmark framework. Existing CI validates integration, not performance.
CALL deadlines still do not bound RECEIVE, pipe or process waits; this milestone
does not add general cancellation or wait APIs.

## Task 1: implemented receipt reuse and benchmark reruns

The kernel now releases logical receipt ownership at the final receipt reference,
under the endpoint lock and without allocation. An embedded receipt never enters
the deferred destruction queue, so an old retirement link cannot outlive its
original delivery and affect a reused record. Endpoint backing storage has a
separate retirement object and remains allocated and destroyed on the BSP.

A CALL record becomes reusable after its caller has collected the outcome and
receipt ownership has ended; a SEND record becomes reusable after receipt
ownership ends. Queued cancellation releases the unpublished receipt and retained
request attachments while preserving a CALL's outcome until collection. Delivered
cancellation retains the record until the provider finishes its receipt and
preserves cancellation notification. Already delivered attachment handles remain
separately owned by the provider after receipt completion. Sixteen outstanding
records still exhaust capacity. The change adds no retry,
pacing, larger delivery table, public ABI or benchmark instrumentation.

The twelve IPC configurations and four HTTP configurations below each completed
one untimed warmup and five verified samples, with normal benchmark shutdown.
Previously failing 256-message IPC and full 1 MiB HTTP consumption now completed
in these runs. The existing lifecycle demos and separate debugger inspection
also passed. The [original baseline](../io-ipc-baselines.md) retains its
historical results. These reruns establish successful consumption for the agreed
workloads; they do not establish a speedup or sustained throughput.

### Measurement provenance

Measured on 2026-09-28 with kernel code committed as
`1daca3e3c97fdb3dd4ae0b0a039d777155381f4c` and userland runtime
`bef299196840cc9f4a9d7b840fe45faa7dc627a6`. The accompanying userland documentation
commit `d5a04dc00a44f0ee2108685f7f0d5898f69fe4d2`
([PR 74](https://git.internal/chronium/pyxis-userland/pulls/74)) changes only
benchmark README text. Ports remained
`6ec1290f87882392390e6a889be61ba3ac1448d1` and lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`.

These measurements used the baseline's nested-KVM environment: Fedora 44,
Linux 6.19.10, exposed i9-12900K, QEMU 10.2.2/Q35 with `-cpu max`, four CPUs,
256 MiB and Pyxis GCC 16.2.0. OVMF used the matching
`/usr/share/edk2/ovmf/OVMF_CODE.fd` and `OVMF_VARS.fd` pair. Workloads ran on CPU 1
with virtio entropy enabled and no host filesystem export. IPC networking was
disabled. HTTP used QEMU user networking and numeric address `10.0.2.2`, avoiding
DNS. Python 3.14.3 served the fixtures from host tmpfs through loopback port 18080.
Their POSIX checksums were 1349564844 for 32768 bytes and 1625934143 for 1048576
bytes; the small fixture matched the full fixture's prefix. No cache eviction,
owner-host or physical-hardware measurement is claimed.

Ordinary image builds and manual interactive commands were used. No debugger
stops or added instrumentation occurred inside timed samples. IPC and HTTP
post-workload GDB inspection found four CPUs, CPU 1 online, and empty completed,
pipe, endpoint and memory work queues and object retirement queue. This is an
observation of pending kernel work, not proof that persistent providers hold no
allocations. Clock overhead is reported separately and was not subtracted.

### IPC commands and samples

Run each command in a fresh boot/session because `session` replaces the shell:

```text
session app://ipcbench.pxe call --messages 256 --size 0 --rounds 5
session app://ipcbench.pxe call --messages 256 --size 64 --rounds 5
session app://ipcbench.pxe call --messages 256 --size 4096 --rounds 5
session app://ipcbench.pxe send --messages 256 --size 0 --rounds 5
session app://ipcbench.pxe send --messages 256 --size 64 --rounds 5
session app://ipcbench.pxe send --messages 256 --size 4096 --rounds 5
session app://ipcbench.pxe call --messages 8 --size 0 --rounds 5
session app://ipcbench.pxe call --messages 8 --size 64 --rounds 5
session app://ipcbench.pxe call --messages 8 --size 4096 --rounds 5
session app://ipcbench.pxe send --messages 8 --size 0 --rounds 5
session app://ipcbench.pxe send --messages 8 --size 64 --rounds 5
session app://ipcbench.pxe send --messages 8 --size 4096 --rounds 5
```

The benchmark uses fresh receiver endpoints for every warmup/sample. CALL is
sequential request/reply; SEND admits groups of at most eight, then waits for a
DRAIN acknowledgment confirming receiver consumption. No measured payload carries
attachments. Every sample confirmed its requested message count and bytes. The
eight-message default remains unchanged. These are wall-clock batch intervals,
not individual-operation latency percentiles or kernel CPU time.

Completion values below are milliseconds, in sample order. Clock batch means are
nanoseconds per read. Ranges are minimum–maximum of the five samples.

| Mode | Messages | Payload bytes | Clock ns | Completion samples ms | Median ms | Range ms |
| --- | ---: | ---: | ---: | --- | ---: | --- |
| CALL | 256 | 0 | 33770 | 64.716, 62.367, 61.884, 68.752, 67.098 | 64.716 | 61.884–68.752 |
| SEND | 256 | 0 | 37850 | 11.148, 12.583, 11.027, 12.098, 10.732 | 11.148 | 10.732–12.583 |
| CALL | 256 | 64 | 38794 | 68.856, 76.558, 67.997, 71.933, 67.977 | 68.856 | 67.977–76.558 |
| SEND | 256 | 64 | 36328 | 11.017, 10.951, 11.414, 11.545, 11.307 | 11.307 | 10.951–11.545 |
| CALL | 256 | 4096 | 36600 | 68.139, 68.344, 69.249, 73.226, 68.004 | 68.344 | 68.004–73.226 |
| SEND | 256 | 4096 | 39529 | 12.069, 11.408, 11.523, 11.725, 11.816 | 11.725 | 11.408–12.069 |
| CALL | 8 | 0 | 37766 | 2.328, 2.112, 2.359, 2.299, 2.204 | 2.299 | 2.112–2.359 |
| SEND | 8 | 0 | 36778 | 0.360, 0.374, 0.393, 0.368, 0.395 | 0.374 | 0.360–0.395 |
| CALL | 8 | 64 | 37028 | 2.269, 2.506, 2.145, 2.315, 3.864 | 2.315 | 2.145–3.864 |
| SEND | 8 | 64 | 36118 | 1.200, 0.374, 0.369, 1.208, 0.380 | 0.380 | 0.369–1.208 |
| CALL | 8 | 4096 | 37986 | 4.142, 2.269, 2.063, 2.215, 2.305 | 2.269 | 2.063–4.142 |
| SEND | 8 | 4096 | 38311 | 0.378, 0.412, 0.389, 0.392, 0.401 | 0.392 | 0.378–0.412 |

SEND admission sums the intervals around the admission loops; completion includes
DRAIN acknowledgments and intervening clock boundaries. Short controls remain
sensitive to clock-call overhead and scheduling.

| Messages | Payload bytes | Admission samples ms | Median ms | Range ms |
| ---: | ---: | --- | ---: | --- |
| 256 | 0 | 1.193, 1.424, 1.223, 1.306, 1.186 | 1.223 | 1.186–1.424 |
| 256 | 64 | 1.248, 1.232, 1.253, 1.184, 1.313 | 1.248 | 1.184–1.313 |
| 256 | 4096 | 1.460, 1.416, 1.550, 1.500, 1.652 | 1.500 | 1.416–1.652 |
| 8 | 0 | 0.037, 0.037, 0.079, 0.037, 0.061 | 0.037 | 0.037–0.079 |
| 8 | 64 | 0.120, 0.044, 0.044, 0.121, 0.035 | 0.044 | 0.035–0.121 |
| 8 | 4096 | 0.042, 0.043, 0.043, 0.043, 0.046 | 0.043 | 0.042–0.046 |

### HTTP commands and samples

Use the [baseline's host-server setup](../io-ipc-baselines.md#manual-reproduction)
with the same two fixtures. The four commands were:

```text
iobench read http://10.0.2.2:18080/iobench.bin --buffer 4088 --rounds 5
iobench read http://10.0.2.2:18080/iobench.bin --buffer 65536 --rounds 5
iobench read http://10.0.2.2:18080/iobench-small.bin --bytes 32768 --buffer 4088 --rounds 5
iobench read http://10.0.2.2:18080/iobench-small.bin --bytes 32768 --buffer 65536 --rounds 5
```

Each pass verified exact length, deterministic contents and EOF. OPEN includes
fetching and staging the full body; payload reads consume the retained snapshot;
complete consumption spans OPEN through EOF and close. Verification is outside
all intervals. Complete consumption does not wait for deferred provider storage
reclamation. The server recorded 24 GETs: one for every warmup/sample, with no
refetch during retained reads.

The 4088-byte transfer limit remains unchanged: 1 MiB requires 257 payload calls
plus EOF; 32 KiB requires nine plus EOF. Requests of 65536 therefore do not enlarge
exported FILE transfers. Full 1 MiB reads now reach verified EOF instead of the
baseline's partial EAGAIN failures. Those failed baseline runs had no successful
measured samples, so no full-size before/after timing comparison is available.

| Fixture bytes | Request bytes | Clock mean ns |
| ---: | ---: | ---: |
| 1048576 | 4088 | 38535 |
| 1048576 | 65536 | 44584 |
| 32768 | 4088 | 36292 |
| 32768 | 65536 | 43452 |

All interval values below are milliseconds; six decimal places preserve the
recorded nanosecond values. Samples appear in execution order within each row.

| Fixture bytes | Request bytes | Interval | Samples ms | Median ms | Range ms |
| ---: | ---: | --- | --- | ---: | --- |
| 1048576 | 4088 | OPEN | 304.032590, 301.990190, 294.516930, 301.779780, 290.514070 | 301.779780 | 290.514070–304.032590 |
| 1048576 | 4088 | Payload | 2.362820, 1.260160, 1.490290, 1.290460, 1.228990 | 1.290460 | 1.228990–2.362820 |
| 1048576 | 4088 | Complete | 306.434660, 303.289490, 296.046510, 303.108660, 291.781780 | 303.108660 | 291.781780–306.434660 |
| 1048576 | 65536 | OPEN | 300.693450, 296.498490, 311.793460, 300.360440, 295.115010 | 300.360440 | 295.115010–311.793460 |
| 1048576 | 65536 | Payload | 1.191920, 1.502710, 1.311090, 1.325730, 1.273050 | 1.311090 | 1.191920–1.502710 |
| 1048576 | 65536 | Complete | 301.985270, 298.040900, 313.143440, 301.724940, 296.427210 | 301.724940 | 296.427210–313.143440 |
| 32768 | 4088 | OPEN | 13.575100, 15.646590, 17.438250, 14.223770, 16.427440 | 15.646590 | 13.575100–17.438250 |
| 32768 | 4088 | Payload | 0.074620, 0.125220, 0.076810, 0.090580, 0.196180 | 0.090580 | 0.074620–0.196180 |
| 32768 | 4088 | Complete | 13.687800, 15.836520, 17.553790, 14.363680, 16.747860 | 15.836520 | 13.687800–17.553790 |
| 32768 | 65536 | OPEN | 17.411030, 14.662160, 15.420890, 14.662460, 13.862310 | 14.662460 | 13.862310–17.411030 |
| 32768 | 65536 | Payload | 0.158120, 0.072750, 0.075440, 0.195700, 0.077060 | 0.077060 | 0.072750–0.195700 |
| 32768 | 65536 | Complete | 17.621560, 14.774400, 15.535490, 14.986360, 13.978710 | 14.986360 | 13.978710–17.621560 |

### Lifecycle and debugger validation

The existing demos ran manually in fresh CPU 1 sessions with networking off,
using the same four-CPU image. No new tests or injected failures were added.
Each ordinary mode reached its existing completion checks; intentional exit
modes produced the expected closure outcomes:

| Executable | Modes | Observed coverage |
| --- | --- | --- |
| `session app://server.pxe` | `--wide`, `--send` | 4096-byte messages, four attachments both ways, sender exit before RECEIVE, attachments still readable after FINISH. |
| `session app://server.pxe` | `--abandon`, `--close`, `--exit` | Abandonment and receiver/process teardown preserve delivered versus not-delivered outcomes. |
| `session app://server.pxe` | `--saturate`, `--mixed` | Sixteen unfinished records still cause genuine QUEUE_FULL for the next CALL/SEND; admitted work completes. |
| `session app://server.pxe` | `--expired`, `--queued-timeout`, `--deadline-reply` | Expiry before admission/delivery and a successful timely reply retain their outcomes. |
| `session app://server.pxe` | `--delivered-timeout`, `--cancel-full`, `--cancel-finish` | Delivered cancellation, late REPLY rejection, notification at full capacity, and FINISH before reading a pending notice; delivered attachments survive. |
| `session app://counter.pxe` | default, `--withdraw`, `--queued-withdraw` | Natural retirement, withdrawal before/after delivery, ACK and ID reuse while stale old export grants stay closed. |
| `session app://counter.pxe` | `--retire-full`, `--exit` | RETIRE/ACK while sixteen ordinary SEND records remain occupied; provider exit with a received export operation. |

A separate `ipcbench call --size 0 --rounds 1` run was inspected during untimed
warmup using hardware breakpoints and read-only GDB inspection. For the same
embedded receipt address and capability slot 3, the old handle was `0x400000003`.
CLOSE cleared the entry and advanced generation 4 to 5; the next delivery used
`0x500000003`. The unchanged handle lookup rejects the old generation. No stale
handle invocation or kernel function call was injected, and this debugger run
is excluded from all timing tables. It completed normally after detaching.

After IPC, HTTP and final lifecycle workloads, GDB showed four CPU contexts,
CPU 1 online, and empty completed-task, pipe, endpoint, memory-work and object
retirement queues. These observations do not prove persistent provider storage
accounting. All owned QEMU/debugger/server processes were stopped and temporary
HTTP fixtures removed. The ordinary `make -j16 image` passed; final header-comment
and documentation changes do not change the measured code. No compiler-container
rebuild is needed.

## Focused PR tasks

- [x] **1. Resolve endpoint receipt reclamation.** Separate logical receipt
  release and delivery reuse from BSP backing destruction. Preserve caller
  collection, cancellation, attachment ownership, retirement and stale handles.
  All twelve IPC and four HTTP configurations passed one warmup and five
  samples; eighteen existing lifecycle modes and GDB ownership/cleanup checks
  passed. The implemented guarantee and remaining live-work limit are recorded
  above and in technical debt.

- [ ] **2. Attribute RAM file-growth cost.** Separate request publication and
  BSP queue wait, allocation/copy service, and completion-to-resumption time.
  Count growth operations and copied bytes so repeated growth is distinguishable
  from expensive individual operations. Compare grow-from-zero with prepared
  overwrite using the same 1 MiB payload and 4080-byte requests, without sync;
  retain write/copy correctness checks outside transfer timing. Existing private
  memory profiling does not cover this FILE path. Agree narrow instrumentation
  first, then report the dominant costs and unresolved intervals. Propose any
  allocator, growth-policy or notification change separately from attribution.

- [ ] **3. Attribute host FILE latency.** Use matched read/write/copy workloads
  to distinguish guest service-queue wait, submission-to-observed-completion,
  and completion-to-caller-resumption time. Describe what the middle interval
  combines rather than assigning it all to the host filesystem or the device.
  Keep request counts and confirmed short transfers visible; the 4088/4080-byte
  read/write mismatch can add suffix writes. Record daemon, cache and backing
  conditions. Keep sync separate; disk-backed fixtures are needed only if the
  agreed question concerns durable-media sync. Do not change transfer sizes,
  batching, caching or scheduling merely to improve the reported number.

- [ ] **4. Improve resolution and publish the comparison.** Review the evidence
  from the first three tasks and settle the smallest additional batch/coverage
  changes needed for meaningful comparisons. In particular, short native and
  SEND intervals approach clock-call overhead; longer safe workloads can improve
  resolution without subtracting a presumed constant. Repeat affected benchmarks
  after each implemented correction, then collect a final matched matrix including
  the previously failing IPC and HTTP workloads. Preserve the original baseline;
  state when changed workload or timing definitions prevent direct comparison.
  Owner-host measurements are useful when available, but are not required to
  finish this milestone. Publish results and limitations, resolve or update
  relevant technical debt, and move this WIP document into a concise implemented
  report under `docs`, removing the checklist and updating incoming links.

## Completion and boundaries

Completion requires verified consumption of the formerly failing workloads,
evidence-backed conclusions for file growth and host service, and a reproducible
comparison report. Reruns belong both to the relevant correction PR and the final
handoff; do not postpone validation of the reliability fix until task 4.

Userland owns benchmark changes; Pyxis owns kernel changes, integration and the
report. Publish dependent userland PRs before updating gitlinks. No ports or
compiler-container change is anticipated. Resolve implementation and measurement
decisions at the start of each task rather than inventing them in this plan.

Shared bulk buffers, larger FILE payloads, async I/O, cache/read-ahead policy,
allocator concurrency, cross-space contention, attachment-cost studies and
general process CPU accounting remain separate work. This milestone does not
attempt to resolve every coverage gap listed in the baseline. See
[attribution debt](../technical-debt.md#io-baseline-attribution-and-coverage) and
[receipt reclamation debt](../technical-debt.md#endpoint-throughput-limited-by-deferred-receipt-reclamation)
for the original consequences and revisit points.
