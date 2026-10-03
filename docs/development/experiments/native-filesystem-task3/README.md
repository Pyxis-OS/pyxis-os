# Native filesystem task-3 measurements

The [populated-pool review](populated-pool-review.md) records the allocation bitmap
blocker found after the initial measurements, its fix and the matched populated
case. The historical intervals below retain their original revisions/configuration.

## Before implementation, 2026-10-03

The kernel is main `132aef1`, with filesystem `810d2af66d`, userspace
`53b6860f08` and ports `a50ae5ccf1`. The kernel was rebuilt; unchanged SDK,
userland and ports bundles were verified against their payload manifests,
interface identity and dependency identities before reuse. Compilation used
Pyxis GCC 16.2.0 and `make -j16 image fs-tools`, with
`PREBUILT="sdk userspace ports"`. No compiler container rebuild was needed.

This is a nested KVM measurement on Linux 6.19.10-300.fc44, with a virtualized
Intel Core i9-12900K host CPU. QEMU v10.2.2 carried the documented AHCI fix
(`d9f78431d8eb`); the executable was
`/tmp/pyxis-qemu-ahci-fix/build/qemu-system-x86_64`. The guest had four CPUs,
256 MiB RAM, `-cpu max`, no graphical display, entropy enabled and virtio-net
enabled. Raw OVMF CODE/VARS came from `/usr/share/edk2/ovmf/`; variables were
fresh for this boot. A trusted init on CPU 3 mounted the benchmark volume,
created a namespace and started the existing remote services with network
configuration. CPUs 1 and 2 ran idle init. The benchmark ran in that remote
session on CPU 3, with profiling off. Remote transport time is not benchmark time.

The disposable legacy pool was 64 MiB. Its `bench` volume contained only copies
of the existing installed 1 MiB and 32 KiB `iobench` fixtures. A 68 MiB GPT disk
held the pool at partition 1, first sector 2049, using 512-byte sectors. The disk
GUID was `12345678-1234-4567-89ab-0123456789ab` and the legacy owner ID was
`0a32efc079ed4c7bab58e224cf119315`. Host consistency inspection passed before
read-only virtio-blk attachment. Disk SHA-256 was
`dd0c89daafba4197162be6259e898d98eceefdbc58472c04e64abfae7f45fe92`.
This is the obsolete native adapter baseline, not a new-format writer result.

Guest commands were submitted individually through the existing machine-mode
remote client; each typed completion was collected before the next command:

```text
iobench read data://iobench.bin --buffer 4088 --rounds 5
iobench write home://native-task3-control.bin --buffer 4080 --rounds 5 --sync
exit
```

Both workloads verified one untimed warmup and all five measured 1 MiB samples.

| Workload / measured interval | Median | Range |
| --- | ---: | ---: |
| Legacy native read OPEN | 19.470 ms | 8.079–19.935 ms |
| Legacy native read payload | 1128.118 ms | 1126.622–1129.164 ms |
| Legacy native read complete consumption | 1149.043 ms | 1137.837–1151.990 ms |
| RAM file write transfer | 7.053 ms | 6.082–9.868 ms |
| RAM file sync | 0.036 ms | 0.036–0.074 ms |

Read samples made 257 payload calls and one EOF probe, with no short reads.
Write samples made 258 calls, with no short writes. RAM sync is a no-op and
provides no disk-durability comparison. Warmup affects caches; reopening does
not imply cold storage. These are elapsed workload intervals, not per-call
percentiles or confidence estimates. They do not qualify the owner's host or
physical hardware. The [complete benchmark output](baseline.txt) and
[serial boot record](baseline-serial.txt) retain the measured evidence.

The remote client exited with shell success and complete output draining. The
read-only QEMU process was stopped; no task validation processes remain.

## Writer measurements

The writer used the same fixture, CPU placement, guest RAM, nested KVM, firmware,
QEMU and network configuration. The new-format pool was 128 MiB with an explicit
8 MiB journal, an imported `bench` volume and an initially empty `empty` volume.
Its 132 MiB GPT disk used the same GUID and sector-2049 partition placement,
with 512-byte writable virtio-blk and `cache=writeback`. Initial structural fsck
passed. The initial disk SHA-256 was
`16c4d3f17f26252f93491638a7f86c2c51ffd009663793b9ce0c074f1111ff6b`.

