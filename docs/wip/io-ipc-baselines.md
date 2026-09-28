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
The read, write, copy, pipe and endpoint contracts below are implemented.
Remaining exported-file/HTTP workload details are settled before task 4.
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
work or spinning indefinitely for capacity. The task-3 contract below fixes payload sets and completion acknowledgments.
Measured messages carry no capability attachments; startup grant transfers are
outside timing.

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

## Implemented task 2: writes and copies

The new modes use libpyxis file capabilities, while task 1's read mode continues
to use libc descriptors. This exposes exclusive creation, resize, explicit
offsets and sync on a retained handle without extending libc or the kernel ABI.
The [tool reference](../../userspace/iobench/README.md#writes-and-copies) describes
the full command and accounting contract.

```text
iobench write home://write-grow.bin
iobench write home://write-prepared.bin --prepared --sync
iobench write host://write-grow.bin --sync
iobench write host://write-prepared.bin --prepared --sync
iobench copy app://share/iobench.bin home://copy.bin
iobench copy app://share/iobench.bin home://copy-prepared.bin --prepared --sync --buffer 4088
iobench copy host://iobench.bin home://copy-host.bin --buffer 65536
```

Each output must be a new file; an existing destination fails without being
opened or truncated. There is no replacement option. The output handle remains
held across all passes and is used for preparation, writes, sync and verification;
copy retains its source handle too. Copy verifies its source before creating
output, then performs actual reads within the measured loop. Dedicated files
must remain free of concurrent mutation. A rename does not change the held
object, but the output path printed afterward need no longer name it.

Both modes produce the same deterministic 1 MiB fixture. Requests default to
4080 bytes, with the existing 1..65536 bound, one untimed warm-up and five
measured passes (1..100 selectable). Write generates payload bytes outside
timing. Copy drains every read completely before the next read; confirmed read
and written byte counts are independent, and copy throughput counts the output
payload once. All reporting and verification are outside the transfer interval.

Default preparation resizes the newly created output to zero before every pass;
timed writes include growth and any allocation. `--prepared` instead fully
writes contrasting bytes beforehand and measures overwrite of that storage.
Host copy-on-write or other backing behavior can still allocate. Creation,
resizing and preparation are not included in the transfer result, and neither
case is a cold-cache workload.

`--sync` syncs the reset/prefilled file outside timing, then separately measures
one file sync immediately after the payload transfer and before verification.
Warm-up also syncs, untimed. Transfer and sync samples and summaries remain
separate; a requested sync failure fails the sample. RAM sync is a no-op. Host
sync covers the file's data/metadata under the backend contract, without parent
directory synchronization or a promise of durable creation of its pathname.

Every completed pass verifies all bytes and EOF through the held output handle.
Positive short transfers advance by confirmed progress; failures stop the run,
never replay uncertain mutations and suppress the failed pass's throughput and
the successful-run summary. Handles close on all exit paths. The file stays
behind for inspection and manual removal, including on failure; host creation
errors can themselves leave a file. No automatic unlink or rollback occurs.

Use `iobench read OUTPUT` to inspect a successful output, then explicitly remove
only the selected disposable names. For writable host runs, use the same
[virtio-fs setup](../virtio-fs.md#start-the-host-service) as task 1 but omit the
daemon's `--readonly`. The CPU 2 session still carries a read-only host grant.
No compiler-container rebuild is needed.

### Write/copy observations

Measured on 2026-09-28 with kernel/source `cd99791`, userland implementation
`0659684`, ports `6ec1290` and lwIP `a1aadb9`. The subsequent userland documentation
clarification changes no measured code. The ordinary image build passed. This
used the same Fedora 44 nested-KVM host, exposed i9-12900K, Linux 6.19.10, Pyxis
GCC 16.2.0 and QEMU 10.2.2 Q35 configuration described in task 1: four CPUs,
256 MiB, `-cpu max`, workload CPU 1, normal presenter/shell/provider tasks,
networking off and entropy on. The new dedicated export was
`/tmp/pyxis-iobench-write-share`, with the socket at
`/tmp/pyxis-iobench-write-socket/fs.sock`; virtiofsd 1.14.0 used task 1's options
except for omitting `--readonly`. The host's `/tmp` is tmpfs: the host-backed
fixture and outputs were themselves RAM-backed. These host results exercise the
VirtIO/FUSE path to host memory, including its sync request/acknowledgment; no
disk-backed write or durable-media sync was measured. No cache eviction or
debugger stops occurred during measurements.

The commands above and these additional cases each ran once, one workload at a
time, with one untimed warm-up and five measured passes. Every measured pass
wrote and verified 1048576 bytes with no failure; copies also read that many.
The fixtures and output names were all disposable.

```text
iobench write host://write-large.bin --prepared --buffer 65536
iobench copy app://share/iobench.bin host://copy-app.bin --prepared --sync
iobench copy host://iobench.bin host://copy-host.bin
iobench write home://write-overwrite.bin --prepared
```

The following rows preserve sample order and show elapsed milliseconds rounded
to three decimal places. Throughput counts one MiB of confirmed output and
excludes any separately requested sync.

| Workload | Request bytes | Sync | Transfer samples, ms | Median (range), ms | MiB/s at median | Clock loop, us/read |
| --- | ---: | --- | --- | --- | ---: | ---: |
| Write RAM grow | 4080 | no | 52.311, 58.975, 55.654, 52.200, 58.565 | 55.654 (52.200–58.975) | 17.968 | 36.025 |
| Write RAM prepared | 4080 | yes | 0.296, 0.272, 0.262, 0.292, 0.276 | 0.276 (0.262–0.296) | 3628.184 | 38.579 |
| Write host grow | 4080 | yes | 134.973, 130.145, 126.127, 124.094, 120.883 | 126.127 (120.883–134.973) | 7.929 | 35.961 |
| Write host prepared | 4080 | yes | 146.636, 143.855, 138.121, 123.431, 119.709 | 138.121 (119.709–146.636) | 7.240 | 39.414 |
| Write host prepared | 65536 | no | 139.330, 124.139, 117.045, 196.232, 128.607 | 128.607 (117.045–196.232) | 7.776 | 36.985 |
| Copy archive to RAM grow | 4080 | no | 56.429, 58.390, 58.232, 58.710, 54.664 | 58.232 (54.664–58.710) | 17.173 | 37.223 |
| Copy archive to RAM prepared | 4088 | yes | 0.574, 0.591, 0.691, 0.598, 0.554 | 0.591 (0.554–0.691) | 1691.561 | 34.780 |
| Copy host to RAM grow | 65536 | no | 185.792, 202.436, 202.424, 175.258, 194.501 | 194.501 (175.258–202.436) | 5.141 | 38.314 |
| Copy archive to host prepared | 4080 | yes | 144.825, 136.767, 124.494, 134.924, 142.196 | 136.767 (124.494–144.825) | 7.312 | 37.482 |
| Copy host to host grow | 4080 | no | 262.924, 259.927, 283.478, 268.439, 269.459 | 268.439 (259.927–283.478) | 3.725 | 36.913 |
| Write RAM prepared | 4080 | no | 0.314, 0.309, 0.355, 0.309, 0.347 | 0.314 (0.309–0.355) | 3181.876 | 40.152 |

Sync samples come from the same passes in the same order:

| Workload | File sync samples, ms | Median (range), ms |
| --- | --- | --- |
| Write RAM prepared | 0.036, 0.036, 0.036, 0.036, 0.036 | 0.036 (0.036–0.036) |
| Write host grow | 0.260, 0.272, 6.820, 8.768, 0.378 | 0.378 (0.260–8.768) |
| Write host prepared | 0.324, 0.809, 0.269, 0.285, 0.237 | 0.285 (0.237–0.809) |
| Copy archive to RAM prepared | 0.032, 0.033, 0.033, 0.046, 0.032 | 0.033 (0.032–0.046) |
| Copy archive to host prepared | 3.360, 1.476, 2.324, 1.136, 3.919 | 2.324 (1.136–3.919) |

All writes took 258 payload helper calls. The 65536-byte write requests produced
257 positive short writes; 4080-byte requests produced none. Copies at 4080
took 258 reads and 258 writes, with no short transfers. Copies at 4088 and 65536
took 257 reads and 513 writes, with 256 short writes; only the 65536 case also
had 256 short reads. This confirms the expected cost of draining the eight-byte
suffix when native read and write limits differ.

For matched 4080-byte RAM writes without sync, grow-from-zero measured a
55.654 ms median versus 0.314 ms for prepared overwrite. This establishes a
large difference between those workload boundaries here, without attributing
time to allocation, copying or BSP scheduling individually. Host transfer and
sync observations include guest service, transport, host scheduling and backing
filesystem behavior. Their spread is not a tail-latency distribution, and no
physical-disk result or owner's-host result is claimed. RAM sync intervals are
roughly the clock-call overhead itself; they do not measure persistence cost.

Manual checks rejected an existing host destination, a RAM source/output alias,
a short copy source before output creation, duplicate flags and CPU 2's
read-only host grant, all with status 1 and no successful-run summary. The
existing read mode verified a retained RAM output with two samples after the
alias rejection. Every host output matched the fixture checksum; failed source,
option and read-only checks created no destination. After removing the RAM outputs, GDB showed zero
host lookup/open references, idle virtqueues and empty completed/file/memory
queues. All owned QEMU/GDB/virtiofsd processes stopped and temporary host files
were removed. Owner's-host measurements and finer attribution remain deferred.

## Implemented task 3: pipes and IPC

Userland supplies the [pipe mode](https://git.internal/chronium/pyxis-userland/src/branch/main/iobench/README.md#pipes)
and [endpoint benchmark](https://git.internal/chronium/pyxis-userland/src/branch/main/ipcbench/README.md).
`session` hands off the current shell; each invocation needs a fresh session
after the preceding benchmark exits.

- `session app://iobench.pxe pipe`: the fixed 1 MiB fixture, descriptor requests
  1..65536 bytes, default 4096; representative requests 64, 4096 and 65536.
  Separately time producer acceptance and consumer acknowledgment. A coordinator
  batch-launches two workers to supply exclusive pipe stdout/stdin grants.
- `session app://ipcbench.pxe call|send`: payloads 0..4096 bytes, default 64;
  representative payloads 0, 64 and 4096. Default eight messages per pass, bounded
  to 1..256 so retained contents stay at most 1 MiB per direction.
- Fresh endpoint receivers for every warmup and measured pass, preventing prior
  samples from occupying the next sample's delivery records. One untimed warmup,
  five measured passes by default (1..100). Launch, allocation,
  readiness, fixture preparation and verification remain outside timing. Received
  bytes are retained for later validation; successful completion counts alone do
  not make a verified sample. No measured capability attachments.
- CALL echoes the same bytes and length. Explicit exported operation tags
  distinguish echo and control without consuming application payload space.
  Report round trips, per-direction bytes and batch means, not latency percentiles.
- SEND admits groups of at most eight without concurrent consumer draining. A
  separate control CALL supplies the admitted count; the receiver drains exactly
  that prefix, finishes its receipts and acknowledges consumption. Sum admission
  intervals and separately measure whole-pass completion, including control
  exchanges and clock boundaries. Queue-full fails the sample, drains admitted
  messages and reports rejected/admitted/acknowledged-consumed counts without
  data retries. On failure, acknowledged consumption is a confirmed lower bound.
  SEND uses raw data and exported control endpoints; CALL uses one exported
  endpoint for echoes and control. Closing the last control client allows RETIRE
  to wake a companion when queue saturation prevents STOP admission.
- All workers use existing launch placement on the caller's CPU and space; no
  cross-space or scheduler API changes. CALL deadlines do not bound pipe,
  RECEIVE or process-WAIT stalls. Capability attachments and continuously
  overlapping SEND traffic remain separate future workloads.

### Pipe and IPC observations

Manual four-CPU Q35 nested-KVM boots with 256 MiB, QEMU 10.2.2, networking
disabled and entropy enabled used the development session on CPU 1. No host
filesystem was attached. Host/compiler environment matches the task-2 run above.
Each command used a fresh boot because `session` hands off and exits the shell.

All three pipe configurations verified every byte in one warmup and five measured
passes, with normal worker exits and no read/write errors. Values below are
milliseconds, in sample order; acceptance and completion share the producer's
start timestamp.

| Request | Descriptor calls per direction | Positive shorts per direction | Acceptance samples (ms) | Completion samples (ms) |
| --- | --- | --- | --- | --- |
| 64 | 16,384 | 0 | 5.082440, 5.161530, 5.422950, 5.240930, 5.275590 | 5.391600, 5.628460, 5.869020, 5.547610, 5.579530 |
| 4096 | 256 | 0 | 0.726260, 1.031070, 0.730660, 0.707180, 0.706410 | 1.034710, 1.339840, 1.038560, 1.032150, 1.026070 |
| 65536 | 256 | 255 | 0.730930, 0.796430, 0.707640, 0.751530, 0.722510 | 1.099540, 1.119830, 1.025450, 1.067230, 1.031620 |

Clock-call means were 36,306, 36,258 and 36,839 ns respectively. Acceptance medians
were 5.241, 0.726 and 0.731 ms; completion medians were 5.580, 1.035 and 1.067 ms.
Large requests did not reduce calls below the 4096-byte transfer boundary. These
are nested-VM elapsed observations, including scheduler handoffs and clock/control
cost; the submillisecond acceptance intervals do not isolate pipe-copy CPU cost.

Zero-byte, 256-message endpoint runs exposed the existing deferred receipt
reclamation limit during warmup. CALL completed 21 round trips before QUEUE_FULL;
SEND admitted and acknowledged 16 messages, then rejected the 17th. Completed
receipts retain delivery slots until BSP reclamation, so sequential CALLs and
acknowledged SEND groups can still exhaust capacity. These are observed failure
points, not deterministic thresholds.

Both larger runs failed visibly without printing throughput. CALL also reported
failed STOP admission; closing its last control client allowed retirement
notification and a normal nonzero child exit. SEND verified its acknowledged
prefix and completed normal shutdown. These checks used ordinary workloads,
without fault injection. The default is eight messages on fresh endpoints;
the 256-message upper bound remains available to expose capacity failures.
This measures short batches, not sustainable endpoint throughput. The consequence
and revisit point are recorded in
[technical debt](../technical-debt.md#endpoint-throughput-limited-by-deferred-receipt-reclamation).

All six final endpoint configurations passed one warmup and five measured
samples on fresh receivers, eight messages per sample. Each CALL replied with
exactly the requested length/content; each SEND admitted and acknowledged all
eight with zero rejections. There were no measured attachments. At 64 bytes,
CALL confirmed 512 request and 512 reply bytes, while SEND confirmed 512 bytes;
at 4096 bytes those counts were 32,768 per direction and 32,768 respectively.
Zero-byte samples report counts and elapsed time without byte throughput.

| Mode / payload bytes | Clock loop ns/read | Completion samples (ms) | Completion median (ms) | SEND admission samples (ms) |
| --- | --- | --- | --- | --- |
| CALL 0 | 37,673 | 2.193, 6.950, 2.052, 2.080, 2.051 | 2.080 | — |
| CALL 64 | 35,795 | 2.069, 2.047, 2.610, 2.090, 2.074 | 2.074 | — |
| CALL 4096 | 38,190 | 2.250, 2.378, 2.168, 5.234, 2.175 | 2.250 | — |
| SEND 0 | 36,128 | 0.361, 1.206, 1.218, 1.258, 1.203 | 1.206 | 0.037, 0.120, 0.123, 0.148, 0.127 |
| SEND 64 | 39,265 | 0.385, 0.367, 1.053, 0.466, 0.435 | 0.435 | 0.039, 0.037, 0.123, 0.037, 0.055 |
| SEND 4096 | 35,977 | 0.399, 0.422, 0.377, 0.377, 0.374 | 0.377 | 0.042, 0.040, 0.042, 0.042, 0.042 |

SEND admission medians were 0.123, 0.039 and 0.042 ms for 0, 64 and 4096 bytes.
The shortest admission intervals are comparable to clock-call overhead. CALL
includes export dispatch, receiver retention and sender reply retention; SEND
completion includes the control exchange, retention and finishing receipts.
Fresh measured endpoints include first-delivery costs despite the separate
warmup. Sample variability and these boundaries prevent interpreting payload
rates as isolated kernel copy cost or sustainable service throughput. No clock
constant is subtracted, and no scheduler or reclamation change was made.

Validation used `make -j16 image`, interactive QEMU boots and GDB inspection.
All nine representative configurations (three pipe and six endpoint) verified
five samples. Manual checks rejected pipe `--sync`, oversized IPC payloads,
zero message counts, duplicate options and missing session authority. The
existing archive-read workload verified its fixture after the parser changes.
GDB after the final 4096-byte SEND run showed four idle CPUs, CPU 1 online and
empty completion/pipe/endpoint/memory queues and object-retirement list. All
owned QEMU/debugger jobs were stopped. No compiler-container rebuild is needed.

## Focused PR tasks

- [x] **1. Measurement contract and file reads.** Settle command syntax, default
  sizes, bounded fixtures, timing and repetition rules. Add `iobench` read mode
  for archive, RAM and optional host-backed files, with explicit short-transfer
  accounting. Document reproducible manual commands and initial observations.
- [x] **2. Writes and copies.** Agree output-file and sync policies, then add
  bounded write/copy workloads. Report acceptance and requested sync separately,
  verify resulting data, and distinguish allocation/growth from steady I/O.
- [x] **3. Pipes and IPC.** Settle the small companion/acknowledgment protocol
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
