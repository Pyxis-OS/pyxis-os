# I/O reliability and bottleneck attribution

Status: tasks 1–3 are complete. Logical receipt release fixes completed-work
capacity retention, and the agreed IPC/HTTP reruns passed. RAM file-growth
attribution identifies publication-to-BSP-service wait as the dominant measured
interval. Host profiling separates queue/service/transport intervals but strongly
perturbs the measured workload; its limits and follow-up are recorded below.
Task 4 is comparison resolution and the final handoff, with scope still to agree. This follows
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

## Task 2: implemented RAM FILE profiling

The profile capability now has explicit FILE authority and independent
caller-local FILE BEGIN/SNAPSHOT/END collection, preserving private-memory meaning.
RAM buffer replacement records preparation before wait setup, the boundary before
publication locking, BSP service start/end and caller resumption. It records
elapsed sums/maxima for the resulting intervals and allocation, existing-data copy and
release calls within service. It counts attempts, successes/failures, summed
requested capacity and actual copied bytes, saturating without exposing addresses.

BSP owns request result/timestamps from publication until wakeup; the caller then
aggregates. Disabled collection adds no clocks or allocations. This preserves
existing early wakeup, allocation, growth, fallback and notification behavior.
FILE ownership waits, incoming payload copies and nonreplacement work remain an
unattributed residual. None of these elapsed intervals is kernel CPU time.

`iobench write/copy --profile` collects each measured transfer after preparation
and before verification/reporting; warmup is unprofiled. BEGIN/END are outside the
transfer clock interval; per-request instrumentation is inside. Optional sync
remains separate and outside collection. The matched comparison uses
grow/prepared RAM writes and archive-to-RAM copies: 1 MiB, 4080-byte requests, no sync, one warmup and five
samples, each with profile-off controls. No growth or notification optimization
belongs in this attribution task.

Code inspection predicts ten successful geometric replacements and 2,084,880
existing-data bytes copied for each growing pass without allocation failures.
FILE request publication has no explicit BSP notification, unlike private memory;
this is a hypothesis for queue cost, not a measured cause.

### RAM measurement provenance and reproduction

Measured on 2026-09-28 with kernel implementation
`0757faecc186c1b3fb1649a103ae18980736bfba` and userland
`f180e4330a9218aa08bc49c93be2458c7c9005f5`. These commit the code used in the
ordinary image build; later report/gitlink changes do not alter runtime code.
Ports stayed `6ec1290f87882392390e6a889be61ba3ac1448d1`, lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`. The same nested-KVM host and QEMU/Q35,
GCC, OVMF and 256 MiB/four-CPU configuration described in task 1 were used.
Work ran on CPU 1; entropy was enabled, networking disabled, and no host filesystem
was exported. Outputs and redirected logs used RAM `home://`; copy source was the
packaged deterministic 1 MiB archive fixture. No cache eviction was attempted.

The eight commands ran sequentially in the order below in one boot, with unique
retained output/log names. Defaults are 4080-byte requests, 1 MiB and five measured
passes after one untimed warmup; sync is off. Logs were inspected manually with
`head`/`cat` and screenshots after each run, outside all benchmark intervals.

```text
iobench write home://wg-off 2> home://wg-off.log
iobench write home://wg-on --profile 2> home://wg-on.log
iobench write home://wp-off --prepared 2> home://wp-off.log
iobench write home://wp-on --prepared --profile 2> home://wp-on.log
iobench copy app://share/iobench.bin home://cg-off 2> home://cg-off.log
iobench copy app://share/iobench.bin home://cg-on --profile 2> home://cg-on.log
iobench copy app://share/iobench.bin home://cp-off --prepared 2> home://cp-off.log
iobench copy app://share/iobench.bin home://cp-on --prepared --profile 2> home://cp-on.log
```

