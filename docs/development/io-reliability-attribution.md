# I/O reliability and bottleneck attribution

The milestone closed after the HOST publication correction in
[PR 205](https://git.internal/PyxisOS/pyxis-os/pulls/205). Completed endpoint
receipts release delivery capacity immediately, and the agreed IPC/HTTP reruns
passed. Independent FILE and HOST profiling expose request phases and counts.
The HOST correction removed most of the measured profiler slowdown, while full
profiling still changes execution substantially.

The final combined IPC/HTTP/RAM/HOST matrix and additional measurement-resolution
work were deferred when the milestone closed. They were **not run or completed**.
This reference records implemented behavior and the separate measurements that
were collected; it does not present them as a final matched comparison.
The [original I/O/IPC baselines](io-ipc-baselines.md) remain historical evidence.

## Receipt reuse

Final receipt release performs logical cleanup under the endpoint lock without
allocation. Embedded receipts never enter the deferred destruction queue, so a
retirement link cannot outlive its delivery and affect a reused record. A
separate endpoint backing object preserves BSP allocation/destruction ownership.

A CALL record becomes reusable after the caller collects its outcome and receipt
ownership ends; a SEND record becomes reusable when receipt ownership ends.
Queued cancellation releases the unpublished receipt and request attachments,
while retaining a CALL outcome until collection. Delivered cancellation retains
the record until the provider finishes its receipt and preserves cancellation
notification. Delivered attachments remain separately owned by the provider
after receipt completion. Sixteen unfinished records still exhaust capacity.
No retry, pacing, larger delivery table or public ABI change was introduced.

All twelve IPC and four HTTP configurations completed one untimed warmup and five
verified samples with normal shutdown. IPC used fresh receiver endpoints for
each pass: CALL was sequential request/reply; SEND admitted groups of at most
eight and waited for DRAIN acknowledgment. Payloads carried no attachments.
Completion medians below are wall-clock batch milliseconds, not operation
latency percentiles or kernel CPU time.

| Mode | Messages | Payload bytes | Completion median ms |
| --- | ---: | ---: | ---: |
| CALL | 256 | 0 | 64.716 |
| CALL | 256 | 64 | 68.856 |
| CALL | 256 | 4096 | 68.344 |
| SEND | 256 | 0 | 11.148 |
| SEND | 256 | 64 | 11.307 |
| SEND | 256 | 4096 | 11.725 |
| CALL | 8 | 0 | 2.299 |
| CALL | 8 | 64 | 2.315 |
| CALL | 8 | 4096 | 2.269 |
| SEND | 8 | 0 | 0.374 |
| SEND | 8 | 64 | 0.380 |
| SEND | 8 | 4096 | 0.392 |

Reproduce each IPC configuration in a fresh boot/session with
`session app://ipcbench.pxe MODE --messages N --size BYTES --rounds 5`.
SEND admission medians for 256 messages were 1.223, 1.248 and 1.500 ms at payload
sizes 0, 64 and 4096. Eight-message admission medians were 0.037, 0.044 and
0.043 ms. Short controls remain sensitive to clock overhead and scheduling.

HTTP used the [baseline server setup](io-ipc-baselines.md#manual-reproduction)
and `iobench read http://10.0.2.2:18080/NAME --buffer BYTES --rounds 5`, adding
`--bytes 32768` for `iobench-small.bin`. Every pass verified length, deterministic
contents and EOF. OPEN fetches and stages the full body; payload reads consume
the retained snapshot. Complete consumption spans OPEN through EOF and close,
without waiting for deferred provider storage reclamation. The server recorded
24 GETs and no refetch during retained reads.

| Fixture bytes | Request bytes | OPEN median ms | Payload median ms | Complete median ms |
| ---: | ---: | ---: | ---: | ---: |
| 1048576 | 4088 | 301.779780 | 1.290460 | 303.108660 |
| 1048576 | 65536 | 300.360440 | 1.311090 | 301.724940 |
| 32768 | 4088 | 15.646590 | 0.090580 | 15.836520 |
| 32768 | 65536 | 14.662460 | 0.077060 | 14.986360 |

The 4088-byte exported transfer limit is unchanged: 1 MiB requires 257 payload
calls plus EOF, and 32 KiB requires nine plus EOF. A 65536-byte request does not
enlarge transfers. Previously failing full-size reads reached verified EOF;
the failed baseline had no full-size successful samples for a timing comparison.
These reruns establish successful consumption, not a speedup or sustained rate.

## RAM FILE profiling

`PROFILE_RIGHT_FILE` authorizes independent caller-local FILE BEGIN/SNAPSHOT/END
collection, preserving private-memory profiling meaning. RAM buffer replacement
records preparation, the boundary before publication locking, BSP service
start/end and caller resumption. Elapsed sums/maxima separate publication, queue,
service and resumption, with allocation, existing-data copy and release calls
nested inside service. Counters record attempts, success/failure, summed requested
capacity and actual copied bytes; they saturate and expose no addresses.

BSP owns request results/timestamps from publication until wakeup; the caller
then aggregates into its caller-local FILE snapshot in the separate persistent
profile allocation before releasing the request. Transient samples live in the
typed FILE replacement record in the reusable user-request allocation, which
uses the common BSP FIFO and promptly notifies an idle executor without a VM
handoff.
The publication timestamp remains immediately before publication locking, and
service timestamps still bracket the local replacement helper. Disabled
collection adds no clocks or allocations. Existing allocation, geometric growth,
failure fallback and early-wakeup behavior remain.
FILE ownership waits, incoming payload copies and nonreplacement work remain
an unattributed residual. None of these elapsed intervals is kernel CPU time.

`iobench write/copy --profile` collects measured transfer work after preparation
and before verification/reporting. Warmup is unprofiled; BEGIN/END are outside
transfer clocks and per-request instrumentation is inside. Optional sync is
separate and outside collection. The eight off/on controls used 1 MiB, 4080-byte
requests, grow/prepared output, no sync, one warmup and five samples.

| Workload | Off median ms | Profile median ms |
| --- | ---: | ---: |
| RAM write, grow | 52.811340 | 59.037810 |
| RAM write, prepared | 0.295900 | 0.656270 |
| Archive → RAM, grow | 56.624600 | 54.279920 |
| Archive → RAM, prepared | 0.519400 | 0.493820 |

Reproduction uses `iobench write home://NAME` or
`iobench copy app://share/iobench.bin home://NAME`, adding `--prepared` and/or
`--profile` for each table cell. Unique outputs and RAM stderr logs were used.
All 48 passes verified contents, length and EOF with 258 writes per pass; copies
also had 258 reads. No short transfers or failures occurred. Every profiled grow
pass reported ten successful replacements, zero failures, 4,173,840 bytes of
summed requested capacity and 2,084,880 existing-data bytes copied. Prepared
passes reported zero replacement counts/durations. Requested capacity is not a
live allocation gauge; no allocation failure was induced or observed.

Publication-to-service queue sums were 44.783–54.129 ms: 88–92% of profiled write
transfer time and 83–92% of copy time. BSP service was 2.799–5.357 ms, including
allocation-call sums of 0.357–0.695 ms, copy of 0.629–1.119 ms and release of
0.351–0.685 ms. Allocation/copy service was not the dominant measured cost.
Queue timestamps combine publication locking, BSP availability and scheduling.
These original samples predate the common-FIFO migration, when FILE publication
had no explicit BSP notification. Their near-8 ms maxima are consistent with
delayed BSP service but do not identify individual wake causes or measure the
current executor path. No growth-policy change was made.
See [RAM service-delay debt](../technical-debt.md#ram-file-bsp-service-delay).

The sequential five-sample groups combine run ordering, cache and nested
scheduling effects. They do not measure a constant profiling cost or precise
speedup. Prepared intervals and short SEND/native work need separately agreed
measurement resolution before fine-grained comparisons.

## HOST profiling and attribution limits

`PROFILE_RIGHT_HOST` authorizes independent caller-local HOST BEGIN/SNAPSHOT/END
collection. Only native READ/WRITE work is counted. Preparation/publication, BSP
forwarding, worker service start/end and caller resumption separate the initial
BSP and worker queues. Preparation timing starts before common request reservation;
the HOST publication hook records requested bytes and its timestamp immediately
before the common FIFO publication lock. Forwarding is timestamped at HOST
worker-queue entry. Transport runs from immediately before submission through
worker-observed used-ring completion. Lazy OPEN transport belongs to its native
READ/WRITE; metadata-only operations, sync and deferred cleanup are excluded.

READ/WRITE counters distinguish attempts, failures, requested/completed bytes,
positive short transfers and zero-byte results for nonzero requests. Submission
and valid completion counts are independent of FUSE success. Failed published
transport has a separate elapsed sum/maximum ending before reset; rejection
before submission has no transport event. Counters saturate. Transient samples
follow caller → executor → HOST worker ownership in the typed request. The HOST
worker clears links and input loans and publishes common completion before waking;
it never touches the record afterward. The caller merges the sample into its
caller-local HOST aggregate in the separate persistent profile allocation at
resumption; services never access that aggregate. The typed request uses the
reusable user-request allocation, and profile controls belong to the profiling
subsystem rather than the scheduler.
The common reservation remains held through reply/user-buffer copying and owned
output detachment, then ends with explicit release before another synchronous BSP
service. Disabled collection adds no clocks/allocations. Snapshots expose no
addresses, names, node IDs or remote-caller activity.

For read, `iobench --host-profile` BEGIN precedes its first clock/OPEN; END follows
the payload end clock before EOF/close, so END contributes only to complete
consumption. Write/copy BEGIN/END lie outside transfer clocks and before optional
sync. Preparation, warmup and verification remain unprofiled. A successful BEGIN
receives an END attempt on error. MEMORY, FILE and HOST collections are independent.

The measurements below predate common-FIFO HOST forwarding. The original ten
off/on controls verified all 60 passes. Each used 1 MiB, one warmup and five
samples, prepared destinations and sync off. Raw intervals,
counts, clock calibration and profile sum/max pairs remain in
[HOST samples](io-host-profile-samples.json). The command keys are `hr` (read),
`hw` (write), `hc` (HOST → RAM), `ac` (archive → HOST) and `ac8` (4088-byte copy).
Use `iobench read host://iobench.bin`, `iobench write host://NAME --prepared`,
`iobench copy host://iobench.bin home://NAME --prepared`, or
`iobench copy app://share/iobench.bin host://NAME --prepared`, adding
`--host-profile` for on and `--buffer 4088` for `ac8`. Defaults are read=4088,
write/copy=4080 bytes. Commands ran off then on in that workload order.

| Workload | Native HOST calls | Transport submissions/completions |
| --- | ---: | ---: |
| Read, 4088 | 257 READ | 258 / 258 |
| Prepared write, 4080 | 258 WRITE | 258 / 258 |
| HOST → prepared RAM, 4080 | 258 READ | 258 / 258 |
| Archive → prepared HOST, 4080 | 258 WRITE | 258 / 258 |
| Archive → prepared HOST, 4088 | 513 WRITE | 513 / 513 |

All active directions completed 1048576 bytes with no native short transfer,
failure or in-window EOF. Fresh read handles include lazy FUSE_OPEN. The 4088-byte
copy drains each full read as 4080 bytes plus an 8-byte suffix: the benchmark
counts 256 helper short writes, while all capped native HOST writes are full.

Original profiled medians were 13.7–16.1× off controls. Initial BSP queue time
accounted for 73.8–78.8% of instrumented transfer time, and transport for
88.6–90.7% of worker service. All 25 profiled samples had exact top-level phase-sum
equality and transport sums no greater than service. Those phases describe
instrumented execution and cannot partition unprofiled latency. Transport still
combines guest/host scheduling, device/daemon/backing work and guest completion
observation; no host-side component timestamps were collected.

The [controlled slowdown experiment](experiments/host-profile-slowdown/README.md)
and its [samples](experiments/host-profile-slowdown/samples.json) preserve the
six-cell notification × off/counts/full study and disposable patches. All six
warmups and thirty samples verified the fixture. Full-profile prepared-write
medians fell from 1855.850 to 234.100 ms with notification; off controls were
124.337 and 116.313 ms, and counts-only 132.160 and 116.876 ms. Initial queue mean
fell from 5.331 to 0.137 ms/request. This establishes notification-dependent
amplification for that workload. Queue-sweep/idle/timer timing is an inference;
individual wake causes were not traced. Counts-only patches remain experiment
artifacts, with unavailable timings omitted; no permanent SDK mode was accepted.

## HOST publication notification

HOST operations now publish through the common BSP FIFO. Publication detaches an
idle executor's waiter under the request lock, then wakes it after unlocking;
a running or already notified executor needs no extra notification. The saved
caller wait preserves early completion and normal parked wake. On CPU 0, the
caller parks to let the executor run; there is no self-IPI. The executor forwards
HOST work to its existing transport worker and reaches a scheduling boundary
without waiting for transport completion. The HOST worker owns final completion,
including failures, while the executor can service another request. Cleanup
queues retain their existing service paths. See [SMP](../kernel/smp.md) and
[HOST ownership](../devices/virtio-fs.md#native-directory-and-file-objects).

The prior publication correction used `task_submit_hostfs()`: it released the
scheduler queue lock and invoked the BSP reschedule helper before sleeping.
This allowed AP publication after a scheduler queue sweep to wake the BSP.
Code review covered early and parked completion without forced artificial
interleavings. That correction preceded the common executor migration; the
measurements below describe that earlier implementation.

The prior correction repeated all ten HOST controls on four CPUs and the first six
on one CPU, with unchanged preparation, sizes and timing boundaries. All 16
warmups and 80 measured passes verified length, contents and EOF. All 40 profiled
samples matched the counts above with no failure, short native transfer,
in-window EOF or saturation. Raw readings and built ELF/ISO hashes remain in
[notification samples](io-host-notification-samples.json).

Payload/transfer medians in milliseconds; read uses payload elapsed:

| Workload | Original off/on, four CPUs | Corrected off/on, four CPUs | Corrected off/on, one CPU |
| --- | ---: | ---: | ---: |
| HOST read, 4088 | 117.733 / 1893.883 | 118.143 / 219.829 | 132.632 / 229.833 |
| Prepared HOST write, 4080 | 128.085 / 1894.893 | 98.343 / 220.392 | 128.666 / 234.705 |
| HOST → prepared RAM, 4080 | 143.897 / 1968.985 | 116.196 / 216.977 | 134.428 / 237.846 |
| Archive → prepared HOST, 4080 | 136.342 / 1998.989 | 121.205 / 208.027 | not run |
| Archive → prepared HOST, 4088 | 245.644 / 3882.483 | 231.671 / 435.147 | not run |

Corrected four-CPU full-profile medians are 88.4–89.6% lower than the original
controls, but on/off ratios remain 1.72–2.24× (one CPU: 1.73–1.82×). These
sequential groups in separate boots are not randomized trials. No precise
unprofiled speedup, scalability, host-storage cost or subtractable profiling
cost follows from these differences. Full profiling still cannot partition
normal latency; see [profiling debt](../technical-debt.md#host-file-profiling-perturbation).

## Provenance and validation

All measurements were collected on 2026-09-28 in nested KVM on Fedora 44/Linux
6.19.10 (correction: 6.19.10-300.fc44.x86_64), exposed i9-12900K, QEMU 10.2.2/Q35,
`-cpu max`, 256 MiB and Pyxis GCC 16.2.0. The matching
`/usr/share/edk2/ovmf/OVMF_CODE.fd` and `OVMF_VARS.fd` pair was used. Four-CPU
work ran on CPU 1; the corrected one-CPU boot used CPU 0. Virtio entropy was on.
No owner-host or physical-hardware results are claimed.

| Measurement | Kernel runtime revision | Userland runtime revision |
| --- | --- | --- |
| Receipt IPC/HTTP | `1daca3e3c97fdb3dd4ae0b0a039d777155381f4c` | `bef299196840cc9f4a9d7b840fe45faa7dc627a6` |
| RAM profiling | `0757faecc186c1b3fb1649a103ae18980736bfba` | `f180e4330a9218aa08bc49c93be2458c7c9005f5` |
| Original HOST profiling | `7d9f3557838d59e22751051506dcf2dd22dd68df` | `f68b4d8c83fec862ebca47ed92ccccfcc6992aee` |
| HOST correction | `8386424781b0ac8abcb29f43a7f51db2b7f20602` | `f86daf40ae2c59e00547cc2eb33c19a31731eebb` |

Ports stayed `6ec1290f87882392390e6a889be61ba3ac1448d1`; lwIP stayed
`a1aadb91a50360ff5b52864f7cec810b8162ee85`. Later documentation/gitlink commits
did not alter measured runtime code. The experiment report records its own
baseline and patch/image identities. The
[original full report](https://git.internal/PyxisOS/pyxis-os/src/commit/627331f6d3aa080fd1cbe131472a032f2375d33c/docs/wip/io-reliability-attribution.md)
preserves every individual IPC/HTTP/RAM sample, RAM phase sum/maximum, clock
calibration, original command and detailed lifecycle observation.

IPC/RAM used networking off and no HOST export. HTTP used QEMU user networking,
numeric `10.0.2.2`, and Python 3.14.3 on loopback port 18080 serving tmpfs fixtures:
POSIX checksums 1625934143 (1048576 bytes) and 1349564844 (32768 bytes).
HOST used fresh virtiofsd 1.14.0 sessions and disposable tmpfs exports with
namespace sandboxing, `--inode-file-handles=never --no-announce-submounts
--rlimit-nofile=0`, default auto cache, no writeback, no cache eviction and sync
off. See [host-service setup](../devices/virtio-fs.md#start-the-host-service). No durable-media
cost or concurrent mutation result is claimed.

Ordinary `make -j16 image` builds and manual interactive runs passed. No logging
or debugger stops occurred inside timed samples; clock overhead was recorded,
never subtracted. Eighteen existing IPC/export lifecycle modes exercised capacity,
attachments, cancellation, retirement and teardown. A separate untimed debugger
run observed receipt-slot generation advance from 4 to 5 on reuse; the unchanged
lookup rejects the old generation. No stale invocation or fault was injected.
Memory-profile controls and a combined RAM/HOST copy also passed. Malformed/denied
profile calls, simultaneous MEMORY/HOST collection and transport failures were
inspected but not separately exercised at runtime.

The correction exercised create/open/read/write/close/rename/readdir/removal,
fixture checksum, missing-file errors and a four-CPU read-only denial. Read-only
post-workload GDB checks found inspected request, retirement and transport queues
empty, no active HOST profile, and zero HOST lookup references/open handles.
These are point-in-time observations, not proof of persistent-provider storage
accounting. Owned QEMU/debugger/server/daemon jobs and temporary files were cleaned
up. No new tests, fault injection, automation or compiler-container rebuild was
needed. Missing devices, timeout/reset recovery, concurrent clients and durable
storage were not exercised by the correction.

## Deferred work

Additional resolution/coverage decisions and a final combined matched matrix
including IPC/HTTP are independent future work, carried in
[attribution debt](../technical-debt.md#io-baseline-attribution-and-coverage). Existing
correction-specific reruns remain valid evidence; they do not fill that matrix.
A FILE notification change, normal-workload HOST phase attribution, host-side
timing and any clock/counts-only design need separately agreed scope.

Shared bulk buffers, larger FILE payloads, async I/O, cache/read-ahead policy,
allocator concurrency, cross-space contention, attachment-cost studies,
mixed-workload fairness and process CPU accounting were outside this milestone.
CALL deadlines still do not bound RECEIVE, pipe or process waits. Unfinished
receipts can still hold all sixteen records; see
[endpoint cancellation/capacity](../technical-debt.md#endpoint-cancellation-and-capacity).
