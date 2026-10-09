# Native filesystem task-3 measurements

Raw captures, transcripts, patches, scripts and screenshots once kept in this directory were removed from the tree; Git history keeps them at `d6733033`.

The [populated-pool review](populated-pool-review.md) records the allocation bitmap
blocker found after the initial measurements, its fix and the matched populated case.
The intervals below keep their original revisions and configuration.

## Before implementation, 2026-10-03

Kernel main `132aef1`, filesystem `810d2af66d`, userspace `53b6860f08`, ports
`a50ae5ccf1`. The kernel was rebuilt (`make -j16 image fs-tools`,
`PREBUILT="sdk userspace ports"`, Pyxis GCC 16.2.0); the unchanged SDK, userland and ports
bundles were verified against their manifests before reuse. No compiler-container rebuild
was needed.

Nested KVM on Linux 6.19.10-300.fc44 with a virtualized Intel Core i9-12900K host CPU;
QEMU v10.2.2 with the documented AHCI fix (`d9f78431d8eb`); four CPUs, 256 MiB, `-cpu max`,
no display, entropy and virtio-net on, fresh OVMF variables. A trusted init on CPU 3
mounted the benchmark volume, created a namespace and started the remote services; CPUs 1
and 2 ran idle init. The benchmark ran in that remote session on CPU 3 with profiling off.

The disposable legacy pool was 64 MiB, its `bench` volume holding only copies of the 1 MiB
and 32 KiB `iobench` fixtures, in a 68 MiB GPT disk (partition 1 at sector 2049, 512-byte
sectors); host consistency inspection passed before read-only virtio-blk attachment. This
is the obsolete native adapter baseline, not a new-format writer result. Commands were
submitted individually through the machine-mode remote client:

```text
iobench read data://iobench.bin --buffer 4088 --rounds 5
iobench write home://native-task3-control.bin --buffer 4080 --rounds 5 --sync
exit
```

Both verified one untimed warmup and five measured 1 MiB samples.

| Workload / measured interval | Median | Range |
| --- | ---: | ---: |
| Legacy native read OPEN | 19.470 ms | 8.079–19.935 ms |
| Legacy native read payload | 1128.118 ms | 1126.622–1129.164 ms |
| Legacy native read complete consumption | 1149.043 ms | 1137.837–1151.990 ms |
| RAM file write transfer | 7.053 ms | 6.082–9.868 ms |
| RAM file sync | 0.036 ms | 0.036–0.074 ms |

Reads made 257 payload calls plus one EOF probe and writes 258 calls, with no short
transfers. RAM sync is a no-op and gives no durability comparison. These are elapsed
workload intervals, not per-call percentiles or confidence estimates, and warmup affects
caches without implying cold storage. They do not qualify the owner's host or hardware.

## Writer measurements

Same fixture, CPU placement, RAM, nested KVM, firmware, QEMU and network configuration. The
new-format pool was 128 MiB with an explicit 8 MiB journal, an imported `bench` volume and
an empty `empty` volume, in a 132 MiB GPT disk (same GUID and sector-2049 placement) on
writable 512-byte virtio-blk with `cache=writeback`; initial structural fsck passed.

Kernel branch `fs/native-writer`, based on main `132aef1`: the first writer boot used the
engine from `6cc8487`, the second the cleanup fixes `e091492` and `e65e35b` with the
adapter integration later committed as `3d04538`. The writable-INFO consumer boot used
parent `4439d94`, filesystem `4dbf07a`, userland `6308de8` and unchanged ports. Main later
advanced to `d04c6a6`; parent `7169f86` merges it with published userland `c9d19c2`,
leaving the mount/sync/INFO code unchanged. The final policy image at `ae0590c` (filesystem
`4dbf07a`, userland `c9d19c2`) accepts FILE_SIZE through READ or WRITE and acknowledges
recoverable errors on sync; a terminal failure supersedes an older recoverable error and
stays latched, and failed pools cannot clear it. Ongoing failures still fail each retry.
Libc's write-only append and end-relative seek paths call SIZE; shell `>>` was rejected as
unsupported before filesystem access, so those paths have source review only. The final
boot's ordinary write/read, repeated directory sync and writable INFO query passed, as did
extracted-pool fsck and the text-file comparison after a clean stop.