All 48 passes verified exact length, contents and EOF. Every pass completed 258
writes; copies also completed 258 reads. No short transfers or failures occurred.
All ten profiled grow samples reported ten attempts/successes, zero failures,
4,173,840 bytes of summed requested capacity and 2,084,880 existing-data bytes
copied. All ten profiled prepared samples reported zero counts and durations.
Requested capacity is a sum of replacement sizes, not a live allocation gauge.
The existing doubling policy, including its allocation-failure fallback, is
unchanged. No allocation failure was induced or observed.

### Transfer samples and controls

Times are milliseconds in execution order; six decimal places retain the original
nanosecond readings. Clock cost is the 1000-call mean in ns/read, not subtracted.

| Workload | Profile | Clock ns | Transfer samples ms | Median ms | Range ms |
| --- | --- | ---: | --- | ---: | --- |
| Write grow | off | 35902 | 50.366100, 52.811340, 59.072960, 52.339550, 58.696730 | 52.811340 | 50.366100–59.072960 |
| Write grow | on | 36862 | 53.091190, 52.851590, 59.037810, 59.043150, 59.205240 | 59.037810 | 52.851590–59.205240 |
| Write prepared | off | 36163 | 0.301790, 0.488580, 0.295900, 0.276580, 0.274440 | 0.295900 | 0.274440–0.488580 |
| Write prepared | on | 36587 | 0.677960, 0.684860, 0.497960, 0.638890, 0.656270 | 0.656270 | 0.497960–0.684860 |
| Copy grow | off | 39762 | 52.191460, 53.127050, 58.463970, 58.450710, 56.624600 | 56.624600 | 52.191460–58.463970 |
| Copy grow | on | 37534 | 52.805100, 56.770750, 53.118070, 54.279920, 59.807410 | 54.279920 | 52.805100–59.807410 |
| Copy prepared | off | 38501 | 0.519400, 0.543820, 0.595790, 0.506010, 0.506140 | 0.519400 | 0.506010–0.595790 |
| Copy prepared | on | 35905 | 0.480040, 0.499850, 0.493820, 0.491520, 0.497120 | 0.493820 | 0.480040–0.499850 |

These off/on groups are matched controls, not paired measurements of constant
instrumentation overhead. Growth write medians were 52.811/59.038 ms; growth copy
medians were 56.625/54.280 ms. Prepared write medians were 0.296/0.656 ms despite
zero replacements in the profiled run, while prepared copy medians were
0.519/0.494 ms. Run ordering, cache state and nested scheduling remain combined;
these five-sample groups do not establish a precise overhead or a speedup. No
clock-cost correction is applied. Short prepared intervals remain a task-4
resolution concern.

### Profile intervals

Each row contains per-pass sums in milliseconds. Allocation, copy and release
are subintervals of service; do not add them to the top-level phases. Release
includes the `kfree(NULL)` call on first allocation. The residual is transfer
minus replacement total and includes instrumentation aggregation, user/kernel
transfer work, clock boundaries and scheduling outside the replacement interval.
It cannot isolate incoming payload-copy cost or be called CPU time.