The kernel branch is `fs/native-writer`, based on main `132aef1`. The first writer
boot used the engine from `6cc8487`; the second used cleanup fixes from `e091492`
and `e65e35b`, with the adapter/configuration integration later committed as
`3d04538`. Its saved running ELF SHA-256 was
`30cd773da6cbb2df825d498f68b4449ec9ca2c752dcc5e344406baf84e6d9f49`.
The writable-INFO consumer boot used parent `4439d94`, filesystem `4dbf07a`,
userland `6308de8` and unchanged ports `a50ae5ccf1`. An internal diagnostic-field
rename between the second measurement and that revision changed neither layout
nor behavior.

During PR delivery, main advanced to `d04c6a6`. Parent `7169f86` merges it and
uses published userland `c9d19c2`, which includes the newer main's lsusb changes
and preserves the mount/sync/INFO code unchanged. The measured workload tables
below retain their original revision context.

The final policy image at `ae0590c`, still using filesystem `4dbf07a` and userland
`c9d19c2`, rebuilt successfully after accepting FILE_SIZE through READ or WRITE
and recoverable-error acknowledgment on sync. Terminal failure supersedes an
older recoverable error and remains latched; failed pools cannot clear it. Review
covered both failed-flush acknowledgment and successful-flush reporting of an
older retained error. Ongoing failures still fail each retry. No error was injected.
Both file-handler and worker SIZE checks now accept either right; libc's write-only
append/end-relative seek paths call SIZE. Runtime shell `>>` was rejected as an
unsupported operator before filesystem access, so append/end-relative seek has
source-review coverage rather than a shell runtime result.
The final boot's ordinary write/read, repeated directory sync and writable INFO
query passed. After clean stop, extracted-pool fsck and the new text-file comparison
passed again. Its output is appended to the [consumer record](writer-final-consumer.txt).

The first trusted init mounted both volumes read/write, created `data://trusted`,
copied the 1 MiB fixture there and completed `sync --disk` before creating the
remote session. Subsequent boots mounted `bench` at writable `data://` and
read-only `ro://`, and `empty` at writable `empty://`. They also completed disk
sync before starting the session. The remote shell inherited directory grants
but no mount capability.

Ordinary builds rebuilt the changed kernel, SDK, ports and userland. The build
used existing Pyxis GCC 16.2.0 and `make -j16 image fs-tools`, with explicit
`MOUNT_DISK`, the trusted init script, `INIT_PRIMARY=app://init-idle` and
`INIT_CPUS=3=app://init`. Local CMake 4.4.3 was supplied for ports through the
build's host-tools environment; `PYTHON=/usr/bin/python3` avoided shadowing the
configuration module with that environment's Python. Changed inputs did not use
stale bundles. No compiler-container rebuild was needed.

### Warm reads and synchronized writes

The principal second-boot commands were:

```text
iobench read data://iobench.bin --buffer 4088 --rounds 5
iobench write data://writer-batched.bin --buffer 4080 --rounds 5 --sync
iobench write data://prepared-batched.bin --prepared --buffer 4080 --rounds 5 --sync
iobench write home://native-task3-control.bin --buffer 4080 --rounds 5 --sync
```

Each verified one untimed warmup and all five measured 1 MiB samples. Prepared
writes also write and synchronize an untimed preparation before each sample;
their sync time includes persistent overwrite publication. The RAM control's
sync remains a no-op.

| Second-boot interval | Median | Range |
| --- | ---: | ---: |
| Native read OPEN | 23.007 ms | 11.839–23.735 ms |
| Native read payload | 147.913 ms | 140.411–151.311 ms |
| Native read complete consumption | 165.347 ms | 160.359–175.545 ms |
| Native grow write transfer | 251.179 ms | 244.989–268.662 ms |
| Native grow write sync | 207.348 ms | 204.907–557.576 ms |
| Native prepared write transfer | 161.491 ms | 155.667–164.052 ms |
| Native prepared write sync | 559.225 ms | 554.307–573.886 ms |
| RAM write transfer | 7.994 ms | 6.231–10.482 ms |
| RAM sync | 0.034 ms | 0.034–0.036 ms |

Read samples made 257 calls plus EOF, and write samples made 258 calls, with no
short transfers. The complete [second-boot output](writer-after-batching.txt)
includes the workflows and expected failures described below. A copy command
also verified warmup and one measured 1 MiB sample: transfer 438.273 ms, retained
file sync 209.538 ms. These are warm workload intervals, not cold-disk results,
per-call distributions or owner-host measurements.

### Cleanup cost found during integration