The first trusted init mounted both volumes read/write, created `data://trusted`, copied
the 1 MiB fixture there and completed `sync --disk` before creating the remote session.
Later boots mounted `bench` at writable `data://` and read-only `ro://`, and `empty` at
writable `empty://`, again syncing before the session; the remote shell inherited
directory grants but no mount capability. Images used `MOUNT_DISK`, the trusted init,
`INIT_PRIMARY=app://init-idle` and `INIT_CPUS=3=app://init`, and no stale bundles.

### Warm reads and synchronized writes

```text
iobench read data://iobench.bin --buffer 4088 --rounds 5
iobench write data://writer-batched.bin --buffer 4080 --rounds 5 --sync
iobench write data://prepared-batched.bin --prepared --buffer 4080 --rounds 5 --sync
iobench write home://native-task3-control.bin --buffer 4080 --rounds 5 --sync
```

Each verified a warmup and five 1 MiB samples. Prepared writes also write and synchronize
an untimed preparation first, so their sync includes persistent overwrite publication.

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

Reads made 257 calls plus EOF and writes 258, with no short transfers. A copy command
verified a warmup and one 1 MiB sample: transfer 438.273 ms, retained-file sync 209.538 ms.
These are warm workload intervals, not cold-disk results, per-call distributions or
owner-host measurements.

### Cleanup cost found during integration

The first writer reclaimed one mapping per transaction after truncate. Its repeated
grow-write workload measured a transfer median of 6065.534 ms (5951.067–6145.990) with
sync median 209.337 ms (207.465–231.965), and returned short writes at page boundaries
(512 calls and 255 short writes per sample). The implementation now handles the complete
request when possible and batches up to 64 reclaimed mappings within a bounded
metadata-image budget; the same workload's transfer median fell to 251.179 ms. QEMU
`info blockstats` snapshots bracketed complete write commands:

| Grow-write command, warmup + five samples (6 MiB logical data) | Target write bytes | Write operations | Flush operations |
| --- | ---: | ---: | ---: |
| First writer, one-mapping cleanup | 53,772,288 | 13,128 | 5,216 |
| Batched cleanup | 7,786,496 | 1,901 | 172 |

The byte ratio fell from about 8.55 to 1.24 (about 85.5% fewer virtual-device write bytes).
Prepared-write deltas were 13,316,096 bytes, 3,251 writes and 100 flushes over **12 MiB**
of logical data including preparation. The counters include creation, warmup, preparation
and asynchronous checkpoint work rather than a single syscall, and a COMMITTED completion
can precede later checkpoint writes. Virtual-device bytes do not establish physical SSD wear.

### Persistence, authority and consumers

The second boot exercised cached close/reopen reads; file and directory sync; resize;
create, rename and replacement; file and empty-directory removal; UTF-8 names; and native
executable capture and run. It rejected writes and sync through `ro://`, cross-volume
rename, removal of a nonempty directory, and remote-shell `sync --disk` without mount
authority. The copy workload turned over the four-chunk cache while verifying bytes.
fastfetch's Disk query had rejected writable INFO because the userland helper required
READ_ONLY; userland `6308de8` removed that requirement, and the rebuilt consumers then
reported both writable volumes and the read-only alias with consistent pool IDs, capacity
and journal sequence, and ran the saved native executable and verified a persisted 1 MiB
file.

After synchronized clean stops, standalone pool extraction and native fsck passed, and the
extracted grow-write, prepared-write and copied files matched the source byte for byte; the
inspector reported nonzero timestamps and no cleanup flags. This exercises normal
persistence, not recovery from interrupted writes. The USB-image builder was migrated to
the native formatter and its explicit journal argument; a 512 MiB image (128 MiB ESP,
382 MiB pool, 8 MiB journal) built, and GPT verification, extracted-pool fsck and a copied
`cat.pxe` comparison passed. No USB boot or installer result is claimed.

### Debugger inspection and limits

GDB covered the first CREATE and the reboot's root acquisition (interrupts enabled on the
BSP worker). A settled second-boot snapshot showed EMPTY journal sequence 2859, zero images,
no retained writeback error, no failed-pool flag and all four cache chunks allocated; no
filesystem calls were injected. `-fstack-usage` found the largest frames at 4144 bytes
(codec), 1712 (rename) and 1504 (cleanup) against a 16 KiB worker stack; this is not a
bound on the combined call stack.

Journal ordering and recovery, admission checks, retained-open unlink, uncertain-I/O
handling and allocation-pressure paths were code-reviewed only. No crash injection, ENOSPC
exercise, read-only-device boot, allocator-pressure exercise or physical-media
qualification was performed, and the 30-second interval is nominal, not a bound on crash
loss.
