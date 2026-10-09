# I/O and IPC performance baselines

`iobench` and `ipcbench` provide manually invoked, verified measurements of
native files, pipes, endpoint delivery and exported HTTP snapshots. The baseline
milestone is complete. This reference records the implemented contracts,
reproduction commands, measured results and remaining limits. The detailed
command references live with [iobench](../../userspace/iobench/README.md) and
[ipcbench](../../userspace/ipcbench/README.md).

The observations below are warmed nested-KVM measurements from 2026-09-28.
Owner-host and physical-hardware results are unavailable. No performance target,
optimization, kernel tracing or CI performance gate is part of this baseline.
Existing [allocation profiling](allocation-profiling.md) and
[`ttcp`](../devices/tcp.md#ttcp) remain separate context: their
workload, direction and completion boundaries differ from these results.

## Measurement contract

The tools use the startup monotonic clock and report elapsed wall time, not CPU
time. A separate 1000-call loop reports mean clock-call plus loop overhead;
no constant is subtracted. Nanosecond units do not imply nanosecond accuracy.
Each invocation defaults to one untimed, verified warmup and five measured
passes (`--rounds 1..100`). Reports go to stderr. Failures retain confirmed
partial progress, return nonzero, and suppress the failed pass's throughput and
successful-run summary. Earlier completed samples remain visible.

Allocation, fixture preparation, verification and diagnostics stay outside the
measured intervals, except for the explicitly included receiver metadata checks
and retention copies. Short transfers advance by confirmed bytes; neither tool
retries failed data operations or paces traffic to hide queue saturation.
Successful samples verify content, counts and completion, not just admission.
Median/range summaries are descriptive; they are not confidence intervals,
individual-call latency percentiles or sustained throughput estimates.

| Workload | Measured interval | Included work and limits |
| --- | --- | --- |
| Read OPEN | Before `open()` through return | Path discovery and descriptor setup; HTTP also resolves hostnames, connects, parses and stages the complete body. Numeric IPv4 avoids DNS. |
| Read payload | After OPEN through final payload read | Descriptor reads and progress accounting from one held file/snapshot; no HTTP refetch. |
| Read complete | Before OPEN through EOF probe and `close()` return | Includes intermediate clock reads; excludes deferred provider storage reclamation. |
| Write/copy transfer | First payload operation through completed output | Growth/allocation when not prepared; actual source reads for copy. Throughput counts output bytes once. |
| Requested file sync | One sync after payload transfer | Separate from transfer; RAM sync is a no-op, host sync follows the backend contract. |
| Pipe acceptance | Producer write loop | Local write acceptance, with normal backpressure. |
| Pipe completion | Producer start through consumer acknowledgment | Consumer has read all bytes; content verification follows timing. |
| Endpoint CALL | Sequential echo batch | Export dispatch, receiver retention and sender reply retention. Request/reply byte counts are separate. |
| Endpoint SEND admission | Sum of bounded admission loops | Receiver waits for control before draining; this is not concurrent free-running delivery. |
| Endpoint SEND completion | Whole pass through drain acknowledgments | Admission, intervening clock reads, control calls, retention and finishing receipts. |

## Fixtures, authority and lifetime

The build installs `share/iobench.bin` (1 MiB) and `share/iobench-small.bin`
(its 32 KiB prefix). Byte at zero-based offset `i` is
`(i ^ (i >> 8) ^ (i >> 16) ^ 0xa5) & 255`. Host Lua generates the files; binaries
are not checked into Git. Read accepts `--bytes 1..1048576` (default 1048576)
and requires exactly that prefix followed by EOF. Other modes retain 1 MiB.
OPEN has no byte-throughput result; read payload/complete use the selected size.

`--buffer` selects application requests, 1..65536 bytes: read defaults to 4088,
write/copy to 4080, pipe to 4096. Descriptor `read()` performs one native transfer;
FILE carries at most 4088 read or 4080 write bytes, and pipes transfer at most
4096 bytes per call. Larger requests can yield short transfers, not larger native
messages. Reported application calls are not instrumented syscall counts.

Read uses libc descriptors and opens afresh for each pass. Write/copy use
libpyxis file capabilities for exclusive creation, resize, explicit offsets and
same-handle sync/verification. Outputs must be new files; existing names fail
without truncation. Copy verifies its source before output creation. Handles
remain held across passes; avoid concurrent mutation of fixtures or outputs.
Default preparation resizes output to zero outside timing, so the transfer
includes growth. `--prepared` writes contrasting bytes beforehand and measures
overwrite; host backing behavior can still allocate. `--sync` also syncs preparation
outside timing, then measures a separate post-transfer sync. Sync does not
include parent-directory synchronization or promise durable pathname creation.
Files remain for inspection and explicit removal, including after failure.

Pipe mode launches a coordinator and two workers using standard-stream grants.
IPC uses a receiver on the caller's CPU and in its space. Each pass gets fresh
workers/endpoints; launch, readiness and teardown are outside timing. IPC payloads
are 0..4096 bytes (default 64), messages 1..256 (default eight). Zero-byte runs
report elapsed time and counts without byte throughput. No measured messages
carry capabilities. CALL uses exported echo operations with matching length/content. SEND uses a raw
data endpoint and separate exported control endpoint, admits groups of at most
eight, then uses a control CALL to drain that prefix and acknowledge consumption.
After a failed acknowledgment, confirmed consumption is only a lower bound.
The default is a short-batch baseline, not a sustainable service workload.

## Manual reproduction

Build with `make -j16 image`; no compiler-container rebuild is needed. Start
with [README](../../README.md), [network setup](../devices/networking.md), and the
[virtio-fs host service](../devices/virtio-fs.md#start-the-host-service) for optional files.
A representative boot selects CPU 1 Development on four CPUs:

```sh
make run CPUS=4 ACCEL=kvm MEMORY=256M QEMU_DISPLAY=gtk \
  OVMF_CODE=/usr/share/edk2/ovmf/OVMF_CODE.fd \
  OVMF_VARS=/usr/share/edk2/ovmf/OVMF_VARS.fd
```

Use matching OVMF paths for the host distribution. Add `VIRTIO_NET=1` for HTTP,
or `VIRTIO_FS_SOCKET=/path/to/fs.sock` for a host export containing the installed
1 MiB fixture. Use a read-only daemon for read-only workloads, a writable export
for writes/copies, and CPU 1's writable grant. CPU 2's host grant is read-only.

Choose unused disposable output names; shell `>` truncates existing files even
though benchmark write/copy use exclusive creation. Run one workload at a time:

```text
cat app://share/iobench.bin > home://iobench.bin
iobench read app://share/iobench.bin
iobench read home://iobench.bin --buffer 65536
iobench read host://iobench.bin
iobench write home://write-grow.bin
iobench write home://write-prepared.bin --prepared
iobench write host://write-prepared.bin --prepared --sync
iobench copy app://share/iobench.bin home://copy.bin
iobench copy app://share/iobench.bin host://copy-prepared.bin --prepared --sync
```

Read request sizes 64, 512, 4088, 4096 and 65536 expose the transfer boundary.
For writes/copies compare 4080, 4088 and 65536, grow-from-zero versus prepared,
and explicitly requested sync. Verify retained outputs with `iobench read OUTPUT`
and remove only the selected disposable names afterward.

Each command below needs a fresh boot/session: `session` hands off the shell and
does not restart it after the benchmark exits. Pipe requests 64/4096/65536 and
IPC payloads 0/64/4096 are the representative matrix:

```text
session app://iobench.pxe pipe --buffer 4096
session app://ipcbench.pxe call --size 64 --messages 8
session app://ipcbench.pxe send --size 64 --messages 8
```

For matched HTTP runs, start a manual host server after building:

```sh
http_fixture_dir=$(mktemp -d)
cp build/userspace-root/share/iobench.bin build/userspace-root/share/iobench-small.bin "$http_fixture_dir/"
python3 -m http.server 18080 --bind 127.0.0.1 --directory "$http_fixture_dir"
```

With QEMU user networking, numeric `10.0.2.2` reaches the host without DNS:

```text
cat app://share/iobench-small.bin > home://http-small.bin
iobench read app://share/iobench-small.bin --bytes 32768
iobench read home://http-small.bin --bytes 32768
iobench read http://10.0.2.2:18080/iobench-small.bin --bytes 32768
iobench read http://10.0.2.2:18080/iobench.bin
```

Repeat selected reads with `--buffer 65536`; the default is 4088. Each OPEN fetches
afresh; retained reads fetch nothing. Reopening/warmup does not evict host caches.
After use, remove guest copies, quit QEMU, stop the host server with Ctrl-C, and
remove the two copied fixtures and its temporary directory. Stop any owned
virtiofsd/debugger jobs too. The 1 MiB HTTP command exposed the capacity failure
recorded below in the original baseline. The subsequent
[receipt-reuse correction](io-reliability-attribution.md#receipt-reuse)
completed that workload; the original tables remain historical observations.

## Recorded environment and source revisions

These results were recorded on 2026-09-28 using Pyxis GCC 16.2.0 and ordinary
image builds. Pipe/IPC dating is recovered from the integration history below.
The development host was itself a KVM guest: Fedora 44, Linux 6.19.10, exposing
an i9-12900K (16 host CPUs recorded for the HTTP runs). QEMU 10.2.2 used Q35,
`-cpu max`, four CPUs, 256 MiB and OVMF. Workloads ran sequentially on CPU 1 with
normal presenter/shell/provider tasks and virtio entropy enabled. Debugger stops
were outside samples. Each successful row used one warmup and five verified
samples. Repeated passes describe one invocation, not independent host trials.

| Result set | Source provenance | Devices and backing |
| --- | --- | --- |
| Original payload-only reads | Kernel/source `2010228`, userland `4acf4b2` | Networking off; read-only virtiofsd 1.14.0 export; fixture copied before boot, no cache eviction. |
| Writes/copies | Kernel/source `cd99791`, implementation `0659684` | Networking off; writable virtiofsd 1.14.0 export in host tmpfs. |
| Pipes/IPC | Integrated in Pyxis `7c65600`, userland `329686f` | Networking off, no host export; fresh boot/session per command. Provenance recovered from Git: integration revisions identify the implementation; the original run notes did not separately record the date or build hashes. |
| Matched native/HTTP reads | Kernel/source `2b9b7e7`, implementation `bef2991` | User networking on, no host export; Python 3.14.3 HTTP server over host loopback, fixtures in tmpfs. |

Ports stayed at `6ec1290` and lwIP at `a1aadb9`. The completed tools are integrated
by Pyxis `0b88922` with userland `bef2991`; the tables retain their original
measurement provenance rather than claiming a rerun at that revision.
Virtiofsd used `--sandbox namespace`, `--inode-file-handles=never`,
`--no-announce-submounts`, `--rlimit-nofile=0`, launched through `unshare -Ur`,
plus `--readonly` for the initial reads. HTTP fixture POSIX checksums were
1349564844 (32768 bytes) and 1625934143 (1048576 bytes); the prefix matched.
No cold-cache, physical-disk, durable-media or owner-host result is claimed.

## Read baselines

The original results below time only payload reads of 1 MiB: OPEN, EOF probe,
close and verification were outside timing. The current three-interval read
contract is represented by the later matched native/HTTP table. These are
separate runs and should not be used as a before/after optimization comparison.
Sample columns preserve order; displayed milliseconds are rounded.

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

Requests of 64/512 bytes took 16384/2048 payload calls, without shorts. Requests
4088/4096/65536 took 257; 4096/65536 had 256 positive shorts. Large requests did
not enlarge transfers. Host elapsed time includes BSP queuing, VirtIO/FUSE,
daemon scheduling and host filesystem service; none was isolated. The roughly
36–41 microsecond clock-loop means are material beside native submillisecond
batches, so small differences between large request sizes are unresolved.

## Write and copy baselines

Every measured pass verified 1 MiB output; copies also read 1 MiB. Transfer
throughput excludes separately requested sync. Host fixtures/outputs were in
tmpfs: those results measure the guest-to-host-memory path, including the sync
request/acknowledgment, without disk durability. Values are milliseconds.

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

Sync samples are from the same passes, in the same order:

| Workload | File sync samples, ms | Median (range), ms |
| --- | --- | --- |
| Write RAM prepared | 0.036, 0.036, 0.036, 0.036, 0.036 | 0.036 (0.036–0.036) |
| Write host grow | 0.260, 0.272, 6.820, 8.768, 0.378 | 0.378 (0.260–8.768) |
| Write host prepared | 0.324, 0.809, 0.269, 0.285, 0.237 | 0.285 (0.237–0.809) |
| Copy archive to RAM prepared | 0.032, 0.033, 0.033, 0.046, 0.032 | 0.033 (0.032–0.046) |
| Copy archive to host prepared | 3.360, 1.476, 2.324, 1.136, 3.919 | 2.324 (1.136–3.919) |

Writes took 258 helper calls. Requests of 65536 had 257 positive short writes;
4080 had none. Copies at 4080 took 258 reads and 258 writes without shorts;
4088/65536 took 257 reads and 513 writes, with 256 short writes. Only 65536 also
had 256 short reads. The extra write drains the eight-byte suffix caused by
different native read/write limits.

Matched RAM writes without sync measured 55.654 ms grow-from-zero versus
0.314 ms prepared overwrite. This establishes a large boundary-dependent
elapsed difference, without attributing it to allocation, copying or BSP
scheduling individually. RAM sync is a no-op and its elapsed time is roughly
clock overhead. Host transfer/sync variation does not establish tail latency.

## Pipe and endpoint baselines

Pipe passes verified the full 1 MiB with normal worker exits and no read/write
errors. Acceptance and completion share the producer's start timestamp. Values
are milliseconds, in sample order:

| Request | Descriptor calls per direction | Positive shorts per direction | Acceptance samples (ms) | Completion samples (ms) |
| --- | --- | --- | --- | --- |
| 64 | 16,384 | 0 | 5.082440, 5.161530, 5.422950, 5.240930, 5.275590 | 5.391600, 5.628460, 5.869020, 5.547610, 5.579530 |
| 4096 | 256 | 0 | 0.726260, 1.031070, 0.730660, 0.707180, 0.706410 | 1.034710, 1.339840, 1.038560, 1.032150, 1.026070 |
| 65536 | 256 | 255 | 0.730930, 0.796430, 0.707640, 0.751530, 0.722510 | 1.099540, 1.119830, 1.025450, 1.067230, 1.031620 |

Clock-loop means were 36306, 36258 and 36839 ns for requests 64, 4096 and 65536.
Acceptance medians were 5.241, 0.726 and 0.731 ms; completion medians were
5.580, 1.035 and 1.067 ms. Large requests did not reduce calls below the
4096-byte transfer boundary. Elapsed intervals include scheduling/control work;
they do not isolate pipe-copy CPU cost.

Endpoint samples used fresh receivers and eight messages. All six configurations
replied/acknowledged all eight with exact content and no rejection. At 64 bytes,
CALL confirmed 512 request and 512 reply bytes; SEND confirmed 512 bytes. At
4096 bytes those totals were 32768 per direction and 32768 respectively.
Zero-byte cases have no byte throughput. Values are milliseconds:

| Mode / payload bytes | Clock loop ns/read | Completion samples (ms) | Completion median (ms) | SEND admission samples (ms) |
| --- | --- | --- | --- | --- |
| CALL 0 | 37,673 | 2.193, 6.950, 2.052, 2.080, 2.051 | 2.080 | — |
| CALL 64 | 35,795 | 2.069, 2.047, 2.610, 2.090, 2.074 | 2.074 | — |
| CALL 4096 | 38,190 | 2.250, 2.378, 2.168, 5.234, 2.175 | 2.250 | — |
| SEND 0 | 36,128 | 0.361, 1.206, 1.218, 1.258, 1.203 | 1.206 | 0.037, 0.120, 0.123, 0.148, 0.127 |
| SEND 64 | 39,265 | 0.385, 0.367, 1.053, 0.466, 0.435 | 0.435 | 0.039, 0.037, 0.123, 0.037, 0.055 |
| SEND 4096 | 35,977 | 0.399, 0.422, 0.377, 0.377, 0.374 | 0.377 | 0.042, 0.040, 0.042, 0.042, 0.042 |

SEND admission medians were 0.123, 0.039 and 0.042 ms for payloads 0, 64 and 4096.
The shortest are comparable to clock-call overhead. Fresh endpoints include
first-delivery costs despite the separate warmup. Different protocol/control
costs and the small samples prevent interpreting these numbers as sustainable
IPC rates or isolated kernel copy costs.

## Matched native and HTTP reads

Each elapsed cell is median [minimum, maximum] over five samples, in milliseconds.
The final column is the separate 1000-call clock-loop mean in nanoseconds, without
subtraction. Server logs showed six GETs per successful HTTP configuration:
one per warmup/sample OPEN and none for retained reads.

| Backend | Bytes | Request | OPEN ms | Payload ms | Complete ms | Clock ns |
| --- | ---: | ---: | --- | --- | --- | ---: |
| archive | 32768 | 4088 | 0.039 [0.037, 0.041] | 0.044 [0.044, 0.045] | 0.118 [0.116, 0.121] | 35913 |
| archive | 32768 | 65536 | 0.037 [0.037, 0.038] | 0.044 [0.043, 0.044] | 0.117 [0.116, 0.117] | 35955 |
| archive | 1048576 | 4088 | 0.039 [0.038, 0.041] | 0.267 [0.250, 0.404] | 0.344 [0.326, 0.478] | 35725 |
| archive | 1048576 | 65536 | 0.039 [0.037, 0.040] | 0.265 [0.254, 0.283] | 0.338 [0.329, 0.358] | 35947 |
| ram | 32768 | 4088 | 0.038 [0.036, 0.040] | 0.043 [0.042, 0.044] | 0.116 [0.112, 0.119] | 36011 |
| ram | 32768 | 65536 | 0.038 [0.036, 0.045] | 0.044 [0.044, 0.050] | 0.117 [0.116, 0.125] | 36200 |
| ram | 1048576 | 4088 | 0.038 [0.037, 0.039] | 0.257 [0.253, 0.279] | 0.335 [0.326, 0.353] | 35906 |
| ram | 1048576 | 65536 | 0.038 [0.037, 0.043] | 0.253 [0.251, 0.266] | 0.328 [0.324, 0.345] | 35777 |
| http | 32768 | 4088 | 14.161 [12.628, 16.298] | 0.073 [0.072, 0.078] | 14.273 [12.738, 16.414] | 36217 |
| http | 32768 | 65536 | 14.452 [11.021, 14.613] | 0.080 [0.073, 0.088] | 14.564 [11.154, 14.732] | 36235 |

All successful 32 KiB passes used nine payload calls; successful 1 MiB passes
used 257. At request 4088 there were no positive shorts; at 65536 there were
eight and 256 respectively. Each successful pass made an additional EOF probe.
HTTP OPEN dominates the small-file complete interval but does not isolate network,
parser, staging or scheduling costs. Close return does not prove provider storage
has been reclaimed.

### Individual matched-read samples

The five individual values in each cell are nanoseconds, in sample order. These
retain the captured observations behind the preceding median/range table; they
are elapsed clock readings, not nanosecond-accuracy claims.

| Backend | Bytes | Request | OPEN samples, ns | Payload samples, ns | Complete samples, ns |
| --- | ---: | ---: | --- | --- | --- |
| archive | 32768 | 4088 | 38670, 41130, 37380, 36850, 40120 | 43950, 44620, 44340, 44050, 44110 | 118070, 121290, 117140, 116320, 119700 |
| archive | 32768 | 65536 | 37740, 37410, 36680, 37150, 37440 | 43310, 44110, 44160, 43510, 44150 | 116510, 116910, 116400, 116130, 117070 |
| archive | 1048576 | 4088 | 38900, 40630, 39450, 39860, 37600 | 265800, 249860, 269660, 267140, 404100 | 343760, 326200, 344560, 342220, 477530 |
| archive | 1048576 | 65536 | 39620, 38700, 37230, 39380, 39020 | 260080, 272100, 264670, 282620, 254020 | 335600, 347070, 337540, 357970, 328820 |
| ram | 32768 | 4088 | 36520, 38530, 37690, 36470, 40290 | 42130, 43560, 42830, 43330, 43700 | 112460, 117320, 115900, 115230, 119440 |
| ram | 32768 | 65536 | 37730, 37580, 37610, 45250, 36480 | 44170, 50170, 44150, 44000, 43780 | 117480, 124230, 117170, 124740, 115710 |
| ram | 1048576 | 4088 | 39000, 39050, 37780, 37250, 36730 | 278530, 264760, 253630, 252650, 257210 | 353490, 339530, 327210, 325770, 335370 |
| ram | 1048576 | 65536 | 43430, 36810, 37660, 38700, 37230 | 265880, 251120, 251650, 253180, 253140 | 345160, 323760, 325610, 328480, 329640 |
| http | 32768 | 4088 | 14161030, 13020460, 12627710, 14179260, 16297850 | 73140, 73500, 71880, 73290, 77680 | 14272770, 13132900, 12738340, 14291560, 16414200 |
| http | 32768 | 65536 | 12818690, 14512070, 11020630, 14451870, 14613130 | 80660, 72860, 87850, 73120, 80010 | 12938350, 14624860, 11154330, 14563750, 14731970 |

## Capacity failures and validation limits

In the original baseline, zero-byte, 256-message endpoint warmups exposed
deferred receipt reclamation:
CALL completed 21 round trips before QUEUE_FULL; SEND admitted and acknowledged
16 messages, then rejected the 17th. Completed receipts retained delivery slots
until BSP destruction. Failure positions depended on reclamation scheduling and
were not fixed thresholds. Both workloads failed without throughput. CALL's STOP
also failed admission; closing the last control client allowed retirement and a
normal nonzero child exit. SEND verified its acknowledged prefix and shut down.
Fresh endpoints and eight-message defaults made short samples possible without
establishing sustained throughput.

Both 1 MiB HTTP warmups failed with EAGAIN (native errno 9) after successful body
fetches. Request 4088 confirmed 61320 bytes before read attempt 16 failed; request
65536 confirmed 122640 bytes and 30 positive shorts before attempt 31 failed.
Neither reached EOF or produced measured samples/throughput. These ordinary
exported-file failures were consistent with the same receipt limit. There was no
retry, pacing or automatic smaller fixture. The 32 KiB success is a bounded
observation, not a workaround for arbitrary consumers.

Manual validation covered content/count/EOF checks, invalid options, missing
inputs, HTTP 404, output aliases/existing names, read-only authority, and missing
session authority. Write/copy/pipe regression runs passed after the read extension.
Post-workload GDB checks found idle/empty inspected kernel queues and host
references where applicable. These are point-in-time checks, not proof of provider
storage accounting. Owned QEMU, debugger, HTTP and virtiofsd jobs were stopped
and temporary fixtures removed. Existing CI builds validated integration, not
performance; the documentation handoff did not rerun these measurements.

## Follow-up investigations

The closed [I/O reliability and bottleneck attribution report](io-reliability-attribution.md)
records receipt reuse, RAM/HOST profiling and the HOST publication correction.
Additional resolution work and the final combined matrix were deferred; the
correction-specific reruns do not replace that matrix.
The attribution/coverage gaps and revisit points are retained in
[technical debt](../technical-debt.md#io-baseline-attribution-and-coverage):

- **Endpoint receipt reclamation — completed:** logical slot reuse is now
  separate from backing destruction, preserving BSP ownership and receipt,
  attachment and delivery lifetimes. The agreed 256-message IPC and 1 MiB HTTP
  reruns passed. The original failures and remaining live-work limit are in
  [technical debt](../technical-debt.md#endpoint-throughput-limited-by-deferred-receipt-reclamation).
- **RAM growth attribution — superseded:** growing RAM writes no longer use BSP
  requests; see [RAM FILE profiling](io-reliability-attribution.md#ram-file-profiling).
- **Host FILE attribution — instrumented limits:** HOST profiling separates guest
  queues, worker service and transport, but still perturbs execution after the
  notification correction. Normal-workload phase attribution remains unresolved.
  Use explicit disk-backed fixtures if durable-media sync is the question.
- **Measurement resolution and coverage:** longer safe batches or scoped
  instrumentation need a separate contract. Keep owner-host repetitions separate
  from nested-VM data, recording revisions, devices, backing, warmup and clock cost.
  These are needed before making fine-grained performance or capacity claims.

Benchmark-wide timeouts also depend on existing
[startup/wait limitations](../technical-debt.md#service-startup-failure-before-publication)
and [synchronous FILE helper limits](../technical-debt.md#provider-calls-through-synchronous-file-helpers).
IPC CALL deadlines do not bound RECEIVE, pipe or process waits. The HTTP fetch's
thirty-second bound does not bound ordinary OPEN/FILE queueing. Capability
attachment cost, cross-space contention, mixed-workload fairness, CPU accounting
and tracing remain unmeasured. Future investigations need their own bounded
scope; this baseline does not authorize work on all of these gaps.