| Grow workload | Interval | Sample sums ms | Median ms |
| --- | --- | --- | ---: |
| Write grow | publication | 0.625080, 0.358630, 0.465400, 0.439400, 0.857710 | 0.465400 |
| Write grow | queue | 47.307070, 47.851240, 54.128940, 53.756960, 52.324090 | 52.324090 |
| Write grow | service | 3.155520, 3.162550, 2.821210, 2.876360, 2.919320 | 2.919320 |
| Write grow | resume | 1.028210, 0.747620, 0.841940, 0.944630, 1.182810 | 0.944630 |
| Write grow | total | 52.115880, 52.120040, 58.257490, 58.017350, 57.283930 | 57.283930 |
| Write grow | allocation | 0.439620, 0.436610, 0.377210, 0.358890, 0.435490 | 0.435490 |
| Write grow | copy | 0.794120, 0.711650, 0.707680, 0.635750, 0.649530 | 0.707680 |
| Write grow | release | 0.356010, 0.356480, 0.351470, 0.391160, 0.359420 | 0.356480 |
| Write grow | residual | 0.975310, 0.731550, 0.780320, 1.025800, 1.921310 | 0.975310 |
| Copy grow | publication | 1.028920, 0.351540, 0.700830, 1.016960, 1.016820 | 1.016820 |
| Copy grow | queue | 44.782840, 51.986590, 46.475210, 46.223620, 49.721010 | 46.475210 |
| Copy grow | service | 3.689400, 2.867450, 2.799300, 3.465280, 5.357400 | 3.465280 |
| Copy grow | resume | 1.165840, 0.660070, 1.099030, 1.408810, 1.422330 | 1.165840 |
| Copy grow | total | 50.667000, 55.865650, 51.074370, 52.114670, 57.517560 | 52.114670 |
| Copy grow | allocation | 0.437640, 0.467410, 0.356780, 0.441900, 0.694960 | 0.441900 |
| Copy grow | copy | 0.727980, 0.655370, 0.628550, 0.824270, 1.119160 | 0.727980 |
| Copy grow | release | 0.564540, 0.400350, 0.357770, 0.448700, 0.685330 | 0.448700 |
| Copy grow | residual | 2.138100, 0.905100, 2.043700, 2.165250, 2.289850 | 2.138100 |

Per-request maxima in each pass, also milliseconds. These are maxima of ten
replacement requests, not latency percentiles; maxima from different rows need
not belong to the same request.

| Grow workload | Interval | Sample maxima ms |
| --- | --- | --- |
| Write grow | publication | 0.134810, 0.041220, 0.151080, 0.115400, 0.132840 |
| Write grow | queue | 8.273050, 7.973390, 8.240830, 8.155500, 8.266700 |
| Write grow | service | 0.709850, 0.688170, 0.416420, 0.414240, 0.487250 |
| Write grow | resume | 0.164990, 0.091030, 0.152970, 0.147740, 0.144050 |
| Write grow | total | 8.567400, 8.348860, 8.519720, 8.445310, 8.622370 |
| Write grow | allocation | 0.117170, 0.116710, 0.056090, 0.037470, 0.115220 |
| Write grow | copy | 0.227960, 0.188600, 0.205230, 0.191560, 0.196240 |
| Write grow | release | 0.037150, 0.041640, 0.035590, 0.073540, 0.044260 |
| Copy grow | publication | 0.143270, 0.035380, 0.137030, 0.125950, 0.119810 |
| Copy grow | queue | 7.751510, 8.104820, 8.022420, 7.777910, 8.054250 |
| Copy grow | service | 1.067930, 0.401580, 0.404910, 1.039030, 1.224360 |
| Copy grow | resume | 0.157980, 0.088210, 0.155040, 0.270500, 0.175500 |
| Copy grow | total | 8.285780, 8.462510, 8.462920, 8.915390, 8.967370 |
| Copy grow | allocation | 0.117000, 0.158670, 0.036280, 0.119560, 0.131160 |
| Copy grow | copy | 0.187460, 0.187730, 0.186530, 0.302150, 0.490250 |
| Copy grow | release | 0.139470, 0.097310, 0.041310, 0.116480, 0.116620 |

### Attribution and follow-up

Publication-to-service queue time accounts for 88–92% of transfer time in the
profiled write samples and 83–92% in copies. Its per-pass sums are 44.783–54.129 ms
across the two workloads, compared with 2.799–5.357 ms of BSP replacement service.
Allocation-call sums are 0.357–0.695 ms, existing-data copy 0.629–1.119 ms and
release calls 0.351–0.685 ms. Thus repeated geometric growth is confirmed, but
allocation/copy service is not the dominant measured cost in these runs.