The first writer implementation reclaimed one mapping per transaction after
truncate. Its repeated grow-write workload therefore measured transfer median
6065.534 ms (5951.067–6145.990), with sync median 209.337 ms
(207.465–231.965). It also returned short writes at page boundaries: 512 calls
and 255 short writes per sample. The implementation now handles the complete
request when possible and batches up to 64 reclaimed mappings within a bounded
metadata-image budget. The same workload's transfer median fell to 251.179 ms.
The [first-boot output](writer-before-batching.txt) preserves the earlier result.

Existing QEMU `info blockstats` snapshots bracketed complete write commands:

| Grow-write command, warmup + five samples (6 MiB logical data) | Target write bytes | Write operations | Flush operations |
| --- | ---: | ---: | ---: |
| First writer, one-mapping cleanup | 53,772,288 | 13,128 | 5,216 |
| Batched cleanup | 7,786,496 | 1,901 | 172 |

The byte ratio fell from about 8.55 to 1.24, or about 85.5% fewer virtual-device
write bytes for this workload. Prepared-write deltas were 13,316,096 bytes,
3,251 writes and 100 flushes over **12 MiB** of logical data including preparation.
[Target-device snapshots](blockstats.txt) retain the counters. They include
creation, warmup, preparation and asynchronous checkpoint work, rather than
isolating a single syscall. A COMMITTED completion can precede later checkpoint
writes. No other successful mutation command ran inside those grow-write windows;
the second run first rejected reuse of the prior boot's exclusive output name,
then used a fresh name. Virtual-device bytes do not establish physical SSD wear.

### Persistence, authority and consumers

The ordinary second boot exercised cached close/reopen reads; file and directory
sync; file resize through the existing benchmark; create, rename and replacement;
file/empty-directory removal; UTF-8 names; and native executable capture/run.
It rejected writes and sync through `ro://`, cross-volume rename, removal of a
nonempty directory, and remote-shell `sync --disk` without mount authority.
Trusted init's disk sync succeeded. The copy workload turned over the bounded
four-chunk cache while verifying source and destination bytes.

The first run tried an uninstalled `echo` executable; this was a launch failure,
not a filesystem failure. The second run found fastfetch's Disk query rejected
writable INFO because the userland helper required READ_ONLY. Userland `6308de8`
removed that obsolete requirement while preserving other validation. After the
SDK and consumers rebuilt, the third clean boot confirmed fastfetch reported
both writable volumes and the read-only alias with consistent pool IDs, capacity
and journal sequence. It also ran the saved native executable and verified a
persisted 1 MiB file. [Final consumer output](writer-final-consumer.txt) records
the commands and typed completion results.

After synchronized clean stops, standalone pool extraction and native host fsck
passed. Extracted grow-write, prepared-write and copied 1 MiB files matched the
source byte for byte; the replaced text matched the boot-archive source. The
second-boot inspector reported nonzero creation/modification timestamps and no
remaining cleanup flags for the grow-write inode. This exercises normal
persistence, not recovery from interrupted writes.

The existing USB-image builder was migrated to the native formatter and its
explicit journal argument. An ordinary 512 MiB image with a 128 MiB ESP and
382 MiB pool, using the 8 MiB small-image default journal, built successfully.
GPT verification, extracted-pool fsck and a copied `cat.pxe` comparison passed.
This checks the image consumer; no USB boot or native installer result is claimed.

### Debugger inspection and limits

[GDB observations](writer-gdb.txt) captured the first CREATE and the reboot's
root acquisition with interrupts enabled on the BSP worker. A settled second-boot
snapshot showed EMPTY journal sequence 2859, zero images, no retained writeback
error, no failed-pool flag and all four cache chunks allocated. Debugger inspection
did not inject filesystem calls. [Serial output](writer-serial.txt) records that
boot's four-CPU and writable virtio-blk configuration.

Separate compilation with the ordinary kernel flags plus `-fstack-usage` found
the largest individual codec frames at 4144 bytes, rename at 1712 bytes and
cleanup at 1504 bytes. The worker stack is 16 KiB; these frame observations are
not a formal bound on the combined call stack.

Journal ordering/recovery, admission checks, retained-open unlink, uncertain-I/O
failure handling and allocation-pressure paths were reviewed in code. No crash
injection, ENOSPC runtime exercise, read-only-device boot, allocator-pressure
exercise or physical-media qualification was performed. The 30-second interval
is nominal, not a bound on crash loss. All validation clients, QEMU and debugger
processes were stopped cleanly.