Queue includes publication lock/link overhead, BSP availability and scheduling;
these timestamps do not distinguish them. Code inspection shows FILE publication
does not explicitly notify the BSP, unlike private-memory publication. Millisecond
queue maxima are consistent with delayed BSP service, but do not prove timer
wakeups are its sole cause. The concrete follow-up proposal is a separate PR to
notify the BSP after FILE request publication, preserving the early-wakeup
handshake and touching no relinquished request storage, followed by this same
profile-off/on matrix. It needs its own design decision; no notification or
allocator/growth-policy change is included here. This limitation and revisit
point are in [technical debt](../technical-debt.md#ram-file-bsp-service-delay).

`make -j16 image` passed. The existing manual
`allocbench pages --profile --size 4096 --live 1 --rounds 8` control reported eight
allocations and eight releases, zero failures and 32768 completed bytes in each
direction, preserving the existing memory-profile behavior. This does not exercise
simultaneous FILE/MEMORY collection or every denied/malformed profile operation.
Read-only post-workload GDB inspection found four CPUs, CPU 1 online, and empty
completed-task, FILE, private-memory and object-retirement queues. No debugger
stops occurred during measurements. Owned QEMU/debugger jobs were stopped; RAM
fixtures/logs disappeared with that boot. No new tests or automation were added,
and no compiler-container rebuild is needed.

## Task 3: implemented host FILE profiling

The profile capability now has independent caller-local HOST authority and
BEGIN/SNAPSHOT/END collection. Only native host READ/WRITE requests are counted.
Preparation, publication, BSP forwarding, worker service start/end and caller
resumption separate the two guest queues. Each transport interval runs from
immediately before queue submission through worker-observed completion. Lazy OPEN
transport traffic belongs to its native READ/WRITE; metadata-only requests, sync and deferred cleanup are excluded.

Separate READ/WRITE counters record attempts, failures, requested/completed bytes,
positive short transfers and zero-byte results for nonzero requests (EOF).
Actual transport submissions and valid used-ring completions are independent
of FUSE success. Failed published transport requests have separate elapsed sums/maxima ending at failure observation before reset;
rejection before submission has no transport event. All counters saturate. Shared
task storage follows caller-to-BSP-to-worker ownership and returns at wakeup;
no worker touches it afterward. Disabled collection adds no clocks or allocations.
Snapshots expose no addresses, names, node IDs or remote caller activity.

`iobench --host-profile` covers measured reads/writes/copies; warmup, preparation
and verification stay unprofiled. RAM and memory collection retain their meaning.
Read BEGIN precedes its first clock/OPEN; only payload READ/WRITE work is counted,
and END follows the payload end clock before EOF/close. END therefore contributes
to complete-consumption elapsed, while BEGIN is outside all read intervals.
Write/copy BEGIN and END sit outside transfer clocks, before optional sync.
Any successful BEGIN receives an END attempt on error paths.

The matched comparison uses profiling off/on for each agreed workload: host read
at 4088 bytes, prepared host write at 4080, host-to-prepared-RAM copy at 4080, and
archive-to-prepared-host copy at both 4080 and 4088. Each uses the same 1 MiB
fixture, one warmup and five samples, with sync off. A disposable tmpfs export
and recorded daemon/cache conditions bound the results. The 4088 copy exposes
existing suffix writes;
no transfer-size, batching, cache, notification or scheduling policy changes.
Transport elapsed combines guest submission/completion work, host scheduling,
daemon/backing service and device transport. Remaining worker/caller work stays
unattributed; neither is CPU time or a physical-storage durability measurement.

### Host measurement provenance and reproduction

Measured on 2026-09-28 with kernel implementation
`7d9f3557838d59e22751051506dcf2dd22dd68df` and userland runtime
`f68b4d8c83fec862ebca47ed92ccccfcc6992aee`. Later userland documentation and parent
report/gitlink commits do not change measured runtime code. Ports stayed
`6ec1290f87882392390e6a889be61ba3ac1448d1`, lwIP
`a1aadb91a50360ff5b52864f7cec810b8162ee85`. The same Fedora 44 nested-KVM host,
Linux 6.19.10, exposed i9-12900K, QEMU 10.2.2/Q35, `-cpu max`, Pyxis GCC 16.2.0,
matching `/usr/share/edk2/ovmf/OVMF_CODE.fd`/`OVMF_VARS.fd`, four CPUs and 256 MiB
configuration was used. Work ran on CPU 1, with virtio entropy enabled and
networking disabled. The virtio-fs device used the ordinary shared-memfd setup.

A fresh writable virtiofsd 1.14.0 exported a dedicated directory in `/dev/shm`,
using the [documented namespace setup](../virtio-fs.md#start-the-host-service):

```sh
unshare -Ur -- /usr/libexec/virtiofsd \
  --shared-dir "$export_dir" --socket-path "$socket_dir/fs.sock" \
  --sandbox namespace --inode-file-handles=never \
  --no-announce-submounts --rlimit-nofile=0
```

Cache policy was the daemon's `auto` default; writeback was not enabled. The guest
still negotiates no optional FUSE features. The full deterministic archive fixture
was copied before boot as `iobench.bin` (POSIX checksum 1625934143, 1048576 bytes).
No cache eviction, concurrent fixture mutation or host workload control was used.
Preparation and the verified warmup warm the retained files. These are tmpfs and
nested-VM observations, not durable-media, owner-host or physical-hardware results.

Commands ran sequentially in this order, in one boot. Defaults supply 1 MiB and
one warmup plus five measured passes; default requests are read=4088 and
write/copy=4080 bytes. All sync options were off. Unique output files were created
exclusively; stderr logs used RAM `home://` and were inspected manually afterward.

```text
iobench read host://iobench.bin 2> home://hr-off.log
iobench read host://iobench.bin --host-profile 2> home://hr-on.log
iobench write host://hw-off --prepared 2> home://hw-off.log
iobench write host://hw-on --prepared --host-profile 2> home://hw-on.log
iobench copy host://iobench.bin home://hc-off --prepared 2> home://hc-off.log
iobench copy host://iobench.bin home://hc-on --prepared --host-profile 2> home://hc-on.log
iobench copy app://share/iobench.bin host://ac-off --prepared 2> home://ac-off.log
iobench copy app://share/iobench.bin host://ac-on --prepared --host-profile 2> home://ac-on.log
iobench copy app://share/iobench.bin host://ac8-off --prepared --buffer 4088 2> home://ac8-off.log
iobench copy app://share/iobench.bin host://ac8-on --prepared --buffer 4088 --host-profile 2> home://ac8-on.log
```

All 60 passes verified exact contents, length and EOF without failure. The
[raw sample artifact](../io-host-profile-samples.json) preserves all individual
transfer/OPEN/complete intervals, per-pass counters and every profile sum/maximum
in nanoseconds. Its `hr`, `hw`, `hc`, `ac` and `ac8` keys correspond to the command
names above; profile duration pairs are `[sum, maximum]`. Inactive directions and
failed transport intervals were zero; no sample saturated.

### Host counts and transfer controls

| Workload | Benchmark reads / writes | Benchmark short writes | Native HOST calls | Transport submissions / completions |
| --- | ---: | ---: | --- | ---: |
| Host read, 4088 | 257 / 0 | 0 | 257 READ | 258 / 258 |
| Prepared host write, 4080 | 0 / 258 | 0 | 258 WRITE | 258 / 258 |
| Host → prepared RAM, 4080 | 258 / 258 | 0 | 258 READ | 258 / 258 |
| Archive → prepared host, 4080 | 258 / 258 | 0 | 258 WRITE | 258 / 258 |
| Archive → prepared host, 4088 | 257 / 513 | 256 | 513 WRITE | 513 / 513 |

Counts apply to every measured pass. Each active HOST direction requested and
completed 1048576 bytes, with zero native short transfers, failures and in-window
EOF results. Final EOF verification is outside collection. `read` opens a fresh
native handle each pass, and its first payload read includes lazy FUSE_OPEN;
retained copy handles were already opened by source verification/preparation.

The 4088-byte copy read is drained as a 4080-byte helper write and an 8-byte suffix.
The benchmark counts 256 short helper writes relative to its original request;
HOST profiling sees the already-capped native requests, all completed fully.
There is no contradiction between these counts, and no change to transfer limits.

All elapsed values below are milliseconds in sample order. Read uses payload
elapsed; write/copy use transfer elapsed. Clock is the 1000-call mean ns/read,
reported without subtraction. OPEN and complete-consumption raw values are in the
sample artifact; profiled read END adds overhead to complete consumption only.
Rows of medians need not sum: each interval is summarized independently.

| Workload | Profile | Clock ns | Payload/transfer samples ms | Median ms |
| --- | --- | ---: | --- | ---: |
| Host read, 4088 | off | 35781 | 118.833710, 121.642990, 116.871290, 117.733190, 109.422430 | 117.733190 |
| Host read, 4088 | on | 40471 | 1990.387220, 1819.903300, 1890.982310, 1932.871880, 1893.882960 | 1893.882960 |
| Host write, 4080 | off | 37280 | 110.049920, 117.713440, 129.372100, 128.084550, 135.506310 | 128.084550 |
| Host write, 4080 | on | 35213 | 1907.392660, 1884.386300, 1894.892910, 1986.641730, 1863.615980 | 1894.892910 |
| Host → RAM, 4080 | off | 41022 | 124.535980, 123.419080, 164.349280, 159.752360, 143.897110 | 143.897110 |
| Host → RAM, 4080 | on | 43002 | 1946.348300, 1907.570850, 1980.617040, 2018.702930, 1968.984920 | 1968.984920 |
| Archive → host, 4080 | off | 36265 | 136.342380, 118.681490, 137.638680, 139.396990, 111.128180 | 136.342380 |
| Archive → host, 4080 | on | 37024 | 2012.910680, 2025.281700, 1998.988580, 1838.549800, 1969.174180 | 1998.988580 |
| Archive → host, 4088 | off | 37805 | 273.900060, 245.644270, 247.401830, 241.899980, 237.055290 | 245.644270 |
| Archive → host, 4088 | on | 36601 | 3915.153510, 3882.482720, 3903.654050, 3748.162510, 3685.585720 | 3882.482720 |

### Instrumented attribution and its limit

Profile-on medians were **13.7–16.1 times** their profile-off controls. This is
substantial workload perturbation, not a constant clock cost that can be
subtracted. The unprofiled read median (117.733 ms/MiB) remains close to the
historical baseline, but the profiled phases cannot partition that baseline.
No baseline optimization or speedup is claimed.

The following are medians of per-pass profile sums, in milliseconds. Transport
is nested inside service. Host publication includes request setup and incoming
write-payload capture; caller-side read-result copying follows the native total.
Residual is payload/transfer elapsed minus the native total and includes caller
work, aggregate updates, other copying and scheduling; it is not CPU time.

| Workload | Publication | BSP queue | Worker queue | Service | Resume | Native total | Transport | Residual |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Host read, 4088 | 17.371380 | 1417.306840 | 68.441560 | 328.487540 | 25.697980 | 1872.986290 | 296.869070 | 18.975300 |
| Host write, 4080 | 18.040710 | 1436.144180 | 60.196530 | 330.512690 | 29.260490 | 1874.674220 | 294.183410 | 19.077540 |
| Host → RAM, 4080 | 15.767690 | 1499.748660 | 53.493210 | 340.541560 | 26.989640 | 1951.426140 | 306.620480 | 17.558780 |
| Archive → host, 4080 | 15.940700 | 1526.561090 | 46.738220 | 338.962540 | 26.910070 | 1981.111490 | 302.507220 | 17.877090 |
| Archive → host, 4088 | 35.466250 | 2910.432980 | 112.466510 | 697.396880 | 56.564610 | 3842.079380 | 625.711620 | 37.814070 |

Across individual profiled samples, the initial publication-to-BSP-forwarding
queue accounts for 73.8–78.8% of payload/transfer elapsed. Completed transport
accounts for 88.6–90.7% of worker service. These identify where the **instrumented**
workloads spend time, not the unprofiled distribution. Transport still combines
host/device/daemon/backing activity with guest/host scheduling and completion
observation. No guest-only timestamp can assign those components separately.
All 25 profiled samples have exact top-level phase-sum equality and transport
sums no greater than service; individual maxima remain available in the artifact.

Code inspection shows the first scheduler host-request queue has no explicit BSP
notification; forwarding wakes the transport worker. Additional clock reads can
change which work is pending when the BSP checks queues and goes idle. That is a
plausible explanation for the observed sensitivity, not a proven cause: no IRQ,
queue sampling or host tracing was added. The initial queue wait is distinct from
virtio-fs device wait. Neither changing virtio-fs batching nor blaming host tmpfs
is justified by these results.

Task 3 therefore delivers working phase/count attribution **with an explicit
normal-workload attribution limit**. Before using these percentages to choose an
optimization, agree a follow-up to reduce timestamp perturbation and/or isolate
initial BSP notification while preserving the handoff contract. Rerun the same
profile-off/on controls after any separately approved correction. No notification,
clock-source, batching, cache, transfer-size or scheduling policy changes are in
this PR. The limitation is recorded in
[technical debt](../technical-debt.md#host-file-profiling-perturbation).

### Host regression and cleanup checks

The ordinary `make -j16 image` passed. Outside the comparison matrix, the existing
`iobench copy host://iobench.bin home://combined --profile --host-profile --rounds 1`
passed warmup and verification. Independent collections reported 258 successful
HOST reads and transport completions plus ten successful RAM replacements and
2,084,880 copied bytes. `allocbench pages --profile --size 4096 --live 1 --rounds 8`
reported eight allocations and releases, zero failures and 32768 completed bytes
in each direction. No new test program, fault injection or automation was added.
Denied/malformed profile operations, simultaneous MEMORY/HOST collection and
transport failure paths were reviewed but not separately exercised at runtime.

Post-workload read-only GDB inspection found four CPUs, CPU 1 online, and empty
completed-task, FILE, host scheduler, memory and retirement queues. The host
worker request/retirement queues were empty, its session remained ready with no
profile pointer, lookup references or open handles, and both VirtIO queues had
no request in flight. These are point-in-time cleanup observations. No debugger
stops occurred during measurements. QEMU and virtiofsd were stopped, and their
disposable host files/socket directories were removed. No compiler-container
rebuild is needed.

## Focused PR tasks

- [x] **1. Resolve endpoint receipt reclamation.** Separate logical receipt
  release and delivery reuse from BSP backing destruction. Preserve caller
  collection, cancellation, attachment ownership, retirement and stale handles.
  All twelve IPC and four HTTP configurations passed one warmup and five
  samples; eighteen existing lifecycle modes and GDB ownership/cleanup checks
  passed. The implemented guarantee and remaining live-work limit are recorded
  above and in technical debt.

- [x] **2. Attribute RAM file-growth cost.** Independent caller-scoped FILE
  profiling separates publication, BSP queue/service and resumption, including
  allocation/copy/release subphases and replacement/copy counts. All eight matched
  grow/prepared write/copy configurations passed warmup plus five verified samples.
  Queue time dominates the profiled growing transfers; a separate FILE publication
  notification proposal and unresolved intervals are recorded above and in debt.

- [x] **3. Attribute host FILE latency.** Independent HOST profiling separates
  the initial BSP queue, worker queue/service, transport and caller resumption.
  All ten agreed off/on configurations passed warmup plus five samples. Counts
  distinguish lazy OPEN and helper suffix writes from native transfers. Profiling
  strongly perturbs these nested-KVM workloads; the report limits phase conclusions
  to instrumented behavior and records the required follow-up before applying them
  to normal-workload optimization. Sync/durable-media costs remain out of scope.

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
